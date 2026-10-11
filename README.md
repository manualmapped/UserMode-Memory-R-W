# UserMode-Memory-R-W

A modern **Windows x64 external memory library** providing fast, thread-safe process memory reading and writing through **direct system calls**. Designed with minimal overhead, automatic resource management, and a simple C++17 API.

No kernel driver. No inline hooks. No dependencies beyond the Windows SDK.

---

## Features

- Native **direct syscall** implementation for memory read/write
- **Driverless** user-mode operation
- Automatic syscall stub generation at runtime
- Dynamic syscall ID resolution across Windows versions
- Independent, move-only process sessions with per-target ownership
- Concurrent memory operations protected against session shutdown and rebinding
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
mov r10, rcx
mov eax, syscall_id
syscall
ret
```

After initialization, every read and write goes directly through the generated syscall stub.

---

## Performance

Concurrent operations use a shared lock so multiple reads and writes can proceed together. Initialization, rebinding, and shutdown take exclusive access and wait for active operations to finish. This adds synchronization overhead; benchmark results are not currently published.

---

# Quick Start

```cpp
#include "memory.h"
#include <cstdio>

int main()
{
    const auto pid = mem::find_process(TEXT("notepad.exe"));
    if (pid == 0)
    {
        printf("Process not found.\n");
        return 1;
    }

    mem::ProcessSession session;
    if (!session.bind(pid))
    {
        printf("Could not open process.\n");
        return 1;
    }

    const auto base = session.base_address();
    printf("Base: 0x%llX\n", base);

    auto dos = session.read<IMAGE_DOS_HEADER>(base);

    if (dos)
        printf("DOS Header: 0x%X\n", dos->e_magic);

    return 0;
}
```

---

# API

## Independent sessions (recommended)

Each `ProcessSession` owns its target PID, process handle, syscall stubs, and base address. Multiple instances can target different processes at the same time. A session is noncopyable and movable; its destructor releases its resources. Reads and writes can run concurrently on an instance, while `bind()` and `close()` wait for active operations to finish.

```cpp
mem::ProcessSession first;
mem::ProcessSession second;

if (!first.bind(pid_a) || !second.bind(pid_b))
    return 1;

auto a = first.read<int>(address_a);
auto b = second.read<int>(address_b);
first.write(address_a, 123);
```

`bind()` replaces only that instance's target. A failed bind leaves that instance closed. The typed `read<T>` and `write<T>` APIs require trivially copyable types; `read_raw` and `write_raw` handle byte buffers.

## Legacy global API

The original free functions, globals, and `ScopedSession` remain available for source compatibility. They share one process-global target, so use only one active legacy session at a time. New code should use `ProcessSession` to keep targets independent.

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

Each `ProcessSession` protects its own handle and generated stubs with a shared mutex. Reads may run together; binding and closing wait for active reads and writes before replacing or releasing resources. Separate sessions can target separate processes independently.

The legacy free functions still use shared global state. Use one active `ScopedSession` at a time with that API, and do not access its exposed globals (`pid`, `process_handle`, syscall pointers, or `base_addr`) while operations are running. `is_ready()` only reports readiness; it does not reserve a session for a following operation.

---

# CMake Integration

The library is header-only and exports the `UserModeMemoryRW::UserModeMemoryRW` CMake target. Add this repository with `add_subdirectory` and link that target to a Windows x64 C++17 executable:

```cmake
add_subdirectory(path/to/UserMode-Memory-R-W)
target_link_libraries(your_app PRIVATE UserModeMemoryRW::UserModeMemoryRW)
```

To install the header and exported target files, run `cmake -S . -B build` followed by `cmake --install build --prefix <install-directory>`.

---

# Internal Flow

```
read_memory()

        │

        ▼

shared session lock

        │

        ▼

read_syscall()

        │

        ▼

mov r10, rcx
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

| Windows Version | Verification |
|----------------|-----------|
| Windows 7 | Not verified |
| Windows 8 | Not verified |
| Windows 10 | Not verified |
| Windows 11 | Not verified |

Syscall numbers are resolved dynamically at runtime. Compatibility still depends on OS behavior and should be verified on the target system; the project does not currently provide automated tests across these Windows versions.

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
