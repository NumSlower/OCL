/*
 * OCL freestanding process entry point.
 *
 * Provides _start for a -nostdlib link: it reads argc/argv straight off
 * the initial stack, calls main, and exits through the port layer.
 */
#include "port.h"

extern int main(int argc, char **argv);

#if defined(__x86_64__)

__asm__(
    ".global _start\n"
    ".type _start,@function\n"
    "_start:\n"
    "    xor  %rbp, %rbp\n"
    "    mov  (%rsp), %rdi\n"        /* argc            */
    "    lea  8(%rsp), %rsi\n"       /* argv            */
    "    and  $-16, %rsp\n"          /* 16-byte align   */
    "    call main\n"
    "    mov  %eax, %edi\n"
    "    mov  $60, %eax\n"           /* SYS_exit        */
    "    syscall\n"
    "    hlt\n"
    ".size _start, .-_start\n"
);

#elif defined(__aarch64__)

__asm__(
    ".global _start\n"
    ".type _start,%function\n"
    "_start:\n"
    "    mov  x29, #0\n"
    "    mov  x30, #0\n"
    "    mov  x19, sp\n"
    "    ldr  x0, [x19]\n"            /* argc            */
    "    add  x1, x19, #8\n"          /* argv            */
    "    mov  x2, #-16\n"
    "    and  sp, x19, x2\n"          /* 16-byte align   */
    "    bl   main\n"
    "    mov  x8, #93\n"              /* SYS_exit        */
    "    svc  #0\n"
    ".size _start, .-_start\n"
);

#else
#  error "OCL freestanding start: unsupported architecture"
#endif
