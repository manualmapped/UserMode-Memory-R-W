#pragma once

#include <iostream>
#include <windows.h>
#include <psapi.h>
#include <TlHelp32.h>
#include <cstdint>

namespace base::memory
{
    inline INT32 PID = 0;
    inline HANDLE handleProcess = nullptr;
    inline uintptr_t base = 0;

    inline void* syscallReadStubPtr = nullptr;
    inline void* syscallWriteStubPtr = nullptr;
    inline bool syscallInitialized = false;

    inline NTSTATUS(NTAPI* syscall_NtReadVirtualMemory)(HANDLE, PVOID, PVOID, ULONG, PULONG) = nullptr;
    inline NTSTATUS(NTAPI* syscall_NtWriteVirtualMemory)(HANDLE, PVOID, PVOID, ULONG, PULONG) = nullptr;

    inline NTSTATUS(NTAPI* ZwReadVirtualMemory)(HANDLE, PVOID, PVOID, SIZE_T, PSIZE_T) = nullptr;
    inline NTSTATUS(NTAPI* ZwWriteVirtualMemory)(HANDLE, PVOID, PVOID, SIZE_T, PSIZE_T) = nullptr;

    inline INT32 FindProcess(LPCTSTR processName) {
        PROCESSENTRY32 processEntry{};
        HANDLE handleSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (handleSnap == INVALID_HANDLE_VALUE) {
            std::cout << "[-] failed to create snapshot" << std::endl;
            return 0;
        }
        processEntry.dwSize = sizeof(PROCESSENTRY32);

        if (Process32First(handleSnap, &processEntry)) {
            do {
                if (!lstrcmpi(processEntry.szExeFile, processName)) {
                    CloseHandle(handleSnap);
                    return processEntry.th32ProcessID;
                }
            } while (Process32Next(handleSnap, &processEntry));
        }
        CloseHandle(handleSnap);
        return 0;
    }

    inline uintptr_t GetProcessBase() {
        HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, PID);
        if (hProcess == nullptr) {
            std::cout << "[-] failed to open process to get base" << std::endl;
            return 0;
        }

        HMODULE handleModules[1024];
        DWORD BytesNeeded = 0;

        uintptr_t baseAddy = 0;
        if (EnumProcessModules(hProcess, handleModules, sizeof(handleModules), &BytesNeeded) && BytesNeeded > 0) {
            baseAddy = reinterpret_cast<uintptr_t>(handleModules[0]);
        }
        else {
            std::cout << "[-] failed to enumerate modules" << std::endl;
            CloseHandle(hProcess);
            return 0;
        }

        CloseHandle(hProcess);
        return baseAddy;
    }

    inline bool IsValid(uintptr_t address) {
        return address != 0 && address >= 0x10000 && address < 0x7FFFFFFFFFFF;
    }

    inline void* CreateSyscallStub(uint32_t syscallId) {
        uint8_t stub[] = {
            0x4C, 0x8B, 0xD1,
            0xB8, 0x00, 0x00, 0x00, 0x00,
            0x0F, 0x05,
            0xC3
        };

        *reinterpret_cast<uint32_t*>(&stub[4]) = syscallId;

        void* page = VirtualAlloc(nullptr, sizeof(stub), MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        if (!page) return nullptr;

        memcpy(page, stub, sizeof(stub));
        return page;
    }

    inline uint32_t GetSyscallID(const char* funcName) {
        HMODULE ntdll = GetModuleHandleA("ntdll.dll");
        if (!ntdll) return 0;

        unsigned char* funcAddr = reinterpret_cast<unsigned char*>(GetProcAddress(ntdll, funcName));
        if (!funcAddr) return 0;

        for (int i = 0; i < 20; i++) {
            if (funcAddr[i] == 0xB8) {
                return *reinterpret_cast<uint32_t*>(funcAddr + i + 1);
            }
        }
        return 0;
    }

    inline void EnsureSyscallInit() {
        if (syscallInitialized)
            return;

        if (PID == 0) {
            std::cout << "[-] PID is 0" << std::endl;
            return;
        }

        if (!handleProcess) {
            handleProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, PID);
            if (!handleProcess) {
                std::cout << "[-] failed to open process handle for syscall" << std::endl;
                return;
            }
        }

        uint32_t readSyscallId = GetSyscallID("NtReadVirtualMemory");
        if (readSyscallId == 0) {
            std::cout << "[-] failed to get syscall ID for NtReadVirtualMemory" << std::endl;
            return;
        }
        syscallReadStubPtr = CreateSyscallStub(readSyscallId);
        if (!syscallReadStubPtr) {
            std::cout << "[-] failed to create syscall_read stub" << std::endl;
            return;
        }
        syscall_NtReadVirtualMemory = reinterpret_cast<decltype(syscall_NtReadVirtualMemory)>(syscallReadStubPtr);

        uint32_t writeSyscallId = GetSyscallID("NtWriteVirtualMemory");
        if (writeSyscallId == 0) {
            std::cout << "[-] failed to get syscall ID for NtWriteVirtualMemory" << std::endl;
            return;
        }
        syscallWriteStubPtr = CreateSyscallStub(writeSyscallId);
        if (!syscallWriteStubPtr) {
            std::cout << "[-] failed to create syscall_write stub" << std::endl;
            return;
        }
        syscall_NtWriteVirtualMemory = reinterpret_cast<decltype(syscall_NtWriteVirtualMemory)>(syscallWriteStubPtr);

        HMODULE ntdll = GetModuleHandleA("ntdll.dll");
        if (!ntdll) {
            std::cout << "[-] failed to get ntdll.dll module" << std::endl;
            return;
        }

        ZwReadVirtualMemory = reinterpret_cast<decltype(ZwReadVirtualMemory)>(GetProcAddress(ntdll, "ZwReadVirtualMemory"));
        ZwWriteVirtualMemory = reinterpret_cast<decltype(ZwWriteVirtualMemory)>(GetProcAddress(ntdll, "ZwWriteVirtualMemory"));
        if (!ZwReadVirtualMemory || !ZwWriteVirtualMemory) {
            std::cout << "[-] failed to get Zw functions" << std::endl;
            return;
        }

        syscallInitialized = true;
    }

    template <typename T>
    T read(uint64_t addy) {
        if (!syscallInitialized) EnsureSyscallInit();

        static const auto NtReadVirtualMemory = reinterpret_cast<NTSTATUS(NTAPI*)(HANDLE, PVOID, PVOID, SIZE_T, PSIZE_T)>(
            GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtReadVirtualMemory")
            );

        T buffer{};
        SIZE_T bytesRead{};
        if (NtReadVirtualMemory && handleProcess)
            NtReadVirtualMemory(handleProcess, reinterpret_cast<PVOID>(addy), &buffer, sizeof(T), &bytesRead);
        return buffer;
    }

    template <typename T>
    T syscall_read(uint64_t address) {
        if (!syscallInitialized) EnsureSyscallInit();

        T buffer{};
        SIZE_T bytesRead = 0;

        if (!handleProcess || !syscall_NtReadVirtualMemory) {
            std::cout << "[-] syscall_read invalid" << std::endl;
            return buffer;
        }

        syscall_NtReadVirtualMemory(
            handleProcess,
            reinterpret_cast<PVOID>(address),
            &buffer,
            sizeof(T),
            reinterpret_cast<PULONG>(&bytesRead)
        );

        return buffer;
    }

    template <typename T>
    T zw_read(uint64_t address) {
        if (!syscallInitialized) EnsureSyscallInit();

        T buffer{};
        SIZE_T bytesRead = 0;

        if (!handleProcess || !ZwReadVirtualMemory) {
            std::cout << "[-] zw_read invalid" << std::endl;
            return buffer;
        }

        ZwReadVirtualMemory(
            handleProcess,
            reinterpret_cast<PVOID>(address),
            &buffer,
            sizeof(T),
            &bytesRead
        );

        return buffer;
    }

    inline NTSTATUS(NTAPI* NtWriteVirtualMemory)(HANDLE, PVOID, PVOID, SIZE_T, PSIZE_T) =
        reinterpret_cast<decltype(NtWriteVirtualMemory)>(GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtWriteVirtualMemory"));

    __forceinline bool write_bytes(uint64_t address, const void* buffer, size_t size) {
        if (!syscallInitialized) EnsureSyscallInit();

        if (!NtWriteVirtualMemory || !handleProcess)
            return false;

        SIZE_T bytesWritten{};
        NTSTATUS status = NtWriteVirtualMemory(handleProcess, (PVOID)address, (PVOID)buffer, size, &bytesWritten);
        return status >= 0 && bytesWritten == size;
    }

    template <typename T>
    __forceinline bool write(uint64_t address, const T& buffer) {
        return write_bytes(address, &buffer, sizeof(T));
    }

    template <typename T>
    bool syscall_write(uint64_t address, const T& buffer) {
        if (!syscallInitialized) EnsureSyscallInit();

        if (!handleProcess || !syscall_NtWriteVirtualMemory) {
            std::cout << "[-] syscall_write invalid" << std::endl;
            return false;
        }

        SIZE_T bytesWritten = 0;
        syscall_NtWriteVirtualMemory(
            handleProcess,
            reinterpret_cast<PVOID>(address),
            (PVOID)&buffer,
            sizeof(T),
            reinterpret_cast<PULONG>(&bytesWritten)
        );

        return true;
    }

    template <typename T>
    bool zw_write(uint64_t address, const T& buffer) {
        if (!syscallInitialized) EnsureSyscallInit();

        if (!handleProcess || !ZwWriteVirtualMemory) {
            std::cout << "[-] zw_write invalid" << std::endl;
            return false;
        }

        SIZE_T bytesWritten = 0;
        ZwWriteVirtualMemory(
            handleProcess,
            reinterpret_cast<PVOID>(address),
            (PVOID)&buffer,
            sizeof(T),
            &bytesWritten
        );

        return (bytesWritten == sizeof(T));
    }
}
