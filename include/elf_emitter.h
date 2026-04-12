#ifndef OCL_ELF_EMITTER_H
#define OCL_ELF_EMITTER_H

#include <stdbool.h>

#include "bytecode.h"
#include "errors.h"

bool elf_emit_binary(Bytecode *bytecode, const char *output_path,
                     ErrorCollector *errors);

#endif
