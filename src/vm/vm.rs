#![no_std]
#![allow(dead_code)]
#![allow(non_camel_case_types)]
#![allow(non_snake_case)]

#[macro_use]
extern crate alloc;

use alloc::string::String;
use alloc::vec::Vec;
use core::alloc::{GlobalAlloc, Layout};
use core::cmp::Ordering;
use core::ffi::{c_char, c_int, c_void, CStr};
use core::panic::PanicInfo;
use core::ptr;

const VM_STACK_MAX: usize = 4096;
const VM_FRAMES_MAX: usize = 4096;
const VM_FRAMES_GROW: usize = 2;
const OCL_FUNC_UNRESOLVED: u32 = 0xFFFF_FFFF;
const OCL_FUNC_BUILTIN: u32 = 0xFFFF_FFFE;
const LOC_NONE: SourceLocation = SourceLocation {
    line: 0,
    column: 0,
    filename: ptr::null(),
};

struct OclAllocator;

unsafe impl GlobalAlloc for OclAllocator {
    unsafe fn alloc(&self, layout: Layout) -> *mut u8 {
        if layout.size() == 0 {
            return ptr::null_mut();
        }
        ocl_malloc(layout.size()) as *mut u8
    }

    unsafe fn dealloc(&self, ptr_: *mut u8, _layout: Layout) {
        if !ptr_.is_null() {
            ocl_free(ptr_ as *mut c_void);
        }
    }

    unsafe fn realloc(&self, ptr_: *mut u8, _layout: Layout, new_size: usize) -> *mut u8 {
        if new_size == 0 {
            if !ptr_.is_null() {
                ocl_free(ptr_ as *mut c_void);
            }
            return ptr::null_mut();
        }
        if ptr_.is_null() {
            return ocl_malloc(new_size) as *mut u8;
        }
        ocl_realloc(ptr_ as *mut c_void, new_size) as *mut u8
    }
}

#[global_allocator]
static GLOBAL_ALLOCATOR: OclAllocator = OclAllocator;

#[alloc_error_handler]
fn alloc_error(_layout: Layout) -> ! {
    unsafe { abort() }
}

#[panic_handler]
fn panic(_info: &PanicInfo<'_>) -> ! {
    unsafe { abort() }
}

#[repr(C)]
#[derive(Copy, Clone, PartialEq, Eq)]
enum ValueType {
    VALUE_INT,
    VALUE_FLOAT,
    VALUE_STRING,
    VALUE_BOOL,
    VALUE_CHAR,
    VALUE_ARRAY,
    VALUE_STRUCT,
    VALUE_FUNCTION,
    VALUE_NULL,
}

#[repr(C)]
#[derive(Copy, Clone, PartialEq, Eq)]
enum Opcode {
    OP_PUSH_CONST,
    OP_DUP,
    OP_POP,
    OP_LOAD_VAR,
    OP_STORE_VAR,
    OP_LOAD_CAPTURE,
    OP_STORE_CAPTURE,
    OP_LOAD_GLOBAL,
    OP_STORE_GLOBAL,
    OP_ADD,
    OP_SUBTRACT,
    OP_MULTIPLY,
    OP_DIVIDE,
    OP_MODULO,
    OP_NEGATE,
    OP_NOT,
    OP_EQUAL,
    OP_NOT_EQUAL,
    OP_LESS,
    OP_LESS_EQUAL,
    OP_GREATER,
    OP_GREATER_EQUAL,
    OP_AND,
    OP_OR,
    OP_JUMP,
    OP_JUMP_IF_FALSE,
    OP_JUMP_IF_TRUE,
    OP_JUMP_IF_NOT_NULL,
    OP_CALL,
    OP_CALL_VALUE,
    OP_RETURN,
    OP_HALT,
    OP_ARRAY_NEW,
    OP_ARRAY_GET,
    OP_ARRAY_SET,
    OP_ARRAY_LEN,
    OP_BIT_AND,
    OP_BIT_OR,
    OP_BIT_XOR,
    OP_BIT_NOT,
    OP_LSHIFT,
    OP_RSHIFT,
    OP_STRUCT_NEW,
    OP_STRUCT_GET,
    OP_STRUCT_SET,
    OP_MAKE_FUNCTION,
}

#[repr(C)]
#[derive(Copy, Clone, PartialEq, Eq)]
enum FuncCaptureSource {
    FUNC_CAPTURE_LOCAL,
    FUNC_CAPTURE_CAPTURE,
}

#[repr(C)]
#[derive(Copy, Clone, PartialEq, Eq)]
enum ErrorKind {
    ERRK_SYNTAX,
    ERRK_TYPE,
    ERRK_OPERATION,
    ERRK_MEMORY,
    ERRK_LOGIC,
}

#[repr(C)]
#[derive(Copy, Clone)]
struct SourceLocation {
    line: c_int,
    column: c_int,
    filename: *const c_char,
}

#[repr(C)]
struct OclArray {
    elements: *mut Value,
    length: usize,
    capacity: usize,
    refcount: c_int,
}

#[repr(C)]
struct OclStruct {
    type_name: *mut c_char,
    field_names: *mut *mut c_char,
    field_values: *mut Value,
    field_count: usize,
    refcount: c_int,
}

#[repr(C)]
struct OclCell {
    value: Value,
    refcount: c_int,
}

#[repr(C)]
struct OclFunction {
    function_index: u32,
    captures: *mut *mut OclCell,
    capture_count: usize,
    refcount: c_int,
}

#[repr(C)]
#[derive(Copy, Clone)]
union ValueData {
    int_val: i64,
    float_val: f64,
    string_val: *mut c_char,
    bool_val: bool,
    char_val: c_char,
    array_val: *mut OclArray,
    struct_val: *mut OclStruct,
    function_val: *mut OclFunction,
}

#[repr(C)]
#[derive(Copy, Clone)]
struct Value {
    kind: ValueType,
    owned: bool,
    data: ValueData,
}

#[repr(C)]
#[derive(Copy, Clone)]
struct FuncCapture {
    name: *mut c_char,
    source: FuncCaptureSource,
    slot: u32,
}

#[repr(C)]
#[derive(Copy, Clone)]
struct Instruction {
    opcode: Opcode,
    operand1: u32,
    operand2: u32,
    location: SourceLocation,
}

#[repr(C)]
struct FuncEntry {
    name: *mut c_char,
    start_ip: u32,
    param_count: c_int,
    local_count: c_int,
    local_names: *mut *mut c_char,
    local_name_count: usize,
    captures: *mut FuncCapture,
    capture_count: usize,
}

#[repr(C)]
struct Bytecode {
    instructions: *mut Instruction,
    instruction_count: usize,
    instruction_capacity: usize,
    constants: *mut Value,
    constant_count: usize,
    constant_capacity: usize,
    functions: *mut FuncEntry,
    function_count: usize,
    function_capacity: usize,
}

#[repr(C)]
struct CallFrame {
    return_ip: u32,
    stack_base: u32,
    locals: *mut *mut OclCell,
    local_count: usize,
    local_capacity: usize,
    function_index: u32,
    closure: *mut OclFunction,
}

#[repr(C)]
struct ErrorCollector {
    _private: [u8; 0],
}

#[repr(C)]
struct VM {
    bytecode: *mut Bytecode,
    errors: *mut ErrorCollector,
    stack: [Value; VM_STACK_MAX],
    stack_top: usize,
    frames: *mut CallFrame,
    frame_top: usize,
    frame_capacity: usize,
    globals: *mut Value,
    global_count: usize,
    global_capacity: usize,
    pc: u32,
    halted: bool,
    exit_code: c_int,
    has_result: bool,
    result: Value,
    program_args: *mut *mut c_char,
    program_argc: c_int,
}

#[repr(C)]
struct StdlibEntry {
    id: c_int,
    name: *const c_char,
    fn_ptr: *mut c_void,
    param_count: c_int,
    return_type: c_int,
}

extern "C" {
    fn abort() -> !;

    fn ocl_malloc(size: usize) -> *mut c_void;
    fn ocl_realloc(ptr: *mut c_void, size: usize) -> *mut c_void;
    fn ocl_free(ptr: *mut c_void);

    fn value_string_copy(s: *const c_char) -> Value;
    fn value_own_copy(v: Value) -> Value;
    fn value_is_truthy(v: Value) -> bool;
    fn value_free(v: Value);
    fn value_type_name(kind: ValueType) -> *const c_char;

    fn ocl_array_new(initial_cap: usize) -> *mut OclArray;
    fn ocl_array_retain(arr: *mut OclArray);
    fn ocl_array_release(arr: *mut OclArray);
    fn ocl_array_set(arr: *mut OclArray, idx: usize, v: Value);
    fn ocl_array_get(arr: *mut OclArray, idx: usize) -> Value;
    fn ocl_array_push(arr: *mut OclArray, v: Value);

    fn ocl_struct_new(type_name: *const c_char, field_count: usize) -> *mut OclStruct;
    fn ocl_struct_retain(obj: *mut OclStruct);
    fn ocl_struct_release(obj: *mut OclStruct);
    fn ocl_struct_set(obj: *mut OclStruct, name: *const c_char, v: Value) -> bool;
    fn ocl_struct_get(obj: *mut OclStruct, name: *const c_char, found: *mut bool) -> Value;

    fn ocl_cell_new(value: Value) -> *mut OclCell;
    fn ocl_cell_retain(cell: *mut OclCell);

    fn ocl_function_new(function_index: u32, capture_count: usize) -> *mut OclFunction;
    fn ocl_function_retain(function: *mut OclFunction);
    fn ocl_function_release(function: *mut OclFunction);

    fn stdlib_lookup_by_name(name: *const c_char) -> *const StdlibEntry;
    fn stdlib_dispatch(vm: *mut VM, id: c_int, argc: c_int) -> bool;

    fn vm_push(vm: *mut VM, v: Value);
    fn vm_pop(vm: *mut VM) -> Value;
    fn vm_peek(vm: *mut VM, depth: usize) -> Value;

    fn ocl_vm_pop_free(vm: *mut VM);
    fn ocl_vm_current_frame(vm: *mut VM) -> *mut CallFrame;
    fn ocl_vm_ensure_local(frame: *mut CallFrame, idx: u32);
    fn ocl_vm_ensure_global(vm: *mut VM, idx: u32);
    fn ocl_vm_accepts_argc(param_count: c_int, argc: u32) -> bool;
    fn ocl_vm_release_frame(frame: *mut CallFrame);
    fn ocl_vm_store_result(vm: *mut VM, value: Value);
    fn ocl_vm_discard_args_push_null(vm: *mut VM, n: c_int);
    fn ocl_vm_report_error(vm: *mut VM, kind: ErrorKind, loc: SourceLocation, message: *const c_char);

    fn fmod(x: f64, y: f64) -> f64;
}

fn value_null() -> Value {
    Value {
        kind: ValueType::VALUE_NULL,
        owned: false,
        data: ValueData { int_val: 0 },
    }
}

fn value_int(v: i64) -> Value {
    Value {
        kind: ValueType::VALUE_INT,
        owned: false,
        data: ValueData { int_val: v },
    }
}

fn value_float(v: f64) -> Value {
    Value {
        kind: ValueType::VALUE_FLOAT,
        owned: false,
        data: ValueData { float_val: v },
    }
}

fn value_bool(v: bool) -> Value {
    Value {
        kind: ValueType::VALUE_BOOL,
        owned: false,
        data: ValueData { bool_val: v },
    }
}

fn value_char(v: c_char) -> Value {
    Value {
        kind: ValueType::VALUE_CHAR,
        owned: false,
        data: ValueData { char_val: v },
    }
}

unsafe fn value_string_owned(ptr_: *mut c_char) -> Value {
    Value {
        kind: ValueType::VALUE_STRING,
        owned: true,
        data: ValueData { string_val: ptr_ },
    }
}

unsafe fn value_string_borrowed(ptr_: *mut c_char) -> Value {
    Value {
        kind: ValueType::VALUE_STRING,
        owned: false,
        data: ValueData { string_val: ptr_ },
    }
}

unsafe fn value_array_owned(arr: *mut OclArray) -> Value {
    if !arr.is_null() {
        ocl_array_retain(arr);
    }
    Value {
        kind: ValueType::VALUE_ARRAY,
        owned: true,
        data: ValueData { array_val: arr },
    }
}

unsafe fn value_struct_owned(obj: *mut OclStruct) -> Value {
    if !obj.is_null() {
        ocl_struct_retain(obj);
    }
    Value {
        kind: ValueType::VALUE_STRUCT,
        owned: true,
        data: ValueData { struct_val: obj },
    }
}

unsafe fn value_function_owned(function: *mut OclFunction) -> Value {
    if !function.is_null() {
        ocl_function_retain(function);
    }
    Value {
        kind: ValueType::VALUE_FUNCTION,
        owned: true,
        data: ValueData {
            function_val: function,
        },
    }
}

unsafe fn c_bytes(ptr_: *const c_char) -> &'static [u8] {
    if ptr_.is_null() {
        b""
    } else {
        CStr::from_ptr(ptr_).to_bytes()
    }
}

unsafe fn c_string(ptr_: *const c_char) -> String {
    String::from_utf8_lossy(c_bytes(ptr_)).into_owned()
}

unsafe fn type_name(kind: ValueType) -> String {
    c_string(value_type_name(kind))
}

fn with_message_ptr<R>(message: String, f: impl FnOnce(*const c_char) -> R) -> R {
    let mut bytes = message.into_bytes();
    for byte in &mut bytes {
        if *byte == 0 {
            *byte = b'?';
        }
    }
    bytes.push(0);
    f(bytes.as_ptr() as *const c_char)
}

unsafe fn report_error(vm: *mut VM, kind: ErrorKind, loc: SourceLocation, message: String) {
    with_message_ptr(message, |ptr_| unsafe {
        ocl_vm_report_error(vm, kind, loc, ptr_);
    });
}

unsafe fn op_error(vm: *mut VM, loc: SourceLocation, message: String) {
    report_error(vm, ErrorKind::ERRK_OPERATION, loc, message);
}

unsafe fn logic_error(vm: *mut VM, loc: SourceLocation, message: String) {
    report_error(vm, ErrorKind::ERRK_LOGIC, loc, message);
}

unsafe fn borrowed_string_to_owned(raw: Value) -> Value {
    if raw.kind == ValueType::VALUE_STRING && !raw.owned {
        value_string_copy(raw.data.string_val as *const c_char)
    } else {
        raw
    }
}

fn is_numeric(kind: ValueType) -> bool {
    matches!(kind, ValueType::VALUE_INT | ValueType::VALUE_FLOAT)
}

unsafe fn as_float(value: Value) -> f64 {
    match value.kind {
        ValueType::VALUE_FLOAT => value.data.float_val,
        _ => value.data.int_val as f64,
    }
}

unsafe fn compare_c_strings(left: *const c_char, right: *const c_char) -> Ordering {
    c_bytes(left).cmp(c_bytes(right))
}

unsafe fn alloc_string_from_bytes(bytes: &[u8]) -> *mut c_char {
    let total = bytes.len() + 1;
    let raw = ocl_malloc(total) as *mut u8;
    if !bytes.is_empty() {
        ptr::copy_nonoverlapping(bytes.as_ptr(), raw, bytes.len());
    }
    *raw.add(bytes.len()) = 0;
    raw as *mut c_char
}

unsafe fn bytecode_ptr(vm: *mut VM) -> *mut Bytecode {
    (*vm).bytecode
}

unsafe fn constant_at(vm: *mut VM, idx: usize) -> Value {
    *(*bytecode_ptr(vm)).constants.add(idx)
}

unsafe fn function_at(vm: *mut VM, idx: usize) -> *mut FuncEntry {
    (*bytecode_ptr(vm)).functions.add(idx)
}

unsafe fn instruction_at(vm: *mut VM, idx: usize) -> Instruction {
    *(*bytecode_ptr(vm)).instructions.add(idx)
}

fn variadic_min(param_count: c_int) -> c_int {
    if param_count < 0 {
        -param_count - 1
    } else {
        param_count
    }
}

unsafe fn make_call_frame(vm: *mut VM, function_index: u32, local_count: c_int, argc: u32) -> *mut CallFrame {
    if (*vm).frame_top >= (*vm).frame_capacity {
        let mut new_capacity = (*vm).frame_capacity.saturating_mul(VM_FRAMES_GROW);
        if new_capacity > VM_FRAMES_MAX {
            new_capacity = VM_FRAMES_MAX;
        }
        (*vm).frames = ocl_realloc(
            (*vm).frames as *mut c_void,
            new_capacity * core::mem::size_of::<CallFrame>(),
        ) as *mut CallFrame;
        (*vm).frame_capacity = new_capacity;
    }

    let frame = (*vm).frames.add((*vm).frame_top);
    (*vm).frame_top += 1;

    (*frame).return_ip = (*vm).pc + 1;
    (*frame).stack_base = (*vm).stack_top as u32;
    (*frame).function_index = function_index;
    (*frame).closure = ptr::null_mut();

    let local_cap = core::cmp::max(local_count, argc as c_int) + 8;
    (*frame).locals = ocl_malloc((local_cap as usize) * core::mem::size_of::<*mut OclCell>()) as *mut *mut OclCell;
    (*frame).local_count = core::cmp::max(local_count, argc as c_int) as usize;
    (*frame).local_capacity = local_cap as usize;

    for i in 0..local_cap as usize {
        *(*frame).locals.add(i) = ocl_cell_new_value_null();
    }

    frame
}

unsafe fn ocl_cell_new_value_null() -> *mut OclCell {
    ocl_cell_new(value_null())
}

unsafe fn free_values(values: &[Value]) {
    for value in values {
        value_free(*value);
    }
}

unsafe fn store_local(frame: *mut CallFrame, slot: usize, value: Value) {
    let cell = *(*frame).locals.add(slot);
    value_free((*cell).value);
    (*cell).value = value;
}

unsafe fn push_null(vm: *mut VM) {
    vm_push(vm, value_null());
}

#[no_mangle]
pub unsafe extern "C" fn vm_execute(vm: *mut VM) -> c_int {
    if vm.is_null() || (*vm).bytecode.is_null() {
        if !vm.is_null() && !(*vm).errors.is_null() {
            report_error(
                vm,
                ErrorKind::ERRK_LOGIC,
                LOC_NONE,
                String::from("vm_execute called with NULL vm or bytecode"),
            );
        }
        return 1;
    }

    while !(*vm).halted && (*vm).pc < (*bytecode_ptr(vm)).instruction_count as u32 {
        let ins = instruction_at(vm, (*vm).pc as usize);
        let loc = ins.location;

        match ins.opcode {
            Opcode::OP_PUSH_CONST => {
                if ins.operand1 >= (*bytecode_ptr(vm)).constant_count as u32 {
                    logic_error(
                        vm,
                        loc,
                        format!(
                            "invalid constant index {} (pool size={})",
                            ins.operand1,
                            (*bytecode_ptr(vm)).constant_count
                        ),
                    );
                    push_null(vm);
                    (*vm).pc += 1;
                    continue;
                }

                let constant = constant_at(vm, ins.operand1 as usize);
                if constant.kind == ValueType::VALUE_STRING {
                    vm_push(vm, value_string_borrowed(constant.data.string_val));
                } else {
                    vm_push(vm, constant);
                }
            }

            Opcode::OP_DUP => {
                let value = vm_peek(vm, 0);
                vm_push(vm, value_own_copy(value));
            }

            Opcode::OP_POP => {
                ocl_vm_pop_free(vm);
            }

            Opcode::OP_LOAD_VAR => {
                let frame = ocl_vm_current_frame(vm);
                if frame.is_null() {
                    op_error(vm, loc, String::from("OP_LOAD_VAR: no active call frame"));
                    push_null(vm);
                } else if ins.operand1 >= (*frame).local_count as u32 {
                    op_error(
                        vm,
                        loc,
                        format!(
                            "OP_LOAD_VAR: slot {} out of bounds (frame has {} locals)",
                            ins.operand1,
                            (*frame).local_count
                        ),
                    );
                    push_null(vm);
                } else {
                    let cell = *(*frame).locals.add(ins.operand1 as usize);
                    vm_push(vm, value_own_copy((*cell).value));
                }
            }

            Opcode::OP_STORE_VAR => {
                let frame = ocl_vm_current_frame(vm);
                if frame.is_null() {
                    op_error(vm, loc, String::from("OP_STORE_VAR: no active call frame"));
                    ocl_vm_pop_free(vm);
                } else {
                    let raw = vm_pop(vm);
                    let stored = borrowed_string_to_owned(raw);
                    ocl_vm_ensure_local(frame, ins.operand1);
                    store_local(frame, ins.operand1 as usize, stored);
                }
            }

            Opcode::OP_LOAD_CAPTURE => {
                let frame = ocl_vm_current_frame(vm);
                if frame.is_null()
                    || (*frame).closure.is_null()
                    || ins.operand1 >= (*(*frame).closure).capture_count as u32
                {
                    op_error(
                        vm,
                        loc,
                        format!("OP_LOAD_CAPTURE: invalid capture slot {}", ins.operand1),
                    );
                    push_null(vm);
                } else {
                    let cell = *(*(*frame).closure).captures.add(ins.operand1 as usize);
                    vm_push(vm, value_own_copy((*cell).value));
                }
            }

            Opcode::OP_STORE_CAPTURE => {
                let frame = ocl_vm_current_frame(vm);
                if frame.is_null()
                    || (*frame).closure.is_null()
                    || ins.operand1 >= (*(*frame).closure).capture_count as u32
                {
                    op_error(
                        vm,
                        loc,
                        format!("OP_STORE_CAPTURE: invalid capture slot {}", ins.operand1),
                    );
                    ocl_vm_pop_free(vm);
                } else {
                    let raw = vm_pop(vm);
                    let stored = borrowed_string_to_owned(raw);
                    let cell = *(*(*frame).closure).captures.add(ins.operand1 as usize);
                    value_free((*cell).value);
                    (*cell).value = stored;
                }
            }

            Opcode::OP_LOAD_GLOBAL => {
                ocl_vm_ensure_global(vm, ins.operand1);
                let value = *(*vm).globals.add(ins.operand1 as usize);
                vm_push(vm, value_own_copy(value));
            }

            Opcode::OP_STORE_GLOBAL => {
                let raw = vm_pop(vm);
                let stored = borrowed_string_to_owned(raw);
                ocl_vm_ensure_global(vm, ins.operand1);
                let slot = (*vm).globals.add(ins.operand1 as usize);
                value_free(*slot);
                *slot = stored;
            }

            Opcode::OP_ADD => {
                let right = vm_pop(vm);
                let left = vm_pop(vm);

                if left.kind == ValueType::VALUE_STRING && right.kind == ValueType::VALUE_STRING {
                    let left_bytes = c_bytes(left.data.string_val as *const c_char);
                    let right_bytes = c_bytes(right.data.string_val as *const c_char);
                    let mut bytes = Vec::with_capacity(left_bytes.len() + right_bytes.len());
                    bytes.extend_from_slice(left_bytes);
                    bytes.extend_from_slice(right_bytes);
                    value_free(left);
                    value_free(right);
                    vm_push(vm, value_string_owned(alloc_string_from_bytes(&bytes)));
                } else if left.kind == ValueType::VALUE_STRING && right.kind == ValueType::VALUE_CHAR {
                    let left_bytes = c_bytes(left.data.string_val as *const c_char);
                    let mut bytes = Vec::with_capacity(left_bytes.len() + 1);
                    bytes.extend_from_slice(left_bytes);
                    bytes.push(right.data.char_val as u8);
                    value_free(left);
                    value_free(right);
                    vm_push(vm, value_string_owned(alloc_string_from_bytes(&bytes)));
                } else if left.kind == ValueType::VALUE_CHAR && right.kind == ValueType::VALUE_STRING {
                    let right_bytes = c_bytes(right.data.string_val as *const c_char);
                    let mut bytes = Vec::with_capacity(right_bytes.len() + 1);
                    bytes.push(left.data.char_val as u8);
                    bytes.extend_from_slice(right_bytes);
                    value_free(left);
                    value_free(right);
                    vm_push(vm, value_string_owned(alloc_string_from_bytes(&bytes)));
                } else if left.kind == ValueType::VALUE_INT && right.kind == ValueType::VALUE_INT {
                    let result = left.data.int_val + right.data.int_val;
                    value_free(left);
                    value_free(right);
                    vm_push(vm, value_int(result));
                } else if is_numeric(left.kind) && is_numeric(right.kind) {
                    let result = as_float(left) + as_float(right);
                    value_free(left);
                    value_free(right);
                    vm_push(vm, value_float(result));
                } else {
                    op_error(
                        vm,
                        loc,
                        format!(
                            "'+' cannot combine {} and {}",
                            type_name(left.kind),
                            type_name(right.kind)
                        ),
                    );
                    value_free(left);
                    value_free(right);
                    push_null(vm);
                }
            }

            Opcode::OP_SUBTRACT => {
                let right = vm_pop(vm);
                let left = vm_pop(vm);

                if left.kind == ValueType::VALUE_INT && right.kind == ValueType::VALUE_INT {
                    vm_push(vm, value_int(left.data.int_val - right.data.int_val));
                } else if is_numeric(left.kind) && is_numeric(right.kind) {
                    vm_push(vm, value_float(as_float(left) - as_float(right)));
                } else {
                    op_error(
                        vm,
                        loc,
                        format!(
                            "arithmetic requires numeric operands, got {} and {}",
                            type_name(left.kind),
                            type_name(right.kind)
                        ),
                    );
                    push_null(vm);
                }
                value_free(left);
                value_free(right);
            }

            Opcode::OP_MULTIPLY => {
                let right = vm_pop(vm);
                let left = vm_pop(vm);

                if (left.kind == ValueType::VALUE_STRING && right.kind == ValueType::VALUE_INT)
                    || (left.kind == ValueType::VALUE_INT && right.kind == ValueType::VALUE_STRING)
                {
                    let src = if left.kind == ValueType::VALUE_STRING {
                        c_bytes(left.data.string_val as *const c_char)
                    } else {
                        c_bytes(right.data.string_val as *const c_char)
                    };
                    let count = if left.kind == ValueType::VALUE_INT {
                        left.data.int_val
                    } else {
                        right.data.int_val
                    };

                    if count <= 0 {
                        value_free(left);
                        value_free(right);
                        vm_push(vm, value_string_owned(alloc_string_from_bytes(b"")));
                    } else {
                        let mut bytes = Vec::with_capacity(src.len().saturating_mul(count as usize));
                        for _ in 0..count {
                            bytes.extend_from_slice(src);
                        }
                        value_free(left);
                        value_free(right);
                        vm_push(vm, value_string_owned(alloc_string_from_bytes(&bytes)));
                    }
                } else if left.kind == ValueType::VALUE_INT && right.kind == ValueType::VALUE_INT {
                    vm_push(vm, value_int(left.data.int_val * right.data.int_val));
                    value_free(left);
                    value_free(right);
                } else if is_numeric(left.kind) && is_numeric(right.kind) {
                    vm_push(vm, value_float(as_float(left) * as_float(right)));
                    value_free(left);
                    value_free(right);
                } else {
                    op_error(
                        vm,
                        loc,
                        format!(
                            "'*' cannot combine {} and {}",
                            type_name(left.kind),
                            type_name(right.kind)
                        ),
                    );
                    value_free(left);
                    value_free(right);
                    push_null(vm);
                }
            }

            Opcode::OP_DIVIDE => {
                let right = vm_pop(vm);
                let left = vm_pop(vm);

                if is_numeric(left.kind) && is_numeric(right.kind) {
                    let divide_by_zero = if right.kind == ValueType::VALUE_FLOAT {
                        right.data.float_val == 0.0
                    } else {
                        right.data.int_val == 0
                    };

                    if divide_by_zero {
                        op_error(vm, loc, String::from("division by zero"));
                        value_free(left);
                        value_free(right);
                        push_null(vm);
                    } else if left.kind == ValueType::VALUE_INT && right.kind == ValueType::VALUE_INT {
                        vm_push(vm, value_int(left.data.int_val / right.data.int_val));
                        value_free(left);
                        value_free(right);
                    } else {
                        vm_push(vm, value_float(as_float(left) / as_float(right)));
                        value_free(left);
                        value_free(right);
                    }
                } else {
                    op_error(
                        vm,
                        loc,
                        format!(
                            "'/' requires numeric operands, got {} and {}",
                            type_name(left.kind),
                            type_name(right.kind)
                        ),
                    );
                    value_free(left);
                    value_free(right);
                    push_null(vm);
                }
            }

            Opcode::OP_MODULO => {
                let right = vm_pop(vm);
                let left = vm_pop(vm);

                if left.kind == ValueType::VALUE_INT && right.kind == ValueType::VALUE_INT {
                    if right.data.int_val == 0 {
                        op_error(vm, loc, String::from("modulo by zero"));
                        value_free(left);
                        value_free(right);
                        push_null(vm);
                    } else {
                        vm_push(vm, value_int(left.data.int_val % right.data.int_val));
                        value_free(left);
                        value_free(right);
                    }
                } else if is_numeric(left.kind) && is_numeric(right.kind) {
                    let right_float = as_float(right);
                    if right_float == 0.0 {
                        op_error(vm, loc, String::from("modulo by zero"));
                        value_free(left);
                        value_free(right);
                        push_null(vm);
                    } else {
                        vm_push(vm, value_float(fmod(as_float(left), right_float)));
                        value_free(left);
                        value_free(right);
                    }
                } else {
                    op_error(
                        vm,
                        loc,
                        format!(
                            "'%' requires numeric operands, got {} and {}",
                            type_name(left.kind),
                            type_name(right.kind)
                        ),
                    );
                    value_free(left);
                    value_free(right);
                    push_null(vm);
                }
            }

            Opcode::OP_NEGATE => {
                let value = vm_pop(vm);
                if value.kind == ValueType::VALUE_INT {
                    vm_push(vm, value_int(-value.data.int_val));
                } else if value.kind == ValueType::VALUE_FLOAT {
                    vm_push(vm, value_float(-value.data.float_val));
                } else {
                    op_error(
                        vm,
                        loc,
                        format!("unary '-' on {}", type_name(value.kind)),
                    );
                    push_null(vm);
                }
                value_free(value);
            }

            Opcode::OP_NOT => {
                let value = vm_pop(vm);
                vm_push(vm, value_bool(!value_is_truthy(value)));
                value_free(value);
            }

            Opcode::OP_BIT_NOT => {
                let value = vm_pop(vm);
                if value.kind != ValueType::VALUE_INT {
                    op_error(
                        vm,
                        loc,
                        format!("bitwise '~' requires Int, got {}", type_name(value.kind)),
                    );
                    value_free(value);
                    push_null(vm);
                } else {
                    vm_push(vm, value_int(!value.data.int_val));
                    value_free(value);
                }
            }

            Opcode::OP_EQUAL => {
                let right = vm_pop(vm);
                let left = vm_pop(vm);
                let mut result = false;

                if left.kind == right.kind {
                    result = match left.kind {
                        ValueType::VALUE_INT => left.data.int_val == right.data.int_val,
                        ValueType::VALUE_FLOAT => left.data.float_val == right.data.float_val,
                        ValueType::VALUE_BOOL => left.data.bool_val == right.data.bool_val,
                        ValueType::VALUE_CHAR => left.data.char_val == right.data.char_val,
                        ValueType::VALUE_STRING => {
                            if !left.data.string_val.is_null() && !right.data.string_val.is_null() {
                                compare_c_strings(
                                    left.data.string_val as *const c_char,
                                    right.data.string_val as *const c_char,
                                ) == Ordering::Equal
                            } else {
                                left.data.string_val == right.data.string_val
                            }
                        }
                        ValueType::VALUE_ARRAY => left.data.array_val == right.data.array_val,
                        ValueType::VALUE_STRUCT => left.data.struct_val == right.data.struct_val,
                        ValueType::VALUE_NULL => true,
                        _ => false,
                    };
                } else if is_numeric(left.kind) && is_numeric(right.kind) {
                    result = as_float(left) == as_float(right);
                }

                value_free(left);
                value_free(right);
                vm_push(vm, value_bool(result));
            }

            Opcode::OP_NOT_EQUAL => {
                let right = vm_pop(vm);
                let left = vm_pop(vm);
                let mut result = true;

                if left.kind == right.kind {
                    result = match left.kind {
                        ValueType::VALUE_INT => left.data.int_val != right.data.int_val,
                        ValueType::VALUE_FLOAT => left.data.float_val != right.data.float_val,
                        ValueType::VALUE_BOOL => left.data.bool_val != right.data.bool_val,
                        ValueType::VALUE_CHAR => left.data.char_val != right.data.char_val,
                        ValueType::VALUE_STRING => {
                            if !left.data.string_val.is_null() && !right.data.string_val.is_null() {
                                compare_c_strings(
                                    left.data.string_val as *const c_char,
                                    right.data.string_val as *const c_char,
                                ) != Ordering::Equal
                            } else {
                                left.data.string_val != right.data.string_val
                            }
                        }
                        ValueType::VALUE_ARRAY => left.data.array_val != right.data.array_val,
                        ValueType::VALUE_STRUCT => left.data.struct_val != right.data.struct_val,
                        ValueType::VALUE_NULL => false,
                        _ => true,
                    };
                } else if is_numeric(left.kind) && is_numeric(right.kind) {
                    result = as_float(left) != as_float(right);
                }

                value_free(left);
                value_free(right);
                vm_push(vm, value_bool(result));
            }

            Opcode::OP_LESS
            | Opcode::OP_LESS_EQUAL
            | Opcode::OP_GREATER
            | Opcode::OP_GREATER_EQUAL => {
                let right = vm_pop(vm);
                let left = vm_pop(vm);
                let result = if left.kind == ValueType::VALUE_STRING && right.kind == ValueType::VALUE_STRING {
                    let cmp = compare_c_strings(
                        left.data.string_val as *const c_char,
                        right.data.string_val as *const c_char,
                    );
                    match ins.opcode {
                        Opcode::OP_LESS => cmp == Ordering::Less,
                        Opcode::OP_LESS_EQUAL => cmp != Ordering::Greater,
                        Opcode::OP_GREATER => cmp == Ordering::Greater,
                        Opcode::OP_GREATER_EQUAL => cmp != Ordering::Less,
                        _ => false,
                    }
                } else if left.kind == ValueType::VALUE_INT && right.kind == ValueType::VALUE_INT {
                    match ins.opcode {
                        Opcode::OP_LESS => left.data.int_val < right.data.int_val,
                        Opcode::OP_LESS_EQUAL => left.data.int_val <= right.data.int_val,
                        Opcode::OP_GREATER => left.data.int_val > right.data.int_val,
                        Opcode::OP_GREATER_EQUAL => left.data.int_val >= right.data.int_val,
                        _ => false,
                    }
                } else if is_numeric(left.kind) && is_numeric(right.kind) {
                    let left_float = as_float(left);
                    let right_float = as_float(right);
                    match ins.opcode {
                        Opcode::OP_LESS => left_float < right_float,
                        Opcode::OP_LESS_EQUAL => left_float <= right_float,
                        Opcode::OP_GREATER => left_float > right_float,
                        Opcode::OP_GREATER_EQUAL => left_float >= right_float,
                        _ => false,
                    }
                } else {
                    false
                };
                value_free(left);
                value_free(right);
                vm_push(vm, value_bool(result));
            }

            Opcode::OP_AND => {
                let right = vm_pop(vm);
                let left = vm_pop(vm);
                let result = value_is_truthy(left) && value_is_truthy(right);
                value_free(left);
                value_free(right);
                vm_push(vm, value_bool(result));
            }

            Opcode::OP_OR => {
                let right = vm_pop(vm);
                let left = vm_pop(vm);
                let result = value_is_truthy(left) || value_is_truthy(right);
                value_free(left);
                value_free(right);
                vm_push(vm, value_bool(result));
            }

            Opcode::OP_BIT_AND
            | Opcode::OP_BIT_OR
            | Opcode::OP_BIT_XOR
            | Opcode::OP_LSHIFT
            | Opcode::OP_RSHIFT => {
                let right = vm_pop(vm);
                let left = vm_pop(vm);

                if left.kind != ValueType::VALUE_INT || right.kind != ValueType::VALUE_INT {
                    op_error(
                        vm,
                        loc,
                        format!(
                            "bitwise operators require Int operands, got {} and {}",
                            type_name(left.kind),
                            type_name(right.kind)
                        ),
                    );
                    value_free(left);
                    value_free(right);
                    push_null(vm);
                } else if matches!(ins.opcode, Opcode::OP_LSHIFT | Opcode::OP_RSHIFT)
                    && (right.data.int_val < 0 || right.data.int_val >= 64)
                {
                    op_error(
                        vm,
                        loc,
                        format!("shift count must be between 0 and 63, got {}", right.data.int_val),
                    );
                    value_free(left);
                    value_free(right);
                    push_null(vm);
                } else {
                    let result = match ins.opcode {
                        Opcode::OP_BIT_AND => left.data.int_val & right.data.int_val,
                        Opcode::OP_BIT_OR => left.data.int_val | right.data.int_val,
                        Opcode::OP_BIT_XOR => left.data.int_val ^ right.data.int_val,
                        Opcode::OP_LSHIFT => left.data.int_val << right.data.int_val,
                        Opcode::OP_RSHIFT => left.data.int_val >> right.data.int_val,
                        _ => 0,
                    };
                    value_free(left);
                    value_free(right);
                    vm_push(vm, value_int(result));
                }
            }

            Opcode::OP_JUMP => {
                (*vm).pc = ins.operand1;
                continue;
            }

            Opcode::OP_JUMP_IF_FALSE => {
                let condition = vm_pop(vm);
                let taken = !value_is_truthy(condition);
                value_free(condition);
                if taken {
                    (*vm).pc = ins.operand1;
                    continue;
                }
            }

            Opcode::OP_JUMP_IF_TRUE => {
                let condition = vm_pop(vm);
                let taken = value_is_truthy(condition);
                value_free(condition);
                if taken {
                    (*vm).pc = ins.operand1;
                    continue;
                }
            }

            Opcode::OP_JUMP_IF_NOT_NULL => {
                let condition = vm_pop(vm);
                let taken = condition.kind != ValueType::VALUE_NULL;
                value_free(condition);
                if taken {
                    (*vm).pc = ins.operand1;
                    continue;
                }
            }

            Opcode::OP_CALL => {
                let function_index = ins.operand1;
                let argc = ins.operand2;

                if function_index == OCL_FUNC_UNRESOLVED {
                    op_error(vm, loc, String::from("call to unresolved function"));
                    ocl_vm_discard_args_push_null(vm, argc as c_int);
                } else if function_index >= (*bytecode_ptr(vm)).function_count as u32 {
                    logic_error(
                        vm,
                        loc,
                        format!(
                            "invalid function index {} (only {} defined)",
                            function_index,
                            (*bytecode_ptr(vm)).function_count
                        ),
                    );
                    ocl_vm_discard_args_push_null(vm, argc as c_int);
                } else {
                    let function = function_at(vm, function_index as usize);
                    if !ocl_vm_accepts_argc((*function).param_count, argc) {
                        if (*function).param_count < 0 {
                            op_error(
                                vm,
                                loc,
                                format!(
                                    "function '{}' expects at least {} argument(s), got {}",
                                    c_string((*function).name as *const c_char),
                                    variadic_min((*function).param_count),
                                    argc
                                ),
                            );
                        } else {
                            op_error(
                                vm,
                                loc,
                                format!(
                                    "function '{}' expects {} argument(s), got {}",
                                    c_string((*function).name as *const c_char),
                                    (*function).param_count,
                                    argc
                                ),
                            );
                        }
                        ocl_vm_discard_args_push_null(vm, argc as c_int);
                    } else if (*function).start_ip == OCL_FUNC_BUILTIN {
                        let entry = stdlib_lookup_by_name((*function).name as *const c_char);
                        if entry.is_null() || !stdlib_dispatch(vm, (*entry).id, argc as c_int) {
                            op_error(
                                vm,
                                loc,
                                format!(
                                    "call to unknown builtin '{}'",
                                    c_string((*function).name as *const c_char)
                                ),
                            );
                            ocl_vm_discard_args_push_null(vm, argc as c_int);
                        }
                    } else if (*vm).frame_top >= VM_FRAMES_MAX {
                        op_error(
                            vm,
                            loc,
                            format!(
                                "call stack overflow (max {} frames, called '{}')",
                                VM_FRAMES_MAX,
                                c_string((*function).name as *const c_char)
                            ),
                        );
                        ocl_vm_discard_args_push_null(vm, argc as c_int);
                    } else {
                        let frame = make_call_frame(vm, function_index, (*function).local_count, argc);

                        for i in (0..argc as usize).rev() {
                            let popped = vm_pop(vm);
                            let stored = borrowed_string_to_owned(popped);
                            store_local(frame, i, stored);
                        }

                        (*vm).pc = (*function).start_ip;
                        continue;
                    }
                }
            }

            Opcode::OP_CALL_VALUE => {
                let argc = ins.operand2 as usize;
                let mut args = vec![value_null(); argc];
                for i in (0..argc).rev() {
                    args[i] = vm_pop(vm);
                }
                let callee = vm_pop(vm);

                if callee.kind != ValueType::VALUE_FUNCTION || callee.data.function_val.is_null() {
                    op_error(vm, loc, String::from("call target is not a function"));
                    free_values(&args);
                    value_free(callee);
                    push_null(vm);
                } else {
                    let function_index = (*callee.data.function_val).function_index;
                    if function_index >= (*bytecode_ptr(vm)).function_count as u32 {
                        logic_error(vm, loc, format!("invalid function index {}", function_index));
                        free_values(&args);
                        value_free(callee);
                        push_null(vm);
                    } else {
                        let function = function_at(vm, function_index as usize);
                        if !ocl_vm_accepts_argc((*function).param_count, argc as u32) {
                            op_error(
                                vm,
                                loc,
                                format!(
                                    "function value '{}' expects {} argument(s), got {}",
                                    c_string((*function).name as *const c_char),
                                    (*function).param_count,
                                    argc
                                ),
                            );
                            free_values(&args);
                            value_free(callee);
                            push_null(vm);
                        } else if (*function).start_ip == OCL_FUNC_BUILTIN {
                            for arg in &args {
                                vm_push(vm, *arg);
                            }
                            let entry = stdlib_lookup_by_name((*function).name as *const c_char);
                            value_free(callee);
                            if entry.is_null() || !stdlib_dispatch(vm, (*entry).id, argc as c_int) {
                                op_error(
                                    vm,
                                    loc,
                                    format!(
                                        "call to unknown builtin '{}'",
                                        c_string((*function).name as *const c_char)
                                    ),
                                );
                                ocl_vm_discard_args_push_null(vm, argc as c_int);
                            }
                        } else if (*vm).frame_top >= VM_FRAMES_MAX {
                            op_error(
                                vm,
                                loc,
                                format!("call stack overflow (max {} frames)", VM_FRAMES_MAX),
                            );
                            free_values(&args);
                            value_free(callee);
                            push_null(vm);
                        } else {
                            let frame = make_call_frame(vm, function_index, (*function).local_count, argc as u32);
                            (*frame).closure = callee.data.function_val;
                            ocl_function_retain((*frame).closure);

                            for i in 0..argc {
                                let stored = borrowed_string_to_owned(args[i]);
                                store_local(frame, i, stored);
                            }

                            value_free(callee);
                            (*vm).pc = (*function).start_ip;
                            continue;
                        }
                    }
                }
            }

            Opcode::OP_MAKE_FUNCTION => {
                if ins.operand1 >= (*bytecode_ptr(vm)).function_count as u32 {
                    logic_error(vm, loc, format!("invalid function index {}", ins.operand1));
                    push_null(vm);
                } else {
                    let function = function_at(vm, ins.operand1 as usize);
                    let closure = ocl_function_new(ins.operand1, (*function).capture_count);
                    let frame = ocl_vm_current_frame(vm);

                    for i in 0..(*function).capture_count {
                        let capture = (*function).captures.add(i);
                        let mut cell: *mut OclCell = ptr::null_mut();

                        if (*capture).source == FuncCaptureSource::FUNC_CAPTURE_LOCAL {
                            if frame.is_null() || (*capture).slot >= (*frame).local_count as u32 {
                                logic_error(
                                    vm,
                                    loc,
                                    format!("invalid local capture slot {}", (*capture).slot),
                                );
                                continue;
                            }
                            cell = *(*frame).locals.add((*capture).slot as usize);
                        } else {
                            if frame.is_null()
                                || (*frame).closure.is_null()
                                || (*capture).slot >= (*(*frame).closure).capture_count as u32
                            {
                                logic_error(
                                    vm,
                                    loc,
                                    format!("invalid outer capture slot {}", (*capture).slot),
                                );
                                continue;
                            }
                            cell = *(*(*frame).closure).captures.add((*capture).slot as usize);
                        }

                        *(*closure).captures.add(i) = cell;
                        ocl_cell_retain(cell);
                    }

                    vm_push(vm, value_function_owned(closure));
                    ocl_function_release(closure);
                }
            }

            Opcode::OP_RETURN => {
                let raw = vm_pop(vm);

                if (*vm).frame_top == 0 {
                    ocl_vm_store_result(vm, raw);
                    value_free(raw);
                    (*vm).halted = true;
                } else {
                    let result = borrowed_string_to_owned(raw);
                    (*vm).frame_top -= 1;
                    let frame = (*vm).frames.add((*vm).frame_top);
                    let return_ip = (*frame).return_ip;
                    let stack_base = (*frame).stack_base as usize;

                    ocl_vm_release_frame(frame);

                    while (*vm).stack_top > stack_base {
                        ocl_vm_pop_free(vm);
                    }

                    vm_push(vm, result);
                    (*vm).pc = return_ip;
                    continue;
                }
            }

            Opcode::OP_HALT => {
                (*vm).halted = true;
                if (*vm).stack_top > 0 {
                    ocl_vm_store_result(vm, vm_peek(vm, 0));
                }
            }

            Opcode::OP_ARRAY_NEW => {
                let count = ins.operand1 as usize;
                let arr = ocl_array_new(if count > 0 { count } else { 8 });

                if count > 0 {
                    let mut values = vec![value_null(); count];
                    for i in (0..count).rev() {
                        values[i] = vm_pop(vm);
                    }
                    for value in &values {
                        ocl_array_push(arr, *value);
                        value_free(*value);
                    }
                }

                vm_push(vm, value_array_owned(arr));
                ocl_array_release(arr);
            }

            Opcode::OP_ARRAY_GET => {
                let index_value = vm_pop(vm);
                let array_value = vm_pop(vm);

                if array_value.kind == ValueType::VALUE_STRING {
                    if index_value.kind != ValueType::VALUE_INT {
                        op_error(
                            vm,
                            loc,
                            format!("string index must be Int, got {}", type_name(index_value.kind)),
                        );
                        value_free(index_value);
                        value_free(array_value);
                        push_null(vm);
                    } else {
                        let bytes = c_bytes(array_value.data.string_val as *const c_char);
                        let mut index = index_value.data.int_val;
                        if index < 0 {
                            index += bytes.len() as i64;
                        }
                        if index < 0 || index as usize >= bytes.len() {
                            op_error(
                                vm,
                                loc,
                                format!(
                                    "string index {} out of bounds [0, {})",
                                    index_value.data.int_val,
                                    bytes.len()
                                ),
                            );
                            value_free(index_value);
                            value_free(array_value);
                            push_null(vm);
                        } else {
                            let ch = bytes[index as usize] as c_char;
                            value_free(index_value);
                            value_free(array_value);
                            vm_push(vm, value_char(ch));
                        }
                    }
                } else if array_value.kind != ValueType::VALUE_ARRAY || array_value.data.array_val.is_null() {
                    op_error(
                        vm,
                        loc,
                        format!("index access on non-indexable type {}", type_name(array_value.kind)),
                    );
                    value_free(index_value);
                    value_free(array_value);
                    push_null(vm);
                } else if index_value.kind != ValueType::VALUE_INT {
                    op_error(
                        vm,
                        loc,
                        format!("array index must be Int, got {}", type_name(index_value.kind)),
                    );
                    value_free(index_value);
                    value_free(array_value);
                    push_null(vm);
                } else {
                    let arr = array_value.data.array_val;
                    let mut index = index_value.data.int_val;
                    if index < 0 {
                        index += (*arr).length as i64;
                    }
                    if index < 0 || index as usize >= (*arr).length {
                        op_error(
                            vm,
                            loc,
                            format!(
                                "array index {} out of bounds [0, {})",
                                index_value.data.int_val,
                                (*arr).length
                            ),
                        );
                        value_free(index_value);
                        value_free(array_value);
                        push_null(vm);
                    } else {
                        let element = ocl_array_get(arr, index as usize);
                        value_free(index_value);
                        value_free(array_value);
                        vm_push(vm, element);
                    }
                }
            }

            Opcode::OP_ARRAY_SET => {
                let index_value = vm_pop(vm);
                let array_value = vm_pop(vm);
                let value = vm_pop(vm);

                if array_value.kind != ValueType::VALUE_ARRAY || array_value.data.array_val.is_null() {
                    op_error(
                        vm,
                        loc,
                        format!("OP_ARRAY_SET on non-Array type {}", type_name(array_value.kind)),
                    );
                    value_free(index_value);
                    value_free(array_value);
                    value_free(value);
                } else if index_value.kind != ValueType::VALUE_INT {
                    op_error(
                        vm,
                        loc,
                        format!("array index must be Int, got {}", type_name(index_value.kind)),
                    );
                    value_free(index_value);
                    value_free(array_value);
                    value_free(value);
                } else {
                    let mut index = index_value.data.int_val;
                    if index < 0 {
                        index += (*array_value.data.array_val).length as i64;
                    }
                    if index < 0 {
                        op_error(
                            vm,
                            loc,
                            format!("array index {} out of bounds", index_value.data.int_val),
                        );
                        value_free(index_value);
                        value_free(array_value);
                        value_free(value);
                    } else {
                        ocl_array_set(array_value.data.array_val, index as usize, value);
                        value_free(index_value);
                        value_free(array_value);
                        value_free(value);
                    }
                }
            }

            Opcode::OP_ARRAY_LEN => {
                let array_value = vm_pop(vm);
                let len = if array_value.kind == ValueType::VALUE_STRING {
                    c_bytes(array_value.data.string_val as *const c_char).len() as i64
                } else if array_value.kind == ValueType::VALUE_ARRAY && !array_value.data.array_val.is_null() {
                    (*array_value.data.array_val).length as i64
                } else {
                    op_error(
                        vm,
                        loc,
                        format!("arrayLen requires Array or String, got {}", type_name(array_value.kind)),
                    );
                    0
                };
                value_free(array_value);
                vm_push(vm, value_int(len));
            }

            Opcode::OP_STRUCT_NEW => {
                let mut type_name_ptr: *const c_char = b"Struct\0".as_ptr() as *const c_char;
                let mut ok = true;

                if ins.operand1 < (*bytecode_ptr(vm)).constant_count as u32 {
                    let type_value = constant_at(vm, ins.operand1 as usize);
                    if type_value.kind == ValueType::VALUE_STRING && !type_value.data.string_val.is_null() {
                        type_name_ptr = type_value.data.string_val as *const c_char;
                    } else {
                        ok = false;
                    }
                } else {
                    ok = false;
                }

                if !ok {
                    logic_error(
                        vm,
                        loc,
                        format!("invalid struct type constant {}", ins.operand1),
                    );
                }

                let obj = ocl_struct_new(type_name_ptr, ins.operand2 as usize);
                if ins.operand2 > 0 {
                    let mut pairs: Vec<(Value, Value)> = Vec::with_capacity(ins.operand2 as usize);
                    for _ in 0..ins.operand2 {
                        let value = vm_pop(vm);
                        let name = vm_pop(vm);
                        pairs.push((name, value));
                    }
                    pairs.reverse();

                    for (name_value, field_value) in pairs {
                        if name_value.kind != ValueType::VALUE_STRING || name_value.data.string_val.is_null() {
                            op_error(vm, loc, String::from("struct field name must be String"));
                            ok = false;
                        } else if !ocl_struct_set(obj, name_value.data.string_val as *const c_char, field_value) {
                            op_error(
                                vm,
                                loc,
                                format!(
                                    "failed to set struct field '{}'",
                                    c_string(name_value.data.string_val as *const c_char)
                                ),
                            );
                            ok = false;
                        }
                        value_free(field_value);
                        value_free(name_value);
                    }
                }

                if ok {
                    vm_push(vm, value_struct_owned(obj));
                } else {
                    push_null(vm);
                }
                ocl_struct_release(obj);
            }

            Opcode::OP_STRUCT_GET => {
                let object_value = vm_pop(vm);
                let mut result = value_null();
                let mut found = false;

                if ins.operand1 >= (*bytecode_ptr(vm)).constant_count as u32 {
                    logic_error(
                        vm,
                        loc,
                        format!("invalid struct field constant {}", ins.operand1),
                    );
                    value_free(object_value);
                    push_null(vm);
                } else {
                    let field_value = constant_at(vm, ins.operand1 as usize);
                    if field_value.kind != ValueType::VALUE_STRING || field_value.data.string_val.is_null() {
                        logic_error(
                            vm,
                            loc,
                            format!("struct field constant {} is not a String", ins.operand1),
                        );
                        value_free(object_value);
                        push_null(vm);
                    } else if object_value.kind != ValueType::VALUE_STRUCT || object_value.data.struct_val.is_null() {
                        op_error(
                            vm,
                            loc,
                            format!("field access requires Struct, got {}", type_name(object_value.kind)),
                        );
                        value_free(object_value);
                        push_null(vm);
                    } else {
                        result = ocl_struct_get(
                            object_value.data.struct_val,
                            field_value.data.string_val as *const c_char,
                            &mut found,
                        );
                        if !found {
                            op_error(
                                vm,
                                loc,
                                format!(
                                    "struct has no field '{}'",
                                    c_string(field_value.data.string_val as *const c_char)
                                ),
                            );
                        }
                        value_free(object_value);
                        vm_push(vm, result);
                    }
                }
            }

            Opcode::OP_STRUCT_SET => {
                let object_value = vm_pop(vm);
                let field_content = vm_pop(vm);

                if ins.operand1 >= (*bytecode_ptr(vm)).constant_count as u32 {
                    logic_error(
                        vm,
                        loc,
                        format!("invalid struct field constant {}", ins.operand1),
                    );
                    value_free(object_value);
                    value_free(field_content);
                } else {
                    let field_name = constant_at(vm, ins.operand1 as usize);
                    if field_name.kind != ValueType::VALUE_STRING || field_name.data.string_val.is_null() {
                        logic_error(
                            vm,
                            loc,
                            format!("struct field constant {} is not a String", ins.operand1),
                        );
                        value_free(object_value);
                        value_free(field_content);
                    } else if object_value.kind != ValueType::VALUE_STRUCT || object_value.data.struct_val.is_null() {
                        op_error(
                            vm,
                            loc,
                            format!("field assignment requires Struct, got {}", type_name(object_value.kind)),
                        );
                        value_free(object_value);
                        value_free(field_content);
                    } else {
                        if !ocl_struct_set(
                            object_value.data.struct_val,
                            field_name.data.string_val as *const c_char,
                            field_content,
                        ) {
                            op_error(
                                vm,
                                loc,
                                format!(
                                    "failed to set struct field '{}'",
                                    c_string(field_name.data.string_val as *const c_char)
                                ),
                            );
                        }
                        value_free(object_value);
                        value_free(field_content);
                    }
                }
            }
        }

        (*vm).pc += 1;
    }

    (*vm).exit_code
}
