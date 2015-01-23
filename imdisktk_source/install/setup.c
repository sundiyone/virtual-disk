#define OEMRESOURCE
#include <windows.h>
#include <windowsx.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <stdio.h>
#include <setupapi.h>
#include <ntsecapi.h>
#include "resource.h"
#include "..\inc\imdisk.h"

#define APP_VERSION "20141217"
#define HEADER_BACKGROUND RGB(255, 255, 255)

static OSVERSIONINFO os_ver;
static HINSTANCE hinst;
static HICON hIcon, hIconWarn;
static HBRUSH hBrush;
static RECT icon_coord, iconwarn_coord;
static HWND hwnd_check[6], hwnd_text1, hwnd_static1, hwnd_static2, hwnd_static3, hwnd_uninst;
static WPARAM prev_wparam = 0;
static int prev_mark = 0;
static BOOL copy_error = FALSE, reboot = FALSE, process_uninst = FALSE;
static DWORD hid_drive_ini;
static HANDLE h_file;
static HKEY reg_key;
static int silent = 0;

static TCHAR path[MAX_PATH + 40], *path_name_ptr;
static TCHAR desk[MAX_PATH + 40], *desk_ptr;
static TCHAR cmd[32768];

static TCHAR version_str[] = L"ImDisk Toolkit\n" APP_VERSION;

static char *description[] = {"Install driver. This includes a control panel applet, a command line tool, imdisk.exe, and interfaces to manage volumes from Explorer context menus.\n\n"
									"Please type \"imdisk\" in a command prompt for details.",
							  "Required to mount advanced image file formats like VHD, VDI, VMDK and some others.\n\n"
									"Also adds a command line tool in the installation folder, DiscUtilsDevio.exe, and a dedicated dialog box to use the library from file context menu in Explorer.",
							  "This tool will allow you to easily configure one or several RamDisks with various settings.",
							  "Add entries in the drives context menu and the files context menu to manage ImDisk volumes and images files directly from Explorer.\n\nThis option can be set later in the Windows Start Menu.",
							  "This affects the commands in the drive context menu: \"Save disk contents as image file\" and \"Unmount ImDisk Virtual Disk\".\n\nSolves some UAC related issues.\n\n"
									"This option can be set later in the Windows Start Menu.",
							  "Create shortcuts on desktop.\n\nAll shortcuts are always created in the Windows Start Menu."};

static TCHAR reg_string_0[] = L"rundll32.exe imdisk.cpl,RunDLL_MountFile %L";
static TCHAR reg_string_1[] = L"rundll32.exe imdisk.cpl,RunDLL_SaveImageFile %L";
static TCHAR reg_string_2[] = L"rundll32.exe imdisk.cpl,RunDLL_RemoveDevice %L";
static char reg_string_3[] = "ImDisk Toolkit";
static char reg_string_4[] = "ImDisk Virtual Disk Driver";

static TCHAR *svc_list[] = {L"ImDskSvc", L"AWEAlloc", L"ImDisk"};

static DWORD EstimatedSize = 1813;


static void CreateLink(TCHAR *PathObj, TCHAR *PathLink, TCHAR *ArgV)
{
	IShellLink *psl;
	IPersistFile *ppf;

	if (SUCCEEDED(CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, &IID_IShellLink, (void**)&psl))) {
		psl->lpVtbl->SetPath(psl, PathObj);
		psl->lpVtbl->SetArguments(psl, ArgV);
		if (SUCCEEDED(psl->lpVtbl->QueryInterface(psl, &IID_IPersistFile, (void**)&ppf))) {
			ppf->lpVtbl->Save(ppf, PathLink, TRUE);
			ppf->lpVtbl->Release(ppf);
		}
		psl->lpVtbl->Release(psl);
	}
}

static void start_process(BOOL wait)
{
	STARTUPINFO si = {sizeof si};
	PROCESS_INFORMATION pi;
	HMODULE hDLL;
	FARPROC lpFunc;
	void *ptr;

	hDLL = GetModuleHandleA("kernel32.dll");
	if ((lpFunc = GetProcAddress(hDLL, "Wow64DisableWow64FsRedirection")))
		lpFunc(&ptr);
	CreateProcess(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
	if (lpFunc)
		GetProcAddress(hDLL, "Wow64RevertWow64FsRedirection")(ptr);

	if (wait) WaitForSingleObject(pi.hProcess, INFINITE);
	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);
}

static void del_key(HKEY hKey, char *key)
{
	HMODULE hDLL;
	FARPROC lpFunc;

	hDLL = GetModuleHandleA("advapi32.dll");
	if ((lpFunc = GetProcAddress(hDLL, "RegDeleteKeyExA")))
		lpFunc(hKey, key, KEY_WOW64_64KEY, 0);
	else if ((lpFunc = GetProcAddress(hDLL, "RegDeleteKeyA")))
		lpFunc(hKey, key);
}

static void del_command_key(char *key)
{
	char name[50];

	del_key(HKEY_CLASSES_ROOT, strcat(strcpy(name, key), "\\command"));
	del_key(HKEY_CLASSES_ROOT, key);
}

static void write_key(char *key, TCHAR *value)
{
	HKEY h_key;

	RegCreateKeyExA(HKEY_CLASSES_ROOT, key, 0, NULL, REG_OPTION_NON_VOLATILE, KEY_SET_VALUE | KEY_WOW64_64KEY, NULL, &h_key, NULL);
	RegSetValueEx(h_key, NULL, 0, REG_SZ, (void*)value, (wcslen(value) + 1) * sizeof(TCHAR));
	RegCloseKey(h_key);
}

static void move(TCHAR *file)
{
	TCHAR name_old[MAX_PATH + 40];

	wcscpy(path_name_ptr, file);
	if (!CopyFile(file, path, FALSE)) {
		wcscpy(name_old, path);
		wcscpy(&name_old[wcslen(name_old) - 3], L"old");
		MoveFileEx(path, name_old, MOVEFILE_REPLACE_EXISTING);
		if (CopyFile(file, path, FALSE))
			reboot = TRUE;
		else {
			if (!copy_error) MessageBox(NULL, L"File copy error.", L"ImDisk - Setup", MB_ICONERROR);
			copy_error = TRUE;
		}
	}
}

static void del(TCHAR *file)
{
	wcscpy(path_name_ptr, file);
	DeleteFile(path);
}

static void write_context_menu(TCHAR *path, BOOL admin_required)
{
	TCHAR path_test[MAX_PATH + 20];

	write_key("*\\shell\\ImDiskMountFile", L"Mount as ImDisk Virtual Disk");
	if (path) {
		swprintf(path_test, L"%sMountImg.exe", path);
		swprintf(cmd, L"\"%sMountImg.exe\" \"%%L\"", path);
		write_key("*\\shell\\ImDiskMountFile\\command", PathFileExists(path_test) ? cmd : reg_string_0);
	} else
		write_key("*\\shell\\ImDiskMountFile\\command", reg_string_0);

	write_key("Drive\\shell\\ImDiskSaveImage", L"Save disk contents as image file");
	if (path) swprintf(cmd, L"\"%s%s\" SaveImageFile %%L", path, L"ImDisk-UAC.exe");
	write_key("Drive\\shell\\ImDiskSaveImage\\command", admin_required ? cmd : reg_string_1);

	write_key("Drive\\shell\\ImDiskUnmount", L"Unmount ImDisk Virtual Disk");
	if (path) swprintf(cmd, L"\"%s%s\" RemoveDevice %%L", path, L"ImDisk-UAC.exe");
	write_key("Drive\\shell\\ImDiskUnmount\\command", admin_required ? cmd : reg_string_2);
}


static BOOL __stdcall CreditsProc(HWND hDlg, UINT Msg, WPARAM wParam, LPARAM lParam)
{
	switch (Msg)
	{
		case WM_INITDIALOG:
			SendMessage(hDlg, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
			SetDlgItemTextA(hDlg, ID_EDIT1,
						   "@ Olof Lagerkvist: ImDisk Virtual Disk Driver, and several tools to use the DiscUtils library\r\n\r\n"
						   "@ v77: RamDisk Configuration Tool, dialog box using DiscUtils and installer of this package\r\n\r\n"
						   "@ Kenneth Bell: DiscUtils .NET library\r\n\r\n"
						   "@ Igor Pavlov: compressor and small 7z-SFX module used for the package\r\n\r\n"
						   "and all those who are involved, directly or not, in the development of the ImDisk Toolkit.");
			SetFocus(GetDlgItem(hDlg, IDOK));
			return FALSE;

		case WM_COMMAND:
			if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
				EndDialog(hDlg, 0);
			return TRUE;

		default:
			return FALSE;
	}
}


static BOOL __stdcall DotNetProc(HWND hDlg, UINT Msg, WPARAM wParam, LPARAM lParam)
{
	PAINTSTRUCT paint;
	SHELLEXECUTEINFO ShExInf;

	switch (Msg)
	{
		case WM_INITDIALOG:
			hIconWarn = LoadImage(NULL, MAKEINTRESOURCE(OIC_WARNING), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE | LR_SHARED);
			iconwarn_coord.left = 14;
			iconwarn_coord.top = 18;
			iconwarn_coord.right = iconwarn_coord.bottom = 0;
			MapDialogRect(hDlg, &iconwarn_coord);
			MessageBeep(MB_ICONWARNING);
			return TRUE;

		case WM_PAINT:
			DrawIcon(BeginPaint(hDlg, &paint), iconwarn_coord.left, iconwarn_coord.top, hIconWarn);
			EndPaint(hDlg, &paint);
			return TRUE;

		case WM_NOTIFY:
			if ((((NMHDR*)lParam)->code == NM_CLICK || ((NMHDR*)lParam)->code == NM_RETURN) && (((NMHDR*)lParam)->hwndFrom == GetDlgItem(hDlg, ID_LINK))) {
				ZeroMemory(&ShExInf, sizeof ShExInf);
				ShExInf.cbSize = sizeof ShExInf;
				ShExInf.fMask = SEE_MASK_CLASSNAME;
				ShExInf.lpFile = ((NMLINK*)lParam)->item.szUrl;
				ShExInf.nShow = SW_SHOWNORMAL;
				ShExInf.lpClass = L"http";
				ShellExecuteEx(&ShExInf);
			}
			return TRUE;

		case WM_COMMAND:
			if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
				EndDialog(hDlg, 0);
			return TRUE;

		default:
			return FALSE;
	}
}


static void install(HWND hDlg)
{
	SERVICE_DESCRIPTION svc_description;
	SC_HANDLE scman_handle, ImDisk_handle;
	HKEY h_key;
	TCHAR startmenu[MAX_PATH + 40], *startmenu_ptr;
	BOOL driver_ok, desk_lnk, is_wow64, show_dotnet = FALSE, dep_awealloc, priv_req;
	DWORD data_size, RD_found, awealloc, dynamic;
	TCHAR privilege_name[] = L"SeLockMemoryPrivilege";
	HANDLE token = INVALID_HANDLE_VALUE;
	TOKEN_PRIVILEGES tok_priv;
	LSA_HANDLE lsa_h = INVALID_HANDLE_VALUE;
	LSA_OBJECT_ATTRIBUTES lsa_oa = {};
	unsigned char sid[SECURITY_MAX_SID_SIZE];
	DWORD sid_size = sizeof sid;
	LSA_UNICODE_STRING lsa_str = {sizeof privilege_name - sizeof(TCHAR), sizeof privilege_name, privilege_name};
	int i;

	GetDlgItemText(hDlg, ID_EDIT1, path, MAX_PATH);
	if (!CreateDirectory(path, NULL) && (GetLastError() != ERROR_ALREADY_EXISTS)) {
		if (silent < 2) MessageBox(hDlg, L"Invalid path.", L"ImDisk - Setup", MB_ICONERROR);
		return;
	}
	path_name_ptr = PathAddBackslash(path);

	Button_Enable(GetDlgItem(hDlg, IDOK), FALSE);
	SetDlgItemTextA(hDlg, IDOK, "Installing...");
	SetCursor(LoadImage(NULL, MAKEINTRESOURCE(OCR_WAIT), IMAGE_CURSOR, 0, 0, LR_DEFAULTSIZE | LR_SHARED));

	move(L"setup.exe");

	// Shortcuts
	SHGetFolderPath(NULL, CSIDL_PROGRAMS, NULL, SHGFP_TYPE_CURRENT, startmenu);
	wcscat(startmenu, L"\\ImDisk");
	CreateDirectory(startmenu, NULL);
	startmenu_ptr = PathAddBackslash(startmenu);
	wcscpy(startmenu_ptr, L"Uninstall.lnk");
	CreateLink(path, startmenu, L"/u");
	wcscpy(startmenu_ptr, L"General Settings.lnk");
	CreateLink(path, startmenu, NULL);
	desk_lnk = IsDlgButtonChecked(hDlg, ID_CHECK6);

	// Uninstall settings
	RegCreateKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\ImDiskApp", 0, NULL, REG_OPTION_NON_VOLATILE, KEY_SET_VALUE | KEY_WOW64_64KEY, NULL, &h_key, NULL);
	RegSetValueEx(h_key, L"DisplayIcon", 0, REG_SZ, (void*)path, (wcslen(path) + 1) * sizeof(TCHAR));
	RegSetValueExA(h_key, "DisplayName", 0, REG_SZ, (void*)reg_string_3, sizeof reg_string_3);
	RegSetValueExA(h_key, "DisplayVersion", 0, REG_SZ, (void*)APP_VERSION, sizeof APP_VERSION);
	RegSetValueExA(h_key, "EstimatedSize", 0, REG_DWORD, (void*)&EstimatedSize, sizeof EstimatedSize);
	wcscat(path_name_ptr, L" /u");
	RegSetValueEx(h_key, L"UninstallString", 0, REG_SZ, (void*)path, (wcslen(path) + 1) * sizeof(TCHAR));
	RegCloseKey(h_key);

	// Driver
	if (IsDlgButtonChecked(hDlg, ID_CHECK1)) {
		scman_handle = OpenSCManager(NULL, NULL, SC_MANAGER_CONNECT);
		if ((ImDisk_handle = OpenServiceA(scman_handle, "ImDisk", SERVICE_QUERY_CONFIG))) {
			reboot = TRUE;
			CloseServiceHandle(ImDisk_handle);
		}
		wcscpy(cmd, L"rundll32 setupapi.dll,InstallHinfSection DefaultInstall 128 driver\\imdisk.inf");
		start_process(TRUE);
		for (i = 0; i < _countof(svc_list); i++) {
			ImDisk_handle = OpenService(scman_handle, svc_list[i], SERVICE_START);
			StartService(ImDisk_handle, 0, NULL);
			CloseServiceHandle(ImDisk_handle);
		}
		CloseServiceHandle(scman_handle);
		CheckDlgButton(hDlg, ID_CHECK1, BST_UNCHECKED);
	}
	driver_ok = RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\ImDisk", 0, KEY_SET_VALUE | KEY_WOW64_64KEY, &h_key) == ERROR_SUCCESS;
	if (driver_ok) {
		RegDeleteValueA(h_key, "DisplayName");
		RegCloseKey(h_key);
		wcscpy(startmenu_ptr, L"ImDisk Virtual Disk Driver.lnk");
		CopyFile(L"cp.lnk", startmenu, FALSE);
		wcscpy(desk_ptr, L"ImDisk Virtual Disk Driver.lnk");
		if (desk_lnk) CopyFile(startmenu, desk, FALSE);
		else DeleteFile(desk);
	}

	// DiscUtils
	wcscpy(startmenu_ptr, L"Mount Image File.lnk");
	wcscpy(desk_ptr, L"Mount Image File.lnk");
	if (IsDlgButtonChecked(hDlg, ID_CHECK2)) {
		if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\.NETFramework\\v4.0.30319", 0, KEY_QUERY_VALUE | KEY_WOW64_64KEY, &h_key) != ERROR_SUCCESS)
			show_dotnet = !silent;
		else
			RegCloseKey(h_key);

		move(L"DiscUtils.dll");
		move(L"DiscUtilsDevio.exe");
		move(L"DevioNet.dll");
		move(L"ImDiskNet.dll");
		move(L"MountImg.exe");

		CreateLink(path, startmenu, NULL);
		if (desk_lnk) CopyFile(startmenu, desk, FALSE);
		else DeleteFile(desk);
	} else {
		DeleteFile(startmenu);
		DeleteFile(desk);
		del(L"DiscUtils.dll");
		del(L"DiscUtilsDevio.exe");
		del(L"DevioNet.dll");
		del(L"ImDiskNet.dll");
		del(L"MountImg.exe");
	}

	// RamDisk Configuration Tool
	wcscpy(startmenu_ptr, L"RamDisk Configuration.lnk");
	wcscpy(desk_ptr, L"RamDisk Configuration.lnk");
	if (IsDlgButtonChecked(hDlg, ID_CHECK3)) {
		IsWow64Process(GetCurrentProcess(), &is_wow64);
		MoveFileEx(is_wow64 ? L"RamDyn64.exe" : L"RamDyn32.exe", L"RamDyn.exe", 0);
		move(L"RamDyn.exe");

		move(L"RamDiskUI.exe");
		CreateLink(path, startmenu, NULL);
		if (desk_lnk) CopyFile(startmenu, desk, FALSE);
		else DeleteFile(desk);

		// Recreate ImDiskRD service if needed
		if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\ImDisk", 0, KEY_QUERY_VALUE | KEY_WOW64_64KEY, &h_key) == ERROR_SUCCESS) {
			data_size = sizeof RD_found;
			if (RegQueryValueEx(h_key, L"RDMountCurrent", NULL, NULL, (void*)&RD_found, &data_size) == ERROR_SUCCESS) {
				data_size = sizeof awealloc;
				if (RegQueryValueEx(h_key, L"Awealloc", NULL, NULL, (void*)&awealloc, &data_size) != ERROR_SUCCESS) awealloc = 0;
				data_size = sizeof dynamic;
				if (RegQueryValueEx(h_key, L"Dynamic", NULL, NULL, (void*)&dynamic, &data_size) != ERROR_SUCCESS) dynamic = 0;
				dep_awealloc = awealloc & ~dynamic & RD_found;
				priv_req = awealloc & dynamic & RD_found;
				for (cmd[0] = '0'; cmd[0] <= 'Z'; cmd[0] == '9' ? cmd[0] = 'A' : cmd[0]++) {
					wcscpy(cmd + 1, L"_Awealloc");
					data_size = sizeof awealloc;
					if (RegQueryValueEx(h_key, cmd, NULL, NULL, (void*)&awealloc, &data_size) == ERROR_SUCCESS) {
						RD_found = TRUE;
						wcscpy(cmd + 2, L"Dynamic");
						data_size = sizeof dynamic;
						if (RegQueryValueEx(h_key, cmd, NULL, NULL, (void*)&dynamic, &data_size) != ERROR_SUCCESS) dynamic = 0;
						dep_awealloc |= awealloc & ~dynamic;
						priv_req |= awealloc & dynamic;
					}
				}
				if (RD_found) {
					if (priv_req) {
						tok_priv.PrivilegeCount = 1;
						tok_priv.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
						if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES, &token) ||
							!LookupPrivilegeValue(NULL, privilege_name, &tok_priv.Privileges[0].Luid) ||
							!AdjustTokenPrivileges(token, FALSE, &tok_priv, 0, NULL, NULL) || GetLastError() != ERROR_SUCCESS) {
							CreateWellKnownSid(WinBuiltinAdministratorsSid, NULL, (SID*)sid, &sid_size);
							LsaOpenPolicy(NULL, &lsa_oa, POLICY_LOOKUP_NAMES, &lsa_h);
							LsaAddAccountRights(lsa_h, (SID*)sid, &lsa_str, 1);
						}
					}
					PathQuoteSpaces(wcscpy(cmd, path));
					wcscat(cmd, L" SVC");
					scman_handle = OpenSCManager(NULL, NULL, SC_MANAGER_CREATE_SERVICE);
					ImDisk_handle = CreateService(scman_handle, L"ImDiskRD", L"ImDisk RamDisk starter", SERVICE_ALL_ACCESS, SERVICE_WIN32_OWN_PROCESS, SERVICE_AUTO_START, SERVICE_ERROR_NORMAL,
												  cmd, NULL, NULL, dep_awealloc ? L"ImDisk\0AWEAlloc\0" : L"ImDisk\0", NULL, NULL);
					if (ImDisk_handle) {
						svc_description.lpDescription = L"Mount a RamDisk.";
						ChangeServiceConfig2(ImDisk_handle, SERVICE_CONFIG_DESCRIPTION, &svc_description);
						StartService(ImDisk_handle, 0, NULL);
						CloseServiceHandle(ImDisk_handle);
					} else if (GetLastError() != ERROR_SERVICE_EXISTS && silent < 2)
						MessageBox(hDlg, L"Error: cannot create service.", L"ImDisk - Setup", MB_ICONERROR);
					CloseServiceHandle(scman_handle);

					RegCreateKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run", 0, NULL, REG_OPTION_NON_VOLATILE, KEY_SET_VALUE | KEY_WOW64_64KEY, NULL, &h_key, NULL);
					data_size = wcslen(cmd) - 3;
					wcscpy(cmd + data_size, L"NOTIF");
					RegSetValueEx(h_key, L"ImDisk_notif", 0, REG_SZ, (void*)&cmd, (data_size + 6) * sizeof(TCHAR));
					RegCloseKey(h_key);
				}
			}
			RegCloseKey(h_key);
		}
	} else {
		DeleteFile(startmenu);
		DeleteFile(desk);
		del(L"RamDiskUI.exe");
		del(L"RamDyn.exe");
	}

	if (os_ver.dwMajorVersion >= 6) move(L"ImDisk-UAC.exe");

	if (driver_ok) {
		// Context menus
		if (IsDlgButtonChecked(hDlg, ID_CHECK4)) {
			*path_name_ptr = 0;
			write_context_menu(path, IsDlgButtonChecked(hDlg, ID_CHECK5));
		} else {
			del_command_key("*\\shell\\ImDiskMountFile");
			del_command_key("Drive\\shell\\ImDiskSaveImage");
			del_command_key("Drive\\shell\\ImDiskUnmount");
		}
	}

	if (show_dotnet) DialogBox(hinst, L"DOTNETBOX", hDlg, DotNetProc);

	if (reboot) {
		RegCreateKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\RunOnce", 0, NULL, REG_OPTION_NON_VOLATILE, KEY_SET_VALUE | KEY_WOW64_64KEY, NULL, &h_key, NULL);
		wcscpy(path_name_ptr, L"*.old");
		swprintf(cmd, L"cmd /c \"del \"%s\"\"", path);
		RegSetValueEx(h_key, L"ImDiskTk_update_cleanup", 0, REG_SZ, (void*)cmd, (wcslen(cmd) + 1) * sizeof(TCHAR));
		RegCloseKey(h_key);
		if (silent < 2) SetupPromptReboot(NULL, hDlg, FALSE);
	} else if (!show_dotnet) {
		wcscpy(cmd, L"Installation finished.");
		if (os_ver.dwMajorVersion >= 6) wcscat(cmd, L"  ☺");
		MessageBox(hDlg, cmd, L"ImDisk - Setup", MB_ICONINFORMATION);
	}

	EndDialog(hDlg, 0);
}


static BOOL __stdcall InstallProc(HWND hDlg, UINT Msg, WPARAM wParam, LPARAM lParam)
{
	BROWSEINFO bi;
	LPITEMIDLIST pid_folder; // PIDLIST_ABSOLUTE on MSDN
	RECT coord;
	HFONT hFont1, hFont2, hFont3;
	LOGFONTA font;
	void *v_buff1, *v_buff2;
	VS_FIXEDFILEINFO *v1, *v2;
	DWORD size_ver, data_size;
	HKEY h_key;
	int i;

	switch (Msg)
	{
		case WM_INITDIALOG:
			swprintf(cmd, L"This will install the ImDisk Toolkit (build %S).", APP_VERSION);
			SetDlgItemText(hDlg, ID_STATIC3, cmd);

			// check imdisk.exe version to see if update is needed
			CheckDlgButton(hDlg, ID_CHECK1, BST_CHECKED);
			if ((size_ver = GetFileVersionInfoSizeA("imdisk.exe", NULL))) {
				v_buff1 = malloc(size_ver);
				GetFileVersionInfoA("imdisk.exe", 0, size_ver, v_buff1);
				VerQueryValueA(v_buff1, "\\", (void*)&v1, (UINT*)&size_ver);
				if ((size_ver = GetFileVersionInfoSizeA("driver\\cli\\i386\\imdisk.exe", NULL))) {
					v_buff2 = malloc(size_ver);
					GetFileVersionInfoA("driver\\cli\\i386\\imdisk.exe", 0, size_ver, v_buff2);
					VerQueryValueA(v_buff2, "\\", (void*)&v2, (UINT*)&size_ver);
					if (v2->dwFileVersionMS < v1->dwFileVersionMS || (v2->dwFileVersionMS == v1->dwFileVersionMS && v2->dwFileVersionLS <= v1->dwFileVersionLS))
						CheckDlgButton(hDlg, ID_CHECK1, BST_UNCHECKED);
					free(v_buff2);
				}
				free(v_buff1);
			}

			SHGetFolderPath(NULL, CSIDL_DESKTOPDIRECTORY, NULL, SHGFP_TYPE_CURRENT, desk);
			desk_ptr = PathAddBackslash(desk);

			SendDlgItemMessage(hDlg, ID_EDIT1, EM_SETLIMITTEXT, MAX_PATH, 0);
			Button_Enable(GetDlgItem(hDlg, ID_CHECK5), os_ver.dwMajorVersion >= 6);
			CheckDlgButton(hDlg, ID_CHECK6, BST_CHECKED);

			if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\ImDiskApp", 0, KEY_QUERY_VALUE | KEY_WOW64_64KEY, &h_key) == ERROR_SUCCESS) {
				// set the options according to the previous installation
				data_size = sizeof path;
				RegQueryValueEx(h_key, L"UninstallString", NULL, NULL, (void*)&path, &data_size);
				RegCloseKey(h_key);
				PathRemoveFileSpec(path);
				i = wcslen(path);
				path_name_ptr = PathAddBackslash(path);
				wcscpy(path_name_ptr, L"MountImg.exe");
				CheckDlgButton(hDlg, ID_CHECK2, PathFileExists(path));
				wcscpy(path_name_ptr, L"RamDiskUI.exe");
				CheckDlgButton(hDlg, ID_CHECK3, PathFileExists(path));
				path[i] = 0;

				if (RegOpenKeyExA(HKEY_CLASSES_ROOT, "Drive\\shell\\ImDiskSaveImage\\command", 0, KEY_QUERY_VALUE | KEY_WOW64_64KEY, &h_key) == ERROR_SUCCESS) {
					CheckDlgButton(hDlg, ID_CHECK4, BST_CHECKED);
					data_size = sizeof cmd;
					RegQueryValueEx(h_key, NULL, NULL, NULL, (void*)&cmd, &data_size);
					RegCloseKey(h_key);
					if (wcsncmp(cmd, L"run", 3) && os_ver.dwMajorVersion >= 6)
						CheckDlgButton(hDlg, ID_CHECK5, BST_CHECKED);
				}

				wcscpy(desk_ptr, L"ImDisk Virtual Disk Driver.lnk");
				if (!PathFileExists(desk)) {
					wcscpy(desk_ptr, L"Mount Image File.lnk");
					if (!PathFileExists(desk)) {
						wcscpy(desk_ptr, L"RamDisk Configuration.lnk");
						if (!PathFileExists(desk))
							CheckDlgButton(hDlg, ID_CHECK6, BST_UNCHECKED);
					}
				}
			} else {
				SHGetFolderPath(NULL, CSIDL_PROGRAM_FILES, NULL, SHGFP_TYPE_CURRENT, path);
				wcscat(path, L"\\ImDisk");

				for (i = ID_CHECK5 - (os_ver.dwMajorVersion < 6); i >= ID_CHECK2; i--)
					CheckDlgButton(hDlg, i, BST_CHECKED);
			}
			SetDlgItemText(hDlg, ID_EDIT1, path);

			if (silent) {
				install(hDlg);
				return TRUE;
			}

			SendMessage(hDlg, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);

			hwnd_text1 = GetDlgItem(hDlg, ID_TEXT1);
			hwnd_static1 = GetDlgItem(hDlg, ID_STATIC1);
			hwnd_static2 = GetDlgItem(hDlg, ID_STATIC2);
			hwnd_static3 = GetDlgItem(hDlg, ID_STATIC3);

			icon_coord.left = 13;
			icon_coord.top = 7;
			icon_coord.right = icon_coord.bottom = 0;
			MapDialogRect(hDlg, &icon_coord);

			coord.top = 23; coord.left = 4;
			coord.bottom = 15; coord.right = 3;
			MapDialogRect(hDlg, &coord);
			ZeroMemory(&font, sizeof font);
			font.lfHeight = coord.top / 2;
			font.lfWidth = coord.left;
			font.lfWeight = FW_SEMIBOLD;
			font.lfCharSet = DEFAULT_CHARSET;
			strcpy(font.lfFaceName, "MS Shell Dlg");
			hFont1 = CreateFontIndirectA(&font);
			SendMessage(hwnd_text1, WM_SETFONT, (WPARAM)hFont1, 0);

			font.lfHeight = coord.bottom / 2;
			font.lfWidth = coord.right;
			font.lfWeight = FW_NORMAL;
			hFont2 = CreateFontIndirectA(&font);
			SendMessage(GetDlgItem(hDlg, ID_TEXT2), WM_SETFONT, (WPARAM)hFont2, 0);
			SendMessage(GetDlgItem(hDlg, ID_GROUP1), WM_SETFONT, (WPARAM)hFont2, 0);
			SendMessage(GetDlgItem(hDlg, ID_GROUP2), WM_SETFONT, (WPARAM)hFont2, 0);

			// :)
			coord.top = 26; coord.left = 5;
			MapDialogRect(hDlg, &coord);
			font.lfHeight = coord.top / 2;
			font.lfWidth = coord.left;
			font.lfOutPrecision = OUT_OUTLINE_PRECIS;
			font.lfQuality = CLEARTYPE_QUALITY;
			strcpy(font.lfFaceName, "Arial Black");
			hFont3 = CreateFontIndirectA(&font);

			for (i = 0; i < 6; i++) {
				SendMessage(GetDlgItem(hDlg, ID_TEXT3 + i), WM_SETFONT, (WPARAM)hFont3, 0);
				hwnd_check[i] = GetDlgItem(hDlg, ID_CHECK1 + i);
			}

			hBrush = CreateSolidBrush(HEADER_BACKGROUND);
			SetCursor(LoadImage(NULL, MAKEINTRESOURCE(OCR_NORMAL), IMAGE_CURSOR, 0, 0, LR_DEFAULTSIZE | LR_SHARED));

			return TRUE;

		case WM_SETCURSOR:
			if (wParam == prev_wparam) return FALSE;
			for (i = 0; i < 6; i++)
				if (wParam == (WPARAM)hwnd_check[i]) {
					SetDlgItemTextA(hDlg, ID_TEXT3 + prev_mark, "");
					SetDlgItemText(hDlg, ID_TEXT3 + i, L"☺");
					SetDlgItemTextA(hDlg, ID_TEXT2, description[i]);
					prev_mark = i;
				}
			prev_wparam = wParam;
			return TRUE;

		case WM_CTLCOLORSTATIC:
			if ((HWND)lParam == hwnd_text1 || (HWND)lParam == hwnd_static1 || (HWND)lParam == hwnd_static2 || (HWND)lParam == hwnd_static3) {
				SetBkColor((HDC)wParam, HEADER_BACKGROUND);
				return (INT_PTR)hBrush;
			}
			return FALSE;

		case WM_COMMAND:
			if (os_ver.dwMajorVersion >= 6)
				Button_Enable(hwnd_check[4], IsDlgButtonChecked(hDlg, ID_CHECK4));

			if (LOWORD(wParam) == ID_PBUTTON1) {
				ZeroMemory(&bi, sizeof bi);
				bi.hwndOwner = hDlg;
				bi.pszDisplayName = path;
				bi.lpszTitle = L"ImDisk - Setup";
				bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
				pid_folder = SHBrowseForFolder(&bi);
				if (pid_folder) {
					SHGetPathFromIDList(pid_folder, path);
					SetDlgItemText(hDlg, ID_EDIT1, path);
				}
			}

			if (LOWORD(wParam) == ID_PBUTTON2)
				DialogBox(hinst, L"CREDITSBOX", hDlg, CreditsProc);

			if (LOWORD(wParam) == IDOK)
				install(hDlg);

			if (LOWORD(wParam) == IDCANCEL)
				EndDialog(hDlg, 0);

			return TRUE;

		default:
			return FALSE;
	}
}


static DWORD __stdcall uninstall(LPVOID lpParam)
{
	DWORD data_size;
	SC_HANDLE scman_handle, ImDiskRD_handle;
	HKEY h_key;
	TCHAR dir[MAX_PATH + 20];
	HMODULE h_cpl;
	FARPROC ImDiskGetDeviceListEx;
	ULONG *list;
	int i;

	SetCurrentDirectoryA("\\"); // required to delete installation directory

	// driver
	if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\ImDisk", 0, KEY_SET_VALUE | KEY_WOW64_64KEY, &h_key) == ERROR_SUCCESS) {
		if (IsDlgButtonChecked(hwnd_uninst, ID_CHECK1)) {
			RegCloseKey(h_key);
			if ((h_cpl = LoadLibraryA("imdisk.cpl")) && (ImDiskGetDeviceListEx = GetProcAddress(h_cpl, "ImDiskGetDeviceListEx"))) {
				list = malloc(64002 * sizeof(ULONG));
				if (ImDiskGetDeviceListEx(64002, list) && list[0]) {
					if (MessageBox(hwnd_uninst, L"ImDisk virtual disk detected.\nAll existing ImDisk virtual disks will be unmounted!", L"ImDisk - Setup", MB_OKCANCEL | MB_ICONWARNING) == IDCANCEL) {
						EndDialog(hwnd_uninst, 0);
						return 0;
					}
					SetDlgItemText(hwnd_uninst, ID_TEXT1, L"Unmounting ImDisk volumes...");
					i = 1; do {
						swprintf(cmd, L"imdisk -D -u %u", list[i]);
						start_process(TRUE);
					} while (++i <= list[0]);
				}
				free(list);
			}
			for (i = 0; i < _countof(svc_list); i++) {
				swprintf(cmd, L"Stopping %s...", svc_list[i]);
				SetDlgItemText(hwnd_uninst, ID_TEXT1, cmd);
				swprintf(cmd, L"net stop %s", svc_list[i]);
				start_process(TRUE);
			}
			wcscpy(cmd, L"taskkill /f /im imdsksvc.exe");
			start_process(TRUE);
			SHGetFolderPath(NULL, CSIDL_WINDOWS, NULL, SHGFP_TYPE_CURRENT, dir);
			swprintf(cmd, L"rundll32 setupapi.dll,InstallHinfSection DefaultUninstall 132 %s\\inf\\imdisk.inf", dir);
			start_process(FALSE);
		} else {
			RegSetValueExA(h_key, "DisplayName", 0, REG_SZ, (void*)reg_string_4, sizeof reg_string_4);
			RegCloseKey(h_key);
			write_context_menu(NULL, FALSE);
		}
	}

	SetDlgItemText(hwnd_uninst, ID_TEXT1, L"Removing remaining components...");

	// RamDisk service
	scman_handle = OpenSCManager(NULL, NULL, SC_MANAGER_CONNECT);
	ImDiskRD_handle = OpenServiceA(scman_handle, "ImDiskRD", DELETE);
	DeleteService(ImDiskRD_handle);
	CloseServiceHandle(ImDiskRD_handle);
	CloseServiceHandle(scman_handle);
	RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_SET_VALUE | KEY_WOW64_64KEY, &h_key);
	RegDeleteValue(h_key, L"ImDisk_notif");
	RegCloseKey(h_key);

	// settings
	if (IsDlgButtonChecked(hwnd_uninst, ID_CHECK2))
		del_key(HKEY_LOCAL_MACHINE, "SOFTWARE\\ImDisk");

	// shortcuts
	SHGetFolderPath(NULL, CSIDL_DESKTOPDIRECTORY, NULL, SHGFP_TYPE_CURRENT, path);
	path_name_ptr = PathAddBackslash(path);
	del(L"ImDisk Virtual Disk Driver.lnk");
	del(L"Mount Image File.lnk");
	del(L"RamDisk Configuration.lnk");
	SHGetFolderPath(NULL, CSIDL_PROGRAMS, NULL, SHGFP_TYPE_CURRENT, path);
	wcscat(path, L"\\ImDisk");
	path_name_ptr = PathAddBackslash(path);
	del(L"ImDisk Virtual Disk Driver.lnk");
	del(L"Mount Image File.lnk");
	del(L"RamDisk Configuration.lnk");
	del(L"General Settings.lnk");
	del(L"Uninstall.lnk");
	path_name_ptr[0] = 0;
	RemoveDirectory(path);

	// files
	if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\ImDiskApp", 0, KEY_QUERY_VALUE | KEY_WOW64_64KEY, &h_key) == ERROR_SUCCESS) {
		data_size = sizeof path;
		RegQueryValueEx(h_key, L"UninstallString", NULL, NULL, (void*)&path, &data_size);
		RegCloseKey(h_key);
		del_key(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\ImDiskApp");
		path_name_ptr = wcsrchr(path, '\\') + 1;
		del(L"DevioNet.dll");
		del(L"DiscUtils.dll");
		del(L"DiscUtilsDevio.exe");
		del(L"ImDiskNet.dll");
		del(L"ImDisk-UAC.exe");
		del(L"MountImg.exe");
		del(L"RamDiskUI.exe");
		del(L"RamDyn.exe");
		wcscpy(path_name_ptr, L"setup.exe");
		wcscpy(dir, path);
		path_name_ptr[-1] = 0;
		swprintf(cmd, L"cmd /c \"for /l %%I in (0,0,1) do (del \"%s\"&rd \"%s\"&if not exist \"%s\" exit)\"", dir, path, dir);
		start_process(FALSE);
	}

	EndDialog(hwnd_uninst, 0);
	return 0;
}


static BOOL __stdcall UninstallProc(HWND hDlg, UINT Msg, WPARAM wParam, LPARAM lParam)
{
	switch (Msg)
	{
		case WM_INITDIALOG:
			SendMessage(hDlg, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);

			CheckDlgButton(hDlg, ID_CHECK1, BST_CHECKED);
			CheckDlgButton(hDlg, ID_CHECK2, BST_CHECKED);

			hwnd_uninst = hDlg;

			return TRUE;

		case WM_COMMAND:
			if (process_uninst) return TRUE;

			if (LOWORD(wParam) == IDOK) {
				process_uninst = TRUE;
				ShowWindow(GetDlgItem(hDlg, ID_CHECK1), SW_HIDE);
				ShowWindow(GetDlgItem(hDlg, ID_CHECK2), SW_HIDE);
				ShowWindow(GetDlgItem(hDlg, ID_STATIC1), SW_HIDE);
				ShowWindow(GetDlgItem(hDlg, IDOK), SW_HIDE);
				ShowWindow(GetDlgItem(hDlg, IDCANCEL), SW_HIDE);
				CreateThread(NULL, 0, uninstall, NULL, 0, NULL);
			}

			if (LOWORD(wParam) == IDCANCEL)
				EndDialog(hDlg, 0);

			return TRUE;

		default:
			return FALSE;
	}
}


static void write_file_reg_tmp(TCHAR *value)
{
	DWORD data_size, bytes_written;
	unsigned char *ptr;
	TCHAR txt[16];

	data_size = sizeof path;
	if (RegQueryValueEx(reg_key, value, NULL, NULL, (void*)&path, &data_size) == ERROR_SUCCESS) {
		swprintf(txt, L"\"%s\"=hex(2):", value);
		WriteFile(h_file, txt, wcslen(txt) * sizeof(TCHAR), &bytes_written, NULL);
		ptr = (unsigned char*)&path;
		while (data_size--) {
			swprintf(txt, L"%02x%s", *ptr, data_size ? L"," : L"\r\n");
			WriteFile(h_file, txt, wcslen(txt) * sizeof(TCHAR), &bytes_written, NULL);
			ptr++;
		}
	}
}

static BOOL __stdcall SettingsProc(HWND hDlg, UINT Msg, WPARAM wParam, LPARAM lParam)
{
	RECT ctrl1, ctrl2, ctrl3;
	HFONT font;
	DWORD data_size, bytes_written;
	DWORD hidden_drives = 0;
	OPENFILENAME ofn = {sizeof ofn};
	BOOL disp_warn;
	int i;

	switch (Msg)
	{
		case WM_INITDIALOG:
			SetDlgItemText(hDlg, ID_TEXT1, version_str);
			Static_Enable(GetDlgItem(hDlg, ID_TEXT1), FALSE);
			SendMessage(hDlg, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);

			// create list of letters
			GetWindowRect(GetDlgItem(hDlg, ID_CHECK_A), &ctrl1);
			GetWindowRect(GetDlgItem(hDlg, ID_CHECK_A + 1), &ctrl2);
			GetWindowRect(GetDlgItem(hDlg, ID_TEXT2), &ctrl3);
			ctrl2.left -= ctrl1.left;
			ctrl1.right -= ctrl1.left;
			ctrl1.bottom -= ctrl1.top;
			ctrl3.right -= ctrl3.left;
			ctrl3.bottom -= ctrl3.top;
			ScreenToClient(hDlg, (POINT*)&ctrl1);
			ScreenToClient(hDlg, (POINT*)&ctrl3);
			font = (HFONT)SendMessage(GetDlgItem(hDlg, ID_TEXT2), WM_GETFONT, 0, 0);
			cmd[1] = 0;
			for (i = 2; i < 26; i++) {
				CreateWindow(WC_BUTTON, NULL, WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | WS_TABSTOP, ctrl1.left + i * ctrl2.left, ctrl1.top, ctrl1.right, ctrl1.bottom, hDlg, (HMENU)(ID_CHECK_A + i), hinst, NULL);
				cmd[0] = 'A' + i;
				SendMessage(CreateWindow(WC_STATIC, cmd, WS_CHILD | WS_VISIBLE | SS_NOPREFIX, ctrl3.left + i * ctrl2.left, ctrl3.top, ctrl3.right, ctrl3.bottom, hDlg, NULL, hinst, NULL), WM_SETFONT, (WPARAM)font, TRUE);
			}

			if (RegOpenKeyExA(HKEY_CLASSES_ROOT, "Drive\\shell\\ImDiskSaveImage\\command", 0, KEY_QUERY_VALUE | KEY_WOW64_64KEY, &reg_key) == ERROR_SUCCESS) {
				CheckDlgButton(hDlg, ID_CHECK1, BST_CHECKED);
				data_size = sizeof path;
				RegQueryValueEx(reg_key, NULL, NULL, NULL, (void*)&path, &data_size);
				RegCloseKey(reg_key);
				if (wcsncmp(path, L"run", 3))
					CheckDlgButton(hDlg, ID_CHECK2, BST_CHECKED);
			} else
				Button_Enable(GetDlgItem(hDlg, ID_CHECK2), FALSE);
			if (os_ver.dwMajorVersion < 6)
				Button_Enable(GetDlgItem(hDlg, ID_CHECK2), FALSE);

			RegCreateKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer", 0, NULL, REG_OPTION_NON_VOLATILE, KEY_QUERY_VALUE | KEY_WOW64_64KEY, NULL, &reg_key, NULL);
			data_size = sizeof hidden_drives;
			RegQueryValueEx(reg_key, L"NoDrives", NULL, NULL, (void*)&hidden_drives, &data_size);			
			RegCloseKey(reg_key);
			hid_drive_ini = hidden_drives;
			for (i = 0; i < 26; i++) {
				if (hidden_drives & 1)
					CheckDlgButton(hDlg, ID_CHECK_A + i, BST_CHECKED);
				hidden_drives >>= 1;
			}

			return TRUE;

		case WM_COMMAND:
			if (os_ver.dwMajorVersion >= 6)
				Button_Enable(GetDlgItem(hDlg, ID_CHECK2), IsDlgButtonChecked(hDlg, ID_CHECK1));

			for (i = 25; i >= 0; i--) {
				hidden_drives <<= 1;
				if (IsDlgButtonChecked(hDlg, ID_CHECK_A + i))
					hidden_drives++;
			}
			disp_warn = hidden_drives != hid_drive_ini;
			SetDlgItemText(hDlg, ID_TEXT1, disp_warn ? L"These changes will take effect only after restarting Windows!" : version_str);
			Static_Enable(GetDlgItem(hDlg, ID_TEXT1), disp_warn);

			if (LOWORD(wParam) == ID_PBUTTON1) {
				path[0] = 0;
				ofn.hwndOwner = hDlg;
				ofn.lpstrFilter = L"Reg Files (*.reg)\0*.reg\0All Files (*.*)\0*.*\0";
				ofn.lpstrFile = path;
				ofn.nMaxFile = MAX_PATH;
				ofn.Flags = OFN_OVERWRITEPROMPT;
				ofn.lpstrDefExt = L"reg";
				if (GetSaveFileName(&ofn)) {
					DeleteFile(path);
					swprintf(cmd, L"reg export HKEY_LOCAL_MACHINE\\SOFTWARE\\ImDisk \"%s\"%s", path, os_ver.dwMajorVersion >= 6 ? L" /y" : L"");
					start_process(TRUE);
					if ((h_file = CreateFile(path, FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL)) != INVALID_HANDLE_VALUE) {
						if (!GetLastError())
							WriteFile(h_file, L"\xFEFFWindows Registry Editor Version 5.00\r\n\r\n", 82, &bytes_written, NULL);
						if (hidden_drives) {
							swprintf(cmd, L"[HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer]\r\n\"NoDrives\"=dword:%08x\r\n\r\n", hidden_drives);
							WriteFile(h_file, cmd, wcslen(cmd) * sizeof(TCHAR), &bytes_written, NULL);
						}
						if (IsDlgButtonChecked(hDlg, ID_CHECK3)) {
							WriteFile(h_file, L"[HKEY_CURRENT_USER\\Environment]\r\n", 66, &bytes_written, NULL);
							RegOpenKeyExA(HKEY_CURRENT_USER, "Environment", 0, KEY_QUERY_VALUE, &reg_key);
							write_file_reg_tmp(L"TMP");
							write_file_reg_tmp(L"TEMP");
							RegCloseKey(reg_key);

							WriteFile(h_file, L"\r\n[HKEY_LOCAL_MACHINE\\SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment]\r\n", 170, &bytes_written, NULL);
							RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment", 0, KEY_QUERY_VALUE, &reg_key);
							write_file_reg_tmp(L"TMP");
							write_file_reg_tmp(L"TEMP");
							RegCloseKey(reg_key);
						}
						CloseHandle(h_file);
					}
				}
			}

			if (LOWORD(wParam) == IDOK) {
				if (IsDlgButtonChecked(hDlg, ID_CHECK1)) {
					// retrieve install path
					RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\ImDiskApp", 0, KEY_QUERY_VALUE | KEY_WOW64_64KEY, &reg_key);
					data_size = sizeof path;
					RegQueryValueEx(reg_key, L"UninstallString", NULL, NULL, (void*)&path, &data_size);
					RegCloseKey(reg_key);
					*(wcsrchr(path, '\\') + 1) = 0;

					write_context_menu(path, IsDlgButtonChecked(hDlg, ID_CHECK2));
				} else {
					del_command_key("*\\shell\\ImDiskMountFile");
					del_command_key("Drive\\shell\\ImDiskSaveImage");
					del_command_key("Drive\\shell\\ImDiskUnmount");
				}

				RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer", 0, KEY_SET_VALUE | KEY_WOW64_64KEY, &reg_key);
				if (hidden_drives)
					RegSetValueEx(reg_key, L"NoDrives", 0, REG_DWORD, (void*)&hidden_drives, sizeof hidden_drives);
				else
					RegDeleteValue(reg_key, L"NoDrives");
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


int __stdcall wWinMain(HINSTANCE hinstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow)
{
	int argc;
	LPWSTR *argv;
	TCHAR sys_dir[MAX_PATH + 1];

	os_ver.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
	GetVersionEx(&os_ver);

	CoInitialize(NULL);

	hinst = hinstance;
	hIcon = LoadImage(hinst, MAKEINTRESOURCE(2), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE);

	GetModuleFileName(NULL, path, MAX_PATH);
	PathRemoveFileSpec(path);
	SetCurrentDirectory(path);

	argv = CommandLineToArgvW(GetCommandLine(), &argc);

	if (PathFileExists(L"driver")) {
		if (argc > 1) {
			if (!wcscmp(argv[1], L"/silent")) silent = 1;
			else if (!wcscmp(argv[1], L"/fullsilent")) silent = 2;
			else {
				MessageBoxA(NULL, "Switches:\n\n/silent\t    Silent installation. Error messages and reboot prompt are still displayed.\n\n/fullsilent\t    Silent installation, without error message or prompt.",
							"ImDisk - Setup", MB_ICONINFORMATION);
				return 0;
			}
		}
		SHGetFolderPath(NULL, CSIDL_SYSTEM, NULL, SHGFP_TYPE_CURRENT, sys_dir);
		if (!GetEnvironmentVariable(L"Path", cmd, _countof(cmd)) || !wcsstr(wcscat(_wcsupr(cmd), L";"), wcscat(_wcsupr(sys_dir), L";")))
			MessageBoxA(NULL, "Error: System32 folder not found in the %Path% environment variable.", "ImDisk - Setup", MB_ICONERROR);
		DialogBox(hinst, L"INSTALLBOX", NULL, InstallProc);
	} else if (argc > 1 && !wcscmp(argv[1], L"/u"))
		DialogBox(hinst, L"UNINSTALLBOX", NULL, UninstallProc);
	else
		DialogBox(hinst, L"SETTINGSBOX", NULL, SettingsProc);

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
