#pragma once

#include "sbox.hh"

#include <sys/types.h>
#include <type_traits>

extern "C" {
#include "pbox.h"
}

namespace sbox {

namespace detail {

// Type mapping: C++ types to PBOX_TYPE_*
template<typename T>
struct pbox_type;

template<> struct pbox_type<void> { static constexpr PBoxType value = PBOX_TYPE_VOID; };
template<> struct pbox_type<float> { static constexpr PBoxType value = PBOX_TYPE_FLOAT; };
template<> struct pbox_type<double> { static constexpr PBoxType value = PBOX_TYPE_DOUBLE; };
template<> struct pbox_type<char> { static constexpr PBoxType value = PBOX_TYPE_SINT8; };
template<> struct pbox_type<signed char> { static constexpr PBoxType value = PBOX_TYPE_SINT8; };
template<> struct pbox_type<unsigned char> { static constexpr PBoxType value = PBOX_TYPE_UINT8; };
template<> struct pbox_type<short> { static constexpr PBoxType value = PBOX_TYPE_SINT16; };
template<> struct pbox_type<unsigned short> { static constexpr PBoxType value = PBOX_TYPE_UINT16; };
template<> struct pbox_type<int> { static constexpr PBoxType value = PBOX_TYPE_SINT32; };
template<> struct pbox_type<unsigned int> { static constexpr PBoxType value = PBOX_TYPE_UINT32; };
template<> struct pbox_type<long> { static constexpr PBoxType value = sizeof(long) == 8 ? PBOX_TYPE_SINT64 : PBOX_TYPE_SINT32; };
template<> struct pbox_type<unsigned long> { static constexpr PBoxType value = sizeof(unsigned long) == 8 ? PBOX_TYPE_UINT64 : PBOX_TYPE_UINT32; };
template<> struct pbox_type<long long> { static constexpr PBoxType value = PBOX_TYPE_SINT64; };
template<> struct pbox_type<unsigned long long> { static constexpr PBoxType value = PBOX_TYPE_UINT64; };

// All pointer types map to PBOX_TYPE_POINTER
template<typename T> struct pbox_type<T*> { static constexpr PBoxType value = PBOX_TYPE_POINTER; };

template<typename T>
inline constexpr PBoxType pbox_type_v = pbox_type<T>::value;

} // namespace detail

// Process backend - runs code in sandboxed child process via pbox
template<>
class Sandbox<Process> {
public:
    explicit Sandbox(const char* sandbox_executable) {
        box_ = pbox_create(sandbox_executable);
        if (!box_) {
            throw std::runtime_error("Failed to create sandbox");
        }
    }

    ~Sandbox() {
        if (box_) {
            pbox_destroy(box_);
        }
    }

    // Non-copyable
    Sandbox(const Sandbox&) = delete;
    Sandbox& operator=(const Sandbox&) = delete;

    // Movable
    Sandbox(Sandbox&& other) noexcept
        : box_(other.box_), symbol_cache_(std::move(other.symbol_cache_)) {
        other.box_ = nullptr;
    }

    Sandbox& operator=(Sandbox&& other) noexcept {
        if (this != &other) {
            if (box_) pbox_destroy(box_);
            box_ = other.box_;
            symbol_cache_ = std::move(other.symbol_cache_);
            other.box_ = nullptr;
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
    FnHandle<Process, Sig> fn(const char* name) {
        return FnHandle<Process, Sig>(*this, lookup(name));
    }

    // Call via function pointer (used by FnHandle)
    template<typename Ret, typename... Args>
    Ret call_ptr(void* fn, Args... args) {
        return call_impl<Ret, Args...>(fn, args...);
    }

    // Memory allocation in sandbox
    template<typename T>
    T* alloc(size_t count = 1) {
        return static_cast<T*>(pbox_malloc(box_, sizeof(T) * count));
    }

    template<typename T>
    T* calloc(size_t count) {
        return static_cast<T*>(pbox_calloc(box_, count, sizeof(T)));
    }

    template<typename T>
    T* realloc(T* ptr, size_t count) {
        return static_cast<T*>(pbox_realloc(box_, ptr, sizeof(T) * count));
    }

    void free(void* ptr) {
        pbox_free(box_, ptr);
    }

    // Memory mapping
    void* mmap(void* addr, size_t length, int prot, int flags, int fd, off_t offset) {
        return pbox_mmap(box_, addr, length, prot, flags, fd, offset);
    }

    int munmap(void* addr, size_t length) {
        return pbox_munmap(box_, addr, length);
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

    void copy_from(void* host_dest, const void* sandbox_src, size_t n) {
        pbox_copy_from(box_, host_dest, sandbox_src, n);
    }

    // String helper
    char* copy_string(const char* s) {
        size_t len = std::strlen(s) + 1;
        char* buf = alloc<char>(len);
        copy_to(buf, s, len);
        return buf;
    }

    // Register a callback
    template<typename Ret, typename... Args>
    void* register_callback(Ret(*fn)(Args...)) {
        constexpr int nargs = sizeof...(Args);
        PBoxType arg_types[nargs > 0 ? nargs : 1];
        if constexpr (nargs > 0) {
            fill_arg_types<0, Args...>(arg_types);
        }
        return pbox_register_callback(box_, reinterpret_cast<void*>(fn),
                                      detail::pbox_type_v<Ret>, nargs,
                                      nargs > 0 ? arg_types : nullptr);
    }

    // Process-specific
    pid_t pid() const { return pbox_pid(box_); }
    bool alive() const { return pbox_alive(box_); }

    // Escape hatch for advanced usage (returns pbox handle)
    PBox* native_handle() const { return box_; }

private:
    template<typename Sig, typename... Args>
    auto call_ptr_sig(void* fn, Args... args) {
        return call_with_sig<Sig>(fn, args...);
    }

    // Extract return type and parameter types from signature
    template<typename Ret, typename... Params, typename... Args>
    Ret call_with_sig_impl(void* fn, Ret(*)(Params...), Args... args) {
        return call_impl<Ret, Params...>(fn, convert_arg<Params>(args)...);
    }

    // Convert argument, using reinterpret_cast for function pointers
    template<typename To, typename From>
    static To convert_arg(From arg) {
        if constexpr (std::is_pointer_v<To> && std::is_pointer_v<From>) {
            return reinterpret_cast<To>(arg);
        } else {
            return static_cast<To>(arg);
        }
    }

    template<typename Sig, typename... Args>
    auto call_with_sig(void* fn, Args... args) {
        return call_with_sig_impl(fn, static_cast<Sig*>(nullptr), args...);
    }

    // Actual pbox_call implementation
    template<typename Ret, typename... Args>
    Ret call_impl(void* fn, Args... args) {
        constexpr int nargs = sizeof...(Args);

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
    void fill_arg_ptrs(void**) {}

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
        if (!sym) {
            throw std::runtime_error(std::string("Symbol not found: ") + name);
        }
        symbol_cache_[name] = sym;
        return sym;
    }

    PBox* box_ = nullptr;
    std::unordered_map<const char*, void*> symbol_cache_;
    std::mutex cache_mutex_;
};

} // namespace sbox
