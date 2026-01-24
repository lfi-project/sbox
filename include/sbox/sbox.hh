#pragma once

#include <cstddef>
#include <cstring>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <mutex>

namespace sbox {

// Backend tag types
struct Passthrough {};
struct Process {};

// Forward declaration
template<typename Backend>
class Sandbox;

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
