#include "dyfn.h"

#include <string.h>

#ifndef SBOX_NO_CALLBACKS
struct DyfnClosureInfo dyfn_closure_info[DYFN_MAX_CLOSURES];
atomic_int dyfn_closure_count = 0;
#endif

enum DyfnClass dyfn_classify(enum DyfnType type) {
    switch (type) {
        case DYFN_TYPE_FLOAT:
            return DYFN_CLASS_FLOAT;
        case DYFN_TYPE_DOUBLE:
            return DYFN_CLASS_DOUBLE;
        case DYFN_TYPE_VOID:
            return DYFN_CLASS_VOID;
        default:
            return DYFN_CLASS_INT;
    }
}

size_t dyfn_type_size(enum DyfnType type) {
    switch (type) {
        case DYFN_TYPE_VOID:
            return 0;
        case DYFN_TYPE_UINT8:
        case DYFN_TYPE_SINT8:
            return 1;
        case DYFN_TYPE_UINT16:
        case DYFN_TYPE_SINT16:
            return 2;
        case DYFN_TYPE_UINT32:
        case DYFN_TYPE_SINT32:
            return 4;
        case DYFN_TYPE_UINT64:
        case DYFN_TYPE_SINT64:
            return 8;
        case DYFN_TYPE_FLOAT:
            return 4;
        case DYFN_TYPE_DOUBLE:
            return 8;
        case DYFN_TYPE_POINTER:
            return sizeof(void*);
        default:
            return 0;
    }
}

bool dyfn_prep_call(struct DyfnCallArgs* call, void* func,
                    enum DyfnType ret_type, int nargs,
                    const enum DyfnType* arg_types, void** arg_values) {
    memset(call, 0, sizeof(*call));
    call->func = func;
    call->ret_class = dyfn_classify(ret_type);

    for (int i = 0; i < nargs; i++) {
        enum DyfnClass cls = dyfn_classify(arg_types[i]);
        size_t size = dyfn_type_size(arg_types[i]);

        if (cls == DYFN_CLASS_FLOAT || cls == DYFN_CLASS_DOUBLE) {
            if (call->float_count < DYFN_FLOAT_ARG_REGS) {
                memcpy(&call->float_regs[call->float_count], arg_values[i],
                       size);
                call->float_count++;
            } else {
                memcpy(&call->stack_args[call->stack_count], arg_values[i],
                       size);
                call->stack_count++;
            }
        } else {
            if (call->int_count < DYFN_INT_ARG_REGS) {
                memcpy(&call->int_regs[call->int_count], arg_values[i], size);
                call->int_count++;
            } else {
                memcpy(&call->stack_args[call->stack_count], arg_values[i],
                       size);
                call->stack_count++;
            }
        }
    }
    return true;
}

void dyfn_store_result(const struct DyfnCallResult* result,
                       enum DyfnType ret_type, void* out) {
    enum DyfnClass cls = dyfn_classify(ret_type);
    size_t size = dyfn_type_size(ret_type);
    if (cls == DYFN_CLASS_FLOAT || cls == DYFN_CLASS_DOUBLE)
        memcpy(out, &result->float_val, size);
    else if (cls != DYFN_CLASS_VOID)
        memcpy(out, &result->int_val, size);
}

#ifndef SBOX_NO_CALLBACKS

void* dyfn_closure_alloc(int callback_id, enum DyfnType ret_type, int nargs,
                         const enum DyfnType* arg_types) {
    // Caller (the sandbox dispatch loop, driven by a host RPC) is serialized
    // by the host's callback registration lock, so we don't need a CAS loop
    // here. The atomic is used only to publish the slot's contents to readers
    // (closure dispatchers running on other worker threads) with release/acquire
    // ordering.
    int slot = atomic_load_explicit(&dyfn_closure_count, memory_order_relaxed);
    if (slot >= DYFN_MAX_CLOSURES)
        return NULL;

    struct DyfnClosureInfo* info = &dyfn_closure_info[slot];
    info->callback_id = callback_id;
    info->ret_type = ret_type;
    info->nargs = nargs;
    for (int i = 0; i < nargs; i++)
        info->arg_types[i] = arg_types[i];
    info->active = true;

    atomic_store_explicit(&dyfn_closure_count, slot + 1, memory_order_release);
    return dyfn_stub_table[slot];
}

#endif  // SBOX_NO_CALLBACKS
