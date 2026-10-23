/*
 * The program entry: SDL 1.2.15's SDLmain
 * (SDL_win32_main.c), which mugen.exe links statically (0x4DCF80-0x4DD700,
 * library code). Rebuilt from the original
 * because the prebuilt SDLmain.lib of the SDL development package uses the
 * old CRT's __iob_func.
 *
 * WinMain splits the command line into argv, redirects stdout and stderr to
 * stdout.txt / stderr.txt next to the executable (unless SDL_STDIO_REDIRECT
 * is set to 0), starts SDL with SDL_INIT_NOPARACHUTE and runs SDL_main
 * (main.c). At exit the redirection files are closed and deleted when empty.
 *
 * Quirks kept from the original:
 * - a quoted argument that starts with '"' (an empty "" argument) reads
 *   through a NULL pointer;
 * - when freopen fails, the original copies a freshly opened FILE over
 *   stdout / stderr; the FILE of this build's CRT is opaque, so that
 *   fallback only keeps the stream open.
 */
#include "mugen.h"

static char g_stdoutPath[MAX_PATH];     /* 0x00503178 */
static char g_stderrPath[MAX_PATH];     /* 0x00503280 */
static int g_stdioRedirected;           /* 0x00503384 */

/* 0x004DD0E0 */
static void ShowError(const char *title, const char *message)
{
    fprintf(stderr, "%s: %s\n", title, message);
}

/* 0x004DD110 */
static int OutOfMemory(void)
{
    ShowError("Fatal Error", "Out of memory - aborting");
    return 0;
}

/* 0x004DD130 */
static void cleanup(void)
{
    SDL_Quit();
}

/* 0x004DD140: closes the redirected streams and deletes the files left empty. */
static void cleanup_output(void)
{
    FILE *file;
    int empty;

    fclose(stdout);
    fclose(stderr);
    if (!g_stdioRedirected) {
        return;
    }
    if (g_stdoutPath[0] != '\0') {
        file = fopen(g_stdoutPath, "rb");
        if (file != NULL) {
            empty = fgetc(file) == EOF;
            fclose(file);
            if (empty) {
                remove(g_stdoutPath);
            }
        }
    }
    if (g_stderrPath[0] != '\0') {
        file = fopen(g_stderrPath, "rb");
        if (file != NULL) {
            empty = fgetc(file) == EOF;
            fclose(file);
            if (empty) {
                remove(g_stderrPath);
            }
        }
    }
}

/* 0x004DD4D0: turns \" into " in place. */
static void UnEscapeQuotes(char *arg)
{
    char *last = NULL;

    while (*arg != '\0') {
        if (*arg == '"' && *last == '\\') {
            char *c_curr = arg;
            char *c_last = last;

            while (*c_curr != '\0') {
                *c_last = *c_curr;
                c_last = c_curr;
                c_curr++;
            }
            *c_last = '\0';
        }
        last = arg;
        arg++;
    }
}

/*
 * 0x004DD360: splits cmdline into arguments (whitespace separated, or in
 * double quotes, where \" does not end the argument). With argv NULL only
 * counts them; otherwise cuts cmdline in place and fills argv (NULL ended).
 */
static int ParseCommandLine(char *cmdline, char **argv)
{
    char *bufp;
    char *lastp = NULL;
    int argc = 0;
    int last_argc = 0;

    for (bufp = cmdline; *bufp != '\0';) {
        while (isspace((unsigned char)*bufp)) {
            ++bufp;
        }
        if (*bufp == '"') {
            ++bufp;
            if (*bufp != '\0') {
                if (argv != NULL) {
                    argv[argc] = bufp;
                }
                ++argc;
            }
            while (*bufp != '\0' && (*bufp != '"' || *lastp == '\\')) {
                lastp = bufp;
                ++bufp;
            }
        } else {
            if (*bufp != '\0') {
                if (argv != NULL) {
                    argv[argc] = bufp;
                }
                ++argc;
            }
            while (*bufp != '\0' && !isspace((unsigned char)*bufp)) {
                ++bufp;
            }
        }
        if (*bufp != '\0') {
            if (argv != NULL) {
                *bufp = '\0';
            }
            ++bufp;
        }
        if (argv != NULL && last_argc != argc) {
            UnEscapeQuotes(argv[last_argc]);
        }
        last_argc = argc;
    }
    if (argv != NULL) {
        argv[argc] = NULL;
    }
    return argc;
}

/* 0x004DD550: stdout and stderr to stdout.txt / stderr.txt in the executable's directory. */
static void redirect_output(void)
{
    char path[MAX_PATH];
    DWORD pathlen;
    FILE *newfp;

    pathlen = GetModuleFileNameA(NULL, path, MAX_PATH);
    while (pathlen > 0 && path[pathlen] != '\\') {
        --pathlen;
    }
    path[pathlen] = '\0';

    SDL_strlcpy(g_stdoutPath, path, MAX_PATH);
    SDL_strlcat(g_stdoutPath, "/stdout.txt", MAX_PATH);
    newfp = freopen(g_stdoutPath, "w", stdout);
    if (newfp == NULL) {
        fopen(g_stdoutPath, "w");
    }

    SDL_strlcpy(g_stderrPath, path, MAX_PATH);
    SDL_strlcat(g_stderrPath, "/stderr.txt", MAX_PATH);
    newfp = freopen(g_stderrPath, "w", stderr);
    if (newfp == NULL) {
        fopen(g_stderrPath, "w");
    }

    setvbuf(stdout, NULL, _IOLBF, BUFSIZ);
    setbuf(stderr, NULL);
    g_stdioRedirected = 1;
}

/* 0x004DCF80: starts SDL and runs SDL_main; exits with its status. */
static int console_main(int argc, char *argv[])
{
    char *appname;
    char *ptr;
    size_t n;
    char *bufp;

    appname = argv[0];
    ptr = strrchr(argv[0], '\\');
    if (ptr != NULL) {
        appname = ptr + 1;
    } else {
        ptr = strrchr(argv[0], '/');
        if (ptr != NULL) {
            appname = ptr + 1;
        }
    }
    ptr = strrchr(appname, '.');
    n = ptr == NULL ? strlen(appname) : (size_t)(ptr - appname);
    bufp = _alloca(n + 1);
    if (bufp == NULL) {
        return OutOfMemory();
    }
    SDL_strlcpy(bufp, appname, n + 1);
    appname = bufp;

    if (SDL_Init(SDL_INIT_NOPARACHUTE) < 0) {
        ShowError("WinMain() error", SDL_GetError());
        return 0;
    }
    atexit(cleanup_output);
    atexit(cleanup);
    SDL_SetModuleHandle(GetModuleHandleA(NULL));
    exit(SDL_main(argc, argv));
    return 0;
}

/* 0x004DD240 */
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR szCmdLine, int sw)
{
    HMODULE handle;
    const char *env;
    char *cmdline;
    char *bufp;
    size_t len;
    int argc;
    char **argv;

    /* starts DirectX early (a workaround of SDL for some drivers) */
    handle = LoadLibraryA("DDRAW.DLL");
    if (handle != NULL) {
        FreeLibrary(handle);
    }

    env = SDL_getenv("SDL_STDIO_REDIRECT");
    if (env != NULL) {
        if (atoi(env)) {
            redirect_output();
        }
    } else {
        redirect_output();
    }

    cmdline = GetCommandLineA();
    len = strlen(cmdline) + 1;
    bufp = _alloca(len);
    if (bufp == NULL) {
        return OutOfMemory();
    }
    SDL_strlcpy(bufp, cmdline, len);

    argc = ParseCommandLine(bufp, NULL);
    argv = _alloca((argc + 1) * sizeof(*argv));
    if (argv == NULL) {
        return OutOfMemory();
    }
    ParseCommandLine(bufp, argv);

    console_main(argc, argv);
    return 0;
}

