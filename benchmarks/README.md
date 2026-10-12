# Process-transfer benchmarks

These benchmarks are opt-in and use only a child process launched and controlled by the benchmark executable. The default CMake build does not fetch or link third-party libraries.

## Workloads and reports

`process_transfer_bench.cpp` compares `ProcessSession`, direct `ReadProcessMemory` / `WriteProcessMemory`, libvas, and libmem for 4 B, 8 B, 64 B, 4 KiB, 64 KiB, and 1 MiB transfers. It also compares `read_many` with repeated single reads for contiguous and disjoint ranges, measures 1/2/4/8 concurrent readers, and reports bind plus base-module lookup separately. Every timed run uses a warmup; each result row has seven samples and reports median, nearest-rank p95, operations/s, and MiB/s. The executable randomizes implementation order per sample, verifies data outside timed sections, and prints CPU, Windows build, logical processor count, page size, and compiler. Timed transfers target an explicitly allocated private `PAGE_READWRITE` buffer in the owned child process; setup, initialization, and validation are outside timed sections.

`historical_transfer_bench.cpp` and `run-historical.ps1` benchmark the old global Nt* wrappers from two repository revisions. The old typed-read API creates its result object on the stack, so cases over 64 KiB are omitted after the 1 MiB case exceeded the available stack. Those revisions use different contracts and broad process rights; their timings are contextual measurements, not equivalent API claims. Startup and syscall-stub setup are excluded; the historical Nt* function pointers and process handle are initialized before timing.

## Pinned comparison sources

The following are the upstream branch heads inspected on 2026-10-11:

| Project | Revision | Benchmark scope |
| --- | --- | --- |
| UserMode-Memory-R-W | `307aad3cc410559f2808723fb835cb23b10157fc` | Historical global Nt* wrapper |
| UserMode-Memory-R-W | `38732bab803482a6ae17214fa236cfee36b91ea6` | Historical global Nt* wrapper |
| [libvas](https://github.com/a3f/libvas) | `d3621d4c17b6ca9b9394f1af910c89ddafb3f6d7` | Persistent-handle Win32 read/write API |
| [libmem](https://github.com/rdbo/libmem) | `a07c9942bf1358dabcc83eb0cd072736c749d7f8` | Public Windows read/write API; its implementation opens and closes a process handle per transfer |
| [Blackbone](https://github.com/DarthTon/Blackbone) | `5ede6ce50cd8ad34178bfa6cae05768ff6b3859b` | Inspected but not benchmarked; no adapter was added to this harness |

The libvas adapter compiles its pinned `win32/vas.c` transfer implementation directly. The libmem adapter compiles the pinned Windows transfer and support files. Upstream libmem emits Windows SDK macro redefinition and address integer-to-pointer warnings under MSVC; these are upstream-source warnings, and the adapter does not modify or copy those sources. The adapters measure their published APIs. `libmem` opens and closes a process handle on each transfer, while `ProcessSession`, direct Win32, and libvas keep a handle open; interpret those timings with that API difference in mind.

## Reproduce on Windows x64

Fetch the exact peer revisions into directories outside the project checkout, then configure libvas once to generate its headers. For example:

```powershell
git clone https://github.com/a3f/libvas.git $env:TEMP\umrw-libvas
git -C $env:TEMP\umrw-libvas checkout d3621d4c17b6ca9b9394f1af910c89ddafb3f6d7
cmake -S $env:TEMP\umrw-libvas -B $env:TEMP\umrw-libvas-generated -G "MinGW Makefiles" `
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM=3.5 `
  -DBUILD_AUTO_TESTS=OFF -DBUILD_MANUAL_TESTS=OFF -DBUILD_SHARED_LIBS=ON

git clone https://github.com/rdbo/libmem.git $env:TEMP\umrw-libmem
git -C $env:TEMP\umrw-libmem checkout a07c9942bf1358dabcc83eb0cd072736c749d7f8

cmake -S . -B build-bench -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release `
  -DBUILD_TESTING=ON -DUMRW_BUILD_BENCHMARKS=ON `
  -DUMRW_LIBVAS_SOURCE_DIR="$env:TEMP\umrw-libvas" `
  -DUMRW_LIBVAS_GENERATED_DIR="$env:TEMP\umrw-libvas-generated" `
  -DUMRW_LIBMEM_SOURCE_DIR="$env:TEMP\umrw-libmem"
cmake --build build-bench --config Release --parallel
ctest --test-dir build-bench -C Release --output-on-failure
./build-bench/process_transfer_bench.exe | Tee-Object benchmarks/results/current-run.csv
powershell -NoProfile -ExecutionPolicy Bypass -File benchmarks/run-historical.ps1
```

For a same-toolchain read-many baseline, compile the same source with `UMRW_DISABLE_READ_MANY_COALESCING` defined. With MinGW GCC, for example:

```powershell
g++ -std=c++17 -O3 -DNDEBUG -DUMRW_DISABLE_READ_MANY_COALESCING=1 -I . `
  benchmarks/process_transfer_bench.cpp -lpsapi -ladvapi32 -o $env:TEMP\process-transfer-baseline.exe
& $env:TEMP\process-transfer-baseline.exe | Tee-Object benchmarks/results/baseline.csv
```

The harness records its own runtime metadata. Keep power mode, background load, process bitness, compiler, optimization settings, and machine constant when comparing output files. Performance varies by machine and Windows build; do not treat one machine's result as a universal speed ranking.
