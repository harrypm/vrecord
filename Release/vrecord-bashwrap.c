/*
 * vrecord-bashwrap.c — windowless bash wrapper for the vrecord Windows GUI.
 *
 * gtkdialog (a GUI app) spawns "bash" via PATH for every action; the real
 * MSYS2 bash is a console app and would flash a terminal window per action.
 * This wrapper is built with -mwindows (no console) and launches the real
 * bash with CREATE_NO_WINDOW, passing the inherited stdio pipes through so
 * g_spawn's redirections keep working.
 *
 * Build (UCRT64):
 *   gcc -O2 -mwindows -o bash.exe vrecord-bashwrap.c
 *
 * Install as <portable>/bin/bash.exe — the launcher calls the real bash by
 * absolute path, so only GUI-spawned children get wrapped.
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    const char *bash = "C:\\msys64\\usr\\bin\\bash.exe";
    if (GetFileAttributesA(bash) == INVALID_FILE_ATTRIBUTES) {
        static char gitbash[MAX_PATH], pf[MAX_PATH];
        GetEnvironmentVariableA("ProgramFiles", pf, MAX_PATH);
        snprintf(gitbash, sizeof gitbash, "%s\\Git\\bin\\bash.exe", pf);
        bash = gitbash;
    }

    size_t cl = strlen(bash) + 8;
    for (int i = 1; i < argc; ++i)
        cl += strlen(argv[i]) + 4;
    char *cmd = (char *)malloc(cl);
    if (!cmd)
        return 127;
    strcpy(cmd, "\"");
    strcat(cmd, bash);
    strcat(cmd, "\"");
    for (int i = 1; i < argc; ++i) {
        strcat(cmd, " \"");
        strcat(cmd, argv[i]);
        strcat(cmd, "\"");
    }

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof si);
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput  = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError  = GetStdHandle(STD_ERROR_HANDLE);

    if (!CreateProcessA(bash, cmd, NULL, NULL, TRUE, CREATE_NO_WINDOW,
                        NULL, NULL, &si, &pi)) {
        return 127;
    }
    DWORD code = 0;
    WaitForSingleObject(pi.hProcess, INFINITE);
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    free(cmd);
    return (int)code;
}
