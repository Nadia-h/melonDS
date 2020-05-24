#include "../ARMJIT_x64/ARMJIT_Offsets.h"

.text

#define RCPSR W27
#define RCycles W28
#define RCPU X29

.p2align 4,,15

.global ARM_Dispatch
ARM_Dispatch:
    stp x19, x20, [sp, #-96]!
    stp x21, x22, [sp, #16]
    stp x23, x24, [sp, #32]
    stp x25, x26, [sp, #48]
    stp x27, x28, [sp, #64]
    stp x29, x30, [sp, #80]

    mov RCPU, x0
    ldr RCycles, [RCPU, ARM_Cycles_offset]
    ldr RCPSR, [RCPU, ARM_CPSR_offset]

    br x1

.p2align 4,,15

.global ARM_Ret
ARM_Ret:
    str RCycles, [RCPU, ARM_Cycles_offset]
    str RCPSR, [RCPU, ARM_CPSR_offset]

    ldp x29, x30, [sp, #80]
    ldp x27, x28, [sp, #64]
    ldp x25, x26, [sp, #48]
    ldp x23, x24, [sp, #32]
    ldp x21, x22, [sp, #16]
    ldp x19, x20, [sp], #96

    ret

.global ARM_RestoreContext
ARM_RestoreContext:
    mov sp, x0

    ldp x0, x1, [sp]
    ldp x2, x3, [sp, #16]
    ldp x4, x5, [sp, #32]
    ldp x6, x7, [sp, #48]
    ldp x8, x9, [sp, #64]
    ldp x10, x11, [sp, #80]
    ldp x12, x13, [sp, #96]
    ldp x14, x15, [sp, #112]
    ldp x16, x17, [sp, #128]
    ldp x18, x19, [sp, #144]
    ldp x20, x21, [sp, #160]
    ldp x22, x23, [sp, #176]
    ldp x24, x25, [sp, #192]
    ldp x26, x27, [sp, #208]
    ldp x28, x29, [sp, #224]
    ldr x30, [sp, #240]

    ldp x17, x18, [sp, #248]
    mov sp, x17

    br x18