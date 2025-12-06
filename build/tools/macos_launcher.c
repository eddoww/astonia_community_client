// build/tools/macos_launcher.c
// Tiny Mach-O launcher for Astonia.app
//
// Builds into a small binary that:
//   - Locates the .app bundle
//   - cd's into Contents/Resources/bin
//   - execs ./moac, forwarding all command-line args

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <libgen.h>
#include <limits.h>
#include <string.h>
#include <mach-o/dyld.h>

int main(int argc, char **argv) {
    char exe_path[PATH_MAX];
    uint32_t size = sizeof(exe_path);

    if (_NSGetExecutablePath(exe_path, &size) != 0) {
        fprintf(stderr, "astonia launcher: _NSGetExecutablePath buffer too small\n");
        return 1;
    }

    // exe_path -> .../Astonia.app/Contents/MacOS/astonia
    char *dir = dirname(exe_path); // .../Contents/MacOS
    if (chdir(dir) != 0) {
        perror("astonia launcher: chdir to MacOS failed");
        return 1;
    }

    // cd ../Resources/bin
    if (chdir("../Resources/bin") != 0) {
        perror("astonia launcher: chdir to ../Resources/bin failed");
        return 1;
    }

    // Build argv for ./moac
    char **new_argv = calloc((size_t)argc + 1, sizeof(char *));
    if (!new_argv) {
        fprintf(stderr, "astonia launcher: out of memory\n");
        return 1;
    }

    new_argv[0] = "./moac";
    for (int i = 1; i < argc; i++) {
        new_argv[i] = argv[i];
    }
    new_argv[argc] = NULL;

    execv(new_argv[0], new_argv);

    // If we reach here, exec failed
    perror("astonia launcher: execv ./moac failed");
    free(new_argv);
    return 1;
}