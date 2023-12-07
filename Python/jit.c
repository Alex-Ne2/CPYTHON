#include "Python.h"

#ifdef _Py_JIT

#include "pycore_abstract.h"
#include "pycore_call.h"
#include "pycore_ceval.h"
#include "pycore_dict.h"
#include "pycore_intrinsics.h"
#include "pycore_long.h"
#include "pycore_opcode_metadata.h"
#include "pycore_opcode_utils.h"
#include "pycore_optimizer.h"
#include "pycore_pyerrors.h"
#include "pycore_setobject.h"
#include "pycore_sliceobject.h"
#include "pycore_uops.h"
#include "pycore_jit.h"

#include "ceval_macros.h"
#include "jit_stencils.h"

#ifndef MS_WINDOWS
    #include <sys/mman.h>
#endif

#define FITS_IN_BITS(U, B) \
    (((int64_t)(U) >= -(1LL << ((B) - 1))) && ((int64_t)(U) < (1LL << ((B) - 1))))

static uint64_t
get_page_size(void)
{
#ifdef MS_WINDOWS
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return si.dwPageSize;
#else
    return sysconf(_SC_PAGESIZE);
#endif
}

static void
jit_warn(const char *message)
{
#ifdef MS_WINDOWS
    int errno = GetLastError();
#endif
    PyErr_WarnFormat(PyExc_RuntimeWarning, 0, "JIT %s (%d)", message, errno);
}

static char *
jit_alloc(uint64_t size)
{
    assert(size);
    assert(size % get_page_size() == 0);
#ifdef MS_WINDOWS
    char *memory = VirtualAlloc(NULL, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    int failed = memory == NULL;
#else
    char *memory = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    int failed = memory == MAP_FAILED;
#endif
    if (failed) {
        jit_warn("unable to allocate memory");
        return NULL;
    }
    return memory;
}

static void
jit_free(char *memory, uint64_t size)
{
    assert(size);
    assert(size % get_page_size() == 0);
#ifdef MS_WINDOWS
    int failed = !VirtualFree(memory, 0, MEM_RELEASE);
#else
    int failed = munmap(memory, size);
#endif
    if (failed) {
        jit_warn("unable to free memory");
    }
}

static int
mark_executable(char *memory, uint64_t size)
{
    if (size == 0) {
        return 0;
    }
    assert(size % get_page_size() == 0);
#ifdef MS_WINDOWS
    if (!FlushInstructionCache(GetCurrentProcess(), memory, size)) {
        jit_warn("unable to flush instruction cache");
        return -1;
    }
    DWORD old;
    int failed = !VirtualProtect(memory, size, PAGE_EXECUTE, &old);
#else
    __builtin___clear_cache((char *)memory, (char *)memory + size);
    int failed = mprotect(memory, size, PROT_EXEC);
#endif
    if (failed) {
        jit_warn("unable to protect executable memory");
        return -1;
    }
    return 0;
}

static int
mark_readable(char *memory, uint64_t size)
{
    if (size == 0) {
        return 0;
    }
    assert(size % get_page_size() == 0);
#ifdef MS_WINDOWS
    DWORD old;
    int failed = !VirtualProtect(memory, size, PAGE_READONLY, &old);
#else
    int failed = mprotect(memory, size, PROT_READ);
#endif
    if (failed) {
        jit_warn("unable to protect readable memory");
        return -1;
    }
    return 0;
}

static void
patch(char *base, const Hole *hole, uint64_t *patches)
{
    char *location = base + hole->offset;
    uint64_t value = patches[hole->value] + (uint64_t)hole->symbol + hole->addend;
    uint32_t *addr = (uint32_t *)location;
    switch (hole->kind) {
        case HoleKind_IMAGE_REL_I386_DIR32:
            *addr = (uint32_t)value;
            return;
        case HoleKind_IMAGE_REL_AMD64_REL32:
        case HoleKind_IMAGE_REL_I386_REL32:
        case HoleKind_X86_64_RELOC_BRANCH:
        case HoleKind_X86_64_RELOC_GOT:
        case HoleKind_X86_64_RELOC_GOT_LOAD:
            value -= (uint64_t)location;
            assert(FITS_IN_BITS(value, 32));
            *addr = (uint32_t)value;
            return;
        case HoleKind_ARM64_RELOC_UNSIGNED:
        case HoleKind_IMAGE_REL_AMD64_ADDR64:
        case HoleKind_R_AARCH64_ABS64:
        case HoleKind_R_X86_64_64:
        case HoleKind_X86_64_RELOC_UNSIGNED:
            *(uint64_t *)addr = value;
            return;
        case HoleKind_ARM64_RELOC_GOT_LOAD_PAGE21:
            value = ((value >> 12) << 12) - (((uint64_t)location >> 12) << 12);
            assert((*addr & 0x9F000000) == 0x90000000);
            assert((value & 0xFFF) == 0);
            uint32_t lo = (value << 17) & 0x60000000;
            uint32_t hi = (value >> 9) & 0x00FFFFE0;
            *addr = (*addr & 0x9F00001F) | hi | lo;
            return;
        case HoleKind_R_AARCH64_CALL26:
        case HoleKind_R_AARCH64_JUMP26:
            value -= (uint64_t)location;
            assert(((*addr & 0xFC000000) == 0x14000000) ||
                   ((*addr & 0xFC000000) == 0x94000000));
            assert((value & 0x3) == 0);
            assert(FITS_IN_BITS(value, 28));
            *addr = (*addr & 0xFC000000) | ((uint32_t)(value >> 2) & 0x03FFFFFF);
            return;
        case HoleKind_ARM64_RELOC_GOT_LOAD_PAGEOFF12:
            value &= (1 << 12) - 1;
            assert(((*addr & 0x3B000000) == 0x39000000) ||
                   ((*addr & 0x11C00000) == 0x11000000));
            int shift = 0;
            if ((*addr & 0x3B000000) == 0x39000000) {
                shift = ((*addr >> 30) & 0x3);
                if (shift == 0 && (*addr & 0x04800000) == 0x04800000) {
                    shift = 4;
                }
            }
            assert(((value & ((1 << shift) - 1)) == 0));
            *addr = (*addr & 0xFFC003FF) | ((uint32_t)((value >> shift) << 10) & 0x003FFC00);
            return;
        case HoleKind_R_AARCH64_MOVW_UABS_G0_NC:
            assert(((*addr >> 21) & 0x3) == 0);
            *addr = (*addr & 0xFFE0001F) | (((value >>  0) & 0xFFFF) << 5);
            return;
        case HoleKind_R_AARCH64_MOVW_UABS_G1_NC:
            assert(((*addr >> 21) & 0x3) == 1);
            *addr = (*addr & 0xFFE0001F) | (((value >> 16) & 0xFFFF) << 5);
            return;
        case HoleKind_R_AARCH64_MOVW_UABS_G2_NC:
            assert(((*addr >> 21) & 0x3) == 2);
            *addr = (*addr & 0xFFE0001F) | (((value >> 32) & 0xFFFF) << 5);
            return;
        case HoleKind_R_AARCH64_MOVW_UABS_G3:
            assert(((*addr >> 21) & 0x3) == 3);
            *addr = (*addr & 0xFFE0001F) | (((value >> 48) & 0xFFFF) << 5);
            return;
    }
    Py_UNREACHABLE();
}

static void
copy_and_patch(char *base, const Stencil *stencil, uint64_t *patches)
{
    memcpy(base, stencil->body, stencil->body_size);
    for (uint64_t i = 0; i < stencil->holes_size; i++) {
        patch(base, &stencil->holes[i], patches);
    }
}

static void
emit(const StencilGroup *stencil_group, uint64_t patches[])
{
    char *data = (char *)patches[HoleValue_DATA];
    copy_and_patch(data, &stencil_group->data, patches);
    char *text = (char *)patches[HoleValue_TEXT];
    copy_and_patch(text, &stencil_group->text, patches);
}

static _PyInterpreterFrame *
execute(_PyExecutorObject *executor, _PyInterpreterFrame *frame, PyObject **stack_pointer)
{
    assert(PyObject_TypeCheck(executor, &_PyUOpExecutor_Type));
    frame = ((_PyJITContinueFunction)(((_PyUOpExecutorObject *)(executor))->jit_code))(frame, stack_pointer, PyThreadState_Get());
    Py_DECREF(executor);
    return frame;
}

void
_PyJIT_Free(_PyUOpExecutorObject *executor)
{
    char *memory = (char *)executor->jit_code;
    uint64_t size = executor->jit_size;
    if (memory) {
        executor->jit_code = NULL;
        executor->jit_size = 0;
        jit_free(memory, size);
    }
}

int
_PyJIT_Compile(_PyUOpExecutorObject *executor)
{
    uint64_t text_size = 0;
    uint64_t data_size = 0;
    for (int i = 0; i < Py_SIZE(executor); i++) {
        _PyUOpInstruction *instruction = &executor->trace[i];
        const StencilGroup *stencil_group = &stencil_groups[instruction->opcode];
        text_size += stencil_group->text.body_size;
        data_size += stencil_group->data.body_size;
    }
    uint64_t page_size = get_page_size();
    assert((page_size & (page_size - 1)) == 0);
    text_size += page_size - (text_size & (page_size - 1));
    data_size += page_size - (data_size & (page_size - 1));
    char *memory = jit_alloc(text_size + data_size);
    if (memory == NULL) {
        return -1;
    }
    char *text = memory;
    char *data = memory + text_size;
    for (int i = 0; i < Py_SIZE(executor); i++) {
        _PyUOpInstruction *instruction = &executor->trace[i];
        const StencilGroup *stencil_group = &stencil_groups[instruction->opcode];
        uint64_t patches[] = GET_PATCHES();
        patches[HoleValue_CONTINUE] = (uint64_t)text + stencil_group->text.body_size;
        patches[HoleValue_CURRENT_EXECUTOR] = (uint64_t)executor;
        patches[HoleValue_OPARG] = instruction->oparg;
        patches[HoleValue_OPERAND] = instruction->operand;
        patches[HoleValue_TARGET] = instruction->target;
        patches[HoleValue_DATA] = (uint64_t)data;
        patches[HoleValue_TEXT] = (uint64_t)text;
        patches[HoleValue_TOP] = (uint64_t)memory;
        patches[HoleValue_ZERO] = 0;
        emit(stencil_group, patches);
        text += stencil_group->text.body_size;
        data += stencil_group->data.body_size;
    }
    if (mark_executable(memory, text_size) ||
        mark_readable(memory + text_size, data_size))
    {
        jit_free(memory, text_size + data_size);
        return -1;
    }
    executor->base.execute = execute;
    executor->jit_code = memory;
    executor->jit_size = text_size + data_size;
    return 0;
}

#endif
