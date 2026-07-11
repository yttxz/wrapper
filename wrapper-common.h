#ifndef WRAPPER_COMMON_H
#define WRAPPER_COMMON_H

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

extern pid_t child_proc;
extern char **environ;

static inline int wrapper_set_android_environment(void) {
    if (setenv("ANDROID_ROOT", "/system", 1) != 0) {
        perror("setenv ANDROID_ROOT");
        return -1;
    }

    if (setenv("ANDROID_DATA", "/data", 1) != 0) {
        perror("setenv ANDROID_DATA");
        return -1;
    }

    return 0;
}

static inline void wrapper_forward_sigint(int signum) {
    (void)signum;
    if (child_proc != -1) {
        kill(child_proc, SIGKILL);
    }
}

static inline int wrapper_install_signal_handler(void) {
    if (signal(SIGINT, wrapper_forward_sigint) == SIG_ERR) {
        perror("signal");
        return -1;
    }
    return 0;
}

static inline int wrapper_bind_urandom(void) {
    if (mkdir("./rootfs/dev", 0755) != 0 && errno != EEXIST) {
        perror("mkdir ./rootfs/dev failed");
        return -1;
    }

    int fd = open("./rootfs/dev/urandom", O_CREAT | O_RDWR, 0666);
    if (fd < 0) {
        perror("open ./rootfs/dev/urandom failed");
        return -1;
    }
    close(fd);

    if (mount("/dev/urandom", "./rootfs/dev/urandom", NULL, MS_BIND, NULL) != 0) {
        perror("mount /dev/urandom failed");
        return -1;
    }

    return 0;
}

static inline int wrapper_enter_rootfs(void) {
    if (chdir("./rootfs") != 0) {
        perror("chdir ./rootfs failed");
        return -1;
    }
    if (chroot(".") != 0) {
        perror("chroot . failed");
        return -1;
    }
    return 0;
}

static inline void wrapper_chmod_runtime_binaries(void) {
    chmod("/system/bin/linker64", 0755);
    chmod("/system/bin/main", 0755);
}

static inline int wrapper_ensure_directory(const char *path, mode_t mode) {
    struct stat path_stat;

    if (mkdir(path, mode) == 0) {
        return 0;
    }
    if (errno != EEXIST) {
        return -1;
    }
    if (stat(path, &path_stat) != 0) {
        return -1;
    }
    if (!S_ISDIR(path_stat.st_mode)) {
        errno = ENOTDIR;
        return -1;
    }

    return 0;
}

static inline int wrapper_mkdir_p(const char *path, mode_t mode) {
    char path_copy[1024];
    size_t path_len;

    if (path == NULL || path[0] == '\0') {
        errno = EINVAL;
        return -1;
    }

    path_len = strlen(path);
    if (path_len >= sizeof(path_copy)) {
        errno = ENAMETOOLONG;
        return -1;
    }

    memcpy(path_copy, path, path_len + 1);
    while (path_len > 1 && path_copy[path_len - 1] == '/') {
        path_copy[--path_len] = '\0';
    }

    char *cursor = path_copy;
    if (cursor[0] == '/') {
        cursor++;
    }

    for (; cursor[0] != '\0'; cursor++) {
        if (cursor[0] != '/' || cursor[-1] == '/') {
            continue;
        }

        cursor[0] = '\0';
        if (wrapper_ensure_directory(path_copy, mode) != 0) {
            cursor[0] = '/';
            return -1;
        }
        cursor[0] = '/';
    }

    return wrapper_ensure_directory(path_copy, mode);
}

static inline int wrapper_create_runtime_dirs(const char *base_dir) {
    if (wrapper_mkdir_p(base_dir, 0777) != 0) {
        perror("create base directory failed");
        return -1;
    }

    char db_path[1024];
    int needed = snprintf(db_path, sizeof(db_path), "%s/mpl_db", base_dir);
    if (needed < 0 || (size_t)needed >= sizeof(db_path)) {
        fprintf(stderr, "mpl_db path is too long\n");
        return -1;
    }
    if (wrapper_mkdir_p(db_path, 0777) != 0) {
        perror("create mpl_db directory failed");
        return -1;
    }

    return 0;
}

#endif
