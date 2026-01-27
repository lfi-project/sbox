#pragma once

#include <cstddef>
#include <cstring>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <mutex>

// Macro to reference a function - expands differently based on backend
#ifdef SBOX_STATIC
#define SBOX_FN(name) (name)
#else
#define SBOX_FN(name) (#name)
#endif

namespace sbox {

// Backend tag types
struct Passthrough {};
struct Process {};
struct LFI {};

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
        : sandbox_(&sandbox), fn_ptr_(fn_ptr) {}

    Ret operator()(Args... args) const {
        return sandbox_->template call_ptr<Ret, Args...>(fn_ptr_, args...);
    }

private:
    Sandbox<Backend>* sandbox_;
    void* fn_ptr_;
};

} // namespace sbox
