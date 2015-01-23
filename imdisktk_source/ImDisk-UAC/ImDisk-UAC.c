#include <windows.h>
#include <shlwapi.h>

int __stdcall wWinMain(HINSTANCE hinstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow)
{
	STARTUPINFO si = {sizeof si};
	PROCESS_INFORMATION pi;
	TCHAR cmdline[64];

	if (*lpCmdLine && wnsprintf(cmdline, _countof(cmdline), L"rundll32.exe imdisk.cpl,RunDLL_%s", lpCmdLine) > 0)
		CreateProcess(NULL, cmdline, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);

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
