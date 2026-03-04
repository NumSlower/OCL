#include "runtime.h"
#include "vm.h"
#include "common.h"
#include <stdio.h>

/*
 * runtime.c — helper functions for the VM's runtime support layer.
 *
 * The VM inlines most frame and global access for performance, so
 * these functions are primarily used by the stdlib and error paths.
 */

/* ── Error reporting ──────────────────────────────────────────────── */

/*
 * runtime_error — halt the VM with a formatted error message.
 * Prints to stderr and sets vm->halted + vm->exit_code = 1.
 */
void runtime_error(VM *vm, const char *fmt, ...) {
    va_list ap;
    fprintf(stderr, "RUNTIME ERROR: ");
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");

    if (vm) {
        vm->halted    = true;
        vm->exit_code = 1;
    }
}