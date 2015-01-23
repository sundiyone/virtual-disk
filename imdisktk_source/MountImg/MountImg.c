#define OEMRESOURCE
#include <windows.h>
#include <windowsx.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <stdio.h>
#include <winternl.h>
#include "resource.h"
#include "..\inc\imdisk.h"

static HINSTANCE hinst;
static HICON hIcon;
static RECT icon_coord;
static HWND hDialog;
static OSVERSIONINFO os_ver;
static TCHAR *arg_name;
static DWORD mask0;
static HKEY registry_key;
static HMODULE h_cpl;

static TCHAR dev_list[] = {'h', 'c', 'f'};
static TCHAR ro_list[] = {'w', 'o'};
static TCHAR *rm_list[] = {L"fix", L"rem"};
static DWORD floppy_size[] = {160 << 10, 180 << 10, 320 << 10, 360 << 10, 640 << 10, 1200 << 10, 720 << 10, 820 << 10, 1440 << 10, 1680 << 10, 1722 << 10, 2880 << 10, 123264 << 10, 234752 << 10};

static TCHAR filename[MAX_PATH], mountdir[MAX_PATH], cmdline[MAX_PATH * 2 + 200];
static TCHAR drive_list[26][4];
static TCHAR drive[MAX_PATH + 2];

static BOOL mount_point;
static BOOL init_ok = FALSE;
static UINT dev_type, partition;
static BOOL readonly, removable;
static BOOL new_file, no_net;

static TOOLINFO ti;
static HWND hTTip;
static HWND hwnd_edit1, hwnd_edit2, hwnd_combo1, hwnd_pbutton2, hwnd_pbutton3, hwnd_check1, hwnd_check2, hwnd_rb3, hwnd_rb4, hwnd_rb5, hwnd_updown, hwnd_ok;


static DWORD start_process(BOOL wait)
{
	STARTUPINFO si = {sizeof si};
	PROCESS_INFORMATION pi;
	DWORD ExitCode = 0;

	CreateProcess(NULL, cmdline, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
	if (wait) {
		WaitForSingleObject(pi.hProcess, INFINITE);
		GetExitCodeProcess(pi.hProcess, &ExitCode);
	}
	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);
	return ExitCode;
}

static void file_check()
{
	TCHAR *ext;
	WIN32_FILE_ATTRIBUTE_DATA file_data;
	int i;

	GetDlgItemText(hDialog, ID_EDIT1, filename, _countof(filename));
	ext = PathFindExtension(filename);
	CheckRadioButton(hDialog, ID_RB3, ID_RB5, !_wcsicmp(ext, L".iso") || !_wcsicmp(ext, L".nrg") || !_wcsicmp(ext, L".bin") ? ID_RB4 : ID_RB3);
	if (GetFileAttributesEx(filename, GetFileExInfoStandard, &file_data) && !file_data.nFileSizeHigh)
		for (i = 0; i < _countof(floppy_size); i++)
			if (floppy_size[i] == file_data.nFileSizeLow) {
				CheckRadioButton(hDialog, ID_RB3, ID_RB5, ID_RB5);
				if (drive_list[0][0] == 'A' || drive_list[0][0] == 'B')
					SendDlgItemMessage(hDialog, ID_COMBO1, CB_SETCURSEL, 0, 0);
			}
}

static void disp_controls()
{
	ShowWindow(hwnd_combo1, !mount_point);
	ShowWindow(hwnd_edit2, mount_point);
	ShowWindow(hwnd_pbutton2, mount_point);
	ShowWindow(hwnd_pbutton3, mount_point);
	Button_Enable(hwnd_ok, filename[0] != 0 && (!mount_point || mountdir[0] != 0));
}

static void draw_icon(HWND hDlg)
{
	PAINTSTRUCT paint;

	DrawIcon(BeginPaint(hDlg, &paint), icon_coord.left, icon_coord.top, hIcon);
	EndPaint(hDlg, &paint);
}

static void set_drives_list()
{
	DWORD mask;
	int i, drive_select, n_drive, letter;

	drive_select = SendDlgItemMessage(hDialog, ID_COMBO1, CB_GETCURSEL, 0, 0);
	if (drive_select == CB_ERR) letter = 0;
	else letter = drive_list[drive_select][0];

	SendDlgItemMessage(hDialog, ID_COMBO1, CB_RESETCONTENT, 0, 0);
	drive_select = -1;
	ZeroMemory(drive_list, sizeof drive_list);
	n_drive = 0;
	mask = mask0 | GetLogicalDrives();
	if (letter && (mask & (1 << (letter - 'A')))) letter = 0;
	for (i = 'A'; i <= 'Z'; i++) {
		if (!(mask & 1)) {
			if (i == letter || (!letter && drive_select == -1 && i > 'C')) drive_select = n_drive;
			drive_list[n_drive][0] = i;
			drive_list[n_drive][1] = ':';
			SendDlgItemMessage(hDialog, ID_COMBO1, CB_ADDSTRING, 0, (LPARAM)drive_list[n_drive++]);
		}
		mask >>= 1;
	}
	if (drive_select == -1) {
		if (n_drive)
			drive_select = 0;
		else {
			MessageBox(hDialog, L"Error: no available drive letter.", L"ImDisk", MB_ICONERROR);
			EndDialog(hDialog, 0);
			return;
		}
	}
	SendDlgItemMessage(hDialog, ID_COMBO1, CB_SETCURSEL, drive_select, 0);
}

static DWORD __stdcall RefreshList(LPVOID lpParam)
{
	HANDLE h;
	FARPROC ImDiskOpenRefreshEvent;

	if (!(ImDiskOpenRefreshEvent = GetProcAddress(h_cpl, "ImDiskOpenRefreshEvent"))) return 0;
	if (!(h = (HANDLE)ImDiskOpenRefreshEvent(FALSE))) return 0;
	while (WaitForSingleObject(h, INFINITE) != WAIT_FAILED)
		set_drives_list();
	CloseHandle(h);
	return 1;
}

static BOOL __stdcall CreateFile_Proc(HWND hDlg, UINT Msg, WPARAM wParam, LPARAM lParam)
{
	HANDLE h;
	TCHAR txt_size[18];
	__int64 size;
	void *buff;
	DWORD bytes;
	int i, unit;

	switch (Msg)
	{
		case WM_INITDIALOG:
			hIcon = LoadImage(NULL, MAKEINTRESOURCE(OIC_QUES), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE | LR_SHARED);
			icon_coord.left = 13;
			icon_coord.top = 11;
			icon_coord.right = 0;
			icon_coord.bottom = 0;
			MapDialogRect(hDlg, &icon_coord);

			CheckRadioButton(hDlg, ID_RB21, ID_RB24, ID_RB21);
			CheckDlgButton(hDlg, ID_CHECK21, !mount_point);

			return TRUE;

		case WM_PAINT:
			draw_icon(hDlg);
			return TRUE;

		case WM_COMMAND:
			unit = 0;
			for (i = 0; i < 4; i++)
				if (IsDlgButtonChecked(hDlg, ID_RB21 + i)) unit = 10 * i;

			GetDlgItemText(hDlg, ID_EDIT21, txt_size, _countof(txt_size));
			size = min(_wtoi64(txt_size), _I64_MAX >> unit) << unit;
			Button_Enable(GetDlgItem(hDlg, IDOK), size >= 20480);

			if (LOWORD(wParam) == IDOK) {
				if ((h = CreateFile(filename, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL)) == INVALID_HANDLE_VALUE) {
					MessageBox(hDlg, L"Error: file cannot be created.", L"ImDisk", MB_ICONERROR);
					return TRUE;
				}
				SetFilePointerEx(h, (LARGE_INTEGER)size, NULL, FILE_BEGIN);
				if (!SetEndOfFile(h)) {
					CloseHandle(h);
					DeleteFile(filename);
					MessageBox(hDlg, L"Error: cannot create a file of the specified size.", L"ImDisk", MB_ICONERROR);
					return TRUE;
				}
				SetFilePointerEx(h, (LARGE_INTEGER)0LL, NULL, FILE_BEGIN);
				buff = calloc(20480, 1);
				new_file = WriteFile(h, buff, 20480, &bytes, NULL);
				CloseHandle(h);
				free(buff);
				if (new_file)
					EndDialog(hDlg, IsDlgButtonChecked(hDlg, ID_CHECK21));
				else {
					DeleteFile(filename);
					MessageBox(hDlg, L"Error: cannot initialize file.", L"ImDisk", MB_ICONERROR);
				}
			}

			if (LOWORD(wParam) == IDCANCEL)
				EndDialog(hDlg, FALSE);

			return TRUE;

		default:
			return FALSE;
	}
}

static BOOL __stdcall InvalidFS_Proc(HWND hDlg, UINT Msg, WPARAM wParam, LPARAM lParam)
{
	switch (Msg)
	{
		case WM_INITDIALOG:
			hIcon = LoadImage(NULL, MAKEINTRESOURCE(OIC_WARNING), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE | LR_SHARED);
			icon_coord.left = 14;
			icon_coord.top = 18;
			icon_coord.right = 0;
			icon_coord.bottom = 0;
			MapDialogRect(hDlg, &icon_coord);
			Button_Enable(GetDlgItem(hDlg, ID_PBUTTON12), !mount_point);
			if (!mount_point) SetTimer(hDlg, 1, 1000, NULL);
			MessageBeep(MB_ICONWARNING);
			return TRUE;

		case WM_PAINT:
			draw_icon(hDlg);
			return TRUE;

		case WM_TIMER:
			if (GetVolumeInformation(drive, NULL, 0, NULL, NULL, NULL, NULL, 0)) {
				EndDialog(hDlg, 0);
				EndDialog(hDialog, 0);
			}
			if (GetLastError() == ERROR_PATH_NOT_FOUND)
				EndDialog(hDlg, 0);
			return TRUE;

		case WM_COMMAND:
			if (LOWORD(wParam) == ID_PBUTTON11) {
				swprintf(cmdline, L"imdisk -D -m \"%s\"", drive);
				start_process(FALSE);
				EndDialog(hDlg, 0);
			}

			if (LOWORD(wParam) == ID_PBUTTON12) {
				ShowWindow(hDlg, SW_HIDE);
				ShowWindow(hDialog, SW_HIDE);
				SHFormatDrive(hDialog, drive[0] - 'A', SHFMT_ID_DEFAULT, SHFMT_OPT_FULL);
				EndDialog(hDlg, 0);
				EndDialog(hDialog, 0);
			}

			if (LOWORD(wParam) == ID_PBUTTON13) {
				EndDialog(hDlg, 0);
				EndDialog(hDialog, 0);
			}

			return TRUE;

		default:
			return FALSE;
	}
}

static int Imdisk_Mount()
{
	int i, j, ExitCode;
	TCHAR txt_partition[20];
	TCHAR volume[40];
	BOOL fs_ok = FALSE;
	ULONG *list;
	FARPROC ImDiskGetDeviceListEx;

	txt_partition[0] = 0;
	if (partition) {
		// search the first available unit
		if (!(ImDiskGetDeviceListEx = GetProcAddress(h_cpl, "ImDiskGetDeviceListEx"))) return 1;
		list = malloc(64002 * sizeof(ULONG));
		if (!ImDiskGetDeviceListEx(64002, list)) {
			free(list);
			return 1;
		}
		i = j = 0;
		while (++j <= list[0])
			if (list[j] == i) { j = 0; i++; }
		free(list);

		if (partition > 1) swprintf(txt_partition, L" -v %d", partition);
		swprintf(cmdline, L"imdisk -a -u %d -o %cd,r%c,%s -f \"%s\" -b auto%s", i, dev_list[dev_type], ro_list[readonly], rm_list[removable], filename, txt_partition);
		if (start_process(TRUE)) return 1;
		swprintf(volume, L"\\\\.\\GLOBALROOT\\Device\\ImDisk%d\\", i);
		fs_ok = GetVolumeInformation(volume, NULL, 0, NULL, NULL, NULL, NULL, 0);
		swprintf(cmdline, L"imdisk -D -u %d", i);
		start_process(TRUE);
	}
	i = 0;
	do {
		Sleep(100);
		swprintf(cmdline, L"imdisk -a -m \"%s\" -o %cd,r%c,%s -f \"%s\"%s%s", drive, dev_list[dev_type], ro_list[readonly], rm_list[removable], filename, fs_ok ? L" -b auto" : L"", txt_partition);
		ExitCode = start_process(TRUE);
	} while (ExitCode && ++i < 100);
	return ExitCode;
}

static int DiscUtils_Mount()
{
	STARTUPINFO si = {sizeof si};
	PROCESS_INFORMATION pi;
	DWORD ExitCode;
	TCHAR txt_partition[20];
	int i, error;
	UINT pipe;

	pipe = GetTickCount();
	txt_partition[0] = 0;
	if (partition != 1) swprintf(txt_partition, L" /partition=%d", partition);
	swprintf(cmdline, L"DiscUtilsDevio /name=ImDisk%u%s /filename=\"%s\"", pipe, txt_partition, filename);
	CreateProcess(NULL, cmdline, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
	swprintf(cmdline, L"imdisk -a -t proxy -m \"%s\" -o shm,%cd,r%c,%s -f ImDisk%u", drive, dev_list[dev_type], ro_list[readonly], rm_list[removable], pipe);
	i = 0;
	do {
		Sleep(100);
		// check if DiscUtilsDevio is still active
		GetExitCodeProcess(pi.hProcess, &ExitCode);
		if (ExitCode != STILL_ACTIVE) {
			CloseHandle(pi.hProcess);
			CloseHandle(pi.hThread);
			return 1;
		}
		error = start_process(TRUE);
	} while (error && ++i < 100);
	return error;
}

static DWORD __stdcall Mount(LPVOID lpParam)
{
	TCHAR *ext;
	int i, error;

	if (mount_point) wcscpy(drive, mountdir);
	ext = PathFindExtension(filename);
	if (no_net)
		error = Imdisk_Mount();
	else if (new_file || !*ext || !_wcsicmp(ext, L".iso") || !_wcsicmp(ext, L".nrg") || !_wcsicmp(ext, L".bin") || !_wcsicmp(ext, L".img") || !_wcsicmp(ext, L".ima") || !_wcsicmp(ext, L".vfd") || !_wcsicmp(ext, L".raw")) {
		error = Imdisk_Mount();
		if (error) error = DiscUtils_Mount();
	} else {
		error = DiscUtils_Mount();
		if (error) error = Imdisk_Mount();
	}

	if (error) {
		MessageBox(hDialog, L"An error occurred while the mounting.\nPlease check the parameters.", L"ImDisk", MB_ICONERROR);
		Button_Enable(hwnd_ok, TRUE);
		SetDlgItemText(hDialog, IDOK, L"OK");
		init_ok = TRUE;
		return 0;
	}

	if (new_file) {
		EndDialog(hDialog, 0);
		return 0;
	}

	// check the new drive or mount point
	i = 0;
	if (mount_point) PathAddBackslash(drive);
	do {
		if (GetVolumeInformation(drive, NULL, 0, NULL, NULL, NULL, NULL, 0)) {
			if (os_ver.dwMajorVersion < 6) {
				swprintf(cmdline, L"explorer /n,%s", drive);
				start_process(FALSE);
			} else
				ShellExecute(NULL, NULL, drive, NULL, NULL, SW_SHOWNORMAL);
			break;
		} else if (GetLastError() == ERROR_UNRECOGNIZED_VOLUME) {
			PathRemoveBackslash(drive);
			DialogBox(hinst, L"INVALID_FS", hDialog, InvalidFS_Proc);
			Button_Enable(hwnd_ok, TRUE);
			SetDlgItemText(hDialog, IDOK, L"OK");
			init_ok = TRUE;
			return 0;
		}
		Sleep(100);
	} while (++i < 100);

	EndDialog(hDialog, 0);

	return 0;
}

static BOOL __stdcall DlgProc(HWND hDlg, UINT Msg, WPARAM wParam, LPARAM lParam)
{
	OPENFILENAME ofn = {sizeof ofn};
	BROWSEINFO bi = {};
	LPITEMIDLIST pid_folder; // PIDLIST_ABSOLUTE on MSDN
	WIN32_FIND_DATA FindFileData;
	HANDLE h;
	DWORD flags;
	int i;
	HWND h_updown;
	HMODULE hDLL;
	FARPROC lpFunc;
	HKEY reg_net_key;

	switch (Msg)
	{
		case WM_INITDIALOG:
			SendMessage(hDlg, WM_SETICON, ICON_SMALL, (LPARAM)LoadImage(hinst, MAKEINTRESOURCE(2), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE));

			SendDlgItemMessage(hDlg, ID_EDIT1, EM_SETLIMITTEXT, _countof(filename) - 1, 0);
			SetDlgItemText(hDlg, ID_EDIT1, arg_name);

			CheckRadioButton(hDlg, ID_RB1, ID_RB2, ID_RB1 + mount_point);

			hDialog = hDlg;
			set_drives_list();
			CreateThread(NULL, 0, RefreshList, NULL, 0, NULL);

			SendDlgItemMessage(hDlg, ID_EDIT2, EM_SETLIMITTEXT, _countof(mountdir) - 1, 0);
			SetDlgItemText(hDlg, ID_EDIT2, mountdir);

			// up-down control
			h_updown = CreateWindow(UPDOWN_CLASS, NULL, WS_CHILDWINDOW | WS_VISIBLE | UDS_SETBUDDYINT | UDS_ALIGNRIGHT | UDS_ARROWKEYS | UDS_HOTTRACK, 0, 0, 0, 0, hDlg, NULL, hinst, NULL);
			SendMessage(h_updown, UDM_SETBUDDY, (WPARAM)GetDlgItem(hDlg, ID_UPDOWN), 0);
			SendMessage(h_updown, UDM_SETRANGE, 0, MAKELPARAM(128, 0));
			SetDlgItemInt(hDlg, ID_UPDOWN, 1, FALSE);

			// ChangeWindowMessageFilterEx(hDlg, WM_DROPFILES, MSGFLT_ALLOW, NULL);
			// ChangeWindowMessageFilterEx(hDlg, WM_COPYGLOBALDATA, MSGFLT_ALLOW, NULL);
			hDLL = GetModuleHandleA("user32.dll");
			if ((lpFunc = GetProcAddress(hDLL, "ChangeWindowMessageFilterEx"))) {
				lpFunc(hDlg, WM_DROPFILES, 1, NULL);
				lpFunc(hDlg, 0x0049, 1, NULL);
			} else if ((lpFunc = GetProcAddress(hDLL, "ChangeWindowMessageFilter"))) {
				lpFunc(WM_DROPFILES, 1);
				lpFunc(0x0049, 1);
			}

			// initialize tooltips
			hTTip = CreateWindow(TOOLTIPS_CLASS, NULL, TTS_NOPREFIX, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, NULL, NULL, hinst, NULL); 
			ti.cbSize = sizeof(TOOLINFO);
			ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
			ti.hwnd = hDlg;
			ti.lpszText = LPSTR_TEXTCALLBACK;
			ti.uId = (UINT_PTR)(hwnd_edit1 = GetDlgItem(hDlg, ID_EDIT1));
			SendMessage(hTTip, TTM_ADDTOOL, 0, (LPARAM)&ti);
			ti.uId = (UINT_PTR)(hwnd_edit2 = GetDlgItem(hDlg, ID_EDIT2));
			SendMessage(hTTip, TTM_ADDTOOL, 0, (LPARAM)&ti);
			ti.uId = (UINT_PTR)(hwnd_check1 = GetDlgItem(hDlg, ID_CHECK1));
			SendMessage(hTTip, TTM_ADDTOOL, 0, (LPARAM)&ti);
			ti.uId = (UINT_PTR)(hwnd_check2 = GetDlgItem(hDlg, ID_CHECK2));
			SendMessage(hTTip, TTM_ADDTOOL, 0, (LPARAM)&ti);
			ti.uId = (UINT_PTR)(hwnd_rb3 = GetDlgItem(hDlg, ID_RB3));
			SendMessage(hTTip, TTM_ADDTOOL, 0, (LPARAM)&ti);
			ti.uId = (UINT_PTR)(hwnd_rb4 = GetDlgItem(hDlg, ID_RB4));
			SendMessage(hTTip, TTM_ADDTOOL, 0, (LPARAM)&ti);
			ti.uId = (UINT_PTR)(hwnd_rb5 = GetDlgItem(hDlg, ID_RB5));
			SendMessage(hTTip, TTM_ADDTOOL, 0, (LPARAM)&ti);
			ti.uId = (UINT_PTR)(hwnd_updown = GetDlgItem(hDlg, ID_UPDOWN));
			SendMessage(hTTip, TTM_ADDTOOL, 0, (LPARAM)&ti);
			SendMessage(hTTip, TTM_SETMAXTIPWIDTH, 0, 1000);

			hwnd_combo1 = GetDlgItem(hDialog, ID_COMBO1);
			hwnd_pbutton2 = GetDlgItem(hDialog, ID_PBUTTON2);
			hwnd_pbutton3 = GetDlgItem(hDialog, ID_PBUTTON3);
			file_check();
			hwnd_ok = GetDlgItem(hDlg, IDOK);
			disp_controls();

			init_ok = TRUE;
			return TRUE;

		case WM_NOTIFY:
			if (((NMHDR*)lParam)->code == TTN_GETDISPINFO) {
				SendMessage(hTTip, TTM_SETDELAYTIME, TTDT_AUTOPOP, ((NMHDR*)lParam)->idFrom == (UINT_PTR)hwnd_edit1 ? 20000 : 15000);
				if (((NMHDR*)lParam)->idFrom == (UINT_PTR)hwnd_edit1)
					((NMTTDISPINFO*)lParam)->lpszText = L"Enter image file name, select it with the \"...\" button\nor drag and drop it.\nA wrong extension can make the mouting fail!\n\n"
														 "If the file does not exist, you will be prompted to\ncreate a new raw file.";
				if (((NMHDR*)lParam)->idFrom == (UINT_PTR)hwnd_edit2)
					((NMTTDISPINFO*)lParam)->lpszText = L"The mount point must be an empty directory located on a NTFS volume.\n\nIf you drag and drop a folder, it will be selected as mount point.";
				if (((NMHDR*)lParam)->idFrom == (UINT_PTR)hwnd_rb3 || ((NMHDR*)lParam)->idFrom == (UINT_PTR)hwnd_rb4 || ((NMHDR*)lParam)->idFrom == (UINT_PTR)hwnd_rb5)
					((NMTTDISPINFO*)lParam)->lpszText = L"\"CD/DVD\" is recommended for ISO, BIN and\nNRG files, \"Hard Disk\" for other formats.";
				if (((NMHDR*)lParam)->idFrom == (UINT_PTR)hwnd_updown)
					((NMTTDISPINFO*)lParam)->lpszText = L"Enter 1 if the image file contain no partition or only one.\n0 means the entire image: the partition table will be overwritten.";
				if (((NMHDR*)lParam)->idFrom == (UINT_PTR)hwnd_check1)
					((NMTTDISPINFO*)lParam)->lpszText = L"Prevent writing to the image file.";
				if (((NMHDR*)lParam)->idFrom == (UINT_PTR)hwnd_check2)
					((NMTTDISPINFO*)lParam)->lpszText = L"This usually disable the write caching: the data\nare immediately written to the image file but\nthis can decrease the performances.";
			}
			return TRUE;

		case WM_DROPFILES:
			if (DragQueryFile((HDROP)wParam, 0, filename, _countof(filename))) {
				if (PathIsDirectory(filename)) {
					SetDlgItemText(hDlg, ID_EDIT2, filename);
					CheckRadioButton(hDlg, ID_RB1, ID_RB2, ID_RB2);
					mount_point = TRUE;
					disp_controls();
				} else {
					SetDlgItemText(hDlg, ID_EDIT1, filename);
					file_check();
				}
			}
			DragFinish((HDROP)wParam);
			return TRUE;

		case WM_COMMAND:
			if (!init_ok) return FALSE;

			GetDlgItemText(hDlg, ID_EDIT1, filename, _countof(filename));
			GetDlgItemText(hDlg, ID_EDIT2, mountdir, _countof(mountdir));
			mount_point = IsDlgButtonChecked(hDlg, ID_RB2);
			disp_controls();

			if (LOWORD(wParam) == ID_PBUTTON1) {
				ofn.hwndOwner = hDlg;
				ofn.lpstrFile = filename;
				ofn.nMaxFile = _countof(filename);
				ofn.Flags = OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
				GetOpenFileName(&ofn);
				if (filename[0]) {
					SetDlgItemText(hDlg, ID_EDIT1, filename);
					file_check();
				}
			}

			if (LOWORD(wParam) == ID_PBUTTON2) {
				bi.hwndOwner = hDlg;
				bi.pszDisplayName = mountdir;
				bi.lpszTitle = L"ImDisk - Mount Point";
				bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
				pid_folder = SHBrowseForFolder(&bi);
				if (pid_folder) {
					SHGetPathFromIDList(pid_folder, mountdir);
					SetDlgItemText(hDlg, ID_EDIT2, mountdir);
				}
			}

			if (LOWORD(wParam) == ID_PBUTTON3) {
				Button_Enable(hwnd_pbutton3, FALSE);
				swprintf(cmdline, L"imdisk -D -m \"%s\"", mountdir);
				start_process(TRUE);
				Button_Enable(hwnd_pbutton3, TRUE);
			}

			if (LOWORD(wParam) == ID_PBUTTON4) {
				swprintf(cmdline, L"rundll32 imdisk.cpl,RunDLL_MountFile %s", filename);
				start_process(FALSE);
				EndDialog(hDlg, 1);
			}

			if (LOWORD(wParam) == IDOK) {
				// retrieve parameters
				wcscpy(drive, drive_list[SendDlgItemMessage(hDlg, ID_COMBO1, CB_GETCURSEL, 0, 0)]);
				for (i = 0; i < 3; i++)
					if (IsDlgButtonChecked(hDlg, ID_RB3 + i)) dev_type = i;
				partition = GetDlgItemInt(hDlg, ID_UPDOWN, NULL, FALSE);
				readonly = IsDlgButtonChecked(hDlg, ID_CHECK1);
				removable = IsDlgButtonChecked(hDlg, ID_CHECK2);

				// save parameters
				RegSetValueEx(registry_key, L"MountPoint", 0, REG_DWORD, (void*)&mount_point, sizeof mount_point);
				RegSetValueEx(registry_key, L"MountDir", 0, REG_SZ, (void*)&mountdir, (wcslen(mountdir) + 1) * sizeof(TCHAR));

				if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\.NETFramework\\v4.0.30319", 0, KEY_QUERY_VALUE | KEY_WOW64_64KEY, &reg_net_key) == ERROR_SUCCESS) {
					no_net = FALSE;
					RegCloseKey(reg_net_key);
				} else
					no_net = TRUE;

				new_file = FALSE;
				if (!PathFileExists(filename) && DialogBox(hinst, L"CREATE_FILE", hDlg, CreateFile_Proc) != TRUE)
					return TRUE;

				// check mount point
				if (mount_point) {
					GetFullPathName(mountdir, _countof(cmdline), cmdline, NULL);
					cmdline[3] = 0;
					if (!GetVolumeInformation(cmdline, NULL, 0, NULL, NULL, &flags, NULL, 0) || !(flags & FILE_SUPPORTS_REPARSE_POINTS)) {
						MessageBox(hDlg, L"Error: file system of the selected mount point does not support reparse points.", L"ImDisk", MB_ICONERROR);
						return TRUE;
					}
					FindClose(h = FindFirstFile(mountdir, &FindFileData));
					if (h != INVALID_HANDLE_VALUE && FindFileData.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT && FindFileData.dwReserved0 == IO_REPARSE_TAG_MOUNT_POINT) {
						if (MessageBox(hDlg, L"The selected directory is already a mount point.\nIf you choose to continue, the previously mounted volume will become unreachable via this mount point.\n\nDo you still want to continue?",
									   L"ImDisk", MB_YESNO | MB_DEFBUTTON2 | MB_ICONWARNING) == IDNO)
							return TRUE;
					} else if (!PathIsDirectoryEmpty(mountdir)) {
						MessageBox(hDlg, L"Error: the selected mount point is not an empty directory.", L"ImDisk", MB_ICONERROR);
						return TRUE;
					}
				}

				init_ok = FALSE;
				Button_Enable(hwnd_ok, FALSE);
				SetDlgItemText(hDlg, IDOK, L"Mounting...");
				CreateThread(NULL, 0, Mount, NULL, 0, NULL);
			}

			if (LOWORD(wParam) == IDCANCEL)
				EndDialog(hDlg, 0);

			return TRUE;

		default:
			return FALSE;
	}
}


int __stdcall wWinMain(HINSTANCE hinstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow)
{
	int argc;
	LPWSTR *argv;
	HANDLE h;
	HWND hwnd;
	DWORD data_size, Version;
	int i;

	os_ver.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
	GetVersionEx(&os_ver);

	argv = CommandLineToArgvW(GetCommandLine(), &argc);

	if ((os_ver.dwMajorVersion >= 6) && (argc <= 1 || wcscmp(argv[1], L"/UAC"))) {
		// get non-elevated drive list without ImDisk volumes
		mask0 = GetLogicalDrives();
		wcscpy(drive, L"\\\\.\\A:");
		for (i = 1; i != 1 << 26; i <<= 1) {
			if (mask0 & i) {
				h = CreateFile(drive, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
				if (DeviceIoControl(h, IOCTL_IMDISK_QUERY_VERSION, NULL, 0, &Version, sizeof Version, &data_size, NULL))
					mask0 ^= i;
				CloseHandle(h);
			}
			drive[4]++;
		}

		// send the list to the elevated process
		_snwprintf(cmdline, _countof(cmdline) - 1, L"/UAC %d \"%s\"", mask0, argc > 1 ? argv[1] : L"");
		cmdline[_countof(cmdline) - 1] = 0;
		ShellExecute(NULL, L"runas", argv[0], cmdline, NULL, nCmdShow);
		return 0;
	}

	if (os_ver.dwMajorVersion < 6) {
		mask0 = 0;
		arg_name = argc > 1 ? argv[1] : L"";
	} else {
		mask0 = _wtoi(argv[2]);
		arg_name = argv[3];
	}

	hinst = hinstance;
	if (!(h_cpl = LoadLibraryA("imdisk.cpl")))
		MessageBox(NULL, L"Warning: cannot find imdisk.cpl. Please reinstall the driver.", L"ImDisk", MB_ICONWARNING);

	// load default values
	mount_point = FALSE;
	mountdir[0] = 0;

	// get registry values
	RegCreateKeyEx(HKEY_LOCAL_MACHINE, L"SOFTWARE\\ImDisk", 0, NULL, REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS | KEY_WOW64_64KEY, NULL, &registry_key, NULL);
	data_size = sizeof mount_point;
	RegQueryValueEx(registry_key, L"MountPoint", NULL, NULL, (void*)&mount_point, &data_size);
	data_size = sizeof mountdir;
	RegQueryValueEx(registry_key, L"MountDir", NULL, NULL, (void*)&mountdir, &data_size);

	if (DialogBox(hinst, L"MOUNT_DLG", NULL, DlgProc) == 1) {
		// workaround: the window of the driver GUI sometimes disappears under other windows
		for (i = 0; i < 100; i++) {
			Sleep(50);
			if ((hwnd = FindWindow(NULL, L"Mount new virtual disk")) || (hwnd = FindWindow(NULL, L"Select partition in disk image"))) {
				SetForegroundWindow(hwnd);
				break;
			}
		}
	}

	return 0;
}

#ifdef MINGW_NOSTDLIB
void WinMainCRTStartup()
{
	STARTUPINFO si;
	GetStartupInfo(&si);
	ExitProcess(wWinMain(GetModuleHandle(NULL), NULL, PathGetArgs(GetCommandLine()), si.dwFlags & STARTF_USESHOWWINDOW ? si.wShowWindow : SW_SHOWDEFAULT));
}
#endif
