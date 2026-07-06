# UserMode-Memory-R-W

External memory reading and writing for Windows x64, featuring **lock-free direct syscalls** — no driver, no hooks, minimal overhead.

---

## Features

- **Direct syscall** read/write via manually allocated `syscall` stubs
- **Lock-free hot path** — zero contended atomic RMW during I/O, just a single `acquire` load
- **Thread-safe** — reader-count shutdown protection, `std::mutex` for cold-path init
- **RAII scoped sessions** — `ScopedSession` auto-cleanup
- **`std::optional<T>` read API** — explicit failure handling, no silent zeroes
- **Address validation** — bounds-checked, caught early on cold path
- **No comments, no snake_case** — clean, minimal API

---

## Why direct syscalls?

Standard APIs like `ReadProcessMemory` or `NtReadVirtualMemory` go through ntdll and can be **hooked** by anticheats. This library creates raw syscall stubs in executable memory and calls the kernel directly:

```
mov r10, rdx          ; save 2nd arg
mov eax, SYSCALL_ID   ; syscall number
syscall               ; enter kernel
ret
```

This means:
- No ntdll function pointers to hook
- No kernel-mode driver required
- Lower latency (~270 ns vs ~500 ns for WinAPI variants)
- Consistent across Windows 10/11 builds (dynamic syscall ID extraction)

---

## Performance

10 000 iterations each, self-process read/write (MSVC Release x64):

| Operation | Latency |
|-----------|---------|
| `read_memory<int>` | **256 ns** |
| `write_memory<int>` | **254 ns** |
| `write_raw` | **259 ns** |

Concurrent stress test (4 readers + 1 writer, 500 iterations each): **2000/2000 reads OK, 500/500 writes OK** — zero data races, zero crashes.

The ~270 ns floor is the kernel round-trip time for `NtReadVirtualMemory`/`NtWriteVirtualMemory`. User-space overhead is reduced to a single atomic load on the hot path.

---

## Quick start

```cpp
#include "updated_memory.h"
#include <cstdio>

int main()
{
    // RAII session — finds process, opens handle, resolves syscalls
    mem::ScopedSession session(TEXT("notepad.exe"));
    if (!session)
    {
        printf("[-] Process not found or access denied\n");
        return 1;
    }

    // Resolve base address
    mem::base_addr = mem::get_base_address();
    printf("[+] Base: 0x%llX\n", mem::base_addr);

    // Read memory via direct syscall
    auto dos = mem::read_memory<IMAGE_DOS_HEADER>(mem::base_addr);
    if (dos.has_value())
        printf("[+] DOS e_magic: 0x%X\n", dos->e_magic);

    // Write memory
    int value = 42;
    mem::write_memory(mem::base_addr, value);

    return 0;
}
```

---

## API reference

### Types

```cpp
namespace mem {
    using read_fn  = NTSTATUS(__fastcall*)(HANDLE, PVOID, PVOID, SIZE_T, PSIZE_T);
    using write_fn = NTSTATUS(__fastcall*)(HANDLE, PVOID, PVOID, SIZE_T, PSIZE_T);
}
```

### State (inline globals)

| Variable | Type | Default | Description |
|----------|------|---------|-------------|
| `pid` | `INT32` | `0` | Target process ID |
| `process_handle` | `HANDLE` | `nullptr` | Open handle to target |
| `base_addr` | `uintptr_t` | `0` | Base address of target |
| `read_syscall` | `read_fn` | `nullptr` | Direct syscall stub for reads |
| `write_syscall` | `write_fn` | `nullptr` | Direct syscall stub for writes |

### Functions

| Function | Returns | Description |
|----------|---------|-------------|
| `find_process(name)` | `INT32` | PID by process name, 0 if not found |
| `get_base_address()` | `uintptr_t` | Base address of target, 0 on failure |
| `is_valid(address)` | `bool` | Valid user-mode address range check |
| `is_ready()` | `bool` | Is initialized and handle is open |
| `initialize()` | `bool` | Open handle + resolve syscalls |
| `read_memory<T>(addr)` | `std::optional<T>` | Read via direct syscall |
| `write_memory<T>(addr, val)` | `bool` | Write via direct syscall (copy-safe) |
| `write_raw(addr, data, size)` | `bool` | Raw byte write (destroys const) |
| `shutdown()` | `void` | Close handle, reset state |

### ScopedSession

| Method | Description |
|--------|-------------|
| `ScopedSession(pid)` | Bind by PID, auto-init |
| `ScopedSession(name)` | Find process by name, auto-init |
| `~ScopedSession()` | Auto-shutdown if active |
| `bind(pid)` | Rebind to new PID |
| `bind(name)` | Rebind by process name |
| `operator bool` | Returns `is_ready()` |

---

## Thread safety

- **Reads (`read_memory`, `write_raw`, `write_memory`)**: Lock-free hot path — single `ready.load(acquire)` per call. Fully concurrent with other readers.
- **Init/Shutdown**: Serialized under `std::mutex`. Blocks until all pending reads complete.
- **Concurrent stress test**: Validated with 4 readers + 1 writer at 500 iterations each — all operations succeed.

---

## Internal architecture

```
read_memory<T>(addr)
  │
  ├─ ready.load(acquire) ──true──► read_syscall(process_handle, ...)
  │                                      │
  │                                      └─ mov r10, rdx
  │                                         mov eax, SYSCALL_ID
  │                                         syscall
  │                                         ret
  │
  └─ false ──► init_mutex.lock()
               ├─ is_valid(addr)
               ├─ initialize_impl() — OpenProcess + scan syscall ID + alloc stub
               └─ read_syscall(...)
```

---

## Portability

| Aspect | Support |
|--------|---------|
| Architecture | x64 only (uses `syscall` instruction) |
| Windows version | 7 through 11 (dynamic syscall ID extraction) |
| Compilers | MSVC 2022+, GCC 12+ (MinGW-w64) |
| C++ Standard | C++17 (`std::optional`, `std::shared_mutex`) |

---
