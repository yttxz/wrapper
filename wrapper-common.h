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

static inline int wrapper_create_runtime_dirs(const char *base_dir) {
    if (mkdir(base_dir, 0777) != 0 && errno != EEXIST) {
        perror("mkdir base_dir_arg failed");
        return -1;
    }

    char db_path[1024];
    int needed = snprintf(db_path, sizeof(db_path), "%s/mpl_db", base_dir);
    if (needed < 0 || (size_t)needed >= sizeof(db_path)) {
        fprintf(stderr, "mpl_db path is too long\n");
        return -1;
    }
    if (mkdir(db_path, 0777) != 0 && errno != EEXIST) {
        perror("mkdir mpl_db failed");
        return -1;
    }

    return 0;
}

#endif
