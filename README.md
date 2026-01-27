# sbox

A C++17 header-only library for sandboxing C libraries with swappable backends.

## Example

```cpp
#include "sbox/lfi.hh"

sbox::Sandbox<sbox::LFI> sandbox("./libfoo.lfi");

// Call by name
int result = sandbox.call<int(int, int)>("add", 10, 32);

// Function handle for hot paths
auto add = sandbox.fn<int(int, int)>("add");
int r = add(5, 7);

// Memory allocation
char* buf = sandbox.alloc<char>(256);
sandbox.copy_to(buf, "hello", 6);
sandbox.free(buf);

// In/out parameters
auto ctx = sandbox.context();
int a = 5, b = 3, result;
sandbox.call<void(const int*, const int*, int*)>(ctx, "add", ctx.in(a), ctx.in(b), ctx.out(result));

// Callbacks
static int my_adder(int a, int b) { return a + b; }
auto cb = sandbox.register_callback(my_adder);
sandbox.call<void(int(*)(int,int))>("set_callback", cb);
```

## Backends

| Backend | Isolation | Switch overhead |
|---------|-----------|-----------------|
| **Passthrough** | None | None |
| **Process** | Separate processes | High |
| **LFI** | In-process | Low |

```cpp
sbox::Sandbox<sbox::Passthrough> sb("./libfoo.so");   // Native calls
sbox::Sandbox<sbox::Process> sb("./foo_sandbox");     // Child process
sbox::Sandbox<sbox::LFI> sb("./libfoo.lfi");          // LFI isolation
```

## Build

```bash
meson setup build
ninja -C build
ninja -C build test
```

### Backend Selection

```bash
meson setup build -Dsbox_backend=passthrough  # Default
meson setup build -Dsbox_backend=process      # Requires pbox
meson setup build -Dsbox_backend=lfi          # Requires lfi-runtime + lficc
```

## API Reference

### Sandbox Methods

| Method | Description |
|--------|-------------|
| `call<Sig>(name, args...)` | Call function by name |
| `fn<Sig>(name)` | Get function handle |
| `context()` | Create CallContext for in/out params |
| `alloc<T>(n)` / `free(ptr)` | Memory allocation |
| `copy_to(dst, src, n)` | Copy host → sandbox |
| `copy_from(dst, src, n)` | Copy sandbox → host |
| `copy_string(s)` | Allocate and copy string |
| `register_callback(fn)` | Register host callback |
| `mmap(...)` / `munmap(...)` | Memory mapping |

### CallContext Methods

| Method | Description |
|--------|-------------|
| `in(var)` | Input-only (copied to sandbox) |
| `out(var)` | Output-only (copied back after call) |
| `inout(var)` | Bidirectional (copied both ways) |
