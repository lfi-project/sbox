#pragma once

#include "sbox.hh"

#include <cstdlib>
#include <cstring>
#include <functional>
#include <mutex>
#include <thread>
#include <type_traits>
#include <unistd.h>
#include <unordered_map>
#include <vector>

extern "C" {
#include "lfi_arch.h"
#include "lfi_core.h"
#include "lfi_linux.h"

// Forward declaration for clone function (used to create contexts for new threads)
void lfi_clone(struct LFIBox* box, struct LFIContext** ctxp);
}

namespace sbox {

namespace detail {

// Count integer/pointer args vs float args
template<typename T>
constexpr bool is_float_arg = std::is_floating_point_v<std::remove_cv_t<std::remove_reference_t<T>>>;

template<typename... Args>
constexpr size_t count_int_args = ((is_float_arg<Args> ? 0 : 1) + ... + 0);

template<typename... Args>
constexpr size_t count_float_args = ((is_float_arg<Args> ? 1 : 0) + ... + 0);

#if defined(__x86_64__)
constexpr size_t max_int_reg_args = 6;
constexpr size_t max_float_reg_args = 8;

// Set integer argument in the appropriate register
inline void set_int_arg(LFIRegs* regs, size_t index, uint64_t value) {
    switch (index) {
        case 0: regs->rdi = value; break;
        case 1: regs->rsi = value; break;
        case 2: regs->rdx = value; break;
        case 3: regs->rcx = value; break;
        case 4: regs->r8 = value; break;
        case 5: regs->r9 = value; break;
    }
}

// Set float argument in the appropriate XMM register
inline void set_float_arg(LFIRegs* regs, size_t index, uint64_t value) {
    // XMM registers are at indices 0, 2, 4, 6, 8, 10, 12, 14
    regs->xmm[index * 2] = value;
}

// Get integer return value
inline uint64_t get_int_return(LFIRegs* regs) {
    return regs->rax;
}

// Get float return value
inline uint64_t get_float_return(LFIRegs* regs) {
    return regs->xmm[0];
}

#elif defined(__aarch64__)
constexpr size_t max_int_reg_args = 8;
constexpr size_t max_float_reg_args = 8;

// Set integer argument in the appropriate register
inline void set_int_arg(LFIRegs* regs, size_t index, uint64_t value) {
    switch (index) {
        case 0: regs->x0 = value; break;
        case 1: regs->x1 = value; break;
        case 2: regs->x2 = value; break;
        case 3: regs->x3 = value; break;
        case 4: regs->x4 = value; break;
        case 5: regs->x5 = value; break;
        case 6: regs->x6 = value; break;
        case 7: regs->x7 = value; break;
    }
}

// Set float argument in the appropriate vector register
inline void set_float_arg(LFIRegs* regs, size_t index, uint64_t value) {
    regs->vector[index * 2] = value;
}

// Get integer return value
inline uint64_t get_int_return(LFIRegs* regs) {
    return regs->x0;
}

// Get float return value
inline uint64_t get_float_return(LFIRegs* regs) {
    return regs->vector[0];
}

#else
#error "Unsupported architecture"
#endif

// Helper to marshal a single argument into registers
template<typename T>
void marshal_arg(LFIRegs* regs, size_t& int_idx, size_t& float_idx, T arg) {
    if constexpr (is_float_arg<T>) {
        uint64_t val = 0;
        std::memcpy(&val, &arg, sizeof(arg));
        set_float_arg(regs, float_idx++, val);
    } else {
        uint64_t val = 0;
        if constexpr (std::is_pointer_v<T>) {
            val = reinterpret_cast<uint64_t>(arg);
        } else {
            std::memcpy(&val, &arg, sizeof(arg));
        }
        set_int_arg(regs, int_idx++, val);
    }
}

// Marshal all arguments into registers
template<typename... Args>
void marshal_args(LFIRegs* regs, Args... args) {
    size_t int_idx = 0;
    size_t float_idx = 0;
    (marshal_arg(regs, int_idx, float_idx, args), ...);
}

} // namespace detail

// Forward declaration - CallContext defined after Sandbox
template<>
class CallContext<LFI>;

// LFI backend - sandboxes library using LFI memory isolation
template<>
class Sandbox<LFI> {
    // Core LFI objects
    LFIEngine* engine_ = nullptr;
    LFILinuxEngine* linux_engine_ = nullptr;
    LFILinuxProc* proc_ = nullptr;
    LFILinuxThread* main_thread_ = nullptr;
    LFIBox* box_ = nullptr;

    // Symbol cache
    std::mutex symbol_cache_mutex_;
    std::unordered_map<std::string, lfiptr> symbol_cache_;

    // Thread tracking for per-thread contexts
    mutable std::thread::id main_thread_tid_;

public:
    explicit Sandbox(const char* library_path) {
        // Empty arrays (not nullptr) for dir_maps and envp
        const char* dir_maps[] = { nullptr };
        const char* envp[] = { nullptr };

        // 1. Create LFI engine
        engine_ = lfi_new({
            .pagesize = static_cast<size_t>(getpagesize()),
            .boxsize = 4ULL * 1024 * 1024 * 1024,  // 4GB
            .verbose = false,
            .stores_only = false,
            .no_verify = false,
            .allow_wx = false,
            .no_init_sigaltstack = false,
            .no_rtcall_nullpage = false,
        }, 1);
        if (!engine_) {
            throw std::runtime_error(std::string("Failed to create LFI engine: ") + lfi_errmsg());
        }

        // 2. Create Linux engine
        linux_engine_ = lfi_linux_new(engine_, {
            .stacksize = 2 * 1024 * 1024,  // 2MB
            .verbose = false,
            .perf = false,
            .dir_maps = dir_maps,
            .wd = nullptr,
            .exit_unknown_syscalls = false,
            .sys_passthrough = false,
            .debug = false,
            .brk_control = false,
            .brk_size = 0,
        });
        if (!linux_engine_) {
            lfi_free(engine_);
            throw std::runtime_error(std::string("Failed to create LFI Linux engine: ") + lfi_errmsg());
        }

        // 3. Create proc and load binary
        proc_ = lfi_proc_new(linux_engine_);
        if (!proc_) {
            lfi_linux_free(linux_engine_);
            lfi_free(engine_);
            throw std::runtime_error(std::string("Failed to create LFI proc: ") + lfi_errmsg());
        }

        if (!lfi_proc_load_file(proc_, library_path)) {
            lfi_proc_free(proc_);
            lfi_linux_free(linux_engine_);
            lfi_free(engine_);
            throw std::runtime_error(std::string("Failed to load LFI binary: ") + lfi_errmsg());
        }

        // 4. Initialize return trampoline (use _lfi_ret from boxrt)
        box_ = lfi_proc_box(proc_);
        lfiptr lfi_ret = lfi_proc_sym(proc_, "_lfi_ret");
        if (!lfi_ret) {
            lfi_proc_free(proc_);
            lfi_linux_free(linux_engine_);
            lfi_free(engine_);
            throw std::runtime_error("_lfi_ret symbol not found - library must be linked with -lboxrt");
        }
        lfi_box_register_ret(box_, lfi_ret);

        // 5. Create main thread and run init (argc=0 per rlbox convention)
        const char* argv[] = { library_path, nullptr };
        main_thread_ = lfi_thread_new(proc_, 0, argv, envp);
        if (!main_thread_) {
            lfi_proc_free(proc_);
            lfi_linux_free(linux_engine_);
            lfi_free(engine_);
            throw std::runtime_error(std::string("Failed to create LFI thread: ") + lfi_errmsg());
        }
        int run_result = lfi_thread_run(main_thread_);
        if (run_result != 0) {
            lfi_thread_free(main_thread_);
            lfi_proc_free(proc_);
            lfi_linux_free(linux_engine_);
            lfi_free(engine_);
            throw std::runtime_error("LFI thread initialization failed with code: " + std::to_string(run_result));
        }

        // 6. Enable multi-threaded calls
        lfi_linux_init_clone(main_thread_);

        // 7. Remember which thread created the sandbox (for get_thread_ctx)
        main_thread_tid_ = std::this_thread::get_id();
    }

    ~Sandbox() {
        if (main_thread_) lfi_thread_free(main_thread_);
        if (proc_) lfi_proc_free(proc_);
        if (linux_engine_) lfi_linux_free(linux_engine_);
        if (engine_) lfi_free(engine_);
    }

    // Non-copyable
    Sandbox(const Sandbox&) = delete;
    Sandbox& operator=(const Sandbox&) = delete;

    // Call a function by name
    template<typename Sig, typename... Args>
    auto call(const char* name, Args... args) {
        void* fn = reinterpret_cast<void*>(lookup(name));
        return call_ptr_sig<Sig>(fn, args...);
    }

    // Context-aware call by name
    template<typename Sig, typename... Args>
    auto call(CallContext<LFI>& ctx, const char* name, Args... args)
        -> decltype(this->template call<Sig>(name, args...)) {
        if constexpr (std::is_void_v<decltype(this->template call<Sig>(name, args...))>) {
            this->template call<Sig>(name, args...);
            ctx.finalize();
        } else {
            auto result = this->template call<Sig>(name, args...);
            ctx.finalize();
            return result;
        }
    }

    // Create a call context for in/out/inout parameters (defined after CallContext)
    inline CallContext<LFI> context();

    // Get a function handle for repeated calls
    template<typename Sig>
    FnHandle<LFI, Sig> fn(const char* name) {
        return FnHandle<LFI, Sig>(*this, reinterpret_cast<void*>(lookup(name)));
    }

    // Call via function pointer (used by FnHandle)
    template<typename Ret, typename... Args>
    Ret call_ptr(void* fn_ptr, Args... args) {
        // Static assert: all args must fit in registers
        static_assert(detail::count_int_args<Args...> <= detail::max_int_reg_args,
            "Too many integer/pointer arguments - max is 6 (x86_64) or 8 (arm64)");
        static_assert(detail::count_float_args<Args...> <= detail::max_float_reg_args,
            "Too many floating-point arguments - max is 8");

        // Set up invocation info
        auto ctxp = get_thread_ctx();

        // If context is nullptr (new worker thread), trigger the clone callback
        // BEFORE marshaling arguments. The clone callback creates the context.
        if (*ctxp == nullptr) {
            lfi_clone(box_, ctxp);
        }

        lfi_invoke_info = {
            .ctx = ctxp,
            .targetfn = reinterpret_cast<lfiptr>(fn_ptr),
            .box = box_,
        };

        // Get registers and marshal arguments (now safe since context exists)
        LFIRegs* regs = lfi_ctx_regs(*ctxp);
        detail::marshal_args(regs, args...);

        // Call the trampoline
        lfi_trampoline_struct();

        // Extract return value
        if constexpr (!std::is_void_v<Ret>) {
            Ret result;
            if constexpr (std::is_floating_point_v<Ret>) {
                uint64_t raw = detail::get_float_return(regs);
                std::memcpy(&result, &raw, sizeof(result));
            } else if constexpr (std::is_pointer_v<Ret>) {
                result = reinterpret_cast<Ret>(detail::get_int_return(regs));
            } else {
                uint64_t raw = detail::get_int_return(regs);
                std::memcpy(&result, &raw, sizeof(result));
            }
            return result;
        }
    }

    // Memory allocation within the sandbox
    template<typename T>
    T* alloc(size_t count = 1) {
        return static_cast<T*>(lfi_lib_malloc(box_, get_thread_ctx(), sizeof(T) * count));
    }

    template<typename T>
    T* calloc(size_t count) {
        return static_cast<T*>(lfi_lib_calloc(box_, get_thread_ctx(), count, sizeof(T)));
    }

    template<typename T>
    T* realloc(T* ptr, size_t count) {
        (void)ptr; // lfi_lib_realloc doesn't take existing pointer
        return static_cast<T*>(lfi_lib_realloc(box_, get_thread_ctx(), sizeof(T) * count));
    }

    void free(void* ptr) {
        lfi_lib_free(box_, get_thread_ctx(), ptr);
    }

    // Memory mapping
    void* mmap(void* addr, size_t length, int prot, int flags, int fd, off_t offset) {
        (void)addr;
        int lfi_prot = 0;
        if (prot & 0x1) lfi_prot |= LFI_PROT_READ;
        if (prot & 0x2) lfi_prot |= LFI_PROT_WRITE;
        if (prot & 0x4) lfi_prot |= LFI_PROT_EXEC;

        int lfi_flags = 0;
        if (flags & 0x01) lfi_flags |= LFI_MAP_SHARED;
        if (flags & 0x02) lfi_flags |= LFI_MAP_PRIVATE;
        if (flags & 0x10) lfi_flags |= LFI_MAP_FIXED;
        if (flags & 0x20) lfi_flags |= LFI_MAP_ANONYMOUS;

        return reinterpret_cast<void*>(lfi_box_mapany(box_, length, lfi_prot, lfi_flags, fd, offset));
    }

    int munmap(void* addr, size_t length) {
        return lfi_box_munmap(box_, reinterpret_cast<lfiptr>(addr), length);
    }

    // Data transfer (trivial - shared address space)
    void copy_to(void* sandbox_dest, const void* host_src, size_t n) {
        std::memcpy(sandbox_dest, host_src, n);
    }

    void copy_from(void* host_dest, const void* sandbox_src, size_t n) {
        std::memcpy(host_dest, sandbox_src, n);
    }

    // String helper
    char* copy_string(const char* s) {
        size_t len = std::strlen(s) + 1;
        char* buf = alloc<char>(len);
        copy_to(buf, s, len);
        return buf;
    }

    // Register a callback
    template<typename Fn>
    void* register_callback(Fn fn) {
        return lfi_box_register_cb(box_, reinterpret_cast<void*>(+fn));
    }

    // Stack allocation - push bytes onto sandbox stack, return pointer
    void* stack_push(size_t size) {
        LFIContext* ctx = *get_thread_ctx();
        LFIRegs* regs = lfi_ctx_regs(ctx);
#if defined(__x86_64__)
        lfiptr sp = regs->rsp;
        regs->rsp = sp - size;
#elif defined(__aarch64__)
        lfiptr sp = regs->sp;
        regs->sp = sp - size;
#endif
        return reinterpret_cast<void*>(sp - size);
    }

    // Stack allocation - pop bytes from sandbox stack
    void stack_pop(size_t size) {
        LFIContext* ctx = *get_thread_ctx();
        LFIRegs* regs = lfi_ctx_regs(ctx);
#if defined(__x86_64__)
        regs->rsp = regs->rsp + size;
#elif defined(__aarch64__)
        regs->sp = regs->sp + size;
#endif
    }

    // Escape hatch for advanced usage
    LFIBox* native_handle() const { return box_; }
    LFILinuxProc* proc() const { return proc_; }

private:
    // Convert argument, using reinterpret_cast for pointer conversions
    template<typename To, typename From>
    static To convert_arg(From arg) {
        if constexpr (std::is_pointer_v<To> && std::is_pointer_v<From>) {
            return reinterpret_cast<To>(arg);
        } else {
            return static_cast<To>(arg);
        }
    }

    // Helper to extract return type from signature and call with argument conversion
    template<typename Ret, typename... Params, typename... Args>
    Ret call_with_sig_impl(void* fn, Ret(*)(Params...), Args... args) {
        return call_ptr<Ret, Params...>(fn, convert_arg<Params>(args)...);
    }

    template<typename Sig, typename... Args>
    auto call_ptr_sig(void* fn, Args... args) {
        return call_with_sig_impl(fn, static_cast<Sig*>(nullptr), args...);
    }

    lfiptr lookup(const char* name) {
        std::lock_guard<std::mutex> lock(symbol_cache_mutex_);

        auto it = symbol_cache_.find(name);
        if (it != symbol_cache_.end()) {
            return it->second;
        }

        lfiptr sym = lfi_proc_sym(proc_, name);
        if (!sym) {
            throw std::runtime_error(std::string("Symbol not found: ") + name);
        }
        symbol_cache_[name] = sym;
        return sym;
    }

    LFIContext** get_thread_ctx() {
        // Each host thread needs its own LFI context. lfi_linux_init_clone
        // sets up a clone callback that creates contexts for new threads.
        //
        // For the main thread (the one that created the sandbox), we copy
        // the context from main_thread_. For worker threads, the context
        // starts as nullptr and is created by lfi_clone() in call_ptr().
        static thread_local LFIContext* tls_ctx = nullptr;

        if (tls_ctx == nullptr && main_thread_tid_ == std::this_thread::get_id()) {
            // Main thread - copy context from main_thread_
            tls_ctx = *lfi_thread_ctxp(main_thread_);
        }
        return &tls_ctx;
    }
};

// LFI CallContext - uses stack allocation for in/out/inout parameters
template<>
class CallContext<LFI> {
    Sandbox<LFI>* sandbox_;
    std::vector<std::function<void()>> copybacks_;
    size_t stack_allocated_ = 0;
    bool finalized_ = false;

public:
    explicit CallContext(Sandbox<LFI>& sb) : sandbox_(&sb) {}

    ~CallContext() {
        finalize();
        // Pop all stack allocations at once
        if (stack_allocated_ > 0) {
            sandbox_->stack_pop(stack_allocated_);
        }
    }

    CallContext(const CallContext&) = delete;
    CallContext& operator=(const CallContext&) = delete;

    // Execute all copy-backs (idempotent)
    void finalize() {
        if (finalized_) return;
        finalized_ = true;
        for (auto& cb : copybacks_) {
            cb();
        }
    }

    // Out: allocate on stack, register copy-back
    template<typename T>
    T* out(T& host_ref) {
        T* sbox_ptr = static_cast<T*>(sandbox_->stack_push(sizeof(T)));
        stack_allocated_ += sizeof(T);
        T* host_ptr = &host_ref;
        copybacks_.push_back([host_ptr, sbox_ptr]() {
            *host_ptr = *sbox_ptr;
        });
        return sbox_ptr;
    }

    // In: allocate on stack, copy value in
    // When SBOX_LFI_LOADS_ALLOWED is defined (LFI configured with stores_only=true),
    // the sandbox can read directly from host memory, so no copy is needed.
    template<typename T>
    const T* in(const T& host_ref) {
#ifdef SBOX_LFI_LOADS_ALLOWED
        // Sandbox can read from host memory directly
        return &host_ref;
#else
        T* sbox_ptr = static_cast<T*>(sandbox_->stack_push(sizeof(T)));
        stack_allocated_ += sizeof(T);
        *sbox_ptr = host_ref;
        return sbox_ptr;
#endif
    }

    // InOut: allocate on stack, copy in, register copy-back
    template<typename T>
    T* inout(T& host_ref) {
        T* sbox_ptr = static_cast<T*>(sandbox_->stack_push(sizeof(T)));
        stack_allocated_ += sizeof(T);
        T* host_ptr = &host_ref;
        *sbox_ptr = host_ref;
        copybacks_.push_back([host_ptr, sbox_ptr]() {
            *host_ptr = *sbox_ptr;
        });
        return sbox_ptr;
    }
};

// Deferred definition of Sandbox<LFI>::context()
inline CallContext<LFI> Sandbox<LFI>::context() {
    return CallContext<LFI>(*this);
}

} // namespace sbox
