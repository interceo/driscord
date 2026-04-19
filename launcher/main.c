#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wchar.h>

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR lpCmd, int nShow)
{
    (void)hInst; (void)hPrev; (void)lpCmd; (void)nShow;

    wchar_t self[MAX_PATH];
    GetModuleFileNameW(NULL, self, MAX_PATH);
    wchar_t *sep = wcsrchr(self, L'\\');
    if (!sep) return 1;
    *sep = L'\0';
    const wchar_t *dir = self;

    wchar_t java[MAX_PATH];
    swprintf(java, MAX_PATH, L"%s\\jre\\bin\\javaw.exe", dir);
    if (GetFileAttributesW(java) == INVALID_FILE_ATTRIBUTES) {
        MessageBoxW(NULL,
            L"JRE not found.\nExpected: jre\\bin\\javaw.exe alongside driscord.exe",
            L"Driscord", MB_ICONERROR | MB_OK);
        return 1;
    }

    wchar_t jar[MAX_PATH];
    swprintf(jar, MAX_PATH, L"%s\\driscord.jar", dir);

    wchar_t cmd[4 * MAX_PATH];
    swprintf(cmd, 4 * MAX_PATH,
        L"\"%s\" -Djava.library.path=\"%s\" -jar \"%s\"",
        java, dir, jar);

    STARTUPINFOW si = {0};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {0};
    if (!CreateProcessW(NULL, cmd, NULL, NULL, FALSE,
                        0, NULL, dir, &si, &pi)) {
        wchar_t msg[512];
        swprintf(msg, 512, L"Failed to start: %lu", GetLastError());
        MessageBoxW(NULL, msg, L"Driscord", MB_ICONERROR | MB_OK);
        return 1;
    }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return 0;
}
