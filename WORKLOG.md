# Worklog

## Goal and repository

Configured a local-first Codex engineering team and gated GitHub shipping workflow for `manualmapped/UserMode-Memory-R-W`.

The provided workspace was an empty `master` repository without a remote. It remains untouched. The GitHub project is cloned in `UserMode-Memory-R-W/`, on `main` at `519eba410552cce9ef0443b5ea89deb20f82bf0b`, with `origin` set to the linked repository. Setup files are uncommitted; no project task, feature branch, commit, push, PR, or GitHub write has occurred.

## Environment and repository findings

- Windows 11 x64; Node.js 24.16.0; Codex CLI 0.162.0-alpha.17.2 logged in through ChatGPT; `@openai/codex-sdk` 0.162.1 installed.
- MinGW-w64 GCC 15.2 targeting `x86_64-w64-mingw32` and CMake are available. MSVC (`cl.exe`), `gh`, `clang++`, and Ninja are absent.
- Public `main` contained only `README.md` and `memory.h`, with no issue/PR, build/test setup, CI, or `.github` configuration. The project is a Windows x64 C++17 header-only library. README examples refer to absent `updated_memory.h`; `memory.h` is the checked-in source.
- GitHub app read access found default branch `main`, no open issues/PRs, no rulesets, and branch listing `protected: false`. The legacy protection endpoint returned 403, so an owner/admin still needs to confirm branch protection.

## Team and workflow

Added team/agent instructions, project brief and architecture notes, decisions/worklog, local task template, a bounded SDK runner, Windows C++ smoke check, GitHub issue/PR templates, CI, Dependabot, package metadata, and ignore rules. Local tasks go under ignored `.codex/local-tasks/`.

The runner requires an explicit issue or local task, enforces cycle/time/subagent limits, uses isolated worktrees and fresh Codex threads, and gates any eventual push/PR on review and local checks. It does not merge. Setup did not launch project work.

A real custom `morgan_chen` subagent was successfully started with the Codex CLI (thread `01a12b7f-6e21-7d41-a05e-a02619c4d6e0`); it read tracked file names. An initial `--ephemeral` delegation attempt could not produce a child rollout; a normal persistent run succeeded. A separate structured SDK runtime smoke turn passed (thread `01a12b87-3f11-7ee1-ab37-023309d04da5`).

## Verification

- `node --check scripts/codex-team.mjs`: passed.
- `npm run team -- --help`: passed.
- `npm run check:project`: passed using MinGW GCC 15.2; compiled and ran the x64 C++17 header smoke program. This does not test remote-process operations. MSVC is unavailable locally.
- `g++ -std=c++17 -fsyntax-only -x c++ memory.h`: passed (GCC emits the expected `#pragma once` main-file warning).
- `npm install --package-lock-only --ignore-scripts`: passed; 0 vulnerabilities across 4 audited packages.
- Temporary isolated Git fixtures passed: dry-run made no writes; non-default branch and unrelated dirt were rejected; ignored local task and sole selected `TASK.md` change were accepted; stop-file create/clear passed.
- `git diff --check` passed. Setup files are new/untracked; a separate whitespace scan found no trailing whitespace.
- Explicit issue dry-run stopped safely because `gh` is not installed. No GitHub mutation occurred.

## Remaining setup actions

Review and commit the uncommitted setup files in the nested project checkout before the first project task. Install and authenticate GitHub CLI for issue intake and push/PR support. Ask a repository owner/admin to confirm `main` protection and whether `msvc-x64` should be required. The MSVC CI job needs a future GitHub Actions run for validation.

## Follow-up after setup commit

- Installed GitHub CLI 2.102.0 with Windows Package Manager and completed device authentication as `manualmapped` after user authorization.
- README header correction was committed on `codex/task-correct-the-readme-examples-to-include-the-heade` (`f7af249`) and published as PR #1. PR is open; GitHub reports no checks because the workflow setup is not yet on the remote base branch.
- Setup review found the runner's Sam gate originally trusted structured model claims alone. It was changed to capture the Codex SDK's actual `receiver_thread_ids` and completed `agents_states`, and require a completed child whose prompt begins with the configured Sam role marker.
- Limitation: SDK events do not expose a child agent's configured identity. The runner documents and reports this accurately; the marker-bearing completion plus model-reported Sam review remains advisory, with human GitHub review required.
- Follow-up checks on the setup branch: `node --check scripts/codex-team.mjs`, `npm run team -- --help`, `npm run check:project` (MinGW x64 smoke passed), and `git diff --check` passed. Authenticated `--dry-run` passed while on clean `main`; a later dry-run on the dirty setup feature branch correctly blocked because it requires clean `main`.
- Next action: commit and open a separate PR for the setup workflow so its Windows MSVC CI can run.
- Setup workflow PR #2 is open at https://github.com/manualmapped/UserMode-Memory-R-W/pull/2; its `msvc-x64` GitHub Actions job passed. Human review and merge remain pending.
- Documentation PR #1 is open at https://github.com/manualmapped/UserMode-Memory-R-W/pull/1; it currently reports no checks because the workflow has not reached the base branch.
