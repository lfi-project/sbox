#pragma once

#include "sbox.hh"

#include <dlfcn.h>
#include <sys/mman.h>
#include <unistd.h>
#include <cstdlib>
#include <type_traits>

namespace sbox {

namespace detail {

// Simple arena for passthrough identity memory
class PassthroughArena {
    static constexpr size_t DEFAULT_SIZE = 64 * 1024;  // 64KB default

    void* base_ = nullptr;
    size_t size_ = 0;
    size_t offset_ = 0;

public:
    PassthroughArena() = default;

    ~PassthroughArena() {
        if (base_) {
            ::munmap(base_, size_);
        }
    }

    PassthroughArena(const PassthroughArena&) = delete;
    PassthroughArena& operator=(const PassthroughArena&) = delete;

    void* alloc(size_t size, size_t align = 8) {
        ensure_initialized();

        // Align offset
        offset_ = (offset_ + align - 1) & ~(align - 1);

        if (offset_ + size > size_) {
            return nullptr;  // Arena exhausted
        }

        void* ptr = static_cast<char*>(base_) + offset_;
        offset_ += size;
        return ptr;
    }

    void reset() {
        offset_ = 0;
    }

private:
    void ensure_initialized() {
        if (!base_) {
            size_ = DEFAULT_SIZE;
            base_ = ::mmap(nullptr, size_, PROT_READ | PROT_WRITE,
                           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            if (base_ == MAP_FAILED) {
                base_ = nullptr;
                size_ = 0;
            }
        }
    }
};

// Thread-local arena storage
inline PassthroughArena& get_thread_arena() {
    thread_local PassthroughArena arena;
    return arena;
}

}  // namespace detail

// Passthrough CallContext - just returns pointers to host variables (zero
// overhead) Defined before Sandbox since it doesn't need Sandbox to be complete
template<>
class CallContext<Passthrough> {
    Sandbox<Passthrough>* sandbox_;

public:
    explicit CallContext(Sandbox<Passthrough>& sb) : sandbox_(&sb) {
    }

    // No-op for passthrough
    void finalize() {
    }

    // Just return pointer to host variable
    template<typename T>
    T* out(T& host_ref) {
        return &host_ref;
    }

    template<typename T>
    const T* in(const T& host_ref) {
        return &host_ref;
    }

    template<typename T>
    T* inout(T& host_ref) {
        return &host_ref;
    }
};

// Passthrough backend - loads library normally via dlopen, or static mode
template<>
class Sandbox<Passthrough> {
public:
    // Dynamic mode - loads library via dlopen
    explicit Sandbox(const char* library_path) {
        handle_ = dlopen(library_path, RTLD_NOW);
        if (!handle_) {
            throw std::runtime_error(std::string("Failed to load library: ") +
                                     dlerror());
        }
    }

    // Static mode - no library loading, use direct function pointers
    Sandbox() = default;

    ~Sandbox() {
        if (handle_) {
            dlclose(handle_);
        }
    }

    // Non-copyable
    Sandbox(const Sandbox&) = delete;
    Sandbox& operator=(const Sandbox&) = delete;

    // Movable
    Sandbox(Sandbox&& other) noexcept
        : handle_(other.handle_),
          symbol_cache_(std::move(other.symbol_cache_)) {
        other.handle_ = nullptr;
    }

    Sandbox& operator=(Sandbox&& other) noexcept {
        if (this != &other) {
            if (handle_)
                dlclose(handle_);
            handle_ = other.handle_;
            symbol_cache_ = std::move(other.symbol_cache_);
            other.handle_ = nullptr;
        }
        return *this;
    }

    // Call a function by name (dynamic mode)
    template<typename Sig, typename... Args>
    auto call(const char* name, Args... args) {
        void* fn = lookup(name);
        return call_ptr_sig<Sig>(fn, args...);
    }

    // Call a function by pointer (static mode) - zero overhead
    template<typename Sig, typename... Args>
    auto call(Sig* fn, Args... args) {
        return fn(args...);
    }

    // Context-aware call by name
    template<typename Sig, typename... Args>
    auto call(CallContext<Passthrough>& ctx, const char* name, Args... args)
        -> decltype(this->template call<Sig>(name, args...)) {
        if constexpr (std::is_void_v<decltype(this->template call<Sig>(
                          name, args...))>) {
            this->template call<Sig>(name, args...);
            ctx.finalize();
        } else {
            auto result = this->template call<Sig>(name, args...);
            ctx.finalize();
            return result;
        }
    }

    // Context-aware call by pointer (static mode)
    template<typename Sig, typename... Args>
    auto call(CallContext<Passthrough>& ctx, Sig* fn, Args... args) {
        if constexpr (std::is_void_v<decltype(fn(args...))>) {
            fn(args...);
            ctx.finalize();
        } else {
            auto result = fn(args...);
            ctx.finalize();
            return result;
        }
    }

    // Create a call context for in/out/inout parameters
    CallContext<Passthrough> context() {
        return CallContext<Passthrough>(*this);
    }

    // Get a function handle for repeated calls (dynamic mode)
    template<typename Sig>
    FnHandle<Passthrough, Sig> fn(const char* name) {
        return FnHandle<Passthrough, Sig>(*this, lookup(name));
    }

    // Get a function handle by pointer (static mode) - just returns the pointer
    template<typename Sig>
    Sig* fn(Sig* f) {
        return f;
    }

    // Call via function pointer (used by FnHandle)
    template<typename Ret, typename... Args>
    Ret call_ptr(void* fn, Args... args) {
        using FnPtr = Ret (*)(Args...);
        return reinterpret_cast<FnPtr>(fn)(args...);
    }

    // Memory allocation within the "sandbox" (just regular malloc for
    // passthrough)
    template<typename T>
    T* alloc(size_t count = 1) {
        return static_cast<T*>(std::malloc(sizeof(T) * count));
    }

    template<typename T>
    T* calloc(size_t count) {
        return static_cast<T*>(std::calloc(count, sizeof(T)));
    }

    template<typename T>
    T* realloc(T* ptr, size_t count) {
        return static_cast<T*>(std::realloc(ptr, sizeof(T) * count));
    }

    void free(void* ptr) {
        std::free(ptr);
    }

    // Memory mapping
    void* mmap(void* addr, size_t length, int prot, int flags, int fd,
               off_t offset) {
        return ::mmap(addr, length, prot, flags, fd, offset);
    }

    int munmap(void* addr, size_t length) {
        return ::munmap(addr, length);
    }

    // Identity-mapped memory (trivial for passthrough - all memory is shared)
    void* mmap_identity(size_t length, int prot) {
        return ::mmap(nullptr, length, prot, MAP_PRIVATE | MAP_ANONYMOUS, -1,
                      0);
    }

    int munmap_identity(void* addr, size_t length) {
        return ::munmap(addr, length);
    }

    // Arena allocator for identity-mapped memory (per-thread)
    template<typename T>
    T* idmem_alloc(size_t count = 1) {
        return static_cast<T*>(
            detail::get_thread_arena().alloc(sizeof(T) * count, alignof(T)));
    }

    void idmem_reset() {
        detail::get_thread_arena().reset();
    }

    // File descriptor registration (no-op for passthrough)
    int register_fd(int fd) {
        return fd;
    }

    // Close a file descriptor
    int close_fd(int fd) {
        return ::close(fd);
    }

    // Data transfer (trivial memcpy for passthrough)
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

    // Register a callback (passthrough just returns the function pointer as
    // void*)
    template<typename Fn>
    void* register_callback(Fn fn) {
        return reinterpret_cast<void*>(+fn);
    }

    // Escape hatch for advanced usage (returns dlopen handle)
    void* native_handle() const {
        return handle_;
    }

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

    // Helper to extract return type from signature and call with argument
    // conversion
    template<typename Ret, typename... Params, typename... Args>
    Ret call_with_sig_impl(void* fn, Ret (*)(Params...), Args... args) {
        using FnPtr = Ret (*)(Params...);
        return reinterpret_cast<FnPtr>(fn)(convert_arg<Params>(args)...);
    }

    template<typename Sig, typename... Args>
    auto call_ptr_sig(void* fn, Args... args) {
        return call_with_sig_impl(fn, static_cast<Sig*>(nullptr), args...);
    }

    void* lookup(const char* name) {
        std::lock_guard<std::mutex> lock(cache_mutex_);

        auto it = symbol_cache_.find(name);
        if (it != symbol_cache_.end()) {
            return it->second;
        }

        void* sym = dlsym(handle_, name);
        if (!sym) {
            throw std::runtime_error(std::string("Symbol not found: ") + name);
        }
        symbol_cache_[name] = sym;
        return sym;
    }

    void* handle_ = nullptr;
    std::unordered_map<const char*, void*> symbol_cache_;
    std::mutex cache_mutex_;
};

}  // namespace sbox
