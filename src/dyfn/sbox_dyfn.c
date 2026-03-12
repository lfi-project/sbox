#include "sbox_dyfn.h"

#include <string.h>

__thread struct SboxDyfnClosureInfo
    sbox_dyfn_closure_info[SBOX_DYFN_MAX_CLOSURES];
__thread int sbox_dyfn_closure_count = 0;

enum SboxDyfnClass sbox_dyfn_classify(enum SboxDyfnType type) {
    switch (type) {
        case SBOX_DYFN_TYPE_FLOAT:
            return SBOX_DYFN_CLASS_FLOAT;
        case SBOX_DYFN_TYPE_DOUBLE:
            return SBOX_DYFN_CLASS_DOUBLE;
        case SBOX_DYFN_TYPE_VOID:
            return SBOX_DYFN_CLASS_VOID;
        default:
            return SBOX_DYFN_CLASS_INT;
    }
}

size_t sbox_dyfn_type_size(enum SboxDyfnType type) {
    switch (type) {
        case SBOX_DYFN_TYPE_VOID:
            return 0;
        case SBOX_DYFN_TYPE_UINT8:
        case SBOX_DYFN_TYPE_SINT8:
            return 1;
        case SBOX_DYFN_TYPE_UINT16:
        case SBOX_DYFN_TYPE_SINT16:
            return 2;
        case SBOX_DYFN_TYPE_UINT32:
        case SBOX_DYFN_TYPE_SINT32:
            return 4;
        case SBOX_DYFN_TYPE_UINT64:
        case SBOX_DYFN_TYPE_SINT64:
            return 8;
        case SBOX_DYFN_TYPE_FLOAT:
            return 4;
        case SBOX_DYFN_TYPE_DOUBLE:
            return 8;
        case SBOX_DYFN_TYPE_POINTER:
            return sizeof(void*);
        default:
            return 0;
    }
}

bool sbox_dyfn_prep_call(struct SboxDyfnCallArgs* call, void* func,
                         enum SboxDyfnType ret_type, int nargs,
                         const enum SboxDyfnType* arg_types,
                         void** arg_values) {
    memset(call, 0, sizeof(*call));
    call->func = func;
    call->ret_class = sbox_dyfn_classify(ret_type);

    for (int i = 0; i < nargs; i++) {
        enum SboxDyfnClass cls = sbox_dyfn_classify(arg_types[i]);
        size_t size = sbox_dyfn_type_size(arg_types[i]);

        if (cls == SBOX_DYFN_CLASS_FLOAT || cls == SBOX_DYFN_CLASS_DOUBLE) {
            memcpy(&call->float_regs[call->float_count], arg_values[i], size);
            call->float_count++;
        } else {
#if defined(__x86_64__)
            if (call->int_count < 6) {
                memcpy(&call->int_regs[call->int_count], arg_values[i], size);
                call->int_count++;
            } else {
                memcpy(&call->stack_args[call->stack_count], arg_values[i],
                       size);
                call->stack_count++;
            }
#elif defined(__aarch64__)
            memcpy(&call->int_regs[call->int_count], arg_values[i], size);
            call->int_count++;
#endif
        }
    }
    return true;
}

void sbox_dyfn_store_result(const struct SboxDyfnCallResult* result,
                            enum SboxDyfnType ret_type, void* out) {
    enum SboxDyfnClass cls = sbox_dyfn_classify(ret_type);
    size_t size = sbox_dyfn_type_size(ret_type);
    if (cls == SBOX_DYFN_CLASS_FLOAT || cls == SBOX_DYFN_CLASS_DOUBLE)
        memcpy(out, &result->float_val, size);
    else if (cls != SBOX_DYFN_CLASS_VOID)
        memcpy(out, &result->int_val, size);
}

void* sbox_dyfn_closure_alloc(int callback_id, enum SboxDyfnType ret_type,
                              int nargs, const enum SboxDyfnType* arg_types) {
    if (sbox_dyfn_closure_count >= SBOX_DYFN_MAX_CLOSURES)
        return NULL;

    int slot = sbox_dyfn_closure_count++;
    struct SboxDyfnClosureInfo* info = &sbox_dyfn_closure_info[slot];
    info->callback_id = callback_id;
    info->ret_type = ret_type;
    info->nargs = nargs;
    for (int i = 0; i < nargs; i++)
        info->arg_types[i] = arg_types[i];
    info->active = true;

    return sbox_dyfn_stub_table[slot];
}

void sbox_dyfn_closure_free_all(void) {
    sbox_dyfn_closure_count = 0;
}
