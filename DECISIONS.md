# Decisions

## 2026-10-11 — Keep orchestration local and use the installed Codex CLI login

- Evidence: Windows 11 x64 host; `codex-cli 0.162.0-alpha.17.2` is installed and `codex login status` reports “Logged in using ChatGPT”. The official `@openai/codex-sdk` package documents a local CLI wrapper, fresh `startThread()` calls, structured output, streaming, and cancellation.
- Decision: use the local TypeScript SDK and current CLI session rather than the API-based Agents API (which needs an API key and separate billing).
- Consequence: each bounded cycle is a fresh local Codex thread. Timeouts are best-effort SDK cancellation; durable handoff lives in repository files and a local ignored run record.

## 2026-10-11 — Use a narrow Windows x64 C++17 smoke check

- Evidence: the repository has only `README.md` and `memory.h`; no build system or tests. README specifies Windows x64/C++17/Windows SDK and MSVC 2022+ or GCC 12+; `memory.h` uses Windows headers and `__forceinline`. Local MinGW-w64 GCC 15.2 targets `x86_64-w64-mingw32` and compiled and ran the smoke program; `cl.exe` is absent.
- Decision: add a C++17 compile-and-run smoke check for `mem::is_valid`, preferring MSVC and otherwise requiring the documented x64 MinGW target and GCC 12+.
- Consequence: CI configures an x64 MSVC environment on `windows-latest`; the local check does not claim to validate remote-process operations.

## 2026-10-11 — External GitHub shipping stays gated

- Evidence: repository `manualmapped/UserMode-Memory-R-W` is public, default branch `main`, and the GitHub app can read metadata/content. `gh` is not installed, public rulesets are empty, and the app receives 403 for legacy branch-protection details. The `main` branch listing says `protected: false`.
- Decision: add local issue/PR templates and read-only CI configuration, but make no GitHub mutations in setup. Future push/PR operations require the matching `origin` remote and authenticated `gh`.
- Consequence: branch protection and required checks must be verified by an owner/admin; no GitHub board, labels, rules, or owners are assumed.
