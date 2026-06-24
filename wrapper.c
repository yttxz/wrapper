#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <string.h>
#include <sys/mount.h>

#include "cmdline.h"
#include "wrapper-common.h"

pid_t child_proc = -1;
struct gengetopt_args_info args_info;
#define CAP_SYS_ADMIN_IDX 21
#define CAP_SYS_ADMIN_BIT (1ULL << CAP_SYS_ADMIN_IDX)

int has_cap_sys_admin() {
    FILE *fp;
    char line[256];
    unsigned long long cap_eff = 0;
    int found_cap_eff = 0;

    fp = fopen("/proc/self/status", "r");
    if (fp == NULL) {
        return 0;
    }

    while (fgets(line, sizeof(line), fp) != NULL) {
        if (strncmp(line, "CapEff:", 7) == 0) {
            char *value_str = line + 7;
            while (*value_str == '\t' || *value_str == ' ') {
                value_str++;
            }
            cap_eff = strtoull(value_str, NULL, 16);
            found_cap_eff = 1;
            break;
        }
    }

    fclose(fp);

    if (!found_cap_eff) {
        return 0;
    }

    if (cap_eff & CAP_SYS_ADMIN_BIT) {
        return 1;
    } else {
        return 0;
    }
}

int main(int argc, char *argv[], char *envp[]) {
    cmdline_parser(argc, argv, &args_info);
    if (wrapper_install_signal_handler() != 0) {
        return 1;
    }

    if (wrapper_bind_urandom() != 0) {
        return 1;
    }

    if (wrapper_enter_rootfs() != 0) {
        return 1;
    }

    if (mkdir("/proc", 0755) != 0 && errno != EEXIST) {
        perror("mkdir /proc failed");
        return 1;
    }

    wrapper_chmod_runtime_binaries();

    if (has_cap_sys_admin()) {
        if (unshare(CLONE_NEWPID)) {
            perror("unshare");
            return 1;
        }
    }

    child_proc = fork();
    if (child_proc == -1) {
        perror("fork");
        return 1;
    }

    if (child_proc > 0) {
        wait(NULL);
        return 0;
    }

    if (mount("proc", "/proc", "proc", 0, NULL) != 0) {
        perror("mount proc failed");
        return 1;
    }

    if (wrapper_create_runtime_dirs(args_info.base_dir_arg) != 0) {
        return 1;
    }

    execve("/system/bin/main", argv, envp);
    
    perror("execve");
    return 1;
}
