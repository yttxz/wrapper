#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "cmdline.h"

#ifndef WRAPPER_SOURCE_DIR
#define WRAPPER_SOURCE_DIR "."
#endif

#define DEFAULT_DOCKER_IMAGE "wrapper-macos:local"

extern char **environ;

static int run_command(char *const argv[], int quiet) {
    pid_t pid;
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);

    int devnull = -1;
    if (quiet) {
        devnull = open("/dev/null", O_WRONLY);
        if (devnull == -1) {
            perror("open /dev/null");
            posix_spawn_file_actions_destroy(&actions);
            return -1;
        }
        posix_spawn_file_actions_adddup2(&actions, devnull, STDOUT_FILENO);
        posix_spawn_file_actions_adddup2(&actions, devnull, STDERR_FILENO);
    }

    int ret = posix_spawnp(&pid, argv[0], &actions, NULL, argv, environ);
    posix_spawn_file_actions_destroy(&actions);
    if (devnull != -1) {
        close(devnull);
    }
    if (ret != 0) {
        errno = ret;
        return -1;
    }

    int status;
    do {
        if (waitpid(pid, &status, 0) == -1) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        break;
    } while (1);

    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    return -1;
}

static char *join_path(const char *base, const char *child) {
    const size_t base_len = strlen(base);
    const size_t child_len = strlen(child);
    const int needs_slash = base_len > 0 && base[base_len - 1] != '/';
    char *result = malloc(base_len + needs_slash + child_len + 1);
    if (result == NULL) {
        return NULL;
    }
    memcpy(result, base, base_len);
    if (needs_slash) {
        result[base_len] = '/';
    }
    memcpy(result + base_len + needs_slash, child, child_len + 1);
    return result;
}

static int path_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

static char *format_port_mapping(const char *host, int port) {
    int needed = snprintf(NULL, 0, "%s:%d:%d", host, port, port);
    if (needed < 0) {
        return NULL;
    }
    char *result = malloc((size_t)needed + 1);
    if (result == NULL) {
        return NULL;
    }
    snprintf(result, (size_t)needed + 1, "%s:%d:%d", host, port, port);
    return result;
}

static char *format_volume_mapping(const char *host_path) {
    int needed = snprintf(NULL, 0, "%s:/app/rootfs/data", host_path);
    if (needed < 0) {
        return NULL;
    }
    char *result = malloc((size_t)needed + 1);
    if (result == NULL) {
        return NULL;
    }
    snprintf(result, (size_t)needed + 1, "%s:/app/rootfs/data", host_path);
    return result;
}

static int should_skip_host_arg(int argc, char *argv[], int *index) {
    const char *arg = argv[*index];
    if (strcmp(arg, "-H") == 0 || strcmp(arg, "--host") == 0) {
        if (*index + 1 < argc) {
            (*index)++;
        }
        return 1;
    }
    if (strncmp(arg, "-H", 2) == 0 && arg[2] != '\0') {
        return 1;
    }
    if (strncmp(arg, "--host=", 7) == 0) {
        return 1;
    }
    return 0;
}

static int ensure_docker_image(const char *image) {
    char *inspect_argv[] = {"docker", "image", "inspect", (char *)image, NULL};
    int inspect_status = run_command(inspect_argv, 1);
    if (inspect_status == 0) {
        return 0;
    }

    fprintf(stderr, "[+] Docker image %s not found; building it from %s...\n",
            image, WRAPPER_SOURCE_DIR);
    char *build_argv[] = {
        "docker", "buildx", "build",
        "--platform", "linux/amd64",
        "--load",
        "--tag", (char *)image,
        (char *)WRAPPER_SOURCE_DIR,
        NULL
    };
    int build_status = run_command(build_argv, 0);
    if (build_status != 0) {
        fprintf(stderr, "[!] failed to build Docker image %s\n", image);
        return -1;
    }
    return 0;
}

static int is_doctor_request(int argc, char *argv[]) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--doctor") == 0 || strcmp(argv[i], "doctor") == 0) {
            return 1;
        }
    }
    return 0;
}

static int run_doctor(void) {
    char *doctor_path = join_path(WRAPPER_SOURCE_DIR, "scripts/doctor.sh");
    if (doctor_path == NULL) {
        perror("malloc");
        return EXIT_FAILURE;
    }
    if (!path_exists(doctor_path)) {
        fprintf(stderr, "[!] doctor script not found: %s\n", doctor_path);
        free(doctor_path);
        return EXIT_FAILURE;
    }

    char *doctor_argv[] = {doctor_path, NULL};
    execv(doctor_path, doctor_argv);

    perror("execv doctor");
    free(doctor_path);
    return EXIT_FAILURE;
}

int main(int argc, char *argv[]) {
    if (is_doctor_request(argc, argv)) {
        return run_doctor();
    }

    struct gengetopt_args_info args_info;
    if (cmdline_parser(argc, argv, &args_info) != 0) {
        return EXIT_FAILURE;
    }

    const char *image = getenv("WRAPPER_DOCKER_IMAGE");
    if (image == NULL || image[0] == '\0') {
        image = DEFAULT_DOCKER_IMAGE;
    }

    if (ensure_docker_image(image) != 0) {
        cmdline_parser_free(&args_info);
        return EXIT_FAILURE;
    }

    char *data_path = join_path(WRAPPER_SOURCE_DIR, "rootfs/data");
    if (data_path == NULL) {
        perror("malloc");
        cmdline_parser_free(&args_info);
        return EXIT_FAILURE;
    }
    if (!path_exists(data_path)) {
        fprintf(stderr, "[!] rootfs data directory not found: %s\n", data_path);
        free(data_path);
        cmdline_parser_free(&args_info);
        return EXIT_FAILURE;
    }

    char *volume = format_volume_mapping(data_path);
    char *decrypt_port = format_port_mapping(args_info.host_arg, args_info.decrypt_port_arg);
    char *m3u8_port = format_port_mapping(args_info.host_arg, args_info.m3u8_port_arg);
    char *account_port = format_port_mapping(args_info.host_arg, args_info.account_port_arg);
    if (volume == NULL || decrypt_port == NULL || m3u8_port == NULL || account_port == NULL) {
        perror("malloc");
        free(data_path);
        free(volume);
        free(decrypt_port);
        free(m3u8_port);
        free(account_port);
        cmdline_parser_free(&args_info);
        return EXIT_FAILURE;
    }

    char **docker_argv = calloc((size_t)argc + 24, sizeof(char *));
    if (docker_argv == NULL) {
        perror("calloc");
        free(data_path);
        free(volume);
        free(decrypt_port);
        free(m3u8_port);
        free(account_port);
        cmdline_parser_free(&args_info);
        return EXIT_FAILURE;
    }

    int out = 0;
    docker_argv[out++] = "docker";
    docker_argv[out++] = "run";
    docker_argv[out++] = "--privileged";
    docker_argv[out++] = "--rm";
    docker_argv[out++] = "-i";
    docker_argv[out++] = "-v";
    docker_argv[out++] = volume;
    docker_argv[out++] = "-p";
    docker_argv[out++] = decrypt_port;
    docker_argv[out++] = "-p";
    docker_argv[out++] = m3u8_port;
    docker_argv[out++] = "-p";
    docker_argv[out++] = account_port;
    docker_argv[out++] = "--entrypoint";
    docker_argv[out++] = "./wrapper";
    docker_argv[out++] = (char *)image;
    docker_argv[out++] = "-H";
    docker_argv[out++] = "0.0.0.0";

    for (int i = 1; i < argc; i++) {
        if (should_skip_host_arg(argc, argv, &i)) {
            continue;
        }
        docker_argv[out++] = argv[i];
    }
    docker_argv[out] = NULL;

    fprintf(stderr, "[+] launching Docker-backed wrapper on host %s\n", args_info.host_arg);
    fflush(stderr);
    execvp("docker", docker_argv);

    perror("execvp docker");
    free(docker_argv);
    free(data_path);
    free(volume);
    free(decrypt_port);
    free(m3u8_port);
    free(account_port);
    cmdline_parser_free(&args_info);
    return EXIT_FAILURE;
}
