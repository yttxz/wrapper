#ifndef WRAPPER_TEST_LINK_H
#define WRAPPER_TEST_LINK_H
#include <stdint.h>
#include <stddef.h>

struct dl_phdr_info {
    uintptr_t dlpi_addr;
    const char *dlpi_name;
};
int dl_iterate_phdr(int (*)(struct dl_phdr_info *, size_t, void *), void *);
#endif
