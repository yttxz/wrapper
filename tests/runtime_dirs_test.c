#define _GNU_SOURCE

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef __APPLE__
#include <sys/mount.h>
#define MS_BIND 0
#define mount(...) 0
#endif

#include "../wrapper-common.h"

static int expect_directory(const char *path) {
    struct stat path_stat;

    if (stat(path, &path_stat) != 0) {
        perror(path);
        return -1;
    }
    if (!S_ISDIR(path_stat.st_mode)) {
        fprintf(stderr, "expected directory: %s\n", path);
        return -1;
    }
    return 0;
}

int main(int argc, char **argv) {
    char base_dir[1024];
    char db_dir[1024];
    char blocked_path[1024];
    char blocked_child[1024];
    char oversized_path[1100];
    FILE *blocked_file;

    if (argc != 2) {
        fprintf(stderr, "Usage: %s TEMP_DIR\n", argv[0]);
        return 2;
    }

    if (snprintf(base_dir, sizeof(base_dir),
                 "%s/data/data/com.apple.android.music/files", argv[1]) >=
        (int)sizeof(base_dir)) {
        return 1;
    }
    if (snprintf(db_dir, sizeof(db_dir), "%s/mpl_db", base_dir) >=
        (int)sizeof(db_dir)) {
        return 1;
    }

    if (wrapper_create_runtime_dirs(base_dir) != 0 ||
        wrapper_create_runtime_dirs(base_dir) != 0 ||
        expect_directory(base_dir) != 0 ||
        expect_directory(db_dir) != 0) {
        return 1;
    }

    if (wrapper_mkdir_p("", 0700) == 0 || errno != EINVAL) {
        fprintf(stderr, "empty path was not rejected\n");
        return 1;
    }

    memset(oversized_path, 'a', sizeof(oversized_path) - 1);
    oversized_path[sizeof(oversized_path) - 1] = '\0';
    if (wrapper_mkdir_p(oversized_path, 0700) == 0 || errno != ENAMETOOLONG) {
        fprintf(stderr, "oversized path was not rejected\n");
        return 1;
    }

    if (snprintf(blocked_path, sizeof(blocked_path), "%s/blocker", argv[1]) >=
        (int)sizeof(blocked_path) ||
        snprintf(blocked_child, sizeof(blocked_child), "%s/child", blocked_path) >=
        (int)sizeof(blocked_child)) {
        return 1;
    }

    blocked_file = fopen(blocked_path, "w");
    if (blocked_file == NULL) {
        perror("fopen blocker");
        return 1;
    }
    if (fclose(blocked_file) != 0) {
        perror("fclose blocker");
        return 1;
    }

    if (wrapper_mkdir_p(blocked_child, 0700) == 0 || errno != ENOTDIR) {
        fprintf(stderr, "file path component was not rejected\n");
        return 1;
    }

    return 0;
}
