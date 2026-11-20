/* Windows: runs the Python script of the same name beside this program.
 *
 * The toolchain's drivers (pic16f1-cc and the rest) are Python scripts
 * without an extension, which Windows cannot run by name. A copy of this
 * program stands beside each as NAME.exe and runs
 *
 *     ..\libexec\python\python.exe NAME arguments...
 *
 * with the arguments exactly as they were given, and ends with the script's
 * exit code. If the packaged Python is not there it uses python from the
 * PATH. Built by the release workflow: cl /O2 /MT launcher.c
 */
#include <windows.h>
#include <wchar.h>
#include <stdio.h>
#include <stdlib.h>

#define MAXCMD 32768

int main(void) {
    static wchar_t self[MAXCMD], python[MAXCMD], command[MAXCMD];
    DWORD n = GetModuleFileNameW(NULL, self, MAXCMD);
    if (n == 0 || n >= MAXCMD) {
        fprintf(stderr, "pic16f1: cannot find where this program is\n");
        return 127;
    }
    /* The script: this program's path without ".exe". */
    wchar_t *dot = wcsrchr(self, L'.');
    wchar_t *slash = wcsrchr(self, L'\\');
    if (dot && (!slash || dot > slash))
        *dot = 0;
    if (!slash) {
        fprintf(stderr, "pic16f1: cannot find where this program is\n");
        return 127;
    }
    swprintf(python, MAXCMD, L"%.*ls\\..\\libexec\\python\\python.exe", (int)(slash - self), self);
    int packaged = GetFileAttributesW(python) != INVALID_FILE_ATTRIBUTES;

    /* The arguments, as typed: the command line after its first word. */
    const wchar_t *rest = GetCommandLineW();
    if (*rest == L'"') {
        rest++;
        while (*rest && *rest != L'"')
            rest++;
        if (*rest)
            rest++;
    } else {
        while (*rest && *rest != L' ' && *rest != L'\t')
            rest++;
    }
    if (swprintf(command, MAXCMD, L"\"%ls\" \"%ls\"%ls", packaged ? python : L"python", self, rest) < 0) {
        fprintf(stderr, "pic16f1: the command line is too long\n");
        return 127;
    }
    /* Sources are UTF-8 whatever the system's code page is. */
    SetEnvironmentVariableW(L"PYTHONUTF8", L"1");
    /* Ctrl-C goes to the script, which decides; this waits for it. */
    SetConsoleCtrlHandler(NULL, TRUE);

    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof si);
    si.cb = sizeof si;
    if (!CreateProcessW(packaged ? python : NULL, command, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
        fprintf(stderr, "pic16f1: cannot run Python (%ls): error %lu\n", packaged ? python : L"python on the PATH",
                (unsigned long)GetLastError());
        return 127;
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 127;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return (int)code;
}
