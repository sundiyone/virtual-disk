#include <windows.h>
#include <windowsx.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <dbt.h>
#include <stdio.h>
#include <ntsecapi.h>
#include <ntstatus.h>
#include "resource.h"
#include "..\inc\imdisk.h"

static SERVICE_STATUS SvcStatus = {SERVICE_WIN32_OWN_PROCESS, SERVICE_RUNNING, 0, NO_ERROR, 0, 0, 3600000};

static HINSTANCE hinst;
static HICON hIcon;
static BOOL init1_ok = FALSE, init2_ok = FALSE, apply_ok = TRUE;
static HKEY registry_key;
static HMODULE h_cpl;

static UINT min_size[4][9] = {{2761, 2752, 2754, 2757, 2761, 2785, 2849, 2977, 3009},
							  {20, 20, 20, 20, 20, 20, 20, 20, 20},
							  {36860, 36860, 69623, 135150, 266204, 528312, 1052528, 2100960, 4197824},
							  {181, 181, 181, 181, 181, 208, 288, 448, 768}};
static UINT max_size[2][9] = {{4194003, 33039, 65802, 131329, 262383, 524491, 1048707, 2097139, 4194003},
							  {33555455, 2125824, 4214784, 8392705, 16744451, 33456135, 33555455, 33555455, 33555455}};
static char unit_list[] = {'K', 'M', 'G'};
static TCHAR *filesystem_list[] = {L"NTFS", L"FAT", L"FAT32", L"exFAT"};
static char *cluster_list[] = {"", "/a:512", "/a:1024", "/a:2048", "/a:4096", "/a:8192", "/a:16k", "/a:32k", "/a:64k"};
static char *quickf_list[] = {"", "/q "};
static char *awe_list[] = {"", "-o awe "};
static char *fileawe_list[] = {"-t vm", "-o awe"};
static TCHAR *dep_list[] = {L"ImDisk\0", L"ImDisk\0AWEAlloc\0"};
static TCHAR *param_list[] = {L"DriveSize", L"Unit", L"Dynamic", L"FileSystem", L"TempFolder", L"Cluster", L"Label", L"QuickFormat", L"Awealloc", L"CleanRatio", L"CleanTimer", L"CleanMargin", L"MaxActivity",
							  L"RDMountPoint", L"ImageFile", L"AddParam"};
static DWORD drive_size, unit, dynamic, wanted_drive, filesystem, win_boot, temp_folder, cluster, quick_format, awealloc, reg_awealloc, use_mount_point, reg_use_MP, reg_win_boot, reg_dynamic, mount_current;
static DWORD clean_ratio, clean_timer, clean_margin, max_activity;
static TCHAR label[33], mount_point[MAX_PATH], MP_temp_letter[2], image_file[MAX_PATH + 1], add_param[255];
static BOOL mount_file, mount_dir;
static TCHAR drive_list[26][4], drive_select[3];
static UINT drive_default;
static DWORD mask0;

static TCHAR svc_cmd_line[MAX_PATH + 8], notif_cmd_line[MAX_PATH + 10], key_name[16];
static HWND hwnd_base, hDlg1, hDlg2;
static BOOL item_enable, item_change, init_focus;
static HWND hwnd_edit1, hwnd_edit2, hwnd_edit3, hwnd_edit4, hwnd_edit10, hwnd_check1, hwnd_check2, hwnd_check4, hwnd_check5, hwnd_combo2, hwnd_combo3, hwnd_pbutton3, hwnd_pbutton7;
static HWND hwnd_edit11, hwnd_edit12, hwnd_edit13, hwnd_edit14;
static COMBOBOXINFO combo5;

static RECT circle = {13, 208, 18, 213};
static COLORREF color;
static HWND hTTip;
static TOOLINFO tool_info;
static TCHAR TTip_txt[40];


static void start_process(TCHAR *cmd, BOOL wait)
{
	STARTUPINFO si = {sizeof si};
	PROCESS_INFORMATION pi;

	CreateProcess(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
	if (wait) WaitForSingleObject(pi.hProcess, INFINITE);
	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);
}

static void circ_draw(HWND hwnd)
{
	HDC hdc;
	PAINTSTRUCT paint;
	HBRUSH brush;

	hdc = BeginPaint(hwnd, &paint);
	brush = CreateSolidBrush(color);
	SelectObject(hdc, brush);
	Ellipse(hdc, circle.left, circle.top, circle.right, circle.bottom);
	DeleteObject(brush);
	EndPaint(hwnd, &paint);
}

static void notif(COLORREF c, TCHAR *text)
{
	color = c;
	RedrawWindow(hDlg1, &circle, NULL, RDW_INVALIDATE);
	RedrawWindow(hDlg2, &circle, NULL, RDW_INVALIDATE);
	SetDlgItemText(hDlg1, ID_TEXT1, text);
	SetDlgItemText(hDlg2, ID_TEXT2, text);
}

static void add_tooltip(TOOLINFO *ti)
{
	SendMessage(hTTip, TTM_ADDTOOL, 0, (LPARAM)ti);
}

static void remove_reg_param()
{
	TCHAR param_name[16];
	int i;

	param_name[0] = drive_select[0];
	param_name[1] = '_';
	for (i = 0; i < _countof(param_list); i++) {
		wcscpy(&param_name[2], param_list[i]);
		RegDeleteValue(registry_key, param_name);
	}
}

static void copy_list_param(TCHAR *param_name)
{
	char data[600];
	DWORD data_size, type;
	int i;

	for (i = 0; i < _countof(param_list); i++) {
		wcscpy(&param_name[2], param_list[i]);
		data_size = sizeof data;
		RegQueryValueEx(registry_key, param_list[i], NULL, &type, (void*)&data, &data_size);
		RegSetValueEx(registry_key, param_name, 0, type, (void*)&data, data_size);
	}
}

static void reg_set_dword(TCHAR *name, DWORD *value)
{
	RegSetValueEx(registry_key, name, 0, REG_DWORD, (void*)value, sizeof(DWORD));
}

static long reg_query_dword(TCHAR *name, DWORD *value)
{
	DWORD data_size = sizeof(DWORD);

	return RegQueryValueEx(registry_key, name, NULL, NULL, (void*)value, &data_size);
}

static long param_reg_query_dword(TCHAR *name, DWORD *value)
{
	wcscpy(&key_name[2], name);
	return reg_query_dword(key_name, value);
}

static void search_awealloc(BOOL *RD_found, DWORD *dep_awealloc)
{
	DWORD awe, dyn;

	*RD_found = FALSE;
	key_name[1] = '_';
	for (key_name[0] = '0'; key_name[0] <= 'Z'; key_name[0] == '9' ? key_name[0] = 'A' : key_name[0]++)
		if (param_reg_query_dword(L"Awealloc", &awe) == ERROR_SUCCESS) {
			*RD_found = TRUE;
			if (param_reg_query_dword(L"Dynamic", &dyn) != ERROR_SUCCESS) dyn = 0;
			*dep_awealloc |= awe & ~dyn;
		}
}

static void load_mount_point()
{
	DWORD data_size;
	TCHAR param_name[16], path[MAX_PATH];

	SendDlgItemMessage(hDlg2, ID_COMBO5, CB_RESETCONTENT, 0, 0);
	param_name[1] = '_';
	wcscpy(&param_name[2], L"RDMountPoint");
	for (param_name[0] = '0'; param_name[0] <= '9'; param_name[0]++) {
		data_size = sizeof path;
		if (RegQueryValueEx(registry_key, param_name, NULL, NULL, (void*)&path, &data_size) == ERROR_SUCCESS)
			SendDlgItemMessage(hDlg2, ID_COMBO5, CB_ADDSTRING, 0, (LPARAM)path);
	}
	SetDlgItemText(hDlg2, ID_COMBO5, mount_point);
}

static void remove_mount_point()
{
	DWORD data_size;
	TCHAR cmd_line[MAX_PATH + 20];
	TCHAR param_name[16];
	int i;

	swprintf(cmd_line, L"imdisk -D -m \"%s\"", mount_point);
	start_process(cmd_line, TRUE);
	param_name[1] = '_';
	for (param_name[0] = '0'; param_name[0] <= '9'; param_name[0]++) {
		wcscpy(&param_name[2], L"RDMountPoint");
		data_size = sizeof cmd_line;
		if (RegQueryValueEx(registry_key, param_name, NULL, NULL, (void*)&cmd_line, &data_size) == ERROR_SUCCESS && !wcscmp(cmd_line, mount_point))
			for (i = 0; i < _countof(param_list); i++) {
				wcscpy(&param_name[2], param_list[i]);
				RegDeleteValue(registry_key, param_name);
			}
	}
}

static BOOL is_MP_imdisk_device()
{
	HANDLE h;
	FARPROC ImDiskOpenDeviceByMountPoint;

	if (!(ImDiskOpenDeviceByMountPoint = GetProcAddress(h_cpl, "ImDiskOpenDeviceByMountPoint"))) return FALSE;
	CloseHandle(h = (HANDLE)ImDiskOpenDeviceByMountPoint(mount_point, GENERIC_READ));
	return h != INVALID_HANDLE_VALUE;
}

static void unmount_service()
{
	DWORD dep_awealloc;
	BOOL RD_found;
	SC_HANDLE h_scman, h_ImDiskRD;
	HKEY h_key;

	dep_awealloc = reg_awealloc & ~reg_dynamic & mount_current;
	search_awealloc(&RD_found, &dep_awealloc);

	h_scman = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
	h_ImDiskRD = OpenService(h_scman, L"ImDiskRD", SERVICE_ALL_ACCESS);
	RegCreateKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run", 0, NULL, REG_OPTION_NON_VOLATILE, KEY_SET_VALUE | KEY_WOW64_64KEY, NULL, &h_key, NULL);
	if (RD_found || mount_current) {
		ChangeServiceConfig(h_ImDiskRD, SERVICE_WIN32_OWN_PROCESS, SERVICE_AUTO_START, SERVICE_ERROR_NORMAL, svc_cmd_line, NULL, NULL, dep_list[dep_awealloc], NULL, NULL, NULL);
		RegSetValueEx(h_key, L"ImDisk_notif", 0, REG_SZ, (void*)&notif_cmd_line, (wcslen(notif_cmd_line) + 1) * sizeof(TCHAR));
	} else {
		DeleteService(h_ImDiskRD);
		RegDeleteValue(h_key, L"ImDisk_notif");
	}
	RegCloseKey(h_key);
	CloseServiceHandle(h_ImDiskRD);
	CloseServiceHandle(h_scman);
}

static BOOL SeLockMemoryPrivilege_required()
{
	TCHAR privilege_name[] = L"SeLockMemoryPrivilege";
	HANDLE token = INVALID_HANDLE_VALUE;
	TOKEN_PRIVILEGES tok_priv;
	LSA_HANDLE lsa_h = INVALID_HANDLE_VALUE;
	LSA_OBJECT_ATTRIBUTES lsa_oa = {};
	unsigned char sid[SECURITY_MAX_SID_SIZE];
	DWORD sid_size = sizeof sid;
	LSA_UNICODE_STRING lsa_str = {sizeof privilege_name - sizeof(TCHAR), sizeof privilege_name, privilege_name};

	if (!dynamic || !awealloc) return FALSE;
	tok_priv.PrivilegeCount = 1;
	tok_priv.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
	if (OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES, &token) &&
		LookupPrivilegeValue(NULL, privilege_name, &tok_priv.Privileges[0].Luid) &&
		AdjustTokenPrivileges(token, FALSE, &tok_priv, 0, NULL, NULL) && GetLastError() == ERROR_SUCCESS) {
		CloseHandle(token);
		return FALSE;
	}
	CloseHandle(token);
	if (MessageBox(hwnd_base, L"Using AWE with dynamic ramdisks requires the privilege to lock pages in memory in the local group policy.\nIf you choose to continue, this privilege "
							   "will be granted for the Administrators group.\n\nDo you want to continue?", L"ImDisk", MB_OKCANCEL | MB_ICONWARNING) == IDOK) {
		if (CreateWellKnownSid(WinBuiltinAdministratorsSid, NULL, (SID*)sid, &sid_size) &&
			LsaOpenPolicy(NULL, &lsa_oa, POLICY_LOOKUP_NAMES, &lsa_h) == STATUS_SUCCESS &&
			LsaAddAccountRights(lsa_h, (SID*)sid, &lsa_str, 1) == STATUS_SUCCESS) {
			if (MessageBox(hwnd_base, L"Privilege granted successfully.\nA logout is required for this change to take effect.\n\nLogout now?", L"ImDisk", MB_YESNO | MB_ICONWARNING) == IDYES)
				ExitWindowsEx(EWX_LOGOFF, SHTDN_REASON_MAJOR_OPERATINGSYSTEM | SHTDN_REASON_MINOR_RECONFIG | SHTDN_REASON_FLAG_PLANNED);
		} else {
			MessageBox(hwnd_base, L"Error: cannot grant the privilege.", L"ImDisk", MB_ICONERROR);
			notif(RGB(255, 0, 0), L"Error: cannot grant the privilege.");
		}
		LsaClose(lsa_h);
	}
	return TRUE;
}

static DWORD __stdcall ApplyParameters(LPVOID lpParam)
{
	SHELLEXECUTEINFO ShExInf = {sizeof ShExInf};
	HANDLE h;
	SC_HANDLE h_scman, h_ImDiskRD;
	SERVICE_DESCRIPTION svc_description;
	HKEY h_key;
	DWORD dw, data_size, Version, dep_awealloc, attrib;
	TCHAR cmd_line[2 * MAX_PATH + 400], copy_cmdline[2 * MAX_PATH + 40], proxy_cmdline[MAX_PATH + 50];
	TCHAR event_name[32];
	TCHAR temp_path[8];
	TCHAR label_tmp[34];
	TCHAR drive[MAX_PATH + 1];
	TCHAR param_name[16];
	UINT size_kb;
	BOOL prev_RD;
	TCHAR drive_id[10];
	TCHAR *current_MP;

	notif(RGB(255, 255, 0), L"Starting processing...");

	// check size parameter
	size_kb = min(drive_size, UINT_MAX >> (unit * 10)) << (unit * 10);
	if (!mount_file) {
		if (size_kb < min_size[filesystem][cluster]) {
			swprintf(cmd_line, L"Error: size must be at least %d KB.", min_size[filesystem][cluster]);
			MessageBox(hwnd_base, cmd_line, L"ImDisk", MB_ICONERROR);
			notif(RGB(255, 0, 0), L"Error: incorrect size.");
			apply_ok = TRUE;
			return 0;
		}
		if ((filesystem == 1 || filesystem == 2) && size_kb > max_size[filesystem - 1][cluster]) {
			swprintf(cmd_line, L"Error: size must not exceed %d %cB.", max_size[filesystem - 1][cluster] >> (unit * 10), unit_list[unit]);
			MessageBox(hwnd_base, cmd_line, L"ImDisk", MB_ICONERROR);
			notif(RGB(255, 0, 0), L"Error: incorrect size.");
			apply_ok = TRUE;
			return 0;
		}
	}

	// check the folder of mount point or the drive letter and unmount it
	if (use_mount_point) {
		attrib = GetFileAttributes(mount_point);
		if (attrib == INVALID_FILE_ATTRIBUTES || ((attrib & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) == FILE_ATTRIBUTE_DIRECTORY && !PathIsDirectoryEmpty(mount_point))) {
			MessageBox(hwnd_base, L"Error: no empty folder selected.", L"ImDisk", MB_ICONERROR);
			notif(RGB(255, 0, 0), L"Error: no empty folder selected.");
			apply_ok = TRUE;
			return 0;
		}
		if (is_MP_imdisk_device() && MessageBox(hwnd_base, L"The selected mount point will be unmounted.\nAny unsaved data will be lost.\nContinue?", L"ImDisk", MB_OKCANCEL | MB_ICONWARNING) != IDOK) {
			notif(RGB(0, 255, 0), L"Ready.");
			apply_ok = TRUE;
			return 0;
		}
		notif(RGB(255, 255, 0), L"Unmounting selected mount point...");
		remove_mount_point();
	} else {
		swprintf(drive, L"\\\\.\\%s", drive_select);
		h = CreateFile(drive, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
		if (DeviceIoControl(h, IOCTL_IMDISK_QUERY_VERSION, NULL, 0, &Version, sizeof Version, &data_size, NULL)) {
			if (MessageBoxA(hwnd_base, "This volume will be unmounted.\nAny unsaved data will be lost.\nContinue?", "ImDisk", MB_OKCANCEL | MB_ICONWARNING) != IDOK) {
				CloseHandle(h);
				notif(RGB(0, 255, 0), L"Ready.");
				apply_ok = TRUE;
				return 0;
			} else {
				notif(RGB(255, 255, 0), L"Unmounting current volume...");
				swprintf(cmd_line, L"imdisk -D -m %s", drive_select);
				start_process(cmd_line, TRUE);
			}
		}
		CloseHandle(h);
	}

	// check if the drive is now free
	if (!use_mount_point && (GetLogicalDrives() & 1 << (drive_select[0] - 'A'))) {
		MessageBox(hwnd_base, L"Error: the volume cannot be unmounted.", L"ImDisk", MB_ICONERROR);
		notif(RGB(255, 0, 0), L"Error: the volume cannot be unmounted.");
		apply_ok = TRUE;
		return 0;
	}

	current_MP = use_mount_point ? mount_point : drive_select;

	// build the ramdisk
	notif(RGB(255, 255, 0), L"Mounting new RamDisk...");
	if (dynamic) {
		swprintf(drive_id, L"%x", GetTickCount());
		swprintf(proxy_cmdline, L"RamDyn \"%s\" %s %I64u %u %u %u %u %u", current_MP, drive_id, (ULONGLONG)drive_size << (unit * 10), clean_ratio, clean_timer, clean_margin, max_activity, awealloc);
		start_process(proxy_cmdline, FALSE);
		swprintf(event_name, L"Global\\RamDyn%s_Response", drive_id);
		do Sleep(100);
		while (!(h = OpenEvent(EVENT_MODIFY_STATE, FALSE, event_name)));
		CloseHandle(h);
		swprintf(cmd_line, L"imdisk -a -t proxy -o shm -f RamDyn%s -m \"%s\" %s", drive_id, current_MP, add_param);
	} else if (mount_file)
		swprintf(cmd_line, L"imdisk -a %S -m \"%s\" %s -f \"%s\"", fileawe_list[awealloc], current_MP, add_param, image_file);
	else
		swprintf(cmd_line, L"imdisk -a -m \"%s\" %S%s -s %d%C", current_MP, awe_list[awealloc], add_param, drive_size, unit_list[unit]);

	start_process(cmd_line, TRUE);

	// format
	if (!mount_file) {
		swprintf(drive, L"%c:", use_mount_point ? MP_temp_letter[0] : drive_select[0]);
		if (use_mount_point) {
			cmd_line[8] = 0;
			h = CreateFile(mount_point, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
			DeviceIoControl(h, FSCTL_GET_REPARSE_POINT, NULL, 0, cmd_line, sizeof cmd_line, &data_size, NULL);
			CloseHandle(h);
			if (wcsncmp(&cmd_line[8], L"\\Device\\ImDisk", 14)) { // 16 = offset in REPARSE_DATA_BUFFER structure
				MessageBox(hwnd_base, L"Error: cannot retrieve mount point target.", L"ImDisk", MB_ICONERROR);
				notif(RGB(255, 0, 0), L"Error: cannot retrieve mount point target.");
				apply_ok = TRUE;
				return 0;
			}
			PathRemoveBackslash(&cmd_line[8]);
			DefineDosDevice(DDD_RAW_TARGET_PATH | DDD_NO_BROADCAST_SYSTEM, drive, &cmd_line[8]);
		}

		if (!PathFileExists(drive) && GetLastError() == ERROR_UNRECOGNIZED_VOLUME) {
			swprintf(cmd_line, L"format.com %s /fs:%s %S%S /y", drive, filesystem_list[filesystem], quickf_list[quick_format | dynamic], cluster_list[cluster]);
			start_process(cmd_line, TRUE);
			wcscpy(label_tmp, label);
			if (filesystem) label_tmp[11] = 0;
			drive[2] = L'\\';
			drive[3] = 0;
			SetVolumeLabel(drive, label_tmp);
			drive[2] = 0;
		}

		if (use_mount_point)
			DefineDosDevice(DDD_REMOVE_DEFINITION | DDD_NO_BROADCAST_SYSTEM, drive, NULL);
	}

	// check whether the drive is mounted and contains a valid file system
	swprintf(drive, L"%s\\", current_MP);
	if (!GetVolumeInformation(drive, NULL, 0, NULL, NULL, NULL, NULL, 0)) {
		if (GetLastError() == ERROR_UNRECOGNIZED_VOLUME) {
			MessageBox(hwnd_base, mount_file ? L"Error: the file does not contain a valid file system." : L"Error: the volume cannot be formatted.", L"ImDisk", MB_ICONERROR);
			notif(RGB(255, 0, 0), mount_file ? L"Error: the file does not contain a valid file system." : L"Error: the volume cannot be formatted.");
		} else {
			MessageBox(hwnd_base, L"Error: the volume cannot be mounted.", L"ImDisk", MB_ICONERROR);
			notif(RGB(255, 0, 0), L"Error: the volume cannot be mounted.");
		}
		apply_ok = TRUE;
		return 0;
	}

	// create temp folder
	if (!mount_file && temp_folder) {
		swprintf(temp_path, L"%s\\Temp", current_MP);
		CreateDirectory(temp_path, NULL);
	}

	// copy files
	if (mount_dir) {
		notif(RGB(255, 255, 0), L"Copying files...");
		PathAddBackslash(image_file);
		swprintf(copy_cmdline, L"xcopy \"%s*\" \"%s\" /e /c /q /h /k /y", image_file, current_MP);
		if (!filesystem) wcscat(copy_cmdline, L" /x");
		start_process(copy_cmdline, TRUE);
	}

	// show the mounted drive
	if (!use_mount_point) {
		notif(RGB(255, 255, 0), L"Displaying new drive...");
		ShExInf.fMask = SEE_MASK_INVOKEIDLIST;
		ShExInf.lpVerb = L"properties";
		ShExInf.lpFile = drive_select;
		ShExInf.nShow = SW_SHOWNORMAL;
		ShellExecuteEx(&ShExInf);
	}

	if (mount_current && reg_win_boot) {
		// copy current registered ramdisk
		param_name[0] = wanted_drive;
		param_name[1] = '_';
		copy_list_param(param_name);
	}

	// save parameters
	notif(RGB(255, 255, 0), L"Saving parameters...");
	wanted_drive = drive_select[0];
	reg_awealloc = awealloc;
	mount_current = win_boot & !use_mount_point;
	reg_use_MP = use_mount_point;
	reg_win_boot = win_boot;
	reg_dynamic = dynamic;
	PathRemoveBackslash(image_file);
	reg_set_dword(L"DriveSize", &drive_size);
	reg_set_dword(L"Unit", &unit);
	reg_set_dword(L"Dynamic", &dynamic);
	reg_set_dword(L"WantedDrive", &wanted_drive);
	reg_set_dword(L"FileSystem", &filesystem);
	reg_set_dword(L"WinBoot", &win_boot);
	reg_set_dword(L"TempFolder", &temp_folder);
	reg_set_dword(L"Cluster", &cluster);
	RegSetValueEx(registry_key, L"Label", 0, REG_SZ, (void*)&label, (wcslen(label) + 1) * sizeof(TCHAR));
	reg_set_dword(L"QuickFormat", &quick_format);
	reg_set_dword(L"Awealloc", &awealloc);
	reg_set_dword(L"CleanRatio", &clean_ratio);
	reg_set_dword(L"CleanTimer", &clean_timer);
	reg_set_dword(L"CleanMargin", &clean_margin);
	reg_set_dword(L"MaxActivity", &max_activity);
	reg_set_dword(L"RDUseMP", &use_mount_point);
	RegSetValueEx(registry_key, L"RDMountPoint", 0, REG_SZ, (void*)&mount_point, (wcslen(mount_point) + 1) * sizeof(TCHAR));
	RegSetValueEx(registry_key, L"ImageFile", 0, REG_SZ, (void*)&image_file, (wcslen(image_file) + 1) * sizeof(TCHAR));
	RegSetValueEx(registry_key, L"AddParam", 0, REG_SZ, (void*)&add_param, (wcslen(add_param) + 1) * sizeof(TCHAR));
	reg_set_dword(L"RDMountCurrent", &mount_current);

	if (!use_mount_point)
		// remove previous parameters in case of redefining of an existing ramdisk
		remove_reg_param();
	else {
		if (win_boot) {
			wcscpy(param_name + 1, L"_Unit");
			for (param_name[0] = '0'; param_name[0] <= '9'; param_name[0]++)
				if (reg_query_dword(param_name, &dw) != ERROR_SUCCESS) break;
			if (param_name[0] > '9')
				MessageBox(hwnd_base, L"Warning: too many mount points.\nThis one will not be registered.", L"ImDisk", MB_ICONWARNING);
			else {
				copy_list_param(param_name);
				load_mount_point();
			}
		}
		Button_Enable(hwnd_pbutton7, is_MP_imdisk_device());
	}

	// manage service
	notif(RGB(255, 255, 0), L"Managing service...");
	h_scman = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
	h_ImDiskRD = OpenService(h_scman, L"ImDiskRD", SERVICE_ALL_ACCESS);

	dep_awealloc = awealloc & ~dynamic & mount_current;
	search_awealloc(&prev_RD, &dep_awealloc);

	RegCreateKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run", 0, NULL, REG_OPTION_NON_VOLATILE, KEY_SET_VALUE | KEY_WOW64_64KEY, NULL, &h_key, NULL);
	if (win_boot || prev_RD) {
		if (h_ImDiskRD)
			ChangeServiceConfig(h_ImDiskRD, SERVICE_WIN32_OWN_PROCESS, SERVICE_AUTO_START, SERVICE_ERROR_NORMAL, svc_cmd_line, NULL, NULL, dep_list[dep_awealloc], NULL, NULL, NULL);
		else {
			h_ImDiskRD = CreateService(h_scman, L"ImDiskRD", L"ImDisk RamDisk starter", SERVICE_CHANGE_CONFIG, SERVICE_WIN32_OWN_PROCESS, SERVICE_AUTO_START, SERVICE_ERROR_NORMAL,
									   svc_cmd_line, NULL, NULL, dep_list[dep_awealloc], NULL, NULL);
			if (h_ImDiskRD) {
				svc_description.lpDescription = L"Mount a RamDisk.";
				ChangeServiceConfig2(h_ImDiskRD, SERVICE_CONFIG_DESCRIPTION, &svc_description);
			} else {
				CloseServiceHandle(h_scman);
				MessageBox(hwnd_base, L"Error: cannot create service.", L"ImDisk", MB_ICONERROR);
				notif(RGB(255, 0, 0), L"Error: cannot create service.");
				apply_ok = TRUE;
				return FALSE;
			}
		}
		RegSetValueEx(h_key, L"ImDisk_notif", 0, REG_SZ, (void*)&notif_cmd_line, (wcslen(notif_cmd_line) + 1) * sizeof(TCHAR));
	} else {
		DeleteService(h_ImDiskRD);
		RegDeleteValue(h_key, L"ImDisk_notif");
	}
	RegCloseKey(h_key);
	CloseServiceHandle(h_ImDiskRD);
	CloseServiceHandle(h_scman);

	notif(RGB(0, 255, 0), L"Done.");

	apply_ok = TRUE;
	return 0;
}

static BOOL __stdcall VarProc(HWND hDlg, UINT Msg, WPARAM wParam, LPARAM lParam)
{
	HKEY reg_key;
	DWORD data_size;
	TCHAR path[MAX_PATH];
	int i;

	switch (Msg)
	{
		case WM_INITDIALOG:
			SendMessage(hDlg, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);

			for (i = ID_EDIT5; i <= ID_EDIT9; i++)
				SendDlgItemMessage(hDlg, i, EM_SETLIMITTEXT, _countof(path) - 1, 0);

			RegOpenKeyExA(HKEY_CURRENT_USER, "Environment", 0, KEY_QUERY_VALUE, &reg_key);
			data_size = sizeof path;
			if (RegQueryValueEx(reg_key, L"TEMP", NULL, NULL, (void*)&path, &data_size) == ERROR_SUCCESS)
				SetDlgItemText(hDlg, ID_EDIT5, path);
			data_size = sizeof path;
			if (RegQueryValueEx(reg_key, L"TMP", NULL, NULL, (void*)&path, &data_size) == ERROR_SUCCESS)
				SetDlgItemText(hDlg, ID_EDIT6, path);
			RegCloseKey(reg_key);

			RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment", 0, KEY_QUERY_VALUE, &reg_key);
			data_size = sizeof path;
			if (RegQueryValueEx(reg_key, L"TEMP", NULL, NULL, (void*)&path, &data_size) == ERROR_SUCCESS)
				SetDlgItemText(hDlg, ID_EDIT7, path);
			data_size = sizeof path;
			if (RegQueryValueEx(reg_key, L"TMP", NULL, NULL, (void*)&path, &data_size) == ERROR_SUCCESS)
				SetDlgItemText(hDlg, ID_EDIT8, path);
			RegCloseKey(reg_key);

			swprintf(path, L"%s\\Temp", drive_select);
			SetDlgItemText(hDlg, ID_EDIT9, path);
			SendMessage(hDlg, WM_NEXTDLGCTL, (WPARAM)GetDlgItem(hDlg, ID_EDIT9), TRUE);

			return TRUE;

		case WM_COMMAND:
			if (LOWORD(wParam) == ID_PBUTTON5) {
				GetDlgItemText(hDlg, ID_EDIT9, path, _countof(path));
				for (i = ID_EDIT5; i <= ID_EDIT8; i++)
					SetDlgItemText(hDlg, i, path);
			}

			if (LOWORD(wParam) == ID_PBUTTON6) {
				RegOpenKeyExA(HKEY_USERS, ".DEFAULT\\Environment", 0, KEY_QUERY_VALUE, &reg_key);
				data_size = sizeof path;
				if (RegQueryValueEx(reg_key, L"TEMP", NULL, NULL, (void*)&path, &data_size) == ERROR_SUCCESS)
					SetDlgItemText(hDlg, ID_EDIT5, path);
				data_size = sizeof path;
				if (RegQueryValueEx(reg_key, L"TMP", NULL, NULL, (void*)&path, &data_size) == ERROR_SUCCESS)
					SetDlgItemText(hDlg, ID_EDIT6, path);
				RegCloseKey(reg_key);
				SetDlgItemText(hDlg, ID_EDIT7, L"%SystemRoot%\\TEMP");
				SetDlgItemText(hDlg, ID_EDIT8, L"%SystemRoot%\\TEMP");
			}

			if (LOWORD(wParam) == IDOK) {
				RegOpenKeyExA(HKEY_CURRENT_USER, "Environment", 0, KEY_SET_VALUE, &reg_key);
				GetDlgItemText(hDlg, ID_EDIT5, path, _countof(path));
				RegSetValueEx(reg_key, L"TEMP", 0, REG_EXPAND_SZ, (void*)&path, (wcslen(path) + 1) * sizeof(TCHAR));
				GetDlgItemText(hDlg, ID_EDIT6, path, _countof(path));
				RegSetValueEx(reg_key, L"TMP", 0, REG_EXPAND_SZ, (void*)&path, (wcslen(path) + 1) * sizeof(TCHAR));
				RegCloseKey(reg_key);

				RegOpenKeyEx(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment", 0, KEY_SET_VALUE, &reg_key);
				GetDlgItemText(hDlg, ID_EDIT7, path, _countof(path));
				RegSetValueEx(reg_key, L"TEMP", 0, REG_EXPAND_SZ, (void*)&path, (wcslen(path) + 1) * sizeof(TCHAR));
				GetDlgItemText(hDlg, ID_EDIT8, path, _countof(path));
				RegSetValueEx(reg_key, L"TMP", 0, REG_EXPAND_SZ, (void*)&path, (wcslen(path) + 1) * sizeof(TCHAR));
				RegCloseKey(reg_key);

				EndDialog(hDlg, 0);
			}

			if (LOWORD(wParam) == IDCANCEL)
				EndDialog(hDlg, 0);

			return TRUE;

		default:
			return FALSE;
	}
}

static BOOL __stdcall DynProc(HWND hDlg, UINT Msg, WPARAM wParam, LPARAM lParam)
{
	switch (Msg)
	{
		case WM_INITDIALOG:
			SendMessage(hDlg, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
			SetDlgItemText(hDlg, ID_TEXT11, L"The memory of a dynamic ramdisk is not freed when a file is deleted but periodically, through a cleanup function that "
											 "removes in one go all memory blocks that are no longer used. This function depends on the following parameters.");

			// add tooltips
			tool_info.hwnd = hDlg;
			tool_info.uId = (UINT_PTR)(hwnd_edit11 = GetDlgItem(hDlg, ID_EDIT11));
			add_tooltip(&tool_info);
			tool_info.uId = (UINT_PTR)(hwnd_edit12 = GetDlgItem(hDlg, ID_EDIT12));
			add_tooltip(&tool_info);
			tool_info.uId = (UINT_PTR)(hwnd_edit13 = GetDlgItem(hDlg, ID_EDIT13));
			add_tooltip(&tool_info);
			tool_info.uId = (UINT_PTR)(hwnd_edit14 = GetDlgItem(hDlg, ID_EDIT14));
			add_tooltip(&tool_info);

			// initialize controls
			SetDlgItemInt(hDlg, ID_EDIT11, clean_ratio, FALSE);
			SetDlgItemInt(hDlg, ID_EDIT12, clean_timer, FALSE);
			SetDlgItemInt(hDlg, ID_EDIT13, clean_margin, FALSE);
			SetDlgItemInt(hDlg, ID_EDIT14, max_activity, FALSE);

			return TRUE;

		case WM_NOTIFY:
			if (((NMHDR*)lParam)->code == TTN_GETDISPINFO) {
				if (((NMHDR*)lParam)->idFrom == (UINT_PTR)hwnd_edit11)
					((NMTTDISPINFO*)lParam)->lpszText = L"Default: 10.\n\nThe cleanup function runs only if this amount of memory\ncan be freed. The value is a ratio of the total drive space\n"
														 "(e.g.: 10/1000 of a 4 GB drive = 40 MB). This is only an estimate:\nthe memory amount actually freed can be slightly higher or lower.";
				if (((NMHDR*)lParam)->idFrom == (UINT_PTR)hwnd_edit12)
					((NMTTDISPINFO*)lParam)->lpszText = L"Default: 10.\n\nA low value can make the cleanup more responsive but may be inappropriate\nin case where files are regularly deleted without being recreated.";
				if (((NMHDR*)lParam)->idFrom == (UINT_PTR)hwnd_edit13)
					((NMTTDISPINFO*)lParam)->lpszText = L"Default: 10.\n\nDisk space reserved for other softwares that need to write data while\nthe cleanup is processing. "
														 "If memory blocks are located in this space,\nthey may not be freed (but may be freed at the next cleanup). A low\n"
														 "value can free a few more space but can create errors with softwares\nthat need to write data at the very end of the cleanup processing.";
				if (((NMHDR*)lParam)->idFrom == (UINT_PTR)hwnd_edit14)
					((NMTTDISPINFO*)lParam)->lpszText = L"Default: 10.\n\nWait until reads and writes are below this value before processing the cleanup.";
			}
			return TRUE;

		case WM_COMMAND:
			switch (LOWORD(wParam))
			{
				case IDOK:
					clean_ratio = GetDlgItemInt(hDlg, ID_EDIT11, NULL, FALSE);
					clean_timer = GetDlgItemInt(hDlg, ID_EDIT12, NULL, FALSE);
					clean_margin = GetDlgItemInt(hDlg, ID_EDIT13, NULL, FALSE);
					max_activity = GetDlgItemInt(hDlg, ID_EDIT14, NULL, FALSE);

				case IDCANCEL:
					SendMessage(hTTip, TTM_DELTOOL, 0, (LPARAM)&tool_info);
					tool_info.uId = (UINT_PTR)hwnd_edit13;
					SendMessage(hTTip, TTM_DELTOOL, 0, (LPARAM)&tool_info);
					tool_info.uId = (UINT_PTR)hwnd_edit12;
					SendMessage(hTTip, TTM_DELTOOL, 0, (LPARAM)&tool_info);
					tool_info.uId = (UINT_PTR)hwnd_edit11;
					SendMessage(hTTip, TTM_DELTOOL, 0, (LPARAM)&tool_info);
					EndDialog(hDlg, 0);
			}
			return TRUE;

		default:
			return FALSE;
	}
}

static BOOL __stdcall Tab1Proc(HWND hDlg, UINT Msg, WPARAM wParam, LPARAM lParam)
{
	HKEY reg_key;
	HANDLE h;
	DWORD mask, data_size, Version;
	TCHAR text[20];
	BOOL Translated;
	int n_drive, i;
	char param[4];

	switch (Msg)
	{
		case WM_INITDIALOG:
			hDlg1 = hDlg;

			// add tooltips
			tool_info.cbSize = sizeof(TOOLINFO);
			tool_info.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
			tool_info.hwnd = hDlg;
			tool_info.lpszText = LPSTR_TEXTCALLBACK;
			tool_info.uId = (UINT_PTR)(hwnd_edit1 = GetDlgItem(hDlg, ID_EDIT1));
			add_tooltip(&tool_info);
			tool_info.uId = (UINT_PTR)(hwnd_check1 = GetDlgItem(hDlg, ID_CHECK1));
			add_tooltip(&tool_info);
			tool_info.uId = (UINT_PTR)(hwnd_combo2 = GetDlgItem(hDlg, ID_COMBO2));
			add_tooltip(&tool_info);
			tool_info.uId = (UINT_PTR)(hwnd_pbutton3 = GetDlgItem(hDlg, ID_PBUTTON3));
			add_tooltip(&tool_info);
			tool_info.uId = (UINT_PTR)(hwnd_check2 = GetDlgItem(hDlg, ID_CHECK2));
			add_tooltip(&tool_info);

			// initialize controls
			SetDlgItemInt(hDlg, ID_EDIT1, drive_size, FALSE);

			CheckRadioButton(hDlg, ID_RB1, ID_RB3, ID_RB1 + unit);

			CheckDlgButton(hDlg, ID_CHECK1, dynamic);

			// set list of available drives
			ZeroMemory(drive_list, sizeof drive_list);
			drive_default = n_drive = 0;
			mask = mask0 | GetLogicalDrives();
			wcscpy(text, L"\\\\.\\A:");
			for (i = 'A'; i <= 'Z'; i++) {
				h = CreateFile(text, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
				if (!(mask & 1) || DeviceIoControl(h, IOCTL_IMDISK_QUERY_VERSION, NULL, 0, &Version, sizeof Version, &data_size, NULL)) {
					if (i == wanted_drive) drive_default = n_drive;
					drive_list[n_drive][0] = i;
					drive_list[n_drive][1] = ':';
					SendDlgItemMessage(hDlg, ID_COMBO1, CB_ADDSTRING, 0, (LPARAM)drive_list[n_drive++]);
				}
				CloseHandle(h);
				mask >>= 1;
				text[4]++;
			}
			SendDlgItemMessage(hDlg, ID_COMBO1, CB_SETCURSEL, drive_default, 0);

			for (i = 0; i < 3; i++)
				SendMessage(hwnd_combo2, CB_ADDSTRING, 0, (LPARAM)filesystem_list[i]);
			if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SYSTEM\\CurrentControlSet\\Services\\exfat", 0, KEY_QUERY_VALUE, &reg_key) == ERROR_SUCCESS) {
				SendMessage(hwnd_combo2, CB_ADDSTRING, 0, (LPARAM)filesystem_list[3]);
				RegCloseKey(reg_key);
			}
			SendMessage(hwnd_combo2, CB_SETCURSEL, filesystem, 0);

			CheckDlgButton(hDlg, ID_CHECK2, win_boot);
			CheckDlgButton(hDlg, ID_CHECK3, temp_folder);

			hwnd_base = GetParent(hDlg);
			item_enable = !mount_file;
			item_change = TRUE;

			// set notification circle
			MapDialogRect(hDlg, &circle);
			color = RGB(0, 255, 0);

			init1_ok = TRUE;
			return TRUE;

		case WM_NOTIFY:
			if (((NMHDR*)lParam)->code == TTN_GETDISPINFO) {
				if (((NMHDR*)lParam)->idFrom == (UINT_PTR)hwnd_edit1) {
					swprintf(TTip_txt, L"Min: %d %CB", (min_size[filesystem][cluster] + (1 << (unit * 10)) - 1) >> (unit * 10), unit_list[unit]);
					if (filesystem == 1 || filesystem == 2)
						swprintf(TTip_txt, L"%s\nMax: %d %CB", wcscpy(text, TTip_txt), max_size[filesystem - 1][cluster] >> (unit * 10), unit_list[unit]);
					((NMTTDISPINFO*)lParam)->lpszText = TTip_txt;
				}
				if (((NMHDR*)lParam)->idFrom == (UINT_PTR)hwnd_check1)
					((NMTTDISPINFO*)lParam)->lpszText = L"Instead of allocating the whole size, memory is allocated only\nwhen it is actually used. "
														 "This is slower but can save a lot of\nmemory if you are using only a small part of the ramdisk.";
				if (((NMHDR*)lParam)->idFrom == (UINT_PTR)hwnd_pbutton3)
					((NMTTDISPINFO*)lParam)->lpszText = L"Unmount selected ImDisk drive and unregister it.";
				if (((NMHDR*)lParam)->idFrom == (UINT_PTR)hwnd_combo2)
					((NMTTDISPINFO*)lParam)->lpszText = L"Default: NTFS.\n\n*FAT* file systems are usually faster but with less features.\nFAT and FAT32 also have limitations in the maximal volume size.";
				if (((NMHDR*)lParam)->idFrom == (UINT_PTR)hwnd_check2)
					((NMTTDISPINFO*)lParam)->lpszText = L"Once created, register the ramdisk to be started with Windows.\nSeveral ramdisks can be registered.";
			}

			if (((NMHDR*)lParam)->code == PSN_APPLY) {
				SetWindowLongPtr(hDlg, DWLP_MSGRESULT, use_mount_point ? PSNRET_INVALID_NOCHANGEPAGE : PSNRET_INVALID); // prevent PropertySheet to be closed with the OK button
				if (apply_ok) {
					apply_ok = FALSE;
					CreateThread(NULL, 0, ApplyParameters, NULL, 0, NULL);
				}
			}

			return TRUE;

		case WM_PAINT:
			circ_draw(hDlg);
			return TRUE;

		case WM_COMMAND:
			if (!init1_ok) return FALSE;

			// update parameters
			drive_size = GetDlgItemInt(hDlg, ID_EDIT1, &Translated, FALSE);
			if (!Translated) drive_size = UINT_MAX;
			for (i = 0; i < 3; i++)
				if (IsDlgButtonChecked(hDlg, ID_RB1 + i)) unit = i;
			dynamic = IsDlgButtonChecked(hDlg, ID_CHECK1);
			wcscpy(drive_select, drive_list[SendDlgItemMessage(hDlg, ID_COMBO1, CB_GETCURSEL, 0, 0)]);
			filesystem = SendMessage(hwnd_combo2, CB_GETCURSEL, 0, 0);
			win_boot = IsDlgButtonChecked(hDlg, ID_CHECK2);
			temp_folder = IsDlgButtonChecked(hDlg, ID_CHECK3);

			// manage controls activation
			if (item_change) {
				Static_Enable(GetDlgItem(hDlg, ID_TEXT3), item_enable);
				Edit_Enable(hwnd_edit1, item_enable);
				Button_Enable(GetDlgItem(hDlg, ID_RB1), item_enable);
				Button_Enable(GetDlgItem(hDlg, ID_RB2), item_enable);
				Button_Enable(GetDlgItem(hDlg, ID_RB3), item_enable);
				Button_Enable(GetDlgItem(hDlg, ID_CHECK1), item_enable);
				Static_Enable(GetDlgItem(hDlg, ID_TEXT5), item_enable);
				ComboBox_Enable(hwnd_combo2, item_enable);
				Button_Enable(GetDlgItem(hDlg, ID_CHECK3), item_enable);
				SetFocus(hDlg);
				item_change = FALSE;
			}
			swprintf(text, L"\\\\.\\%s", drive_select);
			h = CreateFile(text, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
			Button_Enable(hwnd_pbutton3, DeviceIoControl(h, IOCTL_IMDISK_QUERY_VERSION, NULL, 0, &Version, sizeof Version, &data_size, NULL));
			CloseHandle(h);

			if (LOWORD(wParam) == ID_CHECK1 && SeLockMemoryPrivilege_required())
				CheckDlgButton(hDlg, ID_CHECK1, dynamic = FALSE);

			if (LOWORD(wParam) == ID_PBUTTON3) {
				notif(RGB(255, 255, 0), L"Unmounting volume...");
				param[0] = drive_select[0];
				param[1] = ':';
				param[2] = 0;
				GetProcAddress(h_cpl, "RunDLL_RemoveDevice")(hDlg, NULL, param, 0);

				if (!PathFileExists(drive_select)) {
					Button_Enable(GetDlgItem(hDlg, ID_PBUTTON3), FALSE);
					SetFocus(hDlg);
					if (drive_select[0] == wanted_drive) {
						mount_current = FALSE;
						reg_set_dword(L"RDMountCurrent", &mount_current);
					}
					remove_reg_param();
					unmount_service();
				}

				notif(RGB(0, 255, 0), L"Done.");
			}

			if (LOWORD(wParam) == ID_PBUTTON4)
				DialogBox(hinst, L"VAR_DLG", hwnd_base, VarProc);

			return TRUE;

		default:
			return FALSE;
	}
}

static BOOL __stdcall Tab2Proc(HWND hDlg, UINT Msg, WPARAM wParam, LPARAM lParam)
{
	BROWSEINFO bi;
	LPITEMIDLIST pid_folder; // PIDLIST_ABSOLUTE on MSDN
	TCHAR text[4 * MAX_PATH + 100], sys_dir[MAX_PATH], temp_str[MAX_PATH + 10];;
	TOOLINFO ti;
	int i;

	switch (Msg)
	{
		case WM_INITDIALOG:
			hDlg2 = hDlg;

			// add tooltips
			ti.cbSize = sizeof(TOOLINFO);
			ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
			ti.hwnd = hDlg;
			ti.lpszText = LPSTR_TEXTCALLBACK;
			ti.uId = (UINT_PTR)(hwnd_combo3 = GetDlgItem(hDlg, ID_COMBO3));
			add_tooltip(&ti);
			ti.uId = (UINT_PTR)(hwnd_edit2 = GetDlgItem(hDlg, ID_EDIT2));
			add_tooltip(&ti);
			ti.uId = (UINT_PTR)(hwnd_check4 = GetDlgItem(hDlg, ID_CHECK4));
			add_tooltip(&ti);
			ti.uId = (UINT_PTR)(hwnd_check5 = GetDlgItem(hDlg, ID_CHECK5));
			add_tooltip(&ti);
			ti.uId = (UINT_PTR)(hwnd_pbutton7 = GetDlgItem(hDlg, ID_PBUTTON7));
			add_tooltip(&ti);
			combo5.cbSize = sizeof(COMBOBOXINFO);
			GetComboBoxInfo(GetDlgItem(hDlg, ID_COMBO5), &combo5);
			ti.uId = (UINT_PTR)(combo5.hwndItem);
			add_tooltip(&ti);
			ti.uId = (UINT_PTR)(hwnd_edit10 = GetDlgItem(hDlg, ID_EDIT10));
			add_tooltip(&ti);
			ti.uId = (UINT_PTR)(hwnd_edit3 = GetDlgItem(hDlg, ID_EDIT3));
			add_tooltip(&ti);
			ti.uId = (UINT_PTR)(hwnd_edit4 = GetDlgItem(hDlg, ID_EDIT4));
			add_tooltip(&ti);

			// initialize controls
			SendMessage(hwnd_combo3, CB_ADDSTRING, 0, (LPARAM)L"Default");
			SendMessage(hwnd_combo3, CB_ADDSTRING, 0, (LPARAM)L"0.5 KB");
			for (i = 1; i <= 64; i <<= 1) {
				swprintf(text, L"%d KB", i);
				SendMessage(hwnd_combo3, CB_ADDSTRING, 0, (LPARAM)text);
			}
			SendMessage(hwnd_combo3, CB_SETCURSEL, cluster, 0);

			SetDlgItemText(hDlg, ID_EDIT2, label);

			CheckDlgButton(hDlg, ID_CHECK4, quick_format);
			CheckDlgButton(hDlg, ID_CHECK5, awealloc);

			CheckDlgButton(hDlg, ID_CHECK6, use_mount_point);
			SendMessage(combo5.hwndCombo, CB_LIMITTEXT, _countof(mount_point) - 1, 0);
			load_mount_point();
			SendMessage(hwnd_edit10, EM_SETLIMITTEXT, 1, 0);
			SetDlgItemText(hDlg, ID_EDIT10, MP_temp_letter);

			SendMessage(hwnd_edit3, EM_SETLIMITTEXT, _countof(image_file) - 2, 0);
			SetDlgItemText(hDlg, ID_EDIT3, image_file);
			init_focus = TRUE;

			SendMessage(hwnd_edit4, EM_SETLIMITTEXT, _countof(add_param) - 1, 0);
			SetDlgItemText(hDlg, ID_EDIT4, add_param);

			init2_ok = TRUE;
			return TRUE;

		case WM_NOTIFY:
			if (((NMHDR*)lParam)->code == TTN_GETDISPINFO) {
				if (((NMHDR*)lParam)->idFrom == (UINT_PTR)hwnd_combo3)
					((NMTTDISPINFO*)lParam)->lpszText = L"Bigger clusters are usually faster but can use more\ndisk space, especially with a large number of files.";
				if (((NMHDR*)lParam)->idFrom == (UINT_PTR)hwnd_edit2)
					((NMTTDISPINFO*)lParam)->lpszText = L"Label size is limited by the file system:\n32 characters with NTFS, 11 with *FAT*.";
				if (((NMHDR*)lParam)->idFrom == (UINT_PTR)hwnd_check4)
					((NMTTDISPINFO*)lParam)->lpszText = L"Default: disabled.\n\nDo a quick format. It can speed up the RamDisk mounting but the allocated\n"
														 "memory does not appear as used in the Windows Task Manager.";
				if (((NMHDR*)lParam)->idFrom == (UINT_PTR)hwnd_check5)
					((NMTTDISPINFO*)lParam)->lpszText = L"Default: disabled.\n\nAllocate memory through AWE.\nThis guarantees that the memory is never written to the pagefile.\n\nOn 32-bit systems, "
														 "this also bypasses the 2GB and 4GB limits per\nprocess and allows to allocate as much memory as the system\nmanages (but not the unmanaged or invisible memory).";
				if (((NMHDR*)lParam)->idFrom == (UINT_PTR)hwnd_pbutton7)
					((NMTTDISPINFO*)lParam)->lpszText = L"Unmount selected ImDisk mount point and unregister it.";
				if (((NMHDR*)lParam)->idFrom == (UINT_PTR)combo5.hwndItem)
					((NMTTDISPINFO*)lParam)->lpszText = L"The mount point must be an empty directory located on a NTFS volume.\nUp to 10 mount points can be registered to be mounted at Windows startup.";
				if (((NMHDR*)lParam)->idFrom == (UINT_PTR)hwnd_edit10)
					((NMTTDISPINFO*)lParam)->lpszText = L"Drive letter temporarily used when a mount point has to be formatted.\nAll the mount points defined here use this drive letter.\n"
														 "Changes are registered immediately.";
				if (((NMHDR*)lParam)->idFrom == (UINT_PTR)hwnd_edit3)
					((NMTTDISPINFO*)lParam)->lpszText = L"If you select a folder, its entire content will be copied once the RamDisk\ncreated, with file attributes and ACL, depending on the file system.\n\n"
														 "Image files cannot be used with dynamic ramdisks.";
				if (((NMHDR*)lParam)->idFrom == (UINT_PTR)hwnd_edit4)
					((NMTTDISPINFO*)lParam)->lpszText = L"You can add here any parameter that is not already used by the GUI.\nFor example, \"-o rem\" will add removable attribute to the drive.\n"
														 "Click on the \"...\" button to list all parameters.";
			}
			return TRUE;

		case WM_PAINT:
			circ_draw(hDlg);
			return TRUE;

		case WM_COMMAND:
			if (!init2_ok) return FALSE;

			// update parameters
			cluster = SendMessage(hwnd_combo3, CB_GETCURSEL, 0, 0);
			SendMessage(hwnd_edit2, EM_SETLIMITTEXT, filesystem ? 11 : 32, 0);
			if (filesystem && wcslen(label) > 11) {
				label[11] = 0;
				SetDlgItemText(hDlg, ID_EDIT2, label);
			} else
				GetDlgItemText(hDlg, ID_EDIT2, label, 33);
			quick_format = IsDlgButtonChecked(hDlg, ID_CHECK4);
			awealloc = IsDlgButtonChecked(hDlg, ID_CHECK5);
			use_mount_point = IsDlgButtonChecked(hDlg, ID_CHECK6);
			GetDlgItemText(hDlg, ID_COMBO5, mount_point, _countof(mount_point));
			GetDlgItemText(hDlg, ID_EDIT4, add_param, _countof(add_param));

			// manage controls activation
			if (init_focus || (LOWORD(wParam) == ID_EDIT3 && HIWORD(wParam) == EN_CHANGE)) {
				GetDlgItemText(hDlg, ID_EDIT3, image_file, _countof(image_file) - 1);
				mount_dir = PathIsDirectory(image_file);
				mount_file = image_file[0] && !mount_dir;
				item_enable = !mount_file;
				Static_Enable(GetDlgItem(hDlg, ID_TEXT6), item_enable);
				ComboBox_Enable(hwnd_combo3, item_enable);
				Static_Enable(GetDlgItem(hDlg, ID_TEXT7), item_enable);
				Edit_Enable(hwnd_edit2, item_enable);
				item_change = TRUE;
			}
			if (init_focus || LOWORD(wParam) == ID_COMBO5) {
				if (HIWORD(wParam) == CBN_SELCHANGE)
					SendMessage(combo5.hwndCombo, CB_GETLBTEXT, SendMessage(combo5.hwndCombo, CB_GETCURSEL, 0, 0), (LPARAM)mount_point);
				Button_Enable(hwnd_pbutton7, is_MP_imdisk_device());
				if (init_focus) {
					SetFocus(hDlg);
					init_focus = FALSE;
				}
			}
			Button_Enable(hwnd_check4, item_enable & !dynamic);
			Button_Enable(GetDlgItem(hDlg, ID_PBUTTON9), item_enable & dynamic);

			if (LOWORD(wParam) == ID_CHECK5 && SeLockMemoryPrivilege_required())
				CheckDlgButton(hDlg, ID_CHECK5, awealloc = FALSE);

			if (LOWORD(wParam) == ID_EDIT10 && HIWORD(wParam) == EN_CHANGE) {
				GetDlgItemText(hDlg, ID_EDIT10, text, _countof(text));
				if (text[0]) {
					MP_temp_letter[0] = text[0];
					RegSetValueEx(registry_key, L"MPTempLetter", 0, REG_SZ, (void*)&MP_temp_letter, sizeof MP_temp_letter);
				}
			}

			if (LOWORD(wParam) == ID_PBUTTON1) {
				ZeroMemory(&bi, sizeof bi);
				bi.hwndOwner = hDlg;
				bi.pszDisplayName = image_file;
				bi.ulFlags = BIF_BROWSEINCLUDEFILES | BIF_RETURNONLYFSDIRS | BIF_USENEWUI | BIF_NONEWFOLDERBUTTON;
				pid_folder = SHBrowseForFolder(&bi);
				if (pid_folder) {
					SHGetPathFromIDList(pid_folder, image_file);
					SetDlgItemText(hDlg, ID_EDIT3, image_file);
				}
				else
					GetDlgItemText(hDlg, ID_EDIT3, image_file, _countof(image_file) - 1);
			}

			if (LOWORD(wParam) == ID_PBUTTON2) {
				GetSystemDirectory(sys_dir, _countof(sys_dir));
				GetTempPath(_countof(temp_str), temp_str);
				wcscat(temp_str, L"ImDisk");
				swprintf(text, L"cmd /c \"%s\\imdisk 2>\"%s\"&notepad \"%s\"&del \"%s\"\"", sys_dir, temp_str, temp_str, temp_str);
				start_process(text, FALSE);
			}

			if (LOWORD(wParam) == ID_PBUTTON7) {
				notif(RGB(255, 255, 0), L"Unmounting volume...");
				remove_mount_point();
				unmount_service();
				load_mount_point();
				Button_Enable(hwnd_pbutton7, is_MP_imdisk_device());
				SetFocus(hDlg);
				notif(RGB(0, 255, 0), L"Done.");
			}

			if (LOWORD(wParam) == ID_PBUTTON8) {
				ZeroMemory(&bi, sizeof bi);
				bi.hwndOwner = hDlg;
				bi.pszDisplayName = mount_point;
				bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_USENEWUI;
				pid_folder = SHBrowseForFolder(&bi);
				if (pid_folder) {
					SHGetPathFromIDList(pid_folder, mount_point);
					SetDlgItemText(hDlg, ID_COMBO5, mount_point);
				}
				else
					GetDlgItemText(hDlg, ID_COMBO5, mount_point, _countof(mount_point));
			}

			if (LOWORD(wParam) == ID_PBUTTON9)
				DialogBox(hinst, L"DYN_DLG", hwnd_base, DynProc);

			return TRUE;

		default:
			return FALSE;
	}
}

static void __stdcall SvcCtrlHandler(DWORD dwCtrl)
{
}

static void __stdcall SvcMain(DWORD dwArgc, LPTSTR *lpszArgv)
{
	SERVICE_STATUS_HANDLE SvcStatusHandle;
	TCHAR cmd_line[2 * MAX_PATH + 400];
	TCHAR proxy_cmdline[MAX_PATH + 50];
	TCHAR event_name[32];
	TCHAR temp_path[MAX_PATH + 5];
	TCHAR drive[4];
	DWORD data_size;
	DWORD reg_drive_size, reg_unit, reg_dynamic, reg_wanted_drive, reg_filesystem, reg_temp_folder, reg_cluster, reg_quick_format, reg_clean_ratio, reg_clean_timer, reg_clean_margin, reg_max_activity;
	TCHAR reg_label[33], reg_mount_point[MAX_PATH], reg_image_file[MAX_PATH + 1], reg_add_param[255];
	TCHAR *current_MP;
	BOOL reg_mount_file, reg_mount_dir;
	HANDLE h;

	SvcStatusHandle = RegisterServiceCtrlHandler(L"", SvcCtrlHandler);
	SetServiceStatus(SvcStatusHandle, &SvcStatus);

	reg_unit = 1;
	reg_dynamic = FALSE;
	reg_filesystem = 0;
	reg_temp_folder = TRUE;
	reg_cluster = 0;
	wcscpy(reg_label, L"RamDisk");
	reg_quick_format = FALSE;
	reg_awealloc = FALSE;
	reg_clean_ratio = 10;
	reg_clean_timer = 10;
	reg_clean_margin = 10;
	reg_max_activity = 10;
	reg_mount_point[0] = 0;
	reg_image_file[0] = 0;
	reg_add_param[0] = 0;

	key_name[1] = '_';
	for (key_name[0] = '0'; key_name[0] <= 'Z'; key_name[0] == '9' ? key_name[0] = 'A' : key_name[0]++) {
		if (param_reg_query_dword(L"DriveSize", &reg_drive_size) == ERROR_SUCCESS) {
			reg_wanted_drive = key_name[0];
			param_reg_query_dword(L"Unit", &reg_unit);
			param_reg_query_dword(L"Dynamic", &reg_dynamic);
			param_reg_query_dword(L"FileSystem", &reg_filesystem);
			param_reg_query_dword(L"TempFolder", &reg_temp_folder);
			param_reg_query_dword(L"Cluster", &reg_cluster);
			wcscpy(&key_name[2], L"Label");
			data_size = sizeof reg_label;
			RegQueryValueEx(registry_key, key_name, NULL, NULL, (void*)&reg_label, &data_size);
			param_reg_query_dword(L"QuickFormat", &reg_quick_format);
			param_reg_query_dword(L"Awealloc", &reg_awealloc);
			param_reg_query_dword(L"CleanRatio", &reg_clean_ratio);
			param_reg_query_dword(L"CleanTimer", &reg_clean_timer);
			param_reg_query_dword(L"CleanMargin", &reg_clean_margin);
			param_reg_query_dword(L"MaxActivity", &reg_max_activity);
			wcscpy(&key_name[2], L"RDMountPoint");
			data_size = sizeof reg_mount_point;
			RegQueryValueEx(registry_key, key_name, NULL, NULL, (void*)&reg_mount_point, &data_size);
			wcscpy(&key_name[2], L"ImageFile");
			data_size = sizeof reg_image_file;
			RegQueryValueEx(registry_key, key_name, NULL, NULL, (void*)&reg_image_file, &data_size);
			wcscpy(&key_name[2], L"AddParam");
			data_size = sizeof reg_add_param;
			RegQueryValueEx(registry_key, key_name, NULL, NULL, (void*)&reg_add_param, &data_size);
		} else if (key_name[0] == wanted_drive && mount_current) {
			reg_drive_size = drive_size;
			reg_unit = unit;
			reg_dynamic = dynamic;
			reg_wanted_drive = wanted_drive;
			reg_filesystem = filesystem;
			reg_temp_folder = temp_folder;
			reg_cluster = cluster;
			wcscpy(reg_label, label);
			reg_quick_format = quick_format;
			reg_awealloc = awealloc;
			reg_clean_ratio = clean_ratio;
			reg_clean_timer = clean_timer;
			reg_clean_margin = clean_margin;
			reg_max_activity = max_activity;
			wcscpy(reg_image_file, image_file);
			wcscpy(reg_add_param, add_param);
		} else continue;

		reg_mount_dir = PathIsDirectory(reg_image_file);
		reg_mount_file = reg_image_file[0] && !reg_mount_dir;

		drive[1] = L':';
		drive[2] = 0;
		if (key_name[0] <= '9') {
			drive[0] = MP_temp_letter[0];
			current_MP = reg_mount_point;
		} else {
			drive[0] = reg_wanted_drive;
			current_MP = drive;
		}
		if (PathFileExists(drive) || GetLastError() != ERROR_PATH_NOT_FOUND) continue;

		if (reg_dynamic) {
			swprintf(proxy_cmdline, L"RamDyn \"%s\" %c %I64u %u %u %u %u %u", current_MP, key_name[0], (ULONGLONG)reg_drive_size << (reg_unit * 10), reg_clean_ratio, reg_clean_timer, reg_clean_margin, reg_max_activity, awealloc);
			start_process(proxy_cmdline, FALSE);
			swprintf(event_name, L"Global\\RamDyn%c_Response", key_name[0]);
			do Sleep(100);
			while (!(h = OpenEvent(EVENT_MODIFY_STATE, FALSE, event_name)));
			CloseHandle(h);
			swprintf(cmd_line, L"imdisk -a -t proxy -o shm -f RamDyn%c -m \"%s\" %s", key_name[0], current_MP, reg_add_param);
		} else if (reg_mount_file)
			swprintf(cmd_line, L"imdisk -a %S -m \"%s\" %s -f \"%s\"", fileawe_list[reg_awealloc], current_MP, reg_add_param, reg_image_file);
		else
			swprintf(cmd_line, L"imdisk -a -m \"%s\" %S%s -s %d%C", current_MP, awe_list[reg_awealloc], reg_add_param, reg_drive_size, unit_list[reg_unit]);

		start_process(cmd_line, TRUE);

		if (reg_mount_file) continue;

		if (key_name[0] <= '9') {
			cmd_line[8] = 0;
			h = CreateFile(reg_mount_point, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
			DeviceIoControl(h, FSCTL_GET_REPARSE_POINT, NULL, 0, cmd_line, sizeof cmd_line, &data_size, NULL);
			CloseHandle(h);
			if (wcsncmp(&cmd_line[8], L"\\Device\\ImDisk", 14)) continue;
			PathRemoveBackslash(&cmd_line[8]);
			DefineDosDevice(DDD_RAW_TARGET_PATH | DDD_NO_BROADCAST_SYSTEM, drive, &cmd_line[8]);
		}

		if (!PathFileExists(drive) && GetLastError() == ERROR_UNRECOGNIZED_VOLUME) {
			swprintf(cmd_line, L"format.com %c: /fs:%s %S%S /y", drive[0], filesystem_list[reg_filesystem], quickf_list[reg_quick_format | reg_dynamic], cluster_list[reg_cluster]);
			start_process(cmd_line, TRUE);
			if (reg_filesystem) reg_label[11] = 0;
			drive[2] = L'\\';
			drive[3] = 0;
			SetVolumeLabel(drive, reg_label);
			drive[2] = 0;
		} else {
			if (key_name[0] <= '9')
				DefineDosDevice(DDD_REMOVE_DEFINITION | DDD_NO_BROADCAST_SYSTEM, drive, NULL);
			continue;
		}

		if (key_name[0] <= '9')
			DefineDosDevice(DDD_REMOVE_DEFINITION | DDD_NO_BROADCAST_SYSTEM, drive, NULL);

		if (reg_temp_folder) {
			swprintf(temp_path, L"%s\\Temp", current_MP);
			CreateDirectory(temp_path, NULL);
		}

		if (reg_mount_dir) {
			PathAddBackslash(reg_image_file);
			swprintf(cmd_line, L"xcopy \"%s*\" \"%s\" /e /c /q /h /k /y", reg_image_file, current_MP);
			if (!reg_filesystem) wcscat(cmd_line, L" /x");
			start_process(cmd_line, TRUE);
		}
	}

	SvcStatus.dwCurrentState = SERVICE_STOPPED;
	SetServiceStatus(SvcStatusHandle, &SvcStatus);
}

static void notif_app()
{
	TCHAR drive[4];
	TCHAR param_name[12];
	DWORD reg_drive_size;
	DEV_BROADCAST_VOLUME dbv;
	int i;

	dbv.dbcv_size = sizeof dbv;
	dbv.dbcv_devicetype = DBT_DEVTYP_VOLUME;
	dbv.dbcv_flags = 0;
	drive[1] = ':';
	drive[2] = 0;
	param_name[1] = '_';
	wcscpy(&param_name[2], L"DriveSize");
	for (param_name[0] = 'A'; param_name[0] <= 'Z'; param_name[0]++) {
		if ((param_name[0] == wanted_drive && mount_current) || reg_query_dword(param_name, &reg_drive_size) == ERROR_SUCCESS) {
			drive[0] = param_name[0];
			for (i = 0; i < 120; i++) {
				if (PathFileExists(drive)) {
					dbv.dbcv_unitmask = 1 << (drive[0] - 'A');
					SendMessageTimeout(HWND_BROADCAST, WM_DEVICECHANGE, DBT_DEVICEARRIVAL, (LPARAM)&dbv, SMTO_BLOCK | SMTO_ABORTIFHUNG, 4000, NULL);
					SendMessageTimeout(HWND_BROADCAST, WM_DEVICECHANGE, DBT_DEVNODES_CHANGED, 0, SMTO_BLOCK | SMTO_ABORTIFHUNG, 4000, NULL);
					SHChangeNotify(SHCNE_DRIVEADD, SHCNF_PATH, drive, NULL);
					break;
				}
				Sleep(1000);
			}
		}
	}
}


int __stdcall wWinMain(HINSTANCE hinstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow)
{
	PROPSHEETPAGE psp[2];
	PROPSHEETHEADER psh = {sizeof psh};
	DWORD data_size;
	OSVERSIONINFO os_ver;
	int argc;
	LPWSTR *argv;
	SERVICE_TABLE_ENTRY DispatchTable[] = {{L"", SvcMain}, {NULL, NULL}};

	os_ver.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
	GetVersionEx(&os_ver);

	// load default values
	drive_size = 64;
	unit = 1;
	dynamic = FALSE;
	wanted_drive = 'R';
	filesystem = 0;
	win_boot = TRUE;
	temp_folder = TRUE;
	cluster = 0;
	wcscpy(label, L"RamDisk");
	quick_format = FALSE;
	awealloc = FALSE;
	clean_ratio = 10;
	clean_timer = 10;
	clean_margin = 10;
	max_activity = 10;
	use_mount_point = 0;
	mount_point[0] = 0;
	wcscpy(MP_temp_letter, L"Z");
	image_file[0] = 0;
	add_param[0] = 0;
	mount_current = TRUE;

	// get registry values
	RegCreateKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\ImDisk", 0, NULL, REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS | KEY_WOW64_64KEY, NULL, &registry_key, NULL);
	reg_query_dword(L"DriveSize", &drive_size);
	reg_query_dword(L"Unit", &unit);
	reg_query_dword(L"Dynamic", &dynamic);
	reg_query_dword(L"WantedDrive", &wanted_drive);
	reg_query_dword(L"FileSystem", &filesystem);
	reg_query_dword(L"WinBoot", &win_boot);
	reg_query_dword(L"TempFolder", &temp_folder);
	reg_query_dword(L"Cluster", &cluster);
	data_size = sizeof label;
	RegQueryValueEx(registry_key, L"Label", NULL, NULL, (void*)&label, &data_size);
	reg_query_dword(L"QuickFormat", &quick_format);
	reg_query_dword(L"Awealloc", &awealloc);
	reg_query_dword(L"CleanRatio", &clean_ratio);
	reg_query_dword(L"CleanTimer", &clean_timer);
	reg_query_dword(L"CleanMargin", &clean_margin);
	reg_query_dword(L"MaxActivity", &max_activity);
	reg_query_dword(L"RDUseMP", &use_mount_point);
	data_size = sizeof mount_point;
	RegQueryValueEx(registry_key, L"RDMountPoint", NULL, NULL, (void*)&mount_point, &data_size);
	data_size = sizeof MP_temp_letter;
	RegQueryValueEx(registry_key, L"MPTempLetter", NULL, NULL, (void*)&MP_temp_letter, &data_size);
	data_size = sizeof image_file;
	RegQueryValueEx(registry_key, L"ImageFile", NULL, NULL, (void*)&image_file, &data_size);
	data_size = sizeof add_param;
	RegQueryValueEx(registry_key, L"AddParam", NULL, NULL, (void*)&add_param, &data_size);
	reg_query_dword(L"RDMountCurrent", &mount_current);

	reg_use_MP = use_mount_point;
	reg_awealloc = awealloc;
	reg_win_boot = win_boot;
	reg_dynamic = dynamic;
	mount_dir = PathIsDirectory(image_file);
	mount_file = image_file[0] && !mount_dir;

	argv = CommandLineToArgvW(GetCommandLine(), &argc);

	if (argc > 1) {
		if (!wcscmp(argv[1], L"SVC")) {
			StartServiceCtrlDispatcher(DispatchTable);
			return 0;
		}
		if (!wcscmp(argv[1], L"NOTIF")) {
			notif_app();
			return 0;
		}
	}

	if ((os_ver.dwMajorVersion >= 6) && (argc <= 1 || wcscmp(argv[1], L"UAC"))) {
		// send non-elevated drive list to the elevated process
		swprintf(add_param, L"UAC %d", GetLogicalDrives());
		ShellExecute(NULL, L"runas", argv[0], add_param, NULL, nCmdShow);
		return 0;
	}

	mask0 = os_ver.dwMajorVersion < 6 ? 0 : _wtoi(argv[2]);

	hinst = hinstance;
	hIcon = LoadImage(hinst, MAKEINTRESOURCE(2), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE);
	if (!(h_cpl = LoadLibraryA("imdisk.cpl")))
		MessageBox(NULL, L"Warning: cannot find imdisk.cpl. Please reinstall the driver.", L"ImDisk", MB_ICONWARNING);
	GetModuleFileName(NULL, svc_cmd_line, _countof(svc_cmd_line));
	PathQuoteSpaces(svc_cmd_line);
	wcscpy(notif_cmd_line, svc_cmd_line);
	wcscat(svc_cmd_line, L" SVC");
	wcscat(notif_cmd_line, L" NOTIF");

	// set up the property sheet
	ZeroMemory(psp, sizeof psp);

	psp[0].dwSize = sizeof(PROPSHEETPAGE);
	psp[0].dwFlags = PSP_USETITLE | PSP_PREMATURE;
	psp[0].hInstance = hinst;
	psp[0].pszTemplate = L"TAB1";
	psp[0].pfnDlgProc = Tab1Proc;
	psp[0].pszTitle = L"Basic";

	psp[1].dwSize = sizeof(PROPSHEETPAGE);
	psp[1].dwFlags = PSP_USETITLE | PSP_PREMATURE;
	psp[1].hInstance = hinst;
	psp[1].pszTemplate = L"TAB2";
	psp[1].pfnDlgProc = Tab2Proc;
	psp[1].pszTitle = L"Advanced";

	psh.dwFlags = PSH_NOAPPLYNOW | PSH_PROPSHEETPAGE | PSH_NOCONTEXTHELP | PSH_USEHICON;
	psh.hIcon = hIcon;
	psh.pszCaption = L"ImDisk";
	psh.nPages = 2;
	psh.ppsp = (LPCPROPSHEETPAGE)&psp;

	// initialize tooltips
	hTTip = CreateWindow(TOOLTIPS_CLASS, NULL, TTS_NOPREFIX, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, NULL, NULL, hinst, NULL); 
	SendMessage(hTTip, TTM_SETMAXTIPWIDTH, 0, 1000);
	SendMessage(hTTip, TTM_SETDELAYTIME, TTDT_AUTOPOP, 20000);

	PropertySheet(&psh);

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
