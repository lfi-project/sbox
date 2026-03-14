#pragma once

#include "sbox.hh"

#ifdef SBOX_STATIC
#error "SBOX_STATIC cannot be used with the LFI backend"
#endif

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <type_traits>
#include <sys/mman.h>
#include <unistd.h>
#include <unordered_map>
#include <vector>

extern "C" {
#include "lfi_arch.h"
#include "lfi_core.h"
#include "lfi_linux.h"

void lfi_clone(struct LFIBox* box, struct LFIContext** ctxp);
}

namespace sbox {

namespace detail {

// Maximum number of arguments supported for LFI calls.
constexpr size_t max_args = 10;

// Classify whether an argument is float/double
template<typename T>
constexpr bool is_float_arg =
    std::is_floating_point_v<std::remove_cv_t<std::remove_reference_t<T>>>;

#if defined(__x86_64__)
constexpr size_t max_int_reg_args = 6;
constexpr size_t max_float_reg_args = 8;

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

inline void set_float_arg(LFIRegs* regs, size_t index, uint64_t value) {
    regs->xmm[index * 2] = value;
}

inline uint64_t get_int_return(LFIRegs* regs) { return regs->rax; }
inline uint64_t get_float_return(LFIRegs* regs) { return regs->xmm[0]; }
inline uint64_t get_sp(LFIRegs* regs) { return regs->rsp; }
inline void set_sp(LFIRegs* regs, uint64_t sp) { regs->rsp = sp; }

#elif defined(__aarch64__)
constexpr size_t max_int_reg_args = 8;
constexpr size_t max_float_reg_args = 8;

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

inline void set_float_arg(LFIRegs* regs, size_t index, uint64_t value) {
    regs->vector[index * 2] = value;
}

inline uint64_t get_int_return(LFIRegs* regs) { return regs->x0; }
inline uint64_t get_float_return(LFIRegs* regs) { return regs->vector[0]; }
inline uint64_t get_sp(LFIRegs* regs) { return regs->sp; }
inline void set_sp(LFIRegs* regs, uint64_t sp) { regs->sp = sp; }

#else
#error "Unsupported architecture for LFI backend"
#endif

// Collects overflow args that don't fit in registers.
struct MarshalResult {
    uint64_t stack_args[max_args];
    size_t n_stack = 0;
};

// Marshal a single argument into a register or the stack overflow buffer.
template<typename T>
void marshal_arg(LFIRegs* regs, MarshalResult& result,
                 size_t& int_idx, size_t& float_idx, T arg) {
    uint64_t val = 0;
    if constexpr (is_float_arg<T>) {
        std::memcpy(&val, &arg, sizeof(arg));
        if (float_idx < max_float_reg_args) {
            set_float_arg(regs, float_idx++, val);
        } else {
            result.stack_args[result.n_stack++] = val;
        }
    } else {
        if constexpr (std::is_pointer_v<T>) {
            val = reinterpret_cast<uint64_t>(arg);
        } else {
            std::memcpy(&val, &arg, sizeof(arg));
        }
        if (int_idx < max_int_reg_args) {
            set_int_arg(regs, int_idx++, val);
        } else {
            result.stack_args[result.n_stack++] = val;
        }
    }
}

// Marshal all arguments. Returns info about any overflow stack args.
template<typename... Args>
MarshalResult marshal_args_lfi([[maybe_unused]] LFIRegs* regs, Args... args) {
    static_assert(sizeof...(Args) <= max_args,
                  "Too many arguments (max is sbox::detail::max_args)");
    MarshalResult result{};
    [[maybe_unused]] size_t int_idx = 0;
    [[maybe_unused]] size_t float_idx = 0;
    (marshal_arg(regs, result, int_idx, float_idx, args), ...);
    return result;
}

// Write overflow stack args directly onto the sandbox stack and adjust RSP.
// This works because LFI sandbox memory is in the host address space.
// The trampoline will load this adjusted RSP, align it (no-op since we
// pre-align to 16), push the return address, and jump to the target.
// The callee sees stack args at RSP+8, RSP+16, etc. -- standard ABI layout.
inline void stage_stack_args(LFIRegs* regs, const uint64_t* stack_args,
                             size_t n_stack) {
    if (n_stack == 0)
        return;

    uint64_t rsp = get_sp(regs);
    uint64_t aligned = (rsp - 8 * n_stack) & ~0xFULL;

    for (size_t i = 0; i < n_stack; i++) {
        *reinterpret_cast<uint64_t*>(aligned + 8 * i) = stack_args[i];
    }

    set_sp(regs, aligned);
}

}  // namespace detail

// Process-global LFI engine manager. Optionally call init() before creating
// sandboxes to set the capacity. If not called, the first Sandbox<LFI>
// creates a default engine with capacity 1.
class LFIManager {
    static inline std::mutex mutex_;
    static inline struct LFIEngine* engine_ = nullptr;
    static inline struct LFILinuxEngine* linux_engine_ = nullptr;

public:
    // Pre-initialize the engine with room for 'n' sandboxes.
    // Must be called before any Sandbox<LFI> is created.
    static void init(size_t n) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (engine_) {
            fprintf(stderr, "sbox: LFIManager already initialized\n");
            abort();
        }
        create(n);
    }

    // Tear down the engine. All sandboxes must be destroyed first.
    static void destroy() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (linux_engine_) {
            lfi_linux_free(linux_engine_);
            linux_engine_ = nullptr;
        }
        if (engine_) {
            lfi_free(engine_);
            engine_ = nullptr;
        }
    }

private:
    friend class Sandbox<LFI>;

    static void ensure() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!engine_) {
            create(1);
        }
    }

    static void create(size_t n) {
        const char* dir_maps[] = {nullptr};

        engine_ = lfi_new(
            {
                .pagesize = static_cast<size_t>(getpagesize()),
                .boxsize = 4ULL * 1024 * 1024 * 1024,
                .verbose = false,
                .stores_only = false,
                .no_verify = false,
                .allow_wx = false,
                .no_init_sigaltstack = false,
                .no_rtcall_nullpage = false,
            },
            n);
        if (!engine_) {
            fprintf(stderr, "sbox: failed to create LFI engine: %s\n",
                    lfi_errmsg());
            abort();
        }

        linux_engine_ = lfi_linux_new(engine_,
                                       {
                                           .stacksize = 2 * 1024 * 1024,
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
            engine_ = nullptr;
            fprintf(stderr, "sbox: failed to create LFI Linux engine: %s\n",
                    lfi_errmsg());
            abort();
        }
    }

    static struct LFILinuxEngine* get() {
        ensure();
        return linux_engine_;
    }
};

// LFI backend - sandboxes library using LFI memory isolation
template<>
class Sandbox<LFI> {
    LFILinuxProc* proc_ = nullptr;
    LFILinuxThread* main_thread_ = nullptr;
    LFIBox* box_ = nullptr;

    std::mutex symbol_cache_mutex_;
    std::unordered_map<std::string, lfiptr> symbol_cache_;

    mutable std::thread::id main_thread_tid_;

public:
    explicit Sandbox(const char* library_path) {
        const char* envp[] = {nullptr};
        LFILinuxEngine* linux_engine = LFIManager::get();

        proc_ = lfi_proc_new(linux_engine);
        if (!proc_) {
            fprintf(stderr, "sbox: failed to create LFI proc: %s\n",
                    lfi_errmsg());
            abort();
        }

        if (!lfi_proc_load_file(proc_, library_path)) {
            lfi_proc_free(proc_);
            fprintf(stderr, "sbox: failed to load LFI binary: %s\n",
                    lfi_errmsg());
            abort();
        }

        box_ = lfi_proc_box(proc_);
        lfiptr lfi_ret = lfi_proc_sym(proc_, "_lfi_ret");
        if (!lfi_ret) {
            lfi_proc_free(proc_);
            fprintf(stderr,
                    "sbox: _lfi_ret not found - link with -lboxrt\n");
            abort();
        }
        lfi_box_register_ret(box_, lfi_ret);

        const char* argv[] = {library_path, nullptr};
        main_thread_ = lfi_thread_new(proc_, 0, argv, envp);
        if (!main_thread_) {
            lfi_proc_free(proc_);
            fprintf(stderr, "sbox: failed to create LFI thread: %s\n",
                    lfi_errmsg());
            abort();
        }
        int run_result = lfi_thread_run(main_thread_);
        if (run_result != 0) {
            lfi_thread_free(main_thread_);
            lfi_proc_free(proc_);
            fprintf(stderr,
                    "sbox: LFI thread init failed with code %d\n",
                    run_result);
            abort();
        }

        lfi_linux_init_clone(main_thread_);
        main_thread_tid_ = std::this_thread::get_id();
    }

    ~Sandbox() {
        if (main_thread_)
            lfi_thread_free(main_thread_);
        if (proc_)
            lfi_proc_free(proc_);
    }

    Sandbox(const Sandbox&) = delete;
    Sandbox& operator=(const Sandbox&) = delete;
    Sandbox(Sandbox&&) = delete;
    Sandbox& operator=(Sandbox&&) = delete;

    // Call a function by name
    template<typename Sig, typename... Args>
    auto call(const char* name, Args... args) {
        void* fn = reinterpret_cast<void*>(lookup(name));
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

    // Get a function handle for repeated calls
    template<typename Sig>
    FnHandle<LFI, Sig> fn(const char* name) {
        return FnHandle<LFI, Sig>(*this, reinterpret_cast<void*>(lookup(name)));
    }

    // Get a function handle with TypedName
    template<typename Ret, typename... Params>
    FnHandle<LFI, Ret(Params...)> fn(TypedName<Ret (*)(Params...)> tn) {
        return FnHandle<LFI, Ret(Params...)>(
            *this, reinterpret_cast<void*>(lookup(tn.name)));
    }

    // Call via function pointer (used by FnHandle)
    template<typename Ret, typename... Args>
    Ret call_ptr(void* fn_ptr, Args... args) {
        detail::tls_current_sandbox = this;

        auto ctxp = get_thread_ctx();
        if (*ctxp == nullptr) {
            lfi_clone(box_, ctxp);
            if (*ctxp == nullptr) {
                fprintf(stderr, "sbox: lfi_clone failed for worker thread\n");
                abort();
            }
        }

        lfi_invoke_info = {
            .ctx = ctxp,
            .targetfn = reinterpret_cast<lfiptr>(fn_ptr),
            .box = box_,
        };

        LFIRegs* regs = lfi_ctx_regs(*ctxp);
        auto result = detail::marshal_args_lfi(regs, args...);

        // Save sandbox RSP before staging. The trampoline saves/restores
        // its own copy, but it saves the already-staged value. We restore
        // to the pre-staging value so that CallContext stack allocations
        // remain valid.
        uint64_t saved_sp = detail::get_sp(regs);
        detail::stage_stack_args(regs, result.stack_args, result.n_stack);

        lfi_trampoline_struct();

        detail::set_sp(regs, saved_sp);

        if constexpr (!std::is_void_v<Ret>) {
            Ret ret;
            if constexpr (std::is_floating_point_v<Ret>) {
                uint64_t raw = detail::get_float_return(regs);
                std::memcpy(&ret, &raw, sizeof(ret));
            } else if constexpr (std::is_pointer_v<Ret>) {
                ret = reinterpret_cast<Ret>(detail::get_int_return(regs));
            } else {
                uint64_t raw = detail::get_int_return(regs);
                std::memcpy(&ret, &raw, sizeof(ret));
            }
            return ret;
        }
    }

    // -- Memory allocation --
    // Sandbox memory is in the host address space, so returns sbox_safe.

    template<typename T>
    sbox_safe<T*> alloc(size_t count = 1) {
        return sbox_safe<T*>(static_cast<T*>(
            lfi_lib_malloc(box_, get_thread_ctx(), sizeof(T) * count)));
    }

    template<typename T>
    sbox_safe<T*> calloc(size_t count) {
        return sbox_safe<T*>(static_cast<T*>(
            lfi_lib_calloc(box_, get_thread_ctx(), count, sizeof(T))));
    }

    template<typename T>
    sbox_safe<T*> realloc(sbox_safe<T*> ptr, size_t count) {
        return sbox_safe<T*>(static_cast<T*>(
            lfi_lib_realloc(box_, get_thread_ctx(), ptr.data(), sizeof(T) * count)));
    }

    void free(void* ptr) {
        lfi_lib_free(box_, get_thread_ctx(), ptr);
    }

    template<typename T>
    void free(sbox<T*> p) {
        lfi_lib_free(box_, get_thread_ctx(), p.unsafe_unverified());
    }

    template<typename T>
    void free(sbox_safe<T*> p) {
        free(sbox<T*>(p));
    }

    // Identity-mapped allocation. For LFI, sandbox memory is already
    // host-accessible, so this uses the sandbox allocator. Tracked for
    // idmem_reset.
    template<typename T>
    T* idmem_alloc(size_t count = 1) {
        void* p = lfi_lib_malloc(box_, get_thread_ctx(), sizeof(T) * count);
        std::lock_guard<std::mutex> lock(idmem_mutex_);
        idmem_allocations_.push_back(p);
        return static_cast<T*>(p);
    }

    void idmem_reset() {
        std::lock_guard<std::mutex> lock(idmem_mutex_);
        for (void* p : idmem_allocations_) {
            lfi_lib_free(box_, get_thread_ctx(), p);
        }
        idmem_allocations_.clear();
    }

    // -- Pointer verification --

    template<typename T>
    sbox_safe<T*> verify(sbox<T*> ptr, size_t count) {
        T* raw = ptr.unsafe_unverified();
        if (raw && !lfi_box_bufvalid(box_, reinterpret_cast<lfiptr>(raw),
                                      sizeof(T) * count)) {
            fprintf(stderr,
                    "sbox: verify failed: pointer %p not in sandbox\n",
                    static_cast<void*>(raw));
            abort();
        }
        return sbox_safe<T*>(raw);
    }

    // -- Data transfer (trivial - shared address space) --

    void copy_to(void* sandbox_dest, const void* host_src, size_t n) {
        std::memcpy(sandbox_dest, host_src, n);
    }

    template<typename T>
    void copy_to(sbox<T*> sandbox_dest, const void* host_src, size_t n) {
        std::memcpy(sandbox_dest.unsafe_unverified(), host_src, n);
    }

    template<typename T>
    void copy_to(sbox_safe<T*> d, const void* s, size_t n) {
        copy_to(sbox<T*>(d), s, n);
    }

    void copy_from(void* host_dest, const void* sandbox_src, size_t n) {
        std::memcpy(host_dest, sandbox_src, n);
    }

    template<typename T>
    void copy_from(void* host_dest, sbox<T*> sandbox_src, size_t n) {
        std::memcpy(host_dest, sandbox_src.unsafe_unverified(), n);
    }

    template<typename T>
    void copy_from(void* d, sbox_safe<T*> s, size_t n) {
        copy_from(d, sbox<T*>(s), n);
    }

    sbox_safe<char*> copy_string(const char* s) {
        size_t len = std::strlen(s) + 1;
        auto buf = alloc<char>(len);
        if (!buf)
            return {};
        copy_to(buf, s, len);
        return buf;
    }

    // -- Memory mapping --

    void* mmap(void* addr, size_t length, int prot, int flags, int fd,
               off_t offset) {
        int lfi_prot = 0;
        if (prot & PROT_READ) lfi_prot |= LFI_PROT_READ;
        if (prot & PROT_WRITE) lfi_prot |= LFI_PROT_WRITE;
        if (prot & PROT_EXEC) lfi_prot |= LFI_PROT_EXEC;

        int lfi_flags = 0;
        if (flags & MAP_SHARED) lfi_flags |= LFI_MAP_SHARED;
        if (flags & MAP_PRIVATE) lfi_flags |= LFI_MAP_PRIVATE;
        if (flags & MAP_FIXED) lfi_flags |= LFI_MAP_FIXED;
        if (flags & MAP_ANONYMOUS) lfi_flags |= LFI_MAP_ANONYMOUS;

        lfiptr result;
        if (flags & MAP_FIXED) {
            result = lfi_box_mapat(box_, reinterpret_cast<lfiptr>(addr),
                                   length, lfi_prot, lfi_flags, fd, offset);
        } else {
            result = lfi_box_mapany(box_, length, lfi_prot, lfi_flags, fd, offset);
        }
        return reinterpret_cast<void*>(result);
    }

    int munmap(void* addr, size_t length) {
        return lfi_box_munmap(box_, reinterpret_cast<lfiptr>(addr), length);
    }

    // -- Callbacks --

    template<typename Ret, typename... Args>
    sbox<Ret (*)(Args...)> register_callback(Ret (*fn)(Args...)) {
        constexpr size_t n_int = ((detail::is_float_arg<Args> ? 0 : 1) + ... + 0);
        constexpr size_t n_float = ((detail::is_float_arg<Args> ? 1 : 0) + ... + 0);
        static_assert(n_int <= detail::max_int_reg_args && n_float <= detail::max_float_reg_args,
                      "LFI callbacks do not support stack arguments");
        void* raw = lfi_box_register_cb(box_, reinterpret_cast<void*>(fn));
        return sbox<Ret (*)(Args...)>(
            reinterpret_cast<Ret (*)(Args...)>(raw));
    }

    template<auto fn>
    auto register_callback() {
        return register_callback(
            &detail::callback_thunk_impl<decltype(fn), fn>::call);
    }

    // -- Stack allocation (used by CallContext) --

    void* stack_push(size_t size, size_t align = 16) {
        LFIContext* ctx = *get_thread_ctx();
        LFIRegs* regs = lfi_ctx_regs(ctx);
        uint64_t sp = detail::get_sp(regs);
        sp = (sp - size) & ~(align - 1);
        detail::set_sp(regs, sp);
        return reinterpret_cast<void*>(sp);
    }

    uint64_t stack_save() {
        LFIContext* ctx = *get_thread_ctx();
        return detail::get_sp(lfi_ctx_regs(ctx));
    }

    void stack_restore(uint64_t sp) {
        LFIContext* ctx = *get_thread_ctx();
        detail::set_sp(lfi_ctx_regs(ctx), sp);
    }

    // Context-aware calls (defined after CallContext)
    template<typename Sig, typename... Args>
    auto call(CallContext<LFI>& ctx, const char* name, Args... args);

    template<typename Ret, typename... Params, typename... Args>
    auto call(CallContext<LFI>& ctx, TypedName<Ret (*)(Params...)> tn,
              Args... args);

    inline CallContext<LFI> context();

    LFIBox* native_handle() const { return box_; }
    LFILinuxProc* proc() const { return proc_; }

private:
    std::vector<void*> idmem_allocations_;
    std::mutex idmem_mutex_;
    template<typename To, typename From>
    static To convert_arg(From arg) {
        if constexpr (detail::is_sbox_ptr_v<From>) {
            return reinterpret_cast<To>(arg.unsafe_unverified());
        } else if constexpr (detail::is_sbox_safe_ptr_v<From>) {
            return reinterpret_cast<To>(arg.data());
        } else if constexpr (std::is_pointer_v<To> &&
                             std::is_pointer_v<From>) {
            return reinterpret_cast<To>(arg);
        } else {
            return static_cast<To>(arg);
        }
    }

    template<typename Ret, typename... Params, typename... Args>
    Ret call_with_sig_impl(void* fn, Ret (*)(Params...), Args... args) {
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
            fprintf(stderr, "sbox: symbol not found: %s\n", name);
            abort();
        }
        symbol_cache_[name] = sym;
        return sym;
    }

    struct ThreadCtxEntry {
        LFIBox* box;
        LFIContext* ctx;
    };

    LFIContext** get_thread_ctx() {
        static thread_local std::deque<ThreadCtxEntry> entries;

        for (auto& e : entries) {
            if (e.box == box_) return &e.ctx;
        }
        entries.push_back({box_, nullptr});
        auto& e = entries.back();
        if (main_thread_tid_ == std::this_thread::get_id()) {
            e.ctx = *lfi_thread_ctxp(main_thread_);
        }
        return &e.ctx;
    }
};

// LFI CallContext - uses sandbox stack for in/out/inout parameters
template<>
class CallContext<LFI> {
    Sandbox<LFI>* sandbox_;
    std::vector<std::function<void()>> copybacks_;
    uint64_t saved_sp_;
    bool finalized_ = false;

public:
    explicit CallContext(Sandbox<LFI>& sb)
        : sandbox_(&sb), saved_sp_(sb.stack_save()) {}

    ~CallContext() {
        finalize();
        sandbox_->stack_restore(saved_sp_);
    }

    CallContext(const CallContext&) = delete;
    CallContext& operator=(const CallContext&) = delete;

    void finalize() {
        if (finalized_)
            return;
        finalized_ = true;
        for (auto& cb : copybacks_) {
            cb();
        }
    }

    template<typename T>
    T* out(T& host_ref) {
        T* sbox_ptr = static_cast<T*>(sandbox_->stack_push(sizeof(T), alignof(T)));
        T* host_ptr = &host_ref;
        copybacks_.push_back(
            [host_ptr, sbox_ptr]() { *host_ptr = *sbox_ptr; });
        return sbox_ptr;
    }

    template<typename T>
    const T* in(const T& host_ref) {
        T* sbox_ptr = static_cast<T*>(sandbox_->stack_push(sizeof(T), alignof(T)));
        *sbox_ptr = host_ref;
        return sbox_ptr;
    }

    template<typename T>
    T* inout(T& host_ref) {
        T* sbox_ptr = static_cast<T*>(sandbox_->stack_push(sizeof(T), alignof(T)));
        T* host_ptr = &host_ref;
        *sbox_ptr = host_ref;
        copybacks_.push_back(
            [host_ptr, sbox_ptr]() { *host_ptr = *sbox_ptr; });
        return sbox_ptr;
    }
};

inline CallContext<LFI> Sandbox<LFI>::context() {
    return CallContext<LFI>(*this);
}

template<typename Sig, typename... Args>
auto Sandbox<LFI>::call(CallContext<LFI>& ctx, const char* name,
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
auto Sandbox<LFI>::call(CallContext<LFI>& ctx,
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
