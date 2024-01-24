#include "Python.h"
#include "pycore_interp.h"
#include "pycore_opcode_metadata.h"
#include "pycore_opcode_utils.h"
#include "pycore_pystate.h"       // _PyInterpreterState_GET()
#include "pycore_uop_metadata.h"
#include "pycore_long.h"
#include "cpython/optimizer.h"
#include "pycore_optimizer.h"
#include "pycore_object.h"
#include "pycore_dict.h"
#include "pycore_function.h"
#include "pycore_uop_metadata.h"
#include "pycore_uop_ids.h"
#include "pycore_range.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define MAX_ABSTRACT_INTERP_SIZE 2048

#define OVERALLOCATE_FACTOR 3

#ifdef Py_DEBUG
    static const char *const DEBUG_ENV = "PYTHON_OPT_DEBUG";
    #define DPRINTF(level, ...) \
    if (lltrace >= (level)) { printf(__VA_ARGS__); }
#else
    #define DPRINTF(level, ...)
#endif

// This represents a value that "terminates" the symbolic.
static inline bool
op_is_terminal(uint32_t opcode)
{
    return (opcode == _LOAD_FAST ||
            opcode == _LOAD_FAST_CHECK ||
            opcode == _LOAD_FAST_AND_CLEAR ||
            opcode == INIT_FAST ||
            opcode == CACHE ||
            opcode == PUSH_NULL);
}

// This represents a value that is already on the stack.
static inline bool
op_is_stackvalue(uint32_t opcode)
{
    return (opcode == CACHE);
}

// See the interpreter DSL in ./Tools/cases_generator/interpreter_definition.md for what these correspond to.
typedef enum {
    // Types with refinement info
    GUARD_KEYS_VERSION_TYPE = 0,
    GUARD_TYPE_VERSION_TYPE = 1,
    // You might think this actually needs to encode oparg
    // info as well, see _CHECK_FUNCTION_EXACT_ARGS.
    // However, since oparg is tied to code object is tied to function version,
    // it should be safe if function version matches.
    PYFUNCTION_TYPE_VERSION_TYPE = 2,

    // Types without refinement info
    PYLONG_TYPE = 3,
    PYFLOAT_TYPE = 4,
    PYUNICODE_TYPE = 5,
    NULL_TYPE = 6,
    PYMETHOD_TYPE = 7,
    GUARD_DORV_VALUES_TYPE = 8,
    GUARD_DORV_VALUES_INST_ATTR_FROM_DICT_TYPE = 9,
    // Can't statically determine if self or null.
    SELF_OR_NULL = 10,

    // Represents something from LOAD_CONST which is truly constant.
    TRUE_CONST = 30,
    INVALID_TYPE = 31,
} _Py_UOpsSymExprTypeEnum;

static const uint32_t IMMUTABLES =
    (
        1 << NULL_TYPE |
        1 << PYLONG_TYPE |
        1 << PYFLOAT_TYPE |
        1 << PYUNICODE_TYPE |
        1 << SELF_OR_NULL |
        1 << TRUE_CONST
    );

#define MAX_TYPE_WITH_REFINEMENT 2
typedef struct {
    // bitmask of types
    uint32_t types;
    // refinement data for the types
    uint64_t refinement[MAX_TYPE_WITH_REFINEMENT + 1];
    // constant propagated value (might be NULL)
    PyObject *const_val;
} _Py_UOpsSymType;


typedef struct _Py_UOpsSymbolicExpression {
    Py_ssize_t operand_count;

    // Value numbering but only for types and constant values.
    // https://en.wikipedia.org/wiki/Value_numbering
    _Py_UOpsSymType *ty_number;

    // The following fields are for codegen.
    _PyUOpInstruction inst;

    struct _Py_UOpsSymbolicExpression *operands[1];
} _Py_UOpsSymbolicExpression;

typedef enum _Py_UOps_IRStore_IdKind {
    TARGET_NONE = -2,
    TARGET_UNUSED = -1,
    TARGET_LOCAL = 0,
} _Py_UOps_IRStore_IdKind;

/*
 * The IR has the following types:
 * IR_PLAIN_INST - a plain CPython bytecode instruction
 * IR_SYMBOLIC - assign a target the value of a symbolic expression
 * IR_FRAME_PUSH_INFO - _PUSH_FRAME
 * IR_FRAME_POP_INFO - _POP_FRAME
 * IR_NOP - nop
 */
typedef enum _Py_UOps_IRStore_EntryKind {
    IR_PLAIN_INST = 0,
    IR_SYMBOLIC = 1,
    IR_FRAME_PUSH_INFO = 2,
    IR_FRAME_POP_INFO = 3,
    IR_NOP = 4,
} _Py_UOps_IRStore_EntryKind;

typedef struct _Py_UOpsOptIREntry {
    _Py_UOps_IRStore_EntryKind typ;
    union {
        // IR_PLAIN_INST
        _PyUOpInstruction inst;
        // IR_SYMBOLIC
        struct {
            _Py_UOps_IRStore_IdKind assignment_target;
            _Py_UOpsSymbolicExpression *expr;
        };
        // IR_FRAME_PUSH_INFO, always precedes a _PUSH_FRAME IR_PLAIN_INST
        struct {
            // Only used in codegen for bookkeeping.
            struct _Py_UOpsOptIREntry *prev_frame_ir;
            // Localsplus of this frame.
            _Py_UOpsSymbolicExpression **my_virtual_localsplus;
        };
        // IR_FRAME_POP_INFO, always prior to a _POP_FRAME IR_PLAIN_INST
        // no fields, just a sentinel
    };
} _Py_UOpsOptIREntry;

typedef struct _Py_UOps_Opt_IR {
    PyObject_VAR_HEAD
    int curr_write;
    _Py_UOpsOptIREntry entries[1];
} _Py_UOps_Opt_IR;

PyTypeObject _Py_UOps_Opt_IR_Type = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0)
    .tp_name = "uops SSA IR",
    .tp_basicsize = sizeof(_Py_UOps_Opt_IR) - sizeof(_Py_UOpsOptIREntry),
    .tp_itemsize = sizeof(_Py_UOpsOptIREntry),
    .tp_dealloc = (destructor)PyObject_Del,
    .tp_free = PyObject_Free,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_DISALLOW_INSTANTIATION
};

static int
ir_store(_Py_UOps_Opt_IR *ir, _Py_UOpsSymbolicExpression *expr, _Py_UOps_IRStore_IdKind store_fast_idx)
{
    // Don't store stuff we know will never get compiled.
    if(op_is_stackvalue(expr->inst.opcode) && store_fast_idx == TARGET_NONE) {
        return 0;
    }
#ifdef Py_DEBUG
    char *uop_debug = Py_GETENV(DEBUG_ENV);
    int lltrace = 0;
    if (uop_debug != NULL && *uop_debug >= '0') {
        lltrace = *uop_debug - '0';  // TODO: Parse an int and all that
    }
    DPRINTF(3, "ir_store: #%d, expr: %s oparg: %d, operand: %p\n", store_fast_idx,
            (expr->inst.opcode >= 300 ? _PyOpcode_uop_name : _PyOpcode_OpName)[expr->inst.opcode],
                        expr->inst.oparg,
                        (void *)expr->inst.operand);
#endif
    _Py_UOpsOptIREntry *entry = &ir->entries[ir->curr_write];
    entry->typ = IR_SYMBOLIC;
    entry->assignment_target = store_fast_idx;
    entry->expr = expr;
    ir->curr_write++;
    if (ir->curr_write >= Py_SIZE(ir)) {
        DPRINTF(1, "ir_store: ran out of space \n");
        return -1;
    }
    return 0;
}

static int
ir_plain_inst(_Py_UOps_Opt_IR *ir, _PyUOpInstruction inst)
{
#ifdef Py_DEBUG
    char *uop_debug = Py_GETENV(DEBUG_ENV);
    int lltrace = 0;
    if (uop_debug != NULL && *uop_debug >= '0') {
        lltrace = *uop_debug - '0';  // TODO: Parse an int and all that
    }
    DPRINTF(3, "ir_inst: opcode: %s oparg: %d, operand: %p\n",
            (inst.opcode >= 300 ? _PyOpcode_uop_name : _PyOpcode_OpName)[inst.opcode],
                        inst.oparg,
                        (void *)inst.operand);
#endif
    _Py_UOpsOptIREntry *entry = &ir->entries[ir->curr_write];
    entry->typ = IR_PLAIN_INST;
    entry->inst = inst;
    ir->curr_write++;
    if (ir->curr_write >= Py_SIZE(ir)) {
        DPRINTF(1, "ir_plain_inst: ran out of space \n");
        return -1;
    }
    return 0;
}

static _Py_UOpsOptIREntry *
ir_frame_push_info(_Py_UOps_Opt_IR *ir)
{
#ifdef Py_DEBUG
    char *uop_debug = Py_GETENV(DEBUG_ENV);
    int lltrace = 0;
    if (uop_debug != NULL && *uop_debug >= '0') {
        lltrace = *uop_debug - '0';  // TODO: Parse an int and all that
    }
    DPRINTF(3, "ir_frame_push_info\n");
#endif
    _Py_UOpsOptIREntry *entry = &ir->entries[ir->curr_write];
    entry->typ = IR_FRAME_PUSH_INFO;
    entry->my_virtual_localsplus = NULL;
    entry->prev_frame_ir = NULL;
    ir->curr_write++;
    if (ir->curr_write >= Py_SIZE(ir)) {
        DPRINTF(1, "ir_frame_push_info: ran out of space \n");
        return NULL;
    }
    return entry;
}


static int
ir_frame_pop_info(_Py_UOps_Opt_IR *ir)
{
#ifdef Py_DEBUG
    char *uop_debug = Py_GETENV(DEBUG_ENV);
    int lltrace = 0;
    if (uop_debug != NULL && *uop_debug >= '0') {
        lltrace = *uop_debug - '0';  // TODO: Parse an int and all that
    }
    DPRINTF(3, "ir_frame_pop_info\n");
#endif
    _Py_UOpsOptIREntry *entry = &ir->entries[ir->curr_write];
    entry->typ = IR_FRAME_POP_INFO;
    ir->curr_write++;
    if (ir->curr_write >= Py_SIZE(ir)) {
        DPRINTF(1, "ir_frame_pop_info: ran out of space \n");
        return -1;
    }
    return 0;
}

typedef struct _Py_UOpsAbstractFrame {
    PyObject_HEAD
    // Strong reference.
    struct _Py_UOpsAbstractFrame *prev;
    // Borrowed reference.
    struct _Py_UOpsAbstractFrame *next;
    // Symbolic version of co_consts
    int sym_consts_len;
    _Py_UOpsSymbolicExpression **sym_consts;
    // Max stacklen
    int stack_len;
    int locals_len;

    _Py_UOpsOptIREntry *frame_ir_entry;

    _Py_UOpsSymbolicExpression **stack_pointer;
    _Py_UOpsSymbolicExpression **stack;
    _Py_UOpsSymbolicExpression **locals;
} _Py_UOpsAbstractFrame;

static void
abstractframe_dealloc(_Py_UOpsAbstractFrame *self)
{
    PyMem_Free(self->sym_consts);
    Py_XDECREF(self->prev);
    Py_TYPE(self)->tp_free((PyObject *)self);
}

PyTypeObject _Py_UOpsAbstractFrame_Type = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0)
    .tp_name = "uops abstract frame",
    .tp_basicsize = sizeof(_Py_UOpsAbstractFrame) ,
    .tp_itemsize = 0,
    .tp_dealloc = (destructor)abstractframe_dealloc,
    .tp_free = PyObject_Free,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_DISALLOW_INSTANTIATION
};

typedef struct sym_arena {
    char *curr_available;
    char *end;
    char *arena;
} sym_arena;

typedef struct ty_arena {
    int ty_curr_number;
    int ty_max_number;
    _Py_UOpsSymType *arena;
} ty_arena;

typedef struct frequent_syms {
    _Py_UOpsSymbolicExpression *nulL_sym;
    _Py_UOpsSymbolicExpression *push_nulL_sym;
} frequent_syms;

// Tier 2 types meta interpreter
typedef struct _Py_UOpsAbstractInterpContext {
    PyObject_HEAD
    // Stores the symbolic for the upcoming new frame that is about to be created.
    _Py_UOpsSymbolicExpression *new_frame_sym;
    // The current "executing" frame.
    _Py_UOpsAbstractFrame *frame;

    _Py_UOps_Opt_IR *ir;

    // Arena for the symbolic expression themselves.
    sym_arena s_arena;
    // Arena for the symbolic expressions' types.
    // This is separate from the s_arena so that we can free
    // all the constants easily.
    ty_arena t_arena;

    // The terminating instruction for the trace. Could be _JUMP_TO_TOP or
    // _EXIT_TRACE.
    _PyUOpInstruction *terminating;

    frequent_syms frequent_syms;

    _Py_UOpsSymbolicExpression **water_level;
    _Py_UOpsSymbolicExpression **limit;
    _Py_UOpsSymbolicExpression *localsplus[1];
} _Py_UOpsAbstractInterpContext;

static void
abstractinterp_dealloc(PyObject *o)
{
    _Py_UOpsAbstractInterpContext *self = (_Py_UOpsAbstractInterpContext *)o;
    Py_XDECREF(self->frame);
    Py_XDECREF(self->ir);
    if (self->s_arena.arena != NULL) {
        int tys = self->t_arena.ty_curr_number;
        for (int i = 0; i < tys; i++) {
            Py_CLEAR(self->t_arena.arena[i].const_val);
        }
    }
    PyMem_Free(self->t_arena.arena);
    PyMem_Free(self->s_arena.arena);
    Py_TYPE(self)->tp_free((PyObject *)self);
}

PyTypeObject _Py_UOpsAbstractInterpContext_Type = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0)
    .tp_name = "uops abstract interpreter's context",
    .tp_basicsize = sizeof(_Py_UOpsAbstractInterpContext) - sizeof(_Py_UOpsSymbolicExpression *),
    .tp_itemsize = sizeof(_Py_UOpsSymbolicExpression *),
    .tp_dealloc = (destructor)abstractinterp_dealloc,
    .tp_free = PyObject_Free,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_DISALLOW_INSTANTIATION
};

static inline _Py_UOps_Opt_IR *
ssa_ir_new(int entries)
{
    _Py_UOps_Opt_IR *ir = PyObject_NewVar(_Py_UOps_Opt_IR,
                                          &_Py_UOps_Opt_IR_Type,
                                          entries);
    ir->curr_write = 0;
    return ir;
}

static inline _Py_UOpsAbstractFrame *
frame_new(_Py_UOpsAbstractInterpContext *ctx,
                          PyObject *co_consts, int stack_len, int locals_len,
                          int curr_stacklen, _Py_UOpsOptIREntry *frame_ir_entry);
static inline int
frame_push(_Py_UOpsAbstractInterpContext *ctx,
           _Py_UOpsAbstractFrame *frame,
           _Py_UOpsSymbolicExpression **localsplus_start,
           int locals_len,
           int curr_stacklen,
           int total_len);

static inline int
frame_initalize(_Py_UOpsAbstractInterpContext *ctx, _Py_UOpsAbstractFrame *frame,
                int locals_len, int curr_stacklen);

static _Py_UOpsAbstractInterpContext *
abstractinterp_context_new(PyCodeObject *co,
                                  int curr_stacklen,
                                  int ir_entries)
{
    int locals_len = co->co_nlocalsplus;
    int stack_len = co->co_stacksize;
    _Py_UOpsAbstractFrame *frame = NULL;
    _Py_UOpsAbstractInterpContext *self = NULL;
    _Py_UOps_Opt_IR *ir = NULL;
    char *arena = NULL;
    _Py_UOpsSymType *t_arena = NULL;
    Py_ssize_t arena_size = (sizeof(_Py_UOpsSymbolicExpression)) * ir_entries * OVERALLOCATE_FACTOR;
    Py_ssize_t ty_arena_size = (sizeof(_Py_UOpsSymType)) * ir_entries * OVERALLOCATE_FACTOR;
    arena = (char *)PyMem_Malloc(arena_size);
    if (arena == NULL) {
        goto error;
    }

    t_arena = (_Py_UOpsSymType *)PyMem_Malloc(ty_arena_size);
    if (t_arena == NULL) {
        goto error;
    }

    ir = ssa_ir_new(ir_entries * OVERALLOCATE_FACTOR);
    if (ir == NULL) {
        goto error;
    }
    _Py_UOpsOptIREntry *root_frame = ir_frame_push_info(ir);
    if (root_frame == NULL) {
        goto error;
    }

    self = PyObject_NewVar(_Py_UOpsAbstractInterpContext,
                               &_Py_UOpsAbstractInterpContext_Type,
                               MAX_ABSTRACT_INTERP_SIZE);
    if (self == NULL) {
        goto error;
    }

    self->limit = self->localsplus + MAX_ABSTRACT_INTERP_SIZE;
    self->water_level = self->localsplus;
    for (int i = 0 ; i < MAX_ABSTRACT_INTERP_SIZE; i++) {
        self->localsplus[i] = NULL;
    }


    // Setup the arena for sym expressions.
    self->s_arena.arena = arena;
    self->s_arena.curr_available = arena;
    assert(arena_size > 0);
    self->s_arena.end = arena + arena_size;
    self->t_arena.ty_curr_number = 0;
    self->t_arena.arena = t_arena;
    self->t_arena.ty_max_number = ir_entries * OVERALLOCATE_FACTOR;

    // Frame setup
    self->new_frame_sym = NULL;
    frame = frame_new(self, co->co_consts, stack_len, locals_len, curr_stacklen, root_frame);
    if (frame == NULL) {
        goto error;
    }
    if (frame_push(self, frame, self->water_level, locals_len, curr_stacklen,
               stack_len + locals_len) < 0) {
        goto error;
    }
    if (frame_initalize(self, frame, locals_len, curr_stacklen) < 0) {
        goto error;
    }
    self->frame = frame;
    assert(frame != NULL);
    root_frame->my_virtual_localsplus = self->localsplus;

    // IR and sym setup
    self->ir = ir;
    self->frequent_syms.nulL_sym = NULL;
    self->frequent_syms.push_nulL_sym = NULL;

    return self;

error:
    PyMem_Free(arena);
    PyMem_Free(t_arena);
    if (self != NULL) {
        // Important so we don't double free them.
        self->t_arena.arena = NULL;
        self->s_arena.arena = NULL;
    }
    self->frame = NULL;
    self->ir = NULL;
    Py_XDECREF(self);
    Py_XDECREF(ir);
    Py_XDECREF(frame);
    return NULL;
}

static inline _Py_UOpsSymbolicExpression*
sym_init_const(_Py_UOpsAbstractInterpContext *ctx, PyObject *const_val, int const_idx);

static inline _Py_UOpsSymbolicExpression **
create_sym_consts(_Py_UOpsAbstractInterpContext *ctx, PyObject *co_consts)
{
    Py_ssize_t co_const_len = PyTuple_GET_SIZE(co_consts);
    _Py_UOpsSymbolicExpression **sym_consts = PyMem_New(_Py_UOpsSymbolicExpression *, co_const_len);
    if (sym_consts == NULL) {
        return NULL;
    }
    for (Py_ssize_t i = 0; i < co_const_len; i++) {
        _Py_UOpsSymbolicExpression *res = sym_init_const(ctx, Py_NewRef(PyTuple_GET_ITEM(co_consts, i)), (int)i);
        if (res == NULL) {
            goto error;
        }
        sym_consts[i] = res;
    }

    return sym_consts;
error:
    PyMem_Free(sym_consts);
    return NULL;
}

static inline _Py_UOpsSymbolicExpression*
sym_init_var(_Py_UOpsAbstractInterpContext *ctx, int locals_idx);

static inline _Py_UOpsSymbolicExpression*
sym_init_unknown(_Py_UOpsAbstractInterpContext *ctx);

static void
sym_copy_immutable_type_info(_Py_UOpsSymbolicExpression *from_sym, _Py_UOpsSymbolicExpression *to_sym);

/*
 * The reason why we have a separate frame_push and frame_initialize is to mimic
 * what CPython's frame push does. This also prepares for inlining.
 * */
static inline int
frame_push(_Py_UOpsAbstractInterpContext *ctx,
           _Py_UOpsAbstractFrame *frame,
           _Py_UOpsSymbolicExpression **localsplus_start,
           int locals_len,
           int curr_stacklen,
           int total_len)
{
    frame->locals = localsplus_start;
    frame->stack = frame->locals + locals_len;
    frame->stack_pointer = frame->stack + curr_stacklen;
    ctx->water_level = localsplus_start + total_len;
    if (ctx->water_level > ctx->limit) {
        return -1;
    }
    return 0;
}

static inline int
frame_initalize(_Py_UOpsAbstractInterpContext *ctx, _Py_UOpsAbstractFrame *frame,
                int locals_len, int curr_stacklen)
{
    // Initialize with the initial state of all local variables
    for (int i = 0; i < locals_len; i++) {
        _Py_UOpsSymbolicExpression *local = sym_init_var(ctx, i);
        if (local == NULL) {
            goto error;
        }
        frame->locals[i] = local;
    }


    // Initialize the stack as well
    for (int i = 0; i < curr_stacklen; i++) {
        _Py_UOpsSymbolicExpression *stackvar = sym_init_unknown(ctx);
        if (stackvar == NULL) {
            goto error;
        }
        frame->stack[i] = stackvar;
    }

    return 0;

error:
    return -1;
}

static inline _Py_UOpsAbstractFrame *
frame_new(_Py_UOpsAbstractInterpContext *ctx,
                          PyObject *co_consts, int stack_len, int locals_len,
                          int curr_stacklen, _Py_UOpsOptIREntry *frame_ir_entry)
{
    _Py_UOpsSymbolicExpression **sym_consts = create_sym_consts(ctx, co_consts);
    if (sym_consts == NULL) {
        return NULL;
    }
    _Py_UOpsAbstractFrame *frame = PyObject_New(_Py_UOpsAbstractFrame,
                                                      &_Py_UOpsAbstractFrame_Type);
    if (frame == NULL) {
        PyMem_Free(sym_consts);
        return NULL;
    }


    frame->sym_consts = sym_consts;
    frame->sym_consts_len = (int)Py_SIZE(co_consts);
    frame->stack_len = stack_len;
    frame->locals_len = locals_len;
    frame->prev = NULL;
    frame->next = NULL;

    frame->frame_ir_entry = frame_ir_entry;
    return frame;
}

static inline bool
sym_is_type(_Py_UOpsSymbolicExpression *sym, _Py_UOpsSymExprTypeEnum typ);
static inline uint64_t
sym_type_get_refinement(_Py_UOpsSymbolicExpression *sym, _Py_UOpsSymExprTypeEnum typ);

static inline PyFunctionObject *
extract_func_from_sym(_Py_UOpsSymbolicExpression *frame_sym)
{
#ifdef Py_DEBUG
char *uop_debug = Py_GETENV(DEBUG_ENV);
    int lltrace = 0;
    if (uop_debug != NULL && *uop_debug >= '0') {
        lltrace = *uop_debug - '0';  // TODO: Parse an int and all that
    }
    DPRINTF(3, "write_stack_to_ir\n");
#endif
    switch(frame_sym->inst.opcode) {
        case _INIT_CALL_PY_EXACT_ARGS: {
            _Py_UOpsSymbolicExpression *callable_sym = frame_sym->operands[0];
            if (!sym_is_type(callable_sym, PYFUNCTION_TYPE_VERSION_TYPE)) {
                DPRINTF(1, "error: _PUSH_FRAME not function type\n");
                return NULL;
            }
            uint64_t func_version = sym_type_get_refinement(callable_sym, PYFUNCTION_TYPE_VERSION_TYPE);
            PyFunctionObject *func = _PyFunction_LookupByVersion((uint32_t)func_version);
            if (func == NULL) {
                DPRINTF(1, "error: _PUSH_FRAME cannot find func version\n");
                return NULL;
            }
            return func;
        }
        default:
            Py_UNREACHABLE();
    }
}

static inline _Py_UOpsSymbolicExpression*
extract_self_or_null_from_sym(_Py_UOpsSymbolicExpression *frame_sym)
{
    switch(frame_sym->inst.opcode) {
        case _INIT_CALL_PY_EXACT_ARGS:
            return frame_sym->operands[1];
        default:
            Py_UNREACHABLE();
    }
}

static inline _Py_UOpsSymbolicExpression**
extract_args_from_sym(_Py_UOpsSymbolicExpression *frame_sym)
{
    switch(frame_sym->inst.opcode) {
        case _INIT_CALL_PY_EXACT_ARGS:
            return &frame_sym->operands[2];
        default:
            Py_UNREACHABLE();
    }
}

// 0 on success, anything else is error.
static int
ctx_frame_push(
    _Py_UOpsAbstractInterpContext *ctx,
    _Py_UOpsOptIREntry *frame_ir_entry,
    PyCodeObject *co,
    _Py_UOpsSymbolicExpression **localsplus_start
)
{
    assert(frame_ir_entry != NULL);
    _Py_UOpsAbstractFrame *frame = frame_new(ctx,
        co->co_consts, co->co_stacksize,
        co->co_nlocalsplus,
        0, frame_ir_entry);
    if (frame == NULL) {
        return -1;
    }
    if (frame_push(ctx, frame, localsplus_start, co->co_nlocalsplus, 0,
               co->co_nlocalsplus + co->co_stacksize) < 0) {
        return -1;
    }
    if (frame_initalize(ctx, frame, co->co_nlocalsplus, 0) < 0) {
        return -1;
    }

    frame->prev = ctx->frame;
    ctx->frame->next = frame;
    ctx->frame = frame;

    frame_ir_entry->my_virtual_localsplus = localsplus_start;

    return 0;
}

static int
ctx_frame_pop(
    _Py_UOpsAbstractInterpContext *ctx
)
{
    _Py_UOpsAbstractFrame *frame = ctx->frame;
    ctx->frame = frame->prev;
    assert(ctx->frame != NULL);
    frame->prev = NULL;

    ctx->water_level = frame->locals;
    Py_DECREF(frame);
    ctx->frame->next = NULL;
    return 0;
}

static void
sym_set_type_from_const(_Py_UOpsSymbolicExpression *sym, PyObject *obj);

// Steals a reference to const_val
// Creates a symbolic expression consisting of subexpressoins
// from arr_start and va_list.
// The order is
// <va_list elements left to right>, <arr_start elements left to right>
static _Py_UOpsSymbolicExpression*
_Py_UOpsSymbolicExpression_New(_Py_UOpsAbstractInterpContext *ctx,
                               _PyUOpInstruction inst,
                               PyObject *const_val,
                               int num_arr,
                               _Py_UOpsSymbolicExpression **arr_start,
                               int num_subexprs, ...)
{
#ifdef Py_DEBUG
    char *uop_debug = Py_GETENV(DEBUG_ENV);
    int lltrace = 0;
    if (uop_debug != NULL && *uop_debug >= '0') {
        lltrace = *uop_debug - '0';  // TODO: Parse an int and all that
    }
#endif
    int total_subexprs = num_arr + num_subexprs;


    _Py_UOpsSymbolicExpression *self = (_Py_UOpsSymbolicExpression *)ctx->s_arena.curr_available;
    ctx->s_arena.curr_available += sizeof(_Py_UOpsSymbolicExpression) + sizeof(_Py_UOpsSymbolicExpression *) * total_subexprs;
    if (ctx->s_arena.curr_available >= ctx->s_arena.end) {
        DPRINTF(1, "out of space for symbolic expression\n");
        return NULL;
    }

    _Py_UOpsSymType *ty = &ctx->t_arena.arena[ctx->t_arena.ty_curr_number];
    if (ctx->t_arena.ty_curr_number >= ctx->t_arena.ty_max_number) {
        DPRINTF(1, "out of space for symbolic expression type\n");
        return NULL;
    }
    ctx->t_arena.ty_curr_number++;
    ty->const_val = NULL;
    ty->types = 0;

    self->ty_number = ty;
    self->ty_number->types = 0;
    self->inst = inst;
    if (const_val != NULL) {
        sym_set_type_from_const(self, const_val);
    }



    // Setup
    int i = 0;
    _Py_UOpsSymbolicExpression **operands = self->operands;
    va_list curr;

    va_start(curr, num_subexprs);

    for (; i < num_subexprs; i++) {
        operands[i] = va_arg(curr, _Py_UOpsSymbolicExpression *);
        assert(operands[i]);
    }

    va_end(curr);

    for (int x = 0; x < num_arr; x++) {
        operands[i+x] = arr_start[x];
        assert(operands[i+x]);
    }

    self->operand_count = total_subexprs;

    return self;
}


static void
sym_set_type(_Py_UOpsSymbolicExpression *sym, _Py_UOpsSymExprTypeEnum typ, uint64_t refinement)
{
    sym->ty_number->types |= 1 << typ;
    if (typ <= MAX_TYPE_WITH_REFINEMENT) {
        sym->ty_number->refinement[typ] = refinement;
    }
}

static void
sym_copy_type_number(_Py_UOpsSymbolicExpression *from_sym, _Py_UOpsSymbolicExpression *to_sym)
{
    to_sym->ty_number = from_sym->ty_number;
}

// Note: for this, to_sym MUST point to brand new sym.
static void
sym_copy_immutable_type_info(_Py_UOpsSymbolicExpression *from_sym, _Py_UOpsSymbolicExpression *to_sym)
{
    to_sym->ty_number->types = (from_sym->ty_number->types & IMMUTABLES);
    if (to_sym->ty_number->types) {
        to_sym->ty_number->const_val = Py_XNewRef(from_sym->ty_number->const_val);
    }
}

// Steals a reference to obj
static void
sym_set_type_from_const(_Py_UOpsSymbolicExpression *sym, PyObject *obj)
{
    PyTypeObject *tp = Py_TYPE(obj);
    sym->ty_number->const_val = obj;

    if (tp == &PyLong_Type) {
        sym_set_type(sym, PYLONG_TYPE, 0);
    }
    else if (tp == &PyFloat_Type) {
        sym_set_type(sym, PYFLOAT_TYPE, 0);
    }
    else if (tp == &PyUnicode_Type) {
        sym_set_type(sym, PYUNICODE_TYPE, 0);
    }

    if (tp->tp_flags & Py_TPFLAGS_MANAGED_DICT) {
        PyDictOrValues *dorv = _PyObject_DictOrValuesPointer(obj);

        if (_PyDictOrValues_IsValues(*dorv) ||
            _PyObject_MakeInstanceAttributesFromDict(obj, dorv)) {
            sym_set_type(sym, GUARD_DORV_VALUES_INST_ATTR_FROM_DICT_TYPE, 0);

            PyTypeObject *owner_cls = tp;
            PyHeapTypeObject *owner_heap_type = (PyHeapTypeObject *)owner_cls;
            sym_set_type(
                sym,
                GUARD_KEYS_VERSION_TYPE,
                owner_heap_type->ht_cached_keys->dk_version
            );
        }

        if (!_PyDictOrValues_IsValues(*dorv)) {
            sym_set_type(sym, GUARD_DORV_VALUES_TYPE, 0);
        }
    }
}


static inline _Py_UOpsSymbolicExpression*
sym_init_var(_Py_UOpsAbstractInterpContext *ctx, int locals_idx)
{
    _PyUOpInstruction inst = {INIT_FAST, locals_idx, 0, 0};
    return _Py_UOpsSymbolicExpression_New(ctx,
                                          inst,
                                          NULL,
                                          0,
                                          NULL,
                                          0);
}

static inline _Py_UOpsSymbolicExpression*
sym_init_unknown(_Py_UOpsAbstractInterpContext *ctx)
{
    _PyUOpInstruction inst = {CACHE, 0, 0, 0};
    return _Py_UOpsSymbolicExpression_New(ctx,
                                          inst,
                                          NULL,
                                          0,
                                          NULL,
                                          0);
}

// Steals a reference to const_val
static inline _Py_UOpsSymbolicExpression*
sym_init_const(_Py_UOpsAbstractInterpContext *ctx, PyObject *const_val, int const_idx)
{
    _PyUOpInstruction inst = {LOAD_CONST, const_idx, 0, 0};
    assert(const_val != NULL);
    _Py_UOpsSymbolicExpression *temp = _Py_UOpsSymbolicExpression_New(
        ctx,
        inst,
        const_val,
        0,
        NULL,
        0
    );
    if (temp == NULL) {
        return NULL;
    }
    sym_set_type(temp, TRUE_CONST, 0);
    return temp;
}

static _Py_UOpsSymbolicExpression*
sym_init_push_null(_Py_UOpsAbstractInterpContext *ctx)
{
    if (ctx->frequent_syms.push_nulL_sym != NULL) {
        return ctx->frequent_syms.push_nulL_sym;
    }
    _Py_UOpsSymbolicExpression *null_sym = sym_init_unknown(ctx);
    if (null_sym == NULL) {
        return NULL;
    }
    null_sym->inst.opcode = PUSH_NULL;
    sym_set_type(null_sym, NULL_TYPE, 0);
    ctx->frequent_syms.push_nulL_sym = null_sym;
    return null_sym;
}

static inline bool
sym_is_type(_Py_UOpsSymbolicExpression *sym, _Py_UOpsSymExprTypeEnum typ)
{
    if ((sym->ty_number->types & (1 << typ)) == 0) {
        return false;
    }
    return true;
}

static inline bool
sym_matches_type(_Py_UOpsSymbolicExpression *sym, _Py_UOpsSymExprTypeEnum typ, uint64_t refinement)
{
    if (!sym_is_type(sym, typ)) {
        return false;
    }
    if (typ <= MAX_TYPE_WITH_REFINEMENT) {
        return sym->ty_number->refinement[typ] == refinement;
    }
    return true;
}

static uint64_t
sym_type_get_refinement(_Py_UOpsSymbolicExpression *sym, _Py_UOpsSymExprTypeEnum typ)
{
    assert(sym_is_type(sym, typ));
    assert(typ <= MAX_TYPE_WITH_REFINEMENT);
    return sym->ty_number->refinement[typ];
}


static inline bool
op_is_end(uint32_t opcode)
{
    return opcode == _EXIT_TRACE || opcode == _JUMP_TO_TOP;
}

static inline bool
op_is_guard(uint32_t opcode)
{
    return _PyUop_Flags[opcode] & HAS_GUARD_FLAG;
}

static inline bool
op_is_pure(uint32_t opcode)
{
    return _PyUop_Flags[opcode] & HAS_PURE_FLAG;
}

static inline bool
op_is_bookkeeping(uint32_t opcode) {
    return (opcode == _SET_IP ||
            opcode == _CHECK_VALIDITY ||
            opcode == _SAVE_RETURN_OFFSET);
}

static inline bool
op_is_specially_handled(uint32_t opcode)
{
    return _PyUop_Flags[opcode] & HAS_SPECIAL_OPT_FLAG;
}

static inline bool
is_const(_Py_UOpsSymbolicExpression *expr)
{
    return expr->ty_number->const_val != NULL;
}

static inline PyObject *
get_const(_Py_UOpsSymbolicExpression *expr)
{
    return expr->ty_number->const_val;
}


static int
write_bookkeeping_to_ir(_Py_UOpsAbstractInterpContext *ctx, _PyUOpInstruction *curr)
{
    if ((curr-1)->opcode == _CHECK_VALIDITY && ((curr-2)->opcode == _SET_IP)) {
        if (ir_plain_inst(ctx->ir, *(curr-2)) < 0) {
            return -1;
        }
        if (ir_plain_inst(ctx->ir, *(curr-1)) < 0) {
            return -1;
        }
    }
    return 0;
}

static int
write_stack_to_ir(_Py_UOpsAbstractInterpContext *ctx, _PyUOpInstruction *curr, bool copy_types) {
#ifdef Py_DEBUG
    char *uop_debug = Py_GETENV(DEBUG_ENV);
    int lltrace = 0;
    if (uop_debug != NULL && *uop_debug >= '0') {
        lltrace = *uop_debug - '0';  // TODO: Parse an int and all that
    }
    DPRINTF(3, "write_stack_to_ir\n");
#endif
    // Emit the state of the stack first.
    Py_ssize_t stack_entries = ctx->frame->stack_pointer - ctx->frame->stack;
    assert(stack_entries <= ctx->frame->stack_len);
    for (Py_ssize_t i = 0; i < stack_entries; i++) {
        if (ir_store(ctx->ir, ctx->frame->stack[i], TARGET_NONE) < 0) {
            goto error;
        }
        _Py_UOpsSymbolicExpression *new_stack = sym_init_unknown(ctx);
        if (new_stack == NULL) {
            goto error;
        }
        if (copy_types) {
            sym_copy_type_number(ctx->frame->stack[i], new_stack);
        } else {
            sym_copy_immutable_type_info(ctx->frame->stack[i], new_stack);
        }
        ctx->frame->stack[i] = new_stack;
    }

    return 0;

error:
    DPRINTF(1, "write_stack_to_ir error\n");
    return -1;
}

static int
clear_locals_type_info(_Py_UOpsAbstractInterpContext *ctx) {
    int locals_entries = ctx->frame->locals_len;
    for (int i = 0; i < locals_entries; i++) {
        _Py_UOpsSymbolicExpression *new_local = sym_init_var(ctx, i);
        if (new_local == NULL) {
            return -1;
        }
        sym_copy_immutable_type_info(ctx->frame->locals[i], new_local);
        ctx->frame->locals[i] = new_local;
    }
    return 0;
}

typedef enum {
    ABSTRACT_INTERP_ERROR,
    ABSTRACT_INTERP_NORMAL,
    ABSTRACT_INTERP_GUARD_REQUIRED,
} AbstractInterpExitCodes;


#define DECREF_INPUTS_AND_REUSE_FLOAT(left, right, dval, result) \
do { \
    { \
        result = PyFloat_FromDouble(dval); \
        if ((result) == NULL) goto error; \
    } \
} while (0)

#define DEOPT_IF(COND, INSTNAME) \
    if ((COND)) {                \
        goto guard_required;         \
    }

#ifndef Py_DEBUG
#define GETITEM(ctx, i) (ctx->frame->sym_consts[(i)])
#else
static inline _Py_UOpsSymbolicExpression *
GETITEM(_Py_UOpsAbstractInterpContext *ctx, Py_ssize_t i) {
    assert(i < ctx->frame->sym_consts_len);
    return ctx->frame->sym_consts[i];
}
#endif

static int
uop_abstract_interpret_single_inst(
    _PyUOpInstruction *inst,
    _PyUOpInstruction *end,
    _Py_UOpsAbstractInterpContext *ctx
)
{
#ifdef Py_DEBUG
    char *uop_debug = Py_GETENV(DEBUG_ENV);
    int lltrace = 0;
    if (uop_debug != NULL && *uop_debug >= '0') {
        lltrace = *uop_debug - '0';  // TODO: Parse an int and all that
    }
#endif

#define STACK_LEVEL()     ((int)(stack_pointer - ctx->frame->stack))
#define STACK_SIZE()      (ctx->frame->stack_len)
#define BASIC_STACKADJ(n) (stack_pointer += n)

#ifdef Py_DEBUG
    #define STACK_GROW(n)   do { \
                                assert(n >= 0); \
                                BASIC_STACKADJ(n); \
                                if (STACK_LEVEL() > STACK_SIZE()) { \
                                    DPRINTF(2, "err: %d, %d\n", STACK_SIZE(), STACK_LEVEL())\
                                } \
                                assert(STACK_LEVEL() <= STACK_SIZE()); \
                            } while (0)
    #define STACK_SHRINK(n) do { \
                                assert(n >= 0); \
                                assert(STACK_LEVEL() >= n); \
                                BASIC_STACKADJ(-(n)); \
                            } while (0)
#else
    #define STACK_GROW(n)          BASIC_STACKADJ(n)
    #define STACK_SHRINK(n)        BASIC_STACKADJ(-(n))
#endif
#define PEEK(idx)              (((stack_pointer)[-(idx)]))
#define GETLOCAL(idx)          ((ctx->frame->locals[idx]))

#define CURRENT_OPARG() (oparg)

#define CURRENT_OPERAND() (operand)

#define STAT_INC(opname, name) ((void)0)
#define TIER_TWO_ONLY ((void)0)

    int oparg = inst->oparg;
    uint32_t opcode = inst->opcode;
    uint64_t operand = inst->operand;

    _Py_UOpsSymbolicExpression **stack_pointer = ctx->frame->stack_pointer;


    DPRINTF(3, "Abstract interpreting %s:%d ",
            (opcode >= 300 ? _PyOpcode_uop_name : _PyOpcode_OpName)[opcode],
            oparg);
    switch (opcode) {
#include "abstract_interp_cases.c.h"
        // Note: LOAD_FAST_CHECK is not pure!!!
        case LOAD_FAST_CHECK: {
            STACK_GROW(1);
            if (write_bookkeeping_to_ir(ctx, inst) < 0) {
                goto error;
            }
            if (ir_plain_inst(ctx->ir, *inst) < 0) {
                goto error;
            }
            _Py_UOpsSymbolicExpression * local = GETLOCAL(oparg);
            _Py_UOpsSymbolicExpression * new_local = sym_init_unknown(ctx);
            if (new_local == NULL) {
                goto error;
            }
            sym_copy_type_number(local, new_local);
            PEEK(1) = new_local;
            break;
        }
        case LOAD_FAST: {
            STACK_GROW(1);
            _Py_UOpsSymbolicExpression * local = GETLOCAL(oparg);
            // Might be NULL - replace with LOAD_FAST_CHECK
            if (sym_is_type(local, NULL_TYPE)) {
                if (write_bookkeeping_to_ir(ctx, inst) < 0) {
                    goto error;
                }
                _PyUOpInstruction temp = *inst;
                temp.opcode = LOAD_FAST_CHECK;
                if (ir_plain_inst(ctx->ir, temp) < 0) {
                    goto error;
                }
                _Py_UOpsSymbolicExpression * new_local = sym_init_unknown(ctx);
                if (new_local == NULL) {
                    goto error;
                }
                sym_copy_type_number(local, new_local);
                PEEK(1) = new_local;
                break;
            }
            // Guaranteed by the CPython bytecode compiler to not be uninitialized.
            PEEK(1) = GETLOCAL(oparg);
            PEEK(1)->inst.target = inst->target;
            assert(PEEK(1));

            break;
        }
        case LOAD_FAST_AND_CLEAR: {
            STACK_GROW(1);
            PEEK(1) = GETLOCAL(oparg);
            assert(PEEK(1)->inst.opcode == INIT_FAST);
            PEEK(1)->inst.opcode = LOAD_FAST_AND_CLEAR;
            ctx->frame->stack_pointer = stack_pointer;
            _Py_UOpsSymbolicExpression *new_local =  sym_init_var(ctx, oparg);
            if (new_local == NULL) {
                goto error;
            }
            sym_set_type(new_local, NULL_TYPE, 0);
            GETLOCAL(oparg) = new_local;
            break;
        }
        case LOAD_CONST: {
            STACK_GROW(1);
            PEEK(1) = (_Py_UOpsSymbolicExpression *)GETITEM(
                ctx, oparg);
            assert(PEEK(1)->ty_number->const_val != NULL);
            break;
        }
        case STORE_FAST_MAYBE_NULL:
        case STORE_FAST: {
            _Py_UOpsSymbolicExpression *value = PEEK(1);
            if (ir_store(ctx->ir, value, oparg) < 0) {
                goto error;
            }
            _Py_UOpsSymbolicExpression *new_local = sym_init_var(ctx, oparg);
            if (new_local == NULL) {
                goto error;
            }
            sym_copy_type_number(value, new_local);
            GETLOCAL(oparg) = new_local;
            STACK_SHRINK(1);
            break;
        }
        case COPY: {
            if (write_stack_to_ir(ctx, inst, true) < 0) {
                goto error;
            }
            if (ir_plain_inst(ctx->ir, *inst) < 0) {
                goto error;
            }
            _Py_UOpsSymbolicExpression *bottom = PEEK(1 + (oparg - 1));
            STACK_GROW(1);
            _Py_UOpsSymbolicExpression *temp = sym_init_unknown(ctx);
            if (temp == NULL) {
                goto error;
            }
            PEEK(1) = temp;
            sym_copy_type_number(bottom, temp);
            break;
        }

        case POP_TOP: {
            if (ir_store(ctx->ir, PEEK(1), -1) < 0) {
                goto error;
            }
            STACK_SHRINK(1);
            break;
        }

        case PUSH_NULL: {
            STACK_GROW(1);
            _Py_UOpsSymbolicExpression *null_sym =  sym_init_push_null(ctx);
            if (null_sym == NULL) {
                goto error;
            }
            PEEK(1) = null_sym;
            break;
        }

        case _PUSH_FRAME: {
            int argcount = oparg;
            // TOS is the new frame.
            if (write_stack_to_ir(ctx, inst, true) < 0) {
                goto error;
            }
            STACK_SHRINK(1);
            ctx->frame->stack_pointer = stack_pointer;
            _Py_UOpsOptIREntry *frame_ir_entry = ir_frame_push_info(ctx->ir);
            if (frame_ir_entry == NULL) {
                goto error;
            }

            assert(ctx->new_frame_sym != NULL);
            PyFunctionObject *func = extract_func_from_sym(ctx->new_frame_sym);
            if (func == NULL) {
                goto error;
            }
            PyCodeObject *co = (PyCodeObject *)func->func_code;

            _Py_UOpsSymbolicExpression *self_or_null = extract_self_or_null_from_sym(ctx->new_frame_sym);
            assert(self_or_null != NULL);
            _Py_UOpsSymbolicExpression **args = extract_args_from_sym(ctx->new_frame_sym);
            assert(args != NULL);
            ctx->new_frame_sym = NULL;
            // Bound method fiddling, same as _INIT_CALL_PY_EXACT_ARGS
            if (!sym_is_type(self_or_null, NULL_TYPE)) {
                args--;
                argcount++;
            }
            if (ir_plain_inst(ctx->ir, *inst) < 0) {
                goto error;
            }
            if (ctx_frame_push(
                ctx,
                frame_ir_entry,
                co,
                ctx->water_level
                ) != 0){
                goto error;
            }
            stack_pointer = ctx->frame->stack_pointer;
            // Cannot determine statically, so we can't propagate types.
            if (!sym_is_type(self_or_null, SELF_OR_NULL)) {
                for (int i = 0; i < argcount; i++) {
                    sym_copy_type_number(args[i], ctx->frame->locals[i]);
                }
            }
            break;
        }

        case _POP_FRAME: {
            assert(STACK_LEVEL() == 1);
            if (write_stack_to_ir(ctx, inst, true) < 0) {
                goto error;
            }
            if (ir_frame_pop_info(ctx->ir) < 0) {
                goto error;
            }
            if (ir_plain_inst(ctx->ir, *inst) < 0) {
                goto error;
            }
            _Py_UOpsSymbolicExpression *retval = PEEK(1);
            STACK_SHRINK(1);
            ctx->frame->stack_pointer = stack_pointer;

            if (ctx_frame_pop(ctx) != 0){
                goto error;
            }
            stack_pointer = ctx->frame->stack_pointer;
            // Push retval into new frame.
            STACK_GROW(1);
            _Py_UOpsSymbolicExpression *new_retval = sym_init_unknown(ctx);
            if (new_retval == NULL) {
                goto error;
            }
            PEEK(1) = new_retval;
            sym_copy_type_number(retval, new_retval);
            break;
        }

        case SWAP: {
            if (write_stack_to_ir(ctx, inst, true) < 0) {
                goto error;
            }
            if (ir_plain_inst(ctx->ir, *inst) < 0) {
                goto error;
            }

            _Py_UOpsSymbolicExpression *top;
            _Py_UOpsSymbolicExpression *bottom;
            top = stack_pointer[-1];
            bottom = stack_pointer[-2 - (oparg-2)];
            assert(oparg >= 2);

            _Py_UOpsSymbolicExpression *new_top = sym_init_unknown(ctx);
            if (new_top == NULL) {
                goto error;
            }
            sym_copy_type_number(top, new_top);

            _Py_UOpsSymbolicExpression *new_bottom = sym_init_unknown(ctx);
            if (new_bottom == NULL) {
                goto error;
            }
            sym_copy_type_number(bottom, new_bottom);

            stack_pointer[-2 - (oparg-2)] = new_top;
            stack_pointer[-1] = new_bottom;
            break;
        }
        case _SET_IP:
        case _CHECK_VALIDITY:
        case _SAVE_RETURN_OFFSET:
            if (write_stack_to_ir(ctx, inst, true) < 0) {
                goto error;
            }
            if (ir_plain_inst(ctx->ir, *inst) < 0) {
                goto error;
            }
            break;
        default:
            DPRINTF(1, "Unknown opcode in abstract interpreter\n");
            Py_UNREACHABLE();
    }

    // Store the frame symbolic to extract information later
    if (opcode == _INIT_CALL_PY_EXACT_ARGS) {
        ctx->new_frame_sym = PEEK(1);
        DPRINTF(3, "call_py_exact_args: {");
        for (Py_ssize_t i = 0; i < (ctx->new_frame_sym->operand_count); i++) {
            DPRINTF(3, "#%ld (%s)", i, ((ctx->new_frame_sym->operands[i]->inst.opcode >= 300 ? _PyOpcode_uop_name : _PyOpcode_OpName)[ctx->new_frame_sym->operands[i]->inst.opcode]))
        }
        DPRINTF(3, "} \n");
    }
    assert(ctx->frame != NULL);
    DPRINTF(3, " stack_level %d\n", STACK_LEVEL());
    ctx->frame->stack_pointer = stack_pointer;
    assert(STACK_LEVEL() >= 0);

    return ABSTRACT_INTERP_NORMAL;

pop_2_error_tier_two:
    STACK_SHRINK(1);
    STACK_SHRINK(1);
error:
    DPRINTF(1, "Encountered error in abstract interpreter\n");
    return ABSTRACT_INTERP_ERROR;

guard_required:
    DPRINTF(3, " stack_level %d\n", STACK_LEVEL());
    ctx->frame->stack_pointer = stack_pointer;
    assert(STACK_LEVEL() >= 0);

    return ABSTRACT_INTERP_GUARD_REQUIRED;

}

static _Py_UOpsAbstractInterpContext *
uop_abstract_interpret(
    PyCodeObject *co,
    _PyUOpInstruction *trace,
    int trace_len,
    int curr_stacklen
)
{

#ifdef Py_DEBUG
    char *uop_debug = Py_GETENV(DEBUG_ENV);
    int lltrace = 0;
    if (uop_debug != NULL && *uop_debug >= '0') {
        lltrace = *uop_debug - '0';  // TODO: Parse an int and all that
    }
#endif
    // Initialize the symbolic consts

    _Py_UOpsAbstractInterpContext *ctx = NULL;

    ctx = abstractinterp_context_new(
        co, curr_stacklen,
        trace_len);
    if (ctx == NULL) {
        goto error;
    }

    _PyUOpInstruction *curr = trace;
    _PyUOpInstruction *end = trace + trace_len;
    AbstractInterpExitCodes status = ABSTRACT_INTERP_NORMAL;

    bool first_impure = true;
    while (curr < end && !op_is_end(curr->opcode)) {

        if (!op_is_pure(curr->opcode) &&
            !op_is_specially_handled(curr->opcode) &&
            !op_is_bookkeeping(curr->opcode) &&
            !op_is_guard(curr->opcode)) {
            DPRINTF(3, "Impure %s\n", (curr->opcode >= 300 ? _PyOpcode_uop_name : _PyOpcode_OpName)[curr->opcode]);
            if (first_impure) {
                if (write_stack_to_ir(ctx, curr, false) < 0) {
                    goto error;
                }
                if (clear_locals_type_info(ctx) < 0) {
                    goto error;
                }
            }
            first_impure = false;
            if (ir_plain_inst(ctx->ir, *curr) < 0) {
                goto error;
            }
        }
        else {
            first_impure = true;
        }


        status = uop_abstract_interpret_single_inst(
            curr, end, ctx
        );
        if (status == ABSTRACT_INTERP_ERROR) {
            goto error;
        }
        else if (status == ABSTRACT_INTERP_GUARD_REQUIRED) {
            DPRINTF(3, "GUARD\n");
            // Emit the state of the stack first.
            // Since this is a guard, copy over the type info
            if (write_stack_to_ir(ctx, curr, true) < 0) {
                goto error;
            }
            if (ir_plain_inst(ctx->ir, *curr) < 0) {
                goto error;
            }
        }

        curr++;

    }

    ctx->terminating = curr;
    if (write_stack_to_ir(ctx, curr, false) < 0) {
        goto error;
    }

    return ctx;

error:
    Py_XDECREF(ctx);
    return NULL;
}

typedef struct _Py_UOpsEmitter {
    _PyUOpInstruction *writebuffer;
    _PyUOpInstruction *writebuffer_end;
    _PyUOpInstruction *writebuffer_true_end;
    int curr_i;
    int curr_reserve_i;

    int consumed_localsplus_slots;
    _Py_UOpsOptIREntry *curr_frame_ir_entry;
} _Py_UOpsEmitter;

static inline int
emit_i(_Py_UOpsEmitter *emitter,
       _PyUOpInstruction inst)
{
#ifdef Py_DEBUG
    char *uop_debug = Py_GETENV(DEBUG_ENV);
    int lltrace = 0;
    if (uop_debug != NULL && *uop_debug >= '0') {
        lltrace = *uop_debug - '0';  // TODO: Parse an int and all that
    }
#endif
    if (emitter->curr_i < 0) {
        DPRINTF(2, "out of emission space\n");
        return -1;
    }
    if (emitter->writebuffer + emitter->curr_i >= emitter->writebuffer_end) {
        DPRINTF(2, "out of emission space\n");
        return -1;
    }
    DPRINTF(2, "Emitting instruction at [%d] op: %s, oparg: %d, target: %d, operand: %" PRIu64 " \n",
            emitter->curr_i,
            (inst.opcode >= 300 ? _PyOpcode_uop_name : _PyOpcode_OpName)[inst.opcode],
            inst.oparg,
            inst.target,
            inst.operand);
    emitter->writebuffer[emitter->curr_i] = inst;
    emitter->curr_i++;
    return 0;
}



static int
count_stack_operands(_Py_UOpsSymbolicExpression *sym)
{
    int total = 0;
    for (Py_ssize_t i = 0; i < sym->operand_count; i++) {
        if (op_is_stackvalue(sym->operands[i]->inst.opcode)) {
            total++;
        }
    }
    return total;
}

static int
compile_sym_to_uops(_Py_UOpsEmitter *emitter,
                   _Py_UOpsSymbolicExpression *sym,
                    _Py_UOpsAbstractInterpContext *ctx)
{
    _PyUOpInstruction inst = sym->inst;;
    // Since CPython is a stack machine, just compile in the order
    // seen in the operands, then the instruction itself.

    if (op_is_terminal(sym->inst.opcode)) {
        // These are for unknown stack entries.
        if (op_is_stackvalue(sym->inst.opcode)) {
            // Leave it be. These are initial values from the start
            return 0;
        }
        if (sym->inst.opcode == INIT_FAST) {
            inst.opcode = LOAD_FAST;
        }
        return emit_i(emitter, inst);
    }

    // Constant propagated value, load immediate constant
    if (sym->ty_number->const_val != NULL && !op_is_stackvalue(sym->inst.opcode)) {
        // Shrink the stack if operands consist of stack values.
        // We don't need them anymore. This could happen because
        // the operands first need to be guarded and the guard could not
        // be eliminated via constant propagation.
        int stack_operands = count_stack_operands(sym);
        if (stack_operands) {
            inst.opcode = _SHRINK_STACK;
            inst.oparg = (int)sym->operand_count;
            inst.operand = 0;
            if (emit_i(emitter, inst) < 0) {
                return -1;
            }
        }

        inst.opcode = _LOAD_CONST_INLINE;
        inst.oparg = sym->inst.oparg;
        inst.operand = (uint64_t)Py_NewRef(sym->ty_number->const_val);
        return emit_i(emitter, inst);
    }

    // Compile each operand
    Py_ssize_t operands_count = sym->operand_count;
    for (Py_ssize_t i = 0; i < operands_count; i++) {
        if (sym->operands[i] == NULL) {
            continue;
        }
        // TODO Py_EnterRecursiveCall ?
        if (compile_sym_to_uops(
            emitter,
            sym->operands[i],
            ctx) < 0) {
            return -1;
        }
    }

    // Finally, emit the operation itself.
    return emit_i(emitter, sym->inst);
}

static int
emit_uops_from_ctx(
    _Py_UOpsAbstractInterpContext *ctx,
    _PyUOpInstruction *trace_writebuffer,
    _PyUOpInstruction *writebuffer_end,
    int *nop_to
)
{

#ifdef Py_DEBUG
    char *uop_debug = Py_GETENV(DEBUG_ENV);
    int lltrace = 0;
    if (uop_debug != NULL && *uop_debug >= '0') {
        lltrace = *uop_debug - '0';  // TODO: Parse an int and all that
    }
#endif


    _Py_UOpsAbstractFrame *root_frame = ctx->frame;
    while (root_frame->prev != NULL) {
        root_frame = root_frame->prev;
    }
    _Py_UOpsEmitter emitter = {
        trace_writebuffer,
        writebuffer_end,
        writebuffer_end,
        0,
        (int)(writebuffer_end - trace_writebuffer),
        0,
        root_frame->frame_ir_entry
    };

    _Py_UOps_Opt_IR *ir = ctx->ir;
    int entries = ir->curr_write;
    // First entry reserved for the root frame info.
    for (int i = 1; i < entries; i++) {
        _Py_UOpsOptIREntry *curr = &ir->entries[i];
        switch (curr->typ) {
            case IR_SYMBOLIC: {
                DPRINTF(3, "symbolic: expr: %s oparg: %d, operand: %p\n",
                        (curr->expr->inst.opcode >= 300 ? _PyOpcode_uop_name : _PyOpcode_OpName)[curr->expr->inst.opcode],
                        curr->expr->inst.oparg,
                        (void *)curr->expr->inst.operand);
                if (compile_sym_to_uops(&emitter, curr->expr, ctx) < 0) {
                    goto error;
                }
                // Anything less means no assignment target at all.
                if (curr->assignment_target >= TARGET_UNUSED) {
                    _PyUOpInstruction inst = {
                        curr->assignment_target == TARGET_UNUSED
                        ? POP_TOP : STORE_FAST,
                        curr->assignment_target, 0, 0};
                    if (emit_i(&emitter, inst) < 0) {
                        goto error;
                    }
                }
                break;
            }
            case IR_PLAIN_INST: {
                if (emit_i(&emitter, curr->inst) < 0) {
                    goto error;
                }
                break;
            }
            case IR_FRAME_PUSH_INFO: {
                _Py_UOpsOptIREntry *prev = emitter.curr_frame_ir_entry;
                emitter.curr_frame_ir_entry = curr;
                curr->prev_frame_ir = prev;
                break;
            }
            case IR_FRAME_POP_INFO: {
                _Py_UOpsOptIREntry *prev = emitter.curr_frame_ir_entry->prev_frame_ir;
                // There will always be the root frame.
                assert(prev != NULL);
                emitter.curr_frame_ir_entry->prev_frame_ir = NULL;
                emitter.curr_frame_ir_entry = prev;
                break;
            }
            case IR_NOP: break;
        }
    }

    if (emit_i(&emitter, *ctx->terminating) < 0) {
        return -1;
    }
    *nop_to = (int)(emitter.writebuffer_end - emitter.writebuffer);
    return emitter.curr_i;

error:
    return -1;
}
static void
remove_unneeded_uops(_PyUOpInstruction *buffer, int buffer_size)
{
    int last_set_ip = -1;
    bool maybe_invalid = false;
    for (int pc = 0; pc < buffer_size; pc++) {
        int opcode = buffer[pc].opcode;
        if (opcode == _SET_IP) {
            buffer[pc].opcode = NOP;
            last_set_ip = pc;
        }
        else if (opcode == _CHECK_VALIDITY) {
            if (maybe_invalid) {
                maybe_invalid = false;
            }
            else {
                buffer[pc].opcode = NOP;
            }
        }
        else if (opcode == _JUMP_TO_TOP || opcode == _EXIT_TRACE) {
            break;
        }
        else {
            if (_PyUop_Flags[opcode] & HAS_ESCAPES_FLAG) {
                maybe_invalid = true;
                if (last_set_ip >= 0) {
                    buffer[last_set_ip].opcode = _SET_IP;
                }
            }
            if ((_PyUop_Flags[opcode] & HAS_ERROR_FLAG) || opcode == _PUSH_FRAME) {
                if (last_set_ip >= 0) {
                    buffer[last_set_ip].opcode = _SET_IP;
                }
            }
        }
    }
}

static void
peephole_optimizations(_PyUOpInstruction *buffer, int buffer_size)
{
    for (int i = 0; i < buffer_size; i++) {
        _PyUOpInstruction *curr = buffer + i;
        int oparg = curr->oparg;
        switch(curr->opcode) {
            case _SHRINK_STACK: {
                // If all that precedes a _SHRINK_STACK is a bunch of LOAD_FAST,
                // then we can safely eliminate that without side effects.
                int load_fast_count = 0;
                _PyUOpInstruction *back = curr-1;
                while((back->opcode == _SET_IP ||
                    back->opcode == _CHECK_VALIDITY ||
                    back->opcode == LOAD_FAST) &&
                    load_fast_count < oparg) {
                    load_fast_count += back->opcode == LOAD_FAST;
                    back--;
                }
                if (load_fast_count == oparg) {
                    curr->opcode = NOP;
                    back = curr-1;
                    load_fast_count = 0;
                    while((back->opcode == _SET_IP ||
                           back->opcode == _CHECK_VALIDITY ||
                           back->opcode == LOAD_FAST) &&
                          load_fast_count < oparg) {
                        back->opcode = NOP;
                        load_fast_count += back->opcode == LOAD_FAST;
                        back--;
                    }
                }
                break;
            }
            default:
                break;
        }
    }
}


int
_Py_uop_analyze_and_optimize(
    PyCodeObject *co,
    _PyUOpInstruction *buffer,
    int buffer_size,
    int curr_stacklen
)
{
    _PyUOpInstruction *temp_writebuffer = NULL;
    bool err_occurred = false;
    _Py_UOpsAbstractInterpContext *ctx = NULL;

    temp_writebuffer = PyMem_New(_PyUOpInstruction, buffer_size * OVERALLOCATE_FACTOR);
    if (temp_writebuffer == NULL) {
        goto error;
    }


    // Pass: Abstract interpretation and symbolic analysis
    ctx = uop_abstract_interpret(
        co, buffer,
        buffer_size, curr_stacklen);

    if (ctx == NULL) {
        goto error;
    }

    _PyUOpInstruction *writebuffer_end = temp_writebuffer + buffer_size;
    // Compile the SSA IR
    int nop_to = 0;
    int trace_len = emit_uops_from_ctx(
        ctx,
        temp_writebuffer,
        writebuffer_end,
        &nop_to
    );
    if (trace_len < 0 || trace_len > buffer_size) {
        goto error;
    }

    peephole_optimizations(temp_writebuffer, trace_len);

    // Fill in our new trace!
    memcpy(buffer, temp_writebuffer, buffer_size * sizeof(_PyUOpInstruction));

    PyMem_Free(temp_writebuffer);
    Py_DECREF(ctx);

    remove_unneeded_uops(buffer, buffer_size);

    return 0;
error:
    Py_XDECREF(ctx);
    // The only valid error we can raise is MemoryError.
    // Other times it's not really errors but things like not being able
    // to fetch a function version because the function got deleted.
    err_occurred = PyErr_Occurred();
    PyMem_Free(temp_writebuffer);
    remove_unneeded_uops(buffer, buffer_size);
    return err_occurred ? -1 : 0;
}