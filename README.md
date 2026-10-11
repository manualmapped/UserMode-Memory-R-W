# UserMode-Memory-R-W

A Windows x64 C++17 header-only library for authorized reads and writes to process memory. The recommended `mem::ProcessSession` API uses documented Win32 process APIs and reports transfer lengths and operating-system errors.

## Capabilities

- Independent, move-only process sessions
- Explicit read-only and read/write access modes
- Typed and raw reads/writes with requested and transferred byte counts
- Immediate Win32 error capture when a transfer API fails
- Advisory virtual-memory region queries
- Bounded multi-range reads
- Existing legacy free functions retained for source compatibility

## Quick start

```cpp
#include "memory.h"
#include <cstdio>
#include <cstdlib>

int main(int argc, char** argv)
{
    if (argc != 2) return 2;
    const auto pid = static_cast<INT32>(std::strtol(argv[1], nullptr, 10));

    mem::ProcessSession session;
    if (!session.bind(pid, mem::AccessMode::read_only)) return 1;

    const auto base = session.base_address();
    if (base == 0) return 1;
    IMAGE_DOS_HEADER header{};
    const auto result = session.read_result(base, header);
    if (!result.complete()) {
        std::fprintf(stderr, "Read transferred %zu/%zu bytes (status=%u, Win32 error=%lu)\n",
            result.transferred, result.requested, static_cast<unsigned>(result.status), result.os_error);
        return 1;
    }
    std::printf("DOS signature: 0x%X\n", header.e_magic);
}
```

The sample address is illustrative; use a valid address belonging to the process you are authorized to inspect. A failed `bind()` does not currently expose its `OpenProcess` error. Transfer results report the immediate `GetLastError()` value when `ReadProcessMemory` or `WriteProcessMemory` returns failure. Library-side validation and access-mode rejections use the matching Win32 error constant without making a transfer call. If the API reports success with a short count, the result has `TransferStatus::partial` and `os_error == ERROR_SUCCESS`.

## ProcessSession

`bind(pid)` retains the read/write default. Choose read-only access when writes are unnecessary:

```cpp
mem::ProcessSession reader;
if (!reader.bind(pid, mem::AccessMode::read_only)) return 1;

int value = 0;
auto read = reader.read_result(address, value);
if (!read.complete()) {
    // Inspect read.requested, read.transferred, read.os_error, and read.status.
}

mem::ProcessSession editor;
if (!editor.bind(pid, mem::AccessMode::read_write)) return 1;
const int replacement = 123;
auto write = editor.write_result(address, replacement);
```

Read-only sessions request `PROCESS_VM_READ | PROCESS_QUERY_INFORMATION` (the latter supports the existing base-module and region queries). Read/write sessions additionally request `PROCESS_VM_WRITE | PROCESS_VM_OPERATION`. Windows enforces process access rights; elevated privileges do not guarantee access to protected or inaccessible processes.

Raw APIs are `read_raw_result(address, buffer, size)` and `write_raw_result(address, buffer, size)`. Existing `read<T>()`, `write<T>()`, `read_raw()`, and `write_raw()` are compatibility wrappers that return `std::optional` or `bool` and therefore do not expose partial transfer details. Typed APIs require trivially copyable values.

`query_region(address, region)` wraps `VirtualQueryEx` and returns a Win32 error code (`ERROR_SUCCESS` on success). Region metadata is advisory: process mappings and protections may change before a subsequent transfer, whose result remains authoritative.

`read_many(requests, results, count)` performs sequential reads into caller-owned buffers and writes one `TransferResult` per request in order. Accepted batches are limited to 256 ranges and 16 MiB aggregate requested bytes. A failed range does not suppress later ranges. If the count or byte limit is exceeded, it returns `false` without performing reads or modifying results.

Sessions own their process handle. They are movable and noncopyable. Reads may run concurrently; bind, close, move, and multi-range reads synchronize on the session so one batch cannot switch targets midway through.

## Legacy API

The original free functions, globals, and `ScopedSession` remain for source compatibility. They share one process-global target and retain their legacy implementation. New code should use `ProcessSession` for per-instance ownership and Win32 transfer diagnostics.

## Build and test

Requirements: Windows x64, C++17, CMake 3.20+, and either MSVC or MinGW-w64 GCC. The project CI builds and tests both toolchains on Windows.

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
cmake --install build --config Release --prefix install
```

For MinGW-w64, configure with `-G "MinGW Makefiles"` and omit `-A x64`.

The integration test launches a child process owned by the test executable and only reads or writes the child’s test data. The test covers access modes, successful and failed transfers, region queries, bounded batches, independent sessions, move/close/rebind, and child exit.

To use the installable CMake target, link `UserModeMemoryRW::UserModeMemoryRW`. The target exports the header include path, C++17 requirement, and `Psapi` dependency.
