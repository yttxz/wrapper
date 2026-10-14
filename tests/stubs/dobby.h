#ifndef WRAPPER_TEST_DOBBY_H
#define WRAPPER_TEST_DOBBY_H
#include <stdint.h>

/* Host syntax checks use the x86_64 shape from the pinned Dobby v1.0.5 API. */
typedef struct {
    uint64_t dummy_0, rsp;
    union {
        struct {
            uint64_t rax, rbx, rcx, rdx, rbp, rsp, rdi, rsi;
            uint64_t r8, r9, r10, r11, r12, r13, r14, r15;
        } regs;
    } general;
    uint64_t dummy_1, flags;
} RegisterContext;
typedef struct { int hook_id; void *target_address; } HookEntryInfo;
int DobbyInstrument(void *, void (*)(RegisterContext *, const HookEntryInfo *));
#endif
