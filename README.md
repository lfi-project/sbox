# SBox

SBox is a C++ library for easily making calls to sandboxed libraries. By
default, it supports passthrough (no sandboxing) and process-based sandboxes,
and with upcoming support for LFI sandboxes.

The goal is that when you migrate a library in your application to be called
through SBox, the API for running the library with any of these backends is the
same, so it is easy to swap between them depending on your needs.

SBox supports multithreading, callbacks, in/out/inout helpers for pointer
parameters, and shared memory between the host and sandbox (including identity
mappings for the process-based backend, which are mapped at the same addresses
in both processes).

## Example

Given a C library (`lib_add.c`):

```c
int add(int a, int b) {
    return a + b;
}
```

For the process-based sandbox, compile this file to an `add_sandbox` executable
by linking it with `libpbox_sandbox.a`.

You can then sandbox and call it from C++:

```cpp
#include <cstdio>
#include "sbox/process.hh"

int main() {
    sbox::Sandbox<sbox::Process> sandbox("./add_sandbox");

    // Call by name (lookup is cached)
    int result = sandbox.call<int(int, int)>("add", 2, 3);
    printf("add(2, 3) = %d\n", result);

    // Use a function handle for repeated calls
    auto add = sandbox.fn<int(int, int)>("add");
    printf("add(100, 200) = %d\n", add(100, 200));
}
```

To swap to passthrough (no sandboxing), just compile to `libadd.so` and change
the backend:

```cpp
#include "sbox/passthrough.hh"
sbox::Sandbox<sbox::Passthrough> sandbox("./libadd.so");
```

## Features

### In/Out Parameters

When a sandbox function takes pointer parameters, use a `CallContext` with
`in()`, `out()`, and `inout()` helpers to automatically manage copying data
between the host and sandbox.

```cpp
auto ctx = sandbox.context();
int counter = 5;
sandbox.call<void(int*)>(ctx, "increment", ctx.inout(counter));
// counter is now 6

int result;
sandbox.call<void(const int*, const int*, int*)>(ctx,
    "add_to_result", ctx.in(a), ctx.in(b), ctx.out(result));
```

### Callbacks

Register host functions as callbacks that sandbox code can invoke:

```cpp
static int my_adder(int a, int b) {
    return a + b;
}

auto cb = sandbox.register_callback(my_adder);
int result = sandbox.call<int(int, int(*)(int, int))>("process_data", 42, cb);
```

### Shared Memory

Map shared memory into both the host and sandbox for zero-copy data exchange:

```cpp
int memfd = memfd_create("shared_buffer", 0);
ftruncate(memfd, 4096);

// Map in host
auto *host_buf = (unsigned char*)mmap(nullptr, 4096,
    PROT_READ | PROT_WRITE, MAP_SHARED, memfd, 0);

// Map in sandbox (same fd, same contents)
void *sandbox_buf = sandbox.mmap(nullptr, 4096,
    PROT_READ | PROT_WRITE, MAP_SHARED, memfd, 0);

// Sandbox writes are visible to the host and vice versa
sandbox.call<void(void*, size_t, unsigned char)>(
    "fill_buffer", sandbox_buf, (size_t)4096, (unsigned char)0xAB);
// host_buf now contains 0xAB bytes
```
