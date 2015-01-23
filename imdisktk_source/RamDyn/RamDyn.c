#include <windows.h>
#include <shlwapi.h>
#include <stdio.h>
#include "..\inc\imdproxy.h"

#define DEF_BUFFER_SIZE (1 << 20)
#define MEM_BLOCK_SIZE (1 << 20)

static IMDPROXY_INFO_RESP proxy_info = {};
static ULONGLONG clean_margin, max_activity;
static unsigned int clean_timer, clean_ratio;
static volatile ULONGLONG data_amount;
static TCHAR *drive_id, *drive_arg, drive_root[MAX_PATH + 1];
static void **ptr_table, *virtual_mem_ptr;
static unsigned char *allocated_block;
static volatile int n_block = 0;
static DWORD err_time = 0;
static FARPROC ImDiskOpenDeviceByMountPoint, ImDiskForceRemoveDevice;
static BOOL physical;
static HANDLE current_process;
static SYSTEM_INFO sys;
static ULONG_PTR n_pages, page_mask, *pfn;
static void (*mem_read)(void *buf, int size, __int64 offset);
static void (*mem_write)(void *buf, int size, __int64 offset);


static void disp_error()
{
	TCHAR err_msg[MAX_PATH + 80];

	if (GetTickCount() - err_time >= 10000) {
		swprintf(err_msg, L"Not enough memory to write data into %s.\nSome data will be lost.", drive_arg);
		MessageBox(NULL, err_msg, NULL, MB_OK | MB_ICONERROR);
		err_time = GetTickCount();
	}
}


static void virtual_mem_read(void *buf, int size, __int64 offset)
{
	size_t index = offset / MEM_BLOCK_SIZE;
	int current_size;
	int block_offset = offset & (MEM_BLOCK_SIZE - 1);

	data_amount += size;
	do {
		current_size = min(size + block_offset, MEM_BLOCK_SIZE) - block_offset;
		if (ptr_table[index])
			memcpy(buf, ptr_table[index] + block_offset, current_size);
		else
			ZeroMemory(buf, current_size);
		block_offset = 0;
		buf += current_size;
		index++;
		size -= current_size;
	} while (size > 0);
}

static void physical_mem_read(void *buf, int size, __int64 offset)
{
	size_t index = offset / MEM_BLOCK_SIZE;
	int current_size;
	int block_offset = offset & (MEM_BLOCK_SIZE - 1);

	data_amount += size;
	do {
		current_size = min(size + block_offset, MEM_BLOCK_SIZE) - block_offset;
		if (allocated_block[index]) {
			MapUserPhysicalPages(virtual_mem_ptr, n_pages, pfn + index * n_pages);
			memcpy(buf, virtual_mem_ptr + block_offset, current_size);
		} else
			ZeroMemory(buf, current_size);
		block_offset = 0;
		offset += current_size;
		buf += current_size;
		index++;
		size -= current_size;
	} while (size > 0);
}


static BOOL data_search(void *ptr, int size)
{
	size_t *scan_ptr;

	if (!size) return FALSE;
	scan_ptr = ptr;
	ptr += size - sizeof(size_t);
	if (*(size_t*)ptr) return TRUE;
	*(size_t*)ptr = 1;
	while (!*(scan_ptr++));
	*(size_t*)ptr = 0;
	return --scan_ptr != ptr;
}

static void virtual_mem_write(void *buf, int size, __int64 offset)
{
	size_t index = offset / MEM_BLOCK_SIZE;
	int current_size;
	int block_offset = offset & (MEM_BLOCK_SIZE - 1);
	void *ptr;
	BOOL data;
	MEMORYSTATUSEX mem_stat;

	data_amount += size;
	mem_stat.dwLength = sizeof mem_stat;
	do {
		current_size = min(size + block_offset, MEM_BLOCK_SIZE) - block_offset;
		data = data_search(buf, current_size);
		if ((ptr = ptr_table[index])) {
			if (data)
				memcpy(ptr + block_offset, buf, current_size);
			else if (data_search(ptr, block_offset) || data_search(ptr + block_offset + current_size, MEM_BLOCK_SIZE - block_offset - current_size))
				ZeroMemory(ptr + block_offset, current_size);
			else {
				VirtualFree(ptr, 0, MEM_RELEASE);
				ptr_table[index] = NULL;
				n_block--;
			}
		}
		else if (data) {
			GlobalMemoryStatusEx(&mem_stat);
			if (mem_stat.ullAvailPageFile > (100 << 20) && (ptr_table[index] = VirtualAlloc(NULL, MEM_BLOCK_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE))) {
				memcpy(ptr_table[index] + block_offset, buf, current_size);
				n_block++;
			} else
				disp_error();
		}
		block_offset = 0;
		buf += current_size;
		index++;
		size -= current_size;
	} while (size > 0);
}

static void physical_mem_write(void *buf, int size, __int64 offset)
{
	size_t index = offset / MEM_BLOCK_SIZE;
	int current_size;
	int block_offset = offset & (MEM_BLOCK_SIZE - 1);
	BOOL data;
	ULONG_PTR allocated_pages, *pfn_ptr;
	MEMORYSTATUSEX mem_stat;

	data_amount += size;
	mem_stat.dwLength = sizeof mem_stat;
	do {
		current_size = min(size + block_offset, MEM_BLOCK_SIZE) - block_offset;
		data = data_search(buf, current_size);
		if (allocated_block[index]) {
			MapUserPhysicalPages(virtual_mem_ptr, n_pages, pfn + index * n_pages);
			if (data)
				memcpy(virtual_mem_ptr + block_offset, buf, current_size);
			else if (data_search(virtual_mem_ptr, block_offset) || data_search(virtual_mem_ptr + block_offset + current_size, MEM_BLOCK_SIZE - block_offset - current_size))
				ZeroMemory(virtual_mem_ptr + block_offset, current_size);
			else {
				allocated_pages = n_pages;
				FreeUserPhysicalPages(current_process, &allocated_pages, pfn + index * n_pages);
				allocated_block[index] = FALSE;
				n_block--;
			}
		}
		else if (data) {
			GlobalMemoryStatusEx(&mem_stat);
			if (mem_stat.ullAvailPageFile < (100 << 20)) disp_error();
			else {
				allocated_pages = n_pages;
				pfn_ptr = pfn + index * n_pages;
				if (!AllocateUserPhysicalPages(current_process, &allocated_pages, pfn_ptr)) disp_error();
				else if (allocated_pages != n_pages) {
					FreeUserPhysicalPages(current_process, &allocated_pages, pfn_ptr);
					disp_error();
				} else {
					MapUserPhysicalPages(virtual_mem_ptr, n_pages, pfn_ptr);
					memcpy(virtual_mem_ptr + block_offset, buf, current_size);
					allocated_block[index] = TRUE;
					n_block++;
				}
			}
		}
		block_offset = 0;
		buf += current_size;
		index++;
		size -= current_size;
	} while (size > 0);
}


static BOOL clean_create_new_file(HANDLE *h)
{
	TCHAR file_name[MAX_PATH + 20];
	int i = 0;

	do swprintf(file_name, L"%sCLEAN%u", drive_root, i++);
	while (PathFileExists(file_name));
	return (*h = CreateFile(file_name, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM | FILE_FLAG_DELETE_ON_CLOSE | FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH, NULL))
			== INVALID_HANDLE_VALUE;
}

static DWORD __stdcall mem_clean(LPVOID lpParam)
{
	__int64 prev_diff = 0, new_diff;
	ULONGLONG free_bytes, total_bytes;
	TCHAR file_name[10];
	HANDLE h_file[8];
	void *empty_buf;
	DWORD bytes_written;
	int i, j, n_file;

	for (;;) {
		Sleep(clean_timer);

		// check ramdisk activity
		do {
			data_amount = 0;
			Sleep(1000);
			if (drive_arg[1] == L':' && drive_arg[2] == 0) {
				swprintf(file_name, L"\\\\.\\%s", drive_arg);
				h_file[0] = CreateFile(file_name, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_NO_BUFFERING, NULL);
			} else
				h_file[0] = CreateFile(drive_arg, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_NO_BUFFERING, NULL);
			FlushFileBuffers(h_file[0]);
			CloseHandle(h_file[0]);
		} while (data_amount >= max_activity);

		// check whether the space between used bytes and allocated blocks has increased
		if (!GetDiskFreeSpaceEx(drive_root, (ULARGE_INTEGER*)&free_bytes, (ULARGE_INTEGER*)&total_bytes, NULL)) continue;
		new_diff = (__int64)n_block * MEM_BLOCK_SIZE - (total_bytes - free_bytes);
		if (new_diff - prev_diff < (__int64)(total_bytes * clean_ratio / 1000)) continue;

		// cleanup
		n_file = 0;
		if (clean_create_new_file(&h_file[0])) continue;
		empty_buf = VirtualAlloc(NULL, MEM_BLOCK_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
		i = j = 0;
		GetDiskFreeSpaceEx(drive_root, (ULARGE_INTEGER*)&free_bytes, (ULARGE_INTEGER*)&total_bytes, NULL);
		while (free_bytes >= clean_margin) {
			if (WriteFile(h_file[n_file], empty_buf, MEM_BLOCK_SIZE, &bytes_written, NULL)) i++, j++;
			else {
				if (j == 4095 && GetVolumeInformation(drive_root, NULL, 0, NULL, NULL, NULL, file_name, _countof(file_name)) && !wcscmp(file_name, L"FAT32")) {
					if (clean_create_new_file(&h_file[++n_file])) break;
					j = 0;
				} else break;
			}
			GetDiskFreeSpaceEx(drive_root, (ULARGE_INTEGER*)&free_bytes, NULL, NULL);
		}
		do CloseHandle(h_file[n_file]); while (--n_file >= 0);

		prev_diff = ((__int64)n_block + i) * MEM_BLOCK_SIZE - (total_bytes - free_bytes);
		VirtualFree(empty_buf, 0, MEM_RELEASE);
	}
	return 0;
}


static int do_comm()
{
	HANDLE shm_request_event, shm_response_event;
	unsigned char *shm_view, *main_buf;
	IMDPROXY_READ_REQ *req_block;
	IMDPROXY_WRITE_RESP *resp_block;
	HANDLE hFileMap;
	ULARGE_INTEGER map_size;
	TCHAR objname[40], *index;
	SIZE_T min_working_set, max_working_set;

	index = objname + swprintf(objname, L"Global\\RamDyn%s", drive_id);

	map_size.QuadPart = DEF_BUFFER_SIZE + IMDPROXY_HEADER_SIZE;

	if (!(hFileMap = CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE | SEC_COMMIT, map_size.HighPart, map_size.LowPart, objname)) ||
		!(shm_view = MapViewOfFile(hFileMap, FILE_MAP_WRITE, 0, 0, 0)))
		return 1;

	if (physical) {
		GetProcessWorkingSetSize(current_process, &min_working_set, &max_working_set);
		min_working_set += DEF_BUFFER_SIZE + IMDPROXY_HEADER_SIZE;
		max_working_set += DEF_BUFFER_SIZE + IMDPROXY_HEADER_SIZE;
		SetProcessWorkingSetSize(current_process, min_working_set, max_working_set);
		VirtualLock(shm_view, DEF_BUFFER_SIZE + IMDPROXY_HEADER_SIZE);
	}

	main_buf = shm_view + IMDPROXY_HEADER_SIZE;
	req_block = (IMDPROXY_READ_REQ*)shm_view;
	resp_block = (IMDPROXY_WRITE_RESP*)shm_view;

	wcscpy(index, L"_Request");
	if (!(shm_request_event = CreateEvent(NULL, FALSE, FALSE, objname)) || GetLastError() == ERROR_ALREADY_EXISTS)
		return 1;
	wcscpy(index, L"_Response");
	if (!(shm_response_event = CreateEvent(NULL, FALSE, FALSE, objname)))
		return 1;

	if (WaitForSingleObject(shm_request_event, 20000) != WAIT_OBJECT_0 || req_block->request_code != IMDPROXY_REQ_INFO)
		return 1;

	memcpy(shm_view, &proxy_info, sizeof proxy_info);

	for (;;) {
		SetEvent(shm_response_event);
		WaitForSingleObject(shm_request_event, INFINITE);

		if (req_block->request_code == IMDPROXY_REQ_READ)
			mem_read(main_buf, req_block->length, req_block->offset);
		else if (req_block->request_code == IMDPROXY_REQ_WRITE)
			mem_write(main_buf, req_block->length, req_block->offset);
		else
			return 0;

		resp_block->errorno = 0;
		resp_block->length = req_block->length;
	}
}

#pragma GCC optimize "Os"

static LRESULT __stdcall WndProc(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
	HANDLE h;
	DWORD data_size;

	if (Msg == WM_ENDSESSION) {
		h = (HANDLE)ImDiskOpenDeviceByMountPoint(drive_arg, GENERIC_READ);
		if (!DeviceIoControl(h, FSCTL_DISMOUNT_VOLUME, NULL, 0, NULL, 0, &data_size, NULL) || !DeviceIoControl(h, IOCTL_STORAGE_EJECT_MEDIA, NULL, 0, NULL, 0, &data_size, NULL))
			ImDiskForceRemoveDevice(h, 0);
		CloseHandle(h);
		return 0;
	} else
		return DefWindowProc(hWnd, Msg, wParam, lParam);
}

static DWORD __stdcall msg_window(LPVOID lpParam)
{
	MSG msg;
	WNDCLASSA wc = {};

	wc.lpfnWndProc = WndProc;
	wc.lpszClassName = "X";
	RegisterClassA(&wc);
	CreateWindowA("X", NULL, 0, 0, 0, 0, 0, NULL, NULL, NULL, NULL);

	for (;;) {
		GetMessage(&msg, NULL, 0, 0);
		DispatchMessage(&msg);
	}
	return 0;
}


int __stdcall wWinMain(HINSTANCE hinstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow)
{
	int argc;
	LPWSTR *argv;
	HANDLE h_cpl;
	ULONGLONG ramdisk_size;
	size_t n_block;
	HANDLE token;
	TOKEN_PRIVILEGES tok_priv;

	argv = CommandLineToArgvW(GetCommandLine(), &argc);
	if (argc < 9) {
		MessageBoxA(NULL, "RamDyn.exe MountPoint ID Size CleanRatio CleanTimer CleanMargin CleanMaxActivity PhysicalMemory\n\n"
						  "* MountPoint: a drive letter followed by a colon, or the full path to an empty NTFS folder.\n"
						  "* ID: a unique string of at most 10 characters.\n"
						  "* Size: size of the volume, in KB.\n"
						  "* CleanRatio: an approximate ratio, per 1000, of the total drive space from which the cleanup function attempts to free the memory of the deleted files (default: 10).\n"
						  "* CleanTimer: minimal time between 2 cleanups (default: 10).\n"
						  "* CleanMargin: amount of data, in MB, that the cleanup function will not attempt to free, because this space can be required by other applications at the very moment "
										"where the function finishes filling the free space of the volume with empty data (default: 10).\n"
						  "* CleanMaxActivity: the cleanup function waits until reads and writes are below this value, in MB/s (default:10).\n"
						  "* PhysicalMemory: use 0 for allocating virtual memory, 1 for allocating physical memory (default: 0); "
										   "allocating physical memory requires the privilege to lock pages in memory in the local group policy.",
					"Syntax", MB_OK);
		return 1;
	}
	drive_arg = argv[1];
	_snwprintf(drive_root, _countof(drive_root) - 1, L"%s\\", argv[1]);
	drive_root[_countof(drive_root) - 1] = 0;
	drive_id = argv[2];
	if (wcslen(drive_id) > 10) drive_id[10] = 0;
	ramdisk_size = _wtoi64(argv[3]) << 10;
	clean_ratio = _wtoi(argv[4]);
	clean_timer = max(1, _wtoi(argv[5])) * 1000;
	clean_margin = (_wtoi64(argv[6]) << 20) + MEM_BLOCK_SIZE;
	max_activity = _wtoi64(argv[7]) << 20;
	physical = argv[8][0] - '0';

	h_cpl = LoadLibraryA("imdisk.cpl");
	ImDiskOpenDeviceByMountPoint = GetProcAddress(h_cpl, "ImDiskOpenDeviceByMountPoint");
	ImDiskForceRemoveDevice = GetProcAddress(h_cpl, "ImDiskForceRemoveDevice");

	n_block = (ramdisk_size + MEM_BLOCK_SIZE - 1) / MEM_BLOCK_SIZE;

	if (physical) {
		tok_priv.PrivilegeCount = 1;
		tok_priv.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
		current_process = GetCurrentProcess();
		if (!OpenProcessToken(current_process, TOKEN_ADJUST_PRIVILEGES, &token) ||
			!LookupPrivilegeValueA(NULL, "SeLockMemoryPrivilege", &tok_priv.Privileges[0].Luid) ||
			!AdjustTokenPrivileges(token, FALSE, &tok_priv, 0, NULL, NULL) ||
			GetLastError() != ERROR_SUCCESS)
			return 1;
		CloseHandle(token);
		GetSystemInfo(&sys);
		n_pages = MEM_BLOCK_SIZE / sys.dwPageSize;
		page_mask = sys.dwPageSize - 1;
		pfn = VirtualAlloc(NULL, n_pages * n_block * sizeof(size_t), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
		allocated_block = VirtualAlloc(NULL, n_block, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
		virtual_mem_ptr = VirtualAlloc(NULL, MEM_BLOCK_SIZE, MEM_RESERVE | MEM_PHYSICAL, PAGE_READWRITE);
		mem_read = physical_mem_read;
		mem_write = physical_mem_write;
	} else {
		ptr_table = VirtualAlloc(NULL, n_block * sizeof(size_t), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
		mem_read = virtual_mem_read;
		mem_write = virtual_mem_write;
	}

	SetProcessShutdownParameters(0x100, 0);
	CreateThread(NULL, 0, msg_window, NULL, 0, NULL);
	if (clean_timer) CreateThread(NULL, 0, mem_clean, NULL, 0, NULL);

	proxy_info.file_size = ramdisk_size;
	proxy_info.req_alignment = 1;

	return do_comm();
}

#ifdef MINGW_NOSTDLIB
void WinMainCRTStartup()
{
	STARTUPINFO si;
	GetStartupInfo(&si);
	ExitProcess(wWinMain(GetModuleHandle(NULL), NULL, PathGetArgs(GetCommandLine()), si.dwFlags & STARTF_USESHOWWINDOW ? si.wShowWindow : SW_SHOWDEFAULT));
}
#endif
