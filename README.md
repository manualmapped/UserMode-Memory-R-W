# UserMode-Memory-R-W

A modern **Windows x64 external memory library** providing fast, thread-safe process memory reading and writing through **direct system calls**. Designed with minimal overhead, automatic resource management, and a simple C++17 API.

No kernel driver. No inline hooks. No dependencies beyond the Windows SDK.

---

## Features

- Native **direct syscall** implementation for memory read/write
- **Driverless** user-mode operation
- Automatic syscall stub generation at runtime
- Dynamic syscall ID resolution across Windows versions
- Lock-free fast path for memory operations
- Thread-safe initialization and shutdown
- RAII session management
- `std::optional<T>` read interface
- Safe typed and raw memory writes
- User-mode address validation
- Header-only public API
- C++17 compatible

---

## Overview

The library allows external applications to access another process' memory while avoiding the normal WinAPI wrappers.

Instead of calling functions such as:

- `ReadProcessMemory`
- `WriteProcessMemory`
- `NtReadVirtualMemory`
- `NtWriteVirtualMemory`

through exported ntdll entry points, the library resolves the required syscall numbers at runtime and executes the system calls directly using dynamically generated syscall stubs.

This removes unnecessary user-mode indirection while keeping the implementation entirely in user mode.

---

## How it works

During initialization the library:

1. Opens the target process.
2. Locates the required native syscall numbers.
3. Generates executable syscall stubs.
4. Stores callable function pointers.
5. Uses those stubs for every future memory operation.

Generated stub:

```asm
mov r10, rdx
mov eax, syscall_id
syscall
ret
```

After initialization, every read and write goes directly through the generated syscall stub.

---

## Performance

Release x64 benchmark (MSVC)

| Operation | Average |
|----------|--------:|
| `read_memory<int>` | ~256 ns |
| `write_memory<int>` | ~254 ns |
| `write_raw` | ~259 ns |

Concurrent validation:

- 4 reader threads
- 1 writer thread
- 500 iterations per thread

Results:

- 2000 / 2000 successful reads
- 500 / 500 successful writes
- No races
- No crashes

The remaining latency is almost entirely the kernel transition itself.

---

# Quick Start

```cpp
#include "updated_memory.h"
#include <cstdio>

int main()
{
    mem::ScopedSession session(TEXT("notepad.exe"));

    if (!session)
    {
        printf("Initialization failed.\n");
        return 1;
    }

    mem::base_addr = mem::get_base_address();

    printf("Base: 0x%llX\n", mem::base_addr);

    auto dos = mem::read_memory<IMAGE_DOS_HEADER>(mem::base_addr);

    if (dos)
        printf("DOS Header: 0x%X\n", dos->e_magic);

    int value = 42;

    mem::write_memory(mem::base_addr, value);

    return 0;
}
```

---

# API

## Global State

| Variable | Type | Description |
|----------|------|-------------|
| `pid` | `INT32` | Target process ID |
| `process_handle` | `HANDLE` | Handle to target process |
| `base_addr` | `uintptr_t` | Cached module base |
| `read_syscall` | `read_fn` | Generated read syscall |
| `write_syscall` | `write_fn` | Generated write syscall |

---

## Functions

### Process

```cpp
INT32 find_process(const TCHAR* name);
```

Find a process by executable name.

---

```cpp
uintptr_t get_base_address();
```

Returns the target module base address.

---

```cpp
bool initialize();
```

Initializes the library.

---

```cpp
void shutdown();
```

Releases all allocated resources.

---

```cpp
bool is_ready();
```

Returns whether the library is initialized.

---

```cpp
bool is_valid(uintptr_t address);
```

Performs a user-mode address range check.

---

### Reading

```cpp
template<typename T>
std::optional<T> read_memory(uintptr_t address);
```

Reads an object of type `T`.

Returns:

- `std::optional<T>` containing the value
- `std::nullopt` on failure

Example:

```cpp
auto health = mem::read_memory<int>(address);

if (health)
    printf("%d\n", *health);
```

---

### Writing

```cpp
template<typename T>
bool write_memory(uintptr_t address, const T& value);
```

Writes a typed value.

Returns `true` on success.

---

```cpp
bool write_raw(uintptr_t address, void* buffer, size_t size);
```

Writes an arbitrary byte buffer.

---

## ScopedSession

RAII helper for automatic initialization and cleanup.

### Constructors

```cpp
ScopedSession(pid);
```

Initialize using a process ID.

```cpp
ScopedSession(name);
```

Initialize using a process name.

---

### Methods

```cpp
bind(pid);
```

Bind to another process ID.

```cpp
bind(name);
```

Bind by executable name.

---

```cpp
operator bool();
```

Returns the initialization state.

---

Destructor automatically calls `shutdown()`.

---

# Thread Safety

Memory operations are safe for concurrent use.

Fast-path reads and writes require only a single atomic acquire load and avoid mutex contention during normal operation.

Initialization and shutdown are serialized internally and wait for active operations to finish before releasing resources.

Validated under concurrent stress testing with multiple reader and writer threads.

---

# Internal Flow

```
read_memory()

        │

        ▼

ready.load()

        │

        ▼

read_syscall()

        │

        ▼

mov r10, rdx
mov eax, syscall_id
syscall
ret

        │

        ▼

Kernel
```

Initialization path:

```
initialize()

        │

        ▼

OpenProcess

        │

        ▼

Resolve syscall IDs

        │

        ▼

Allocate executable stub

        │

        ▼

Ready
```

---

# Requirements

| Component | Requirement |
|-----------|-------------|
| Platform | Windows x64 |
| Architecture | x86-64 |
| Compiler | MSVC 2022+, GCC 12+ |
| Standard | C++17 |
| SDK | Windows SDK |

---

# Compatibility

| Windows Version | Supported |
|----------------|-----------|
| Windows 7 | ✓ |
| Windows 8 | ✓ |
| Windows 10 | ✓ |
| Windows 11 | ✓ |

Syscall numbers are resolved dynamically at runtime, allowing compatibility across supported Windows releases.

---

# Design Goals

- Modern C++17 interface
- Minimal runtime overhead
- Thread-safe implementation
- Driverless deployment
- Easy integration
- Predictable behavior
- RAII resource management
- Explicit error handling
- Cross-compiler compatibility

---

# Example

```cpp
mem::ScopedSession session(TEXT("game.exe"));

if (!session)
    return 1;

uintptr_t player = mem::base_addr + 0x123456;

auto health = mem::read_memory<int>(player);

if (health)
{
    printf("Health: %d\n", *health);

    mem::write_memory(player, 999);
}
```

---
