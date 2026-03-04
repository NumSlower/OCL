#ifndef OCL_RUNTIME_H
#define OCL_RUNTIME_H

#include "common.h"
#include "vm.h"
#include <stdarg.h>

/*
 * runtime_error — halt the VM and print a formatted error to stderr.
 * Sets vm->halted = true and vm->exit_code = 1.
 */
void runtime_error(VM *vm, const char *fmt, ...);

#endif /* OCL_RUNTIME_H */