#pragma once

#include "sbox.hh"

#ifdef SBOX_STATIC
#error "SBOX_STATIC cannot be used with the process backend"
#endif

#include <sys/types.h>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

extern "C" {
#include "pbox.h"
}

namespace sbox {

namespace detail {

// Type mapping: C++ types to PBOX_TYPE_*
template<typename T>
struct pbox_type;

template<>
struct pbox_type<void> {
    static constexpr PBoxType value = PBOX_TYPE_VOID;
};
template<>
struct pbox_type<float> {
    static constexpr PBoxType value = PBOX_TYPE_FLOAT;
};
template<>
struct pbox_type<double> {
    static constexpr PBoxType value = PBOX_TYPE_DOUBLE;
};
template<>
struct pbox_type<char> {
    static constexpr PBoxType value = PBOX_TYPE_SINT8;
};
template<>
struct pbox_type<signed char> {
    static constexpr PBoxType value = PBOX_TYPE_SINT8;
};
template<>
struct pbox_type<unsigned char> {
    static constexpr PBoxType value = PBOX_TYPE_UINT8;
};
template<>
struct pbox_type<short> {
    static constexpr PBoxType value = PBOX_TYPE_SINT16;
};
template<>
struct pbox_type<unsigned short> {
    static constexpr PBoxType value = PBOX_TYPE_UINT16;
};
template<>
struct pbox_type<int> {
    static constexpr PBoxType value = PBOX_TYPE_SINT32;
};
template<>
struct pbox_type<unsigned int> {
    static constexpr PBoxType value = PBOX_TYPE_UINT32;
};
template<>
struct pbox_type<long> {
    static constexpr PBoxType value =
        sizeof(long) == 8 ? PBOX_TYPE_SINT64 : PBOX_TYPE_SINT32;
};
template<>
struct pbox_type<unsigned long> {
    static constexpr PBoxType value =
        sizeof(unsigned long) == 8 ? PBOX_TYPE_UINT64 : PBOX_TYPE_UINT32;
};
template<>
struct pbox_type<long long> {
    static constexpr PBoxType value = PBOX_TYPE_SINT64;
};
template<>
struct pbox_type<unsigned long long> {
    static constexpr PBoxType value = PBOX_TYPE_UINT64;
};

// All pointer types map to PBOX_TYPE_POINTER
template<typename T>
struct pbox_type<T*> {
    static constexpr PBoxType value = PBOX_TYPE_POINTER;
};

// sbox<T*> maps to PBOX_TYPE_POINTER (for callbacks with sbox pointer args)
template<typename T>
struct pbox_type<sbox<T*>> {
    static constexpr PBoxType value = PBOX_TYPE_POINTER;
};

template<typename T>
inline constexpr PBoxType pbox_type_v = pbox_type<T>::value;

// Read a typed argument from packed arg_storage at the given offset
template<size_t I, typename T>
T read_callback_arg(const char* arg_storage, const uint64_t* arg_offsets) {
    T val;
    std::memcpy(&val, &arg_storage[arg_offsets[I]], sizeof(T));
    return val;
}

// Typed callback dispatcher: unpacks args and calls the function
template<typename Ret, typename... Args, size_t... Is>
void callback_dispatch_impl(pbox_fn_t func_ptr, const char* arg_storage,
                            const uint64_t* arg_offsets, char* result_storage,
                            std::index_sequence<Is...>) {
    auto fn = reinterpret_cast<Ret (*)(Args...)>(func_ptr);
    if constexpr (std::is_void_v<Ret>) {
        fn(read_callback_arg<Is, Args>(arg_storage, arg_offsets)...);
    } else {
        Ret result = fn(read_callback_arg<Is, Args>(arg_storage, arg_offsets)...);
        std::memcpy(result_storage, &result, sizeof(Ret));
    }
}

template<typename Ret, typename... Args>
void callback_dispatch(pbox_fn_t func_ptr, const char* arg_storage,
                       const uint64_t* arg_offsets, char* result_storage) {
    callback_dispatch_impl<Ret, Args...>(func_ptr, arg_storage, arg_offsets,
                                         result_storage,
                                         std::index_sequence_for<Args...>{});
}

}  // namespace detail

// Process backend - runs code in sandboxed child process via pbox
template<>
class Sandbox<Process> {
public:
    explicit Sandbox(const char* sandbox_executable) {
        box_ = pbox_create(sandbox_executable);
    }

    ~Sandbox() {
        if (box_) {
            pbox_destroy(box_);
        }
    }

    // Non-copyable
    Sandbox(const Sandbox&) = delete;
    Sandbox& operator=(const Sandbox&) = delete;

    // Non-movable
    Sandbox(Sandbox&&) = delete;
    Sandbox& operator=(Sandbox&&) = delete;

    // Call a function by name.
    // 'name' must be a string literal (pointer is cached directly).
    template<typename Sig, typename... Args>
    auto call(const char* name, Args... args) {
        void* fn = lookup(name);
        if (!fn) {
            fprintf(stderr, "sbox: symbol not found: %s\n", name);
            abort();
        }
        using Ret = detail::sig_return_t<Sig>;
        if constexpr (std::is_void_v<Ret>) {
            call_ptr_sig<Sig>(fn, args...);
        } else {
            return detail::wrap_sbox_return(call_ptr_sig<Sig>(fn, args...));
        }
    }

    // Call with TypedName (signature deduced from declaration)
    template<typename Ret, typename... Params, typename... Args>
    auto call(TypedName<Ret (*)(Params...)> tn, Args... args) {
        static_assert(sizeof...(Params) == sizeof...(Args),
                      "Wrong number of arguments for sandboxed function");
        static_assert(
            (detail::check_sbox_ptr_arg_v<Params, Args> && ...),
            "Pointer arguments must be sbox<T*> or sbox_safe<T*> with a "
            "matching type");
        return call<Ret(Params...)>(tn.name, args...);
    }

    // Context-aware call (defined after CallContext)
    template<typename Sig, typename... Args>
    auto call(CallContext<Process>& ctx, const char* name, Args... args);

    // Context-aware call with TypedName (defined after CallContext)
    template<typename Ret, typename... Params, typename... Args>
    auto call(CallContext<Process>& ctx, TypedName<Ret (*)(Params...)> tn,
              Args... args);

    // Create a call context (defined after CallContext)
    inline CallContext<Process> context();

    // Get a function handle for repeated calls.
    // 'name' must be a string literal (pointer is cached directly).
    template<typename Sig>
    FnHandle<Process, Sig> fn(const char* name) {
        return FnHandle<Process, Sig>(*this, lookup(name));
    }

    // Get a function handle with TypedName (signature deduced from declaration)
    template<typename Ret, typename... Params>
    FnHandle<Process, Ret(Params...)> fn(TypedName<Ret (*)(Params...)> tn) {
        return FnHandle<Process, Ret(Params...)>(*this, lookup(tn.name));
    }

    // Call via function pointer (used by FnHandle)
    template<typename Ret, typename... Args>
    Ret call_ptr(void* fn, Args... args) {
        return call_impl<Ret, Args...>(fn, args...);
    }

    // Memory allocation in sandbox. Returns sbox<T*> (unchecked) since the
    // pointer is in the sandbox's address space (not directly dereferenceable).
    template<typename T>
    sbox<T*> alloc(size_t count = 1) {
        return sbox<T*>(static_cast<T*>(pbox_malloc(box_, sizeof(T) * count)));
    }

    template<typename T>
    sbox<T*> calloc(size_t count) {
        return sbox<T*>(
            static_cast<T*>(pbox_calloc(box_, count, sizeof(T))));
    }

    template<typename T>
    sbox<T*> realloc(sbox<T*> ptr, size_t count) {
        return sbox<T*>(static_cast<T*>(pbox_realloc(
            box_, ptr.unsafe_unverified(), sizeof(T) * count)));
    }

    void free(void* ptr) {
        pbox_free(box_, ptr);
    }

    template<typename T>
    void free(sbox<T*> p) {
        pbox_free(box_, p.unsafe_unverified());
    }
    template<typename T>
    void free(sbox_safe<T*> p) {
        free(sbox<T*>(p));
    }

    // Verify an unchecked sandbox pointer (promote to sbox_safe).
    // Checks that the pointer falls within an identity-mapped region.
    template<typename T>
    sbox_safe<T*> verify(sbox<T*> ptr, size_t count) {
        T* raw = ptr.unsafe_unverified();
        if (raw && !pbox_in_idmem(box_, raw, sizeof(T) * count)) {
            fprintf(stderr,
                    "sbox: verify failed: pointer %p not in "
                    "identity-mapped region\n",
                    static_cast<void*>(raw));
            abort();
        }
        return sbox_safe<T*>(raw);
    }

    // Memory mapping
    void* mmap(void* addr, size_t length, int prot, int flags, int fd,
               off_t offset) {
        return pbox_mmap(box_, addr, length, prot, flags, fd, offset);
    }

    int munmap(void* addr, size_t length) {
        return pbox_munmap(box_, addr, length);
    }

    // Identity-mapped memory (same address in host and sandbox)
    void* mmap_identity(size_t length, int prot) {
        return pbox_mmap_identity(box_, length, prot);
    }

    int munmap_identity(void* addr, size_t length) {
        return pbox_munmap_identity(box_, addr, length);
    }

    // Arena allocator for identity-mapped memory (per-thread)
    template<typename T>
    T* idmem_alloc(size_t count = 1) {
        return static_cast<T*>(pbox_idmem_alloc(box_, sizeof(T) * count));
    }

    void idmem_reset() {
        pbox_idmem_reset(box_);
    }

    // File descriptor registration (sends fd to sandbox)
    int register_fd(int fd) {
        return pbox_send_fd(box_, fd);
    }

    // Close a file descriptor in sandbox
    int close_fd(int sandbox_fd) {
        return pbox_close(box_, sandbox_fd);
    }

    // Data transfer
    void copy_to(void* sandbox_dest, const void* host_src, size_t n) {
        pbox_copy_to(box_, sandbox_dest, host_src, n);
    }

    template<typename T>
    void copy_to(sbox<T*> sandbox_dest, const void* host_src, size_t n) {
        pbox_copy_to(box_, sandbox_dest.unsafe_unverified(), host_src, n);
    }
    template<typename T>
    void copy_to(sbox_safe<T*> d, const void* s, size_t n) {
        copy_to(sbox<T*>(d), s, n);
    }

    void copy_from(void* host_dest, const void* sandbox_src, size_t n) {
        pbox_copy_from(box_, host_dest, sandbox_src, n);
    }

    template<typename T>
    void copy_from(void* host_dest, sbox<T*> sandbox_src, size_t n) {
        pbox_copy_from(box_, host_dest, sandbox_src.unsafe_unverified(), n);
    }
    template<typename T>
    void copy_from(void* d, sbox_safe<T*> s, size_t n) {
        copy_from(d, sbox<T*>(s), n);
    }

    // String helper
    sbox<char*> copy_string(const char* s) {
        size_t len = std::strlen(s) + 1;
        sbox<char*> buf = alloc<char>(len);
        if (!buf)
            return {};
        copy_to(buf, s, len);
        return buf;
    }

    // Register a callback
    template<typename Ret, typename... Args>
    sbox<Ret (*)(Args...)> register_callback(Ret (*fn)(Args...)) {
        constexpr int nargs = sizeof...(Args);
        static_assert(nargs <= PBOX_MAX_ARGS,
                      "Too many callback arguments (max is PBOX_MAX_ARGS)");
        PBoxType arg_types[nargs > 0 ? nargs : 1];
        if constexpr (nargs > 0) {
            fill_arg_types<0, Args...>(arg_types);
        }
        void* raw = pbox_register_callback(
            box_, reinterpret_cast<pbox_fn_t>(fn),
            detail::callback_dispatch<Ret, Args...>,
            detail::pbox_type_v<Ret>, nargs,
            nargs > 0 ? arg_types : nullptr);
        return sbox<Ret (*)(Args...)>(reinterpret_cast<Ret (*)(Args...)>(raw));
    }

    // Register a callback with thunk (for callbacks with sbox<T*> args)
    template<auto fn>
    auto register_callback() {
        return register_callback(
            &detail::callback_thunk_impl<decltype(fn), fn>::call);
    }

    // Process-specific
    pid_t pid() const {
        return pbox_pid(box_);
    }
    bool alive() const {
        return pbox_alive(box_);
    }

    // Escape hatch for advanced usage (returns pbox handle)
    PBox* native_handle() const {
        return box_;
    }

private:
    template<typename Ret, typename... Params, typename... Args>
    Ret call_ptr_sig(void* fn, Ret (*)(Params...), Args... args) {
        return call_impl<Ret, Params...>(fn, convert_arg<Params>(args)...);
    }

    template<typename Sig, typename... Args>
    auto call_ptr_sig(void* fn, Args... args) {
        return call_ptr_sig(fn, static_cast<Sig*>(nullptr), args...);
    }

    // Convert argument, unwrapping sbox types
    template<typename To, typename From>
    static To convert_arg(From arg) {
        if constexpr (detail::is_sbox_ptr_v<From>) {
            return reinterpret_cast<To>(arg.unsafe_unverified());
        } else if constexpr (detail::is_sbox_safe_ptr_v<From>) {
            return reinterpret_cast<To>(arg.data());
        } else if constexpr (std::is_pointer_v<To> && std::is_pointer_v<From>) {
            return reinterpret_cast<To>(arg);
        } else {
            return static_cast<To>(arg);
        }
    }

    // Actual pbox_call implementation
    template<typename Ret, typename... Args>
    Ret call_impl(void* fn, Args... args) {
        detail::tls_current_sandbox = this;
        constexpr int nargs = sizeof...(Args);
        static_assert(nargs <= PBOX_MAX_ARGS,
                      "Too many arguments (max is PBOX_MAX_ARGS)");

        PBoxType arg_types[nargs > 0 ? nargs : 1];
        void* arg_ptrs[nargs > 0 ? nargs : 1];

        if constexpr (nargs > 0) {
            fill_arg_types<0, Args...>(arg_types);
            fill_arg_ptrs<0>(arg_ptrs, args...);
        }

        if constexpr (std::is_void_v<Ret>) {
            pbox_call(box_, fn, PBOX_TYPE_VOID, nargs,
                      nargs > 0 ? arg_types : nullptr,
                      nargs > 0 ? arg_ptrs : nullptr, nullptr);
        } else {
            Ret result;
            pbox_call(box_, fn, detail::pbox_type_v<Ret>, nargs,
                      nargs > 0 ? arg_types : nullptr,
                      nargs > 0 ? arg_ptrs : nullptr, &result);
            return result;
        }
    }

    // Fill argument type array
    template<size_t I, typename T, typename... Rest>
    void fill_arg_types(PBoxType* types) {
        types[I] = detail::pbox_type_v<T>;
        if constexpr (sizeof...(Rest) > 0) {
            fill_arg_types<I + 1, Rest...>(types);
        }
    }

    // Fill argument pointer array
    template<size_t I>
    void fill_arg_ptrs(void**) {
    }

    template<size_t I, typename T, typename... Rest>
    void fill_arg_ptrs(void** ptrs, T& arg, Rest&... rest) {
        ptrs[I] = &arg;
        if constexpr (sizeof...(Rest) > 0) {
            fill_arg_ptrs<I + 1>(ptrs, rest...);
        }
    }

    void* lookup(const char* name) {
        std::lock_guard<std::mutex> lock(cache_mutex_);

        auto it = symbol_cache_.find(name);
        if (it != symbol_cache_.end()) {
            return it->second;
        }

        void* sym = pbox_dlsym(box_, name);
        if (sym) {
            symbol_cache_[name] = sym;
        }
        return sym;
    }

    PBox* box_ = nullptr;
    std::unordered_map<const char*, void*> symbol_cache_;
    std::mutex cache_mutex_;
};

// Process CallContext - uses identity-mapped arena
template<>
class CallContext<Process> {
    Sandbox<Process>* sandbox_;
    std::vector<std::function<void()>> copybacks_;
    bool finalized_ = false;

public:
    explicit CallContext(Sandbox<Process>& sb) : sandbox_(&sb) {
    }

    ~CallContext() {
        finalize();
        sandbox_->idmem_reset();
    }

    CallContext(const CallContext&) = delete;
    CallContext& operator=(const CallContext&) = delete;

    // Execute all copy-backs (idempotent)
    void finalize() {
        if (finalized_)
            return;
        finalized_ = true;
        for (auto& cb : copybacks_) {
            cb();
        }
    }

    // Out: allocate from idmem, register copy-back
    template<typename T>
    T* out(T& host_ref) {
        T* idmem_ptr = sandbox_->template idmem_alloc<T>();
        if (!idmem_ptr)
            throw std::runtime_error("idmem_alloc failed");
        T* host_ptr = &host_ref;
        copybacks_.push_back(
            [host_ptr, idmem_ptr]() { *host_ptr = *idmem_ptr; });
        return idmem_ptr;
    }

    // In: allocate from idmem, copy value in
    template<typename T>
    const T* in(const T& host_ref) {
        T* idmem_ptr = sandbox_->template idmem_alloc<T>();
        if (!idmem_ptr)
            throw std::runtime_error("idmem_alloc failed");
        *idmem_ptr = host_ref;
        return idmem_ptr;
    }

    // InOut: allocate from idmem, copy in, register copy-back
    template<typename T>
    T* inout(T& host_ref) {
        T* idmem_ptr = sandbox_->template idmem_alloc<T>();
        if (!idmem_ptr)
            throw std::runtime_error("idmem_alloc failed");
        T* host_ptr = &host_ref;
        *idmem_ptr = host_ref;
        copybacks_.push_back(
            [host_ptr, idmem_ptr]() { *host_ptr = *idmem_ptr; });
        return idmem_ptr;
    }
};

// Deferred method definitions (need CallContext to be complete)
inline CallContext<Process> Sandbox<Process>::context() {
    return CallContext<Process>(*this);
}

template<typename Sig, typename... Args>
auto Sandbox<Process>::call(CallContext<Process>& ctx, const char* name,
                            Args... args) {
    using Ret = detail::sig_return_t<Sig>;
    if constexpr (std::is_void_v<Ret>) {
        this->template call<Sig>(name, args...);
        ctx.finalize();
    } else {
        auto result = this->template call<Sig>(name, args...);
        ctx.finalize();
        return result;
    }
}

template<typename Ret, typename... Params, typename... Args>
auto Sandbox<Process>::call(CallContext<Process>& ctx,
                            TypedName<Ret (*)(Params...)> tn, Args... args) {
    static_assert(sizeof...(Params) == sizeof...(Args),
                  "Wrong number of arguments for sandboxed function");
    static_assert(
        (detail::check_sbox_ptr_arg_v<Params, Args> && ...),
        "Pointer arguments must be sbox<T*> or sbox_safe<T*> with a "
        "matching type");
    return call<Ret(Params...)>(ctx, tn.name, args...);
}

}  // namespace sbox
