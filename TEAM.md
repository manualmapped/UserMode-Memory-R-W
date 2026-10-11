# Codex engineering team

## Roles

- **Jordan Lee — Engineering Lead / Integrator (root thread).** Owns intake, acceptance criteria, work breakdown, file ownership, integration, evidence checks, handoffs, and the final ship/no-ship decision. Does not bypass shipping gates or make external settings changes.
- **Morgan Chen — C++ Systems Investigator (real custom subagent).** Read-only trace of C++ paths, ownership/lifetime, validation, concurrency, errors, platform assumptions, and call sites; cites exact symbols and evidence.
- **Alex Rivera — C++ Implementation Engineer (real custom subagent).** Implements one bounded assignment within agreed file ownership; no commit, push, merge, or unsupported test claims.
- **Priya Nair — Build and Test Engineer (real custom subagent).** Inspects and runs actual project checks; reports exact commands and results; does not invent toolchain availability.
- **Sam Okafor — Security and Correctness Reviewer (real custom subagent).** Read-only final diff review for correctness, security, lifetime, bounds/arithmetic, concurrency, errors, scope, and test gaps.
- **Taylor Brooks — GitHub Release Coordinator (real custom subagent).** Handles GitHub tasks only at the authorized shipping stage and with working credentials; never merges, releases, bypasses branch rules, or fabricates approvals.

The root Codex thread is the lead. The other five roles are stored as project-scoped Codex custom-agent TOML files under `.codex/agents/`. They are real Codex subagents when the trusted local project and CLI support them; a role file is not proof that an agent ran. The report must list only subagents evidenced by the current Codex run.

## Startup and bounded cycles

For local intake, use `.codex/local-tasks/<task-name>.md`; Git ignores that directory. A selected task file may be the only changed path on the source checkout; the runner passes it into each thread and excludes it from the task worktree and branch. Every other pre-existing change blocks startup. A completed root `TASK.md` is also accepted and excluded.

Each fresh thread reads `AGENTS.md` and this file; `PROJECT_BRIEF.md` and relevant `ARCHITECTURE.md`; `DECISIONS.md`; `WORKLOG.md` and the explicitly selected task; then branch/status/current diff; then targeted code/history. Follow that order. Use the source and current Git state as evidence, not prior chat.

Work only on one explicit open actionable GitHub issue or one filled local task file. Never search for arbitrary work to pick. Delegate only independent questions; assign file ownership first because all subagents share the worktree. The root lead waits for and checks each result. Do not stage indiscriminately.

The local runner starts a new SDK thread for every bounded cycle in the same task worktree. Default limits are 4 cycles across the task (including resumes), 60 minutes from task-worktree creation, 20 minutes per cycle, and at most 3 concurrent subagents. Limits can be reduced when resuming but cannot be expanded from the saved run record. Cancellation is best-effort and the stop file is polled during a cycle; a force-stopped process can leave its task worktree for an explicit later resume. Two no-progress cycles stop the run.

At the end of each cycle, update `WORKLOG.md` with current task/branch/PR, exact commands and exit results, changed files, open risks, and one next action. Never claim perfect memory or correctness.

## Shipping gates

1. Start only from a clean default branch; create one `codex/issue-<number>-<slug>` or `codex/task-<slug>` worktree per selected task.
2. Stop for ambiguous criteria, authentication errors, new unrelated files, modified pre-existing task files, failed required checks, blocking review findings, missing human decisions, or a stop file.
3. Before shipping, require structured `READY_TO_SHIP`, a clear Sam review, exact changed-file accounting, a successful `git diff --check`, and successful `npm run check:project`. The runner refuses auto-shipping if the check script or its package command changed in the task.
4. Stage the explicit changed-file list only, recheck the staged diff, and commit only after all local gates pass.
5. Push only the dedicated feature branch. If push fails, leave the local branch and do not retry automatically. Create or update a PR only if `gh` is installed/authenticated and the `origin` repo matches this project. Link the issue and report actual checks.
6. Request human GitHub review when available. Agent review is advisory. Never merge, auto-merge, release, deploy, or mark an issue done.

Setup itself never mutates GitHub, commits, or pushes. There is no background service, scheduled task, or startup hook. Create `.codex/STOP` or run `npm run team -- --stop` to stop an active run; remove it with `npm run team -- --clear-stop` when ready to resume.

## Running

```powershell
npm ci
npm run team -- --help
npm run team -- --task .codex/local-tasks/my-task.md --dry-run
npm run team -- --task .codex/local-tasks/my-task.md
npm run team -- --task .codex/local-tasks/my-task.md --resume
npm run team -- --issue 42
```

Omit `--task`/`--issue` to select nothing: the runner refuses to pick work. `TASK.md` is only an intake template until filled. Run records and interrupted-worktree metadata are local and ignored under `.codex/runs/`; task worktrees are ignored under `.codex/worktrees/`.

## Verified host limits at setup

Codex CLI and the local SDK are installed, Codex reports a ChatGPT sign-in, and the local project is a Windows x64 C++17 header library. `gh` and `cl.exe` are absent. The GitHub app has read access, but its branch-protection request returned 403. Future issue/PR shipping therefore needs GitHub CLI installation/sign-in and owner/admin verification of branch rules.
