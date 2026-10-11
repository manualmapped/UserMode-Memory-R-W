# Project brief

## Verified purpose and shape

`UserMode-Memory-R-W` is a header-only C++ library for reading and writing another process's memory through Windows native system calls. The repository currently contains `memory.h` and `README.md` only (GitHub tree for `main`, inspected 2026-10-11).

## Verified constraints

- `memory.h` includes Windows headers and uses Win32 process, module, virtual-memory, and Tool Help APIs.
- The header uses C++17 library facilities (`std::optional`, inline variables) and MSVC's `__forceinline`; the README states Windows x64, C++17, Windows SDK, MSVC 2022+ or GCC 12+.
- The setup check compiles and runs a small x64 C++17 program that includes `memory.h` and calls only `mem::is_valid`. It passed locally with MinGW-w64 GCC 15.2 (`x86_64-w64-mingw32`). MSVC is absent locally; CI is configured for MSVC on `windows-latest`.
- No project/build files, dependency manifest, tests, CI, release process, `.github` configuration, or `AGENTS.md` existed on the inspected `main` branch.
- `README.md` refers to `updated_memory.h` in its examples, but that file is absent. Treat the checked-in `memory.h` as the source of truth and flag the documentation mismatch when relevant.

## Assumptions and open questions

- README compatibility claims (Windows 7–11 and GCC 12+) have not been independently validated.
- No automated behavioral test suite exists. The added Windows check compiles and runs a narrow x64 header smoke program; it does not validate remote-process operations.
- Branch protection could not be inspected: the connected GitHub integration returned 403 for the legacy protection endpoint. The public rulesets endpoint returned an empty list.

## Pointers

- Public API and process/session lifecycle: `memory.h` (`mem::ScopedSession`, `initialize`, `shutdown`).
- Native stub generation and syscall-number scanning: `memory.h` (`mem::hidden::alloc_stub`, `mem::hidden::scan_id`).
- Reads/writes and address bounds: `memory.h` (`mem::read_memory`, `mem::write_raw`, `mem::is_valid`).
- Human-facing claimed API/build requirements: `README.md`.
- Local workflow: `TEAM.md`, `TASK.md`, `scripts/codex-team.mjs`.
