#pragma once

#include <cstddef>
#include <cstring>
#include <mutex>
#include <unordered_map>

namespace sbox {

// TypedName - carries a function's name string along with its declared type.
// Created by the SBOX_FN macro in dynamic mode so that call-site type
// checking can be performed against the library header declaration.
template<typename FnPtr>
struct TypedName {
    const char* name;
    // Implicit conversion to const char* for backward compatibility
    // with call<Sig>(const char*, ...) overloads.
    operator const char*() const { return name; }
};

}  // namespace sbox

// Macro to reference a function - expands differently based on backend.
// In static mode: expands to function pointer (direct call, type-safe).
// In dynamic mode: expands to TypedName carrying name + type from declaration.
#ifdef SBOX_STATIC
#define SBOX_FN(name) (name)
#else
#define SBOX_FN(name) (sbox::TypedName<decltype(&name)>{#name})
#endif

namespace sbox {

// Backend tag types
struct Passthrough {};
struct Process {};

// Forward declarations
template<typename Backend>
class Sandbox;

template<typename Backend>
class CallContext;

// Function handle - captures sandbox reference for direct calls
template<typename Backend, typename Sig>
class FnHandle;

template<typename Backend, typename Ret, typename... Args>
class FnHandle<Backend, Ret(Args...)> {
public:
    FnHandle(Sandbox<Backend>& sandbox, void* fn_ptr)
        : sandbox_(&sandbox), fn_ptr_(fn_ptr) {
    }

    Ret operator()(Args... args) const {
        return sandbox_->template call_ptr<Ret, Args...>(fn_ptr_, args...);
    }

private:
    Sandbox<Backend>* sandbox_;
    void* fn_ptr_;
};

}  // namespace sbox
