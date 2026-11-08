/*
 * vrecord-launcher.c — native launcher for the vrecord Windows portable package.
 *
 * Replaces vrecord.cmd: no cmd.exe involved, no %* / for /f quoting fragility.
 * - locates the package root from its own exe path
 * - sets PATH (package bin + MSYS2 usr/bin) and VRECORD_RESOURCES (MSYS form)
 * - runs MSYS2 bash (login shell, cwd = package root) on ./vrecord
 * - passes all arguments through unchanged (quoted individually)
 *
 * Build (UCRT64):
 *   gcc -O2 -o vrecord.exe vrecord-launcher.c
 */
#include <windows.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* "C:\foo\bar" -> "/c/foo/bar" so bash-side variables use MSYS paths. */
static void to_msys(const char *win, char *out, size_t outsz)
{
    if (((win[0] >= 'A' && win[0] <= 'Z') || (win[0] >= 'a' && win[0] <= 'z'))
        && win[1] == ':') {
        size_t pos = (size_t)snprintf(out, outsz, "/%c", (char)tolower((unsigned char)win[0]));
        const char *p;
        for (p = win + 2; *p && pos < outsz - 1; ++p, ++pos)
            out[pos] = (*p == '\\') ? '/' : *p;
        out[pos] = '\0';
        return;
    }
    snprintf(out, outsz, "%s", win);
}

int main(int argc, char **argv)
{
    char root[MAX_PATH], mroot[MAX_PATH], mres[MAX_PATH];
    char path[16384], cmdline[32768];
    const char *bashcmd = NULL;
    char *slash;

    if (!GetModuleFileNameA(NULL, root, MAX_PATH)) {
        fprintf(stderr, "vrecord: cannot locate launcher path\n");
        return 1;
    }
    slash = strrchr(root, '\\');
    if (!slash) {
        fprintf(stderr, "vrecord: cannot locate package root\n");
        return 1;
    }
    *slash = '\0';

    to_msys(root, mroot, sizeof mroot);
    snprintf(mres, sizeof mres, "%s/Resources", mroot);

    /* pick bash: MSYS2 first, then Git Bash */
    if (GetFileAttributesA("C:\\msys64\\usr\\bin\\bash.exe") != INVALID_FILE_ATTRIBUTES) {
        bashcmd = "C:\\msys64\\usr\\bin\\bash.exe";
        snprintf(path, sizeof path, "%s\\bin;C:\\msys64\\usr\\bin;%s",
                 root, getenv("PATH") ? getenv("PATH") : "");
    } else {
        static char gitbash[MAX_PATH], pf[MAX_PATH];
        GetEnvironmentVariableA("ProgramFiles", pf, MAX_PATH);
        snprintf(gitbash, sizeof gitbash, "%s\\Git\\bin\\bash.exe", pf);
        if (GetFileAttributesA(gitbash) != INVALID_FILE_ATTRIBUTES) {
            bashcmd = gitbash;
            snprintf(path, sizeof path, "%s\\bin;%s",
                     root, getenv("PATH") ? getenv("PATH") : "");
        } else {
            fprintf(stderr, "vrecord: need MSYS2 (C:\\msys64) or Git Bash installed\n");
            return 1;
        }
    }
    SetEnvironmentVariableA("PATH", path);
    SetEnvironmentVariableA("VRECORD_RESOURCES", mres);

    /* bash <abs-script> <args...>  — non-login; env PATH/VRECORD_RESOURCES already set,
     * cwd = package root. (A login shell would cd to $HOME and lose ./vrecord.) */
    size_t cl = (size_t)snprintf(cmdline, sizeof cmdline, "\"%s\" \"%s/vrecord\"", bashcmd, mroot);
    for (int i = 1; i < argc && cl < sizeof cmdline - 600; ++i)
        cl += (size_t)snprintf(cmdline + cl, sizeof cmdline - cl, " \"%s\"", argv[i]);

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof si);
    si.cb = sizeof si;
    /* GUI launch: bash is a console app - without CREATE_NO_WINDOW it would
     * open its own terminal window next to the GUI. */
    if (!CreateProcessA(NULL, cmdline, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, root, &si, &pi)) {
        fprintf(stderr, "vrecord: failed to start bash (error %lu)\n", GetLastError());
        return 1;
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return (int)code;
}
