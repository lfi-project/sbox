#pragma once

#include "sbox.hh"

#include <cstdlib>
#include <dlfcn.h>
#include <sys/mman.h>
#include <unistd.h>

namespace sbox {

// Passthrough backend - loads library normally via dlopen
template<>
class Sandbox<Passthrough> {
public:
    explicit Sandbox(const char* library_path) {
        handle_ = dlopen(library_path, RTLD_NOW);
        if (!handle_) {
            throw std::runtime_error(std::string("Failed to load library: ") + dlerror());
        }
    }

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
        : handle_(other.handle_), symbol_cache_(std::move(other.symbol_cache_)) {
        other.handle_ = nullptr;
    }

    Sandbox& operator=(Sandbox&& other) noexcept {
        if (this != &other) {
            if (handle_) dlclose(handle_);
            handle_ = other.handle_;
            symbol_cache_ = std::move(other.symbol_cache_);
            other.handle_ = nullptr;
        }
        return *this;
    }

    // Call a function by name
    template<typename Sig, typename... Args>
    auto call(const char* name, Args... args) {
        void* fn = lookup(name);
        return call_ptr_sig<Sig>(fn, args...);
    }

    // Get a function handle for repeated calls
    template<typename Sig>
    FnHandle<Passthrough, Sig> fn(const char* name) {
        return FnHandle<Passthrough, Sig>(*this, lookup(name));
    }

    // Call via function pointer (used by FnHandle)
    template<typename Ret, typename... Args>
    Ret call_ptr(void* fn, Args... args) {
        using FnPtr = Ret(*)(Args...);
        return reinterpret_cast<FnPtr>(fn)(args...);
    }

    // Memory allocation within the "sandbox" (just regular malloc for passthrough)
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
    void* mmap(void* addr, size_t length, int prot, int flags, int fd, off_t offset) {
        return ::mmap(addr, length, prot, flags, fd, offset);
    }

    int munmap(void* addr, size_t length) {
        return ::munmap(addr, length);
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

    // Register a callback (passthrough just returns the function pointer)
    template<typename Fn>
    auto register_callback(Fn fn) {
        return +fn;
    }

    // Escape hatch for advanced usage (returns dlopen handle)
    void* native_handle() const { return handle_; }

private:
    // Helper to extract return type from signature and call
    template<typename Sig, typename... Args>
    auto call_ptr_sig(void* fn, Args... args) {
        using FnPtr = Sig*;
        return reinterpret_cast<FnPtr>(fn)(args...);
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

} // namespace sbox
