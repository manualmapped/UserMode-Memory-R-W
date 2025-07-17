# UsermodeMemoryRW

External memory reading and writing for Windows, featuring both **standard WinAPI** and **direct syscall-based** access — no driver required

---

## Features

- External memory access (read/write)
- **Syscall stubs** for `NtReadVirtualMemory` / `NtWriteVirtualMemory`
- Manual syscall ID extraction
- Utility: Get base address, validate pointers, write to memory, etc
- Built with Roblox in mind, but general purpose

---

## Whats special about syscalls?

Standard WinAPI functions like `ReadProcessMemory` or `NtReadVirtualMemory` are often **hooked** by anticheats
This repo avoids those hooks by creating **manual syscall stubs** - executing the syscall instructions directly

This means:
- No kernel mode driver
- Reduced detection footprint
- Cleaner testing environment

---

## Example Usage

```cpp
int main() {
    base::memory::PID = base::memory::FindProcess(L"RobloxPlayerBeta.exe");
    base::memory::base = base::memory::GetProcessBase();

    std::cout << std::hex << "[+] Base: 0x" << base::memory::base << std::endl;

    IMAGE_DOS_HEADER dos = base::memory::syscall_read<IMAGE_DOS_HEADER>(base::memory::base);
    std::cout << std::hex << dos.e_magic << std::endl;

    return 0;
}
```
