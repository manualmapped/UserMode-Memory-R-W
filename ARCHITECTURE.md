# Verified architecture

This repository currently has one implementation header, `memory.h`; there are no targets or separate components to map.

- `mem::find_process` (lines 36–56) enumerates local processes with Tool Help and returns the first case-insensitive executable-name match.
- `mem::get_base_address` (58–82) opens or reuses a process handle and uses `EnumProcessModules` to return the first module address.
- `mem::hidden::scan_id` (161–193) looks up an ntdll export, queries its memory region, and scans a bounded byte window for a syscall instruction and preceding `mov eax` opcode.
- `mem::hidden::alloc_stub` (133–159) allocates writable memory, copies a syscall stub, then changes it to execute/read protection.
- `mem::initialize_impl` (195–249) opens the target with `TARGET_RIGHTS`, resolves both syscall IDs, creates both stubs, stores function pointers, and publishes `ready`.
- `mem::read_memory` (257–287) and `mem::write_raw` (289–316) use an atomic ready fast path; otherwise they lock `init_mutex`, validate the address, lazily initialize, and call the syscall wrapper.
- `mem::shutdown_impl` (325–337) clears readiness, closes the shared process handle, and resets `pid` and `base_addr`. `ScopedSession` (345–407) serializes bind/initialization and calls shutdown from its destructor when active.

These references describe the checked-in implementation, not an independent correctness or thread-safety validation. In particular, the README's stronger concurrency and compatibility claims remain unverified.
