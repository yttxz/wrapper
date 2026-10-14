#define _GNU_SOURCE
#include <errno.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/mount.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>

#include "wrapper-common.h"

pid_t child_proc = -1;

static int write_file(const char *path, const char *line) {
    int fd = open(path, O_WRONLY);
    if (fd < 0) return -1;
    ssize_t len = strlen(line);
    ssize_t ret = write(fd, line, len);
    close(fd);
    return (ret == len) ? 0 : -1;
}

static int setup_unprivileged_namespaces() {
    uid_t uid = getuid();
    gid_t gid = getgid();
    char buf[128];

    if (unshare(CLONE_NEWUSER | CLONE_NEWNS | CLONE_NEWPID) == -1) {
        perror("unshare");
        return -1;
    }

    snprintf(buf, sizeof(buf), "0 %u 1\n", uid);
    if (write_file("/proc/self/uid_map", buf) == -1) {
        perror("uid_map");
        return -1;
    }

    if (write_file("/proc/self/setgroups", "deny\n") == -1) {
        perror("setgroups");
        return -1;
    }

    snprintf(buf, sizeof(buf), "0 %u 1\n", gid);
    if (write_file("/proc/self/gid_map", buf) == -1) {
        perror("gid_map");
        return -1;
    }

    return 0;
}

int main(int argc, char *argv[], char *envp[]) {
    if (wrapper_handle_info(argc, argv)) return 0;
    const char *base_dir = wrapper_base_dir(argc, argv);
    if (wrapper_install_signal_handler() != 0) {
        return 1;
    }

    if (setup_unprivileged_namespaces() != 0) {
        return 1;
    }

    child_proc = fork();
    if (child_proc == -1) {
        perror("fork");
        return 1;
    }

    if (child_proc > 0) {
        int status;
        if (waitpid(child_proc, &status, 0) == -1) {
            perror("waitpid");
            return 1;
        }
        if (WIFEXITED(status)) {
            return WEXITSTATUS(status);
        }
        if (WIFSIGNALED(status)) {
            int signal_number = WTERMSIG(status);
            fprintf(stderr, "[!] child terminated by signal %d\n", signal_number);
            return 128 + signal_number;
        }
        return 1;
    }

    if (wrapper_bind_urandom() != 0) {
        return 1;
    }

    if (mkdir("./rootfs/proc", 0755) != 0 && errno != EEXIST) {
        perror("mkdir ./rootfs/proc failed");
        return 1;
    }

    if (mount("proc", "./rootfs/proc", "proc", 0, NULL) != 0) {
        perror("mount proc failed");
        return 1;
    }

    if (wrapper_enter_rootfs() != 0) {
        return 1;
    }

    wrapper_chmod_runtime_binaries();

    if (wrapper_create_runtime_dirs(base_dir) != 0) {
        return 1;
    }

    if (wrapper_set_android_environment() != 0) {
        return 1;
    }

    execve("/system/bin/lite", argv, environ);
    
    perror("execve");
    return 1;
}
