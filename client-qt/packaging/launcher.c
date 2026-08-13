#define _GNU_SOURCE

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char* argv[])
{
    (void)argc;

    char package_root[PATH_MAX];
    const ssize_t path_length = readlink(
        "/proc/self/exe", package_root, sizeof(package_root) - 1);
    if (path_length < 0 || (size_t)path_length >= sizeof(package_root) - 1) {
        fprintf(stderr, "driscord: cannot resolve launcher path: %s\n",
            strerror(errno));
        return 127;
    }
    package_root[path_length] = '\0';

    char* separator = strrchr(package_root, '/');
    if (separator == NULL) {
        fputs("driscord: launcher path has no parent directory\n", stderr);
        return 127;
    }
    *separator = '\0';

    if (chdir(package_root) != 0) {
        fprintf(stderr, "driscord: cannot enter package directory: %s\n",
            strerror(errno));
        return 127;
    }

    static const char* const unsafe_environment[] = {
        "QT_PLUGIN_PATH",
        "QT_QPA_PLATFORM_PLUGIN_PATH",
        "QML_IMPORT_PATH",
        "QML2_IMPORT_PATH",
        "LD_LIBRARY_PATH",
        "LD_PRELOAD",
    };
    for (size_t index = 0;
         index < sizeof(unsafe_environment) / sizeof(unsafe_environment[0]);
         ++index) {
        unsetenv(unsafe_environment[index]);
    }

    char client_path[PATH_MAX];
    const int client_path_length = snprintf(client_path, sizeof(client_path),
        "%s/bin/driscord_client", package_root);
    if (client_path_length < 0
        || (size_t)client_path_length >= sizeof(client_path)) {
        fputs("driscord: client path is too long\n", stderr);
        return 127;
    }

    argv[0] = client_path;
    execv(client_path, argv);
    fprintf(stderr, "driscord: cannot start client: %s\n", strerror(errno));
    return 127;
}
