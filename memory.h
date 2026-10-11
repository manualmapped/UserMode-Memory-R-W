#pragma once

#include <windows.h>
#include <winternl.h>
#include <psapi.h>
#include <TlHelp32.h>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <type_traits>
#include <optional>
#include <mutex>
#include <shared_mutex>
#include <atomic>

namespace mem
{
    using read_fn = NTSTATUS(__fastcall*)(HANDLE, PVOID, PVOID, SIZE_T, PSIZE_T);
    using write_fn = NTSTATUS(__fastcall*)(HANDLE, PVOID, PVOID, SIZE_T, PSIZE_T);

    inline INT32 pid = 0;
    inline HANDLE process_handle = nullptr;
    inline uintptr_t base_addr = 0;

    inline void* read_stub = nullptr;
    inline void* write_stub = nullptr;
    inline read_fn read_syscall = nullptr;
    inline write_fn write_syscall = nullptr;
    inline std::atomic<bool> ready{false};
    inline std::shared_mutex init_mutex;

    constexpr uintptr_t MIN_ADDRESS = 0x10000;
    constexpr uintptr_t MAX_ADDRESS = 0x7FFFFFFFFFFF;
    constexpr DWORD TARGET_RIGHTS = PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_QUERY_INFORMATION;
    constexpr int SCAN_DEPTH = 64;
    constexpr int LOOKBACK_MAX = 32;

    __forceinline INT32 find_process(LPCTSTR name) noexcept
    {
        if (!name) return 0;

        PROCESSENTRY32 entry{};
        entry.dwSize = sizeof(entry);
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap == INVALID_HANDLE_VALUE) return 0;

        BOOL ok = Process32First(snap, &entry);
        while (ok)
        {
            if (!lstrcmpi(entry.szExeFile, name))
            {
                CloseHandle(snap);
                return entry.th32ProcessID;
            }
            ok = Process32Next(snap, &entry);
        }

        CloseHandle(snap);
        return 0;
    }

    __forceinline uintptr_t get_base_address() noexcept
    {
        std::shared_lock<std::shared_mutex> lock(init_mutex);

        HANDLE h = process_handle;
        bool local_handle = false;

        if (!h)
        {
            if (pid == 0) return 0;
            h = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
            if (!h) return 0;
            local_handle = true;
        }

        HMODULE mods[64];
        DWORD need = 0;
        uintptr_t out = 0;

        if (EnumProcessModules(h, mods, sizeof(mods), &need) && need > 0)
            out = reinterpret_cast<uintptr_t>(mods[0]);

        if (local_handle) CloseHandle(h);
        return out;
    }

    __forceinline bool is_valid(uintptr_t address) noexcept
    {
        return address >= MIN_ADDRESS && address <= MAX_ADDRESS;
    }

    __forceinline bool is_valid_range(uint64_t address, std::size_t size) noexcept
    {
        if (size == 0 || address > static_cast<uint64_t>(MAX_ADDRESS)) return false;
        const uint64_t last_offset = static_cast<uint64_t>(size - 1);
        if (last_offset > static_cast<uint64_t>(MAX_ADDRESS) - address) return false;
        return address >= MIN_ADDRESS && address + last_offset <= MAX_ADDRESS;
    }

    __forceinline bool is_ready() noexcept
    {
        return ready.load(std::memory_order_acquire);
    }

    namespace detail
    {
        template <typename T>
        __forceinline std::optional<T> read_io(uint64_t address) noexcept
        {
            T value{};
            SIZE_T fetched = 0;

            NTSTATUS status = read_syscall(
                process_handle,
                reinterpret_cast<PVOID>(address),
                &value,
                sizeof(T),
                &fetched
            );

            if (status >= 0 && fetched == sizeof(T))
                return value;

            return std::nullopt;
        }

        __forceinline bool write_io(uint64_t address, void* data, std::size_t size) noexcept
        {
            SIZE_T pushed = 0;
            NTSTATUS status = write_syscall(
                process_handle,
                reinterpret_cast<PVOID>(address),
                data,
                size,
                &pushed
            );

            return (status >= 0 && pushed == size);
        }
    }

    namespace hidden
    {
        __forceinline void* alloc_stub(uint32_t syscall_id) noexcept
        {
            constexpr uint8_t SHELL[] = {
                0x4C, 0x8B, 0xD1,
                0xB8, 0x00, 0x00, 0x00, 0x00,
                0x0F, 0x05,
                0xC3
            };

            uint8_t buf[sizeof(SHELL)];
            memcpy(buf, SHELL, sizeof(SHELL));
            *reinterpret_cast<uint32_t*>(buf + 4) = syscall_id;

            void* page = VirtualAlloc(nullptr, sizeof(buf), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
            if (!page) return nullptr;

            memcpy(page, buf, sizeof(buf));

            DWORD prot = 0;
            if (!VirtualProtect(page, sizeof(buf), PAGE_EXECUTE_READ, &prot))
            {
                VirtualFree(page, 0, MEM_RELEASE);
                return nullptr;
            }

            return page;
        }

        __forceinline uint32_t scan_id(const char* target) noexcept
        {
            HMODULE ntdll = GetModuleHandleA("ntdll.dll");
            if (!ntdll) return 0;

            const auto* bytes = reinterpret_cast<const uint8_t*>(GetProcAddress(ntdll, target));
            if (!bytes) return 0;

            MEMORY_BASIC_INFORMATION mbi{};
            if (!VirtualQuery(bytes, &mbi, sizeof(mbi))) return 0;
            const DWORD protection = mbi.Protect & 0xFF;
            const bool readable = protection == PAGE_READONLY || protection == PAGE_READWRITE ||
                protection == PAGE_WRITECOPY || protection == PAGE_EXECUTE_READ ||
                protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY;
            if (mbi.State != MEM_COMMIT || (mbi.Protect & PAGE_GUARD) || !readable) return 0;

            uintptr_t region_start = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
            if (mbi.RegionSize > UINTPTR_MAX - region_start) return 0;
            uintptr_t region_end = region_start + mbi.RegionSize;
            uintptr_t func_start = reinterpret_cast<uintptr_t>(bytes);
            if (func_start < region_start || func_start >= region_end) return 0;
            SIZE_T safe_remaining = region_end - func_start;
            int limit = static_cast<int>(safe_remaining < static_cast<SIZE_T>(SCAN_DEPTH) ? safe_remaining : static_cast<SIZE_T>(SCAN_DEPTH));

            for (int i = 0; i < limit - 1; ++i)
            {
                if (bytes[i] == 0x0F && bytes[i + 1] == 0x05)
                {
                    int low = (i - LOOKBACK_MAX) > 0 ? (i - LOOKBACK_MAX) : 0;
                    for (int j = i - 1; j >= low; --j)
                    {
                        if (bytes[j] == 0xB8 && j + 5 <= i)
                        {
                            uint32_t syscall_id = 0;
                            memcpy(&syscall_id, bytes + j + 1, sizeof(syscall_id));
                            return syscall_id;
                        }
                    }
                    break;
                }
            }

            return 0;
        }
    }

    inline bool initialize_impl() noexcept
    {
        if (ready.load(std::memory_order_relaxed)) return true;
        if (pid == 0) return false;

        if (!process_handle)
        {
            process_handle = OpenProcess(TARGET_RIGHTS, FALSE, pid);
            if (!process_handle) return false;
        }

        auto cleanup = []() noexcept
        {
            ready.store(false, std::memory_order_release);
            read_syscall = nullptr;
            write_syscall = nullptr;
            if (read_stub) VirtualFree(read_stub, 0, MEM_RELEASE);
            if (write_stub) VirtualFree(write_stub, 0, MEM_RELEASE);
            read_stub = nullptr;
            write_stub = nullptr;
            if (process_handle) CloseHandle(process_handle);
            process_handle = nullptr;
        };

        HMODULE ntdll = GetModuleHandleA("ntdll.dll");
        if (!ntdll)
        {
            cleanup();
            return false;
        }

        uint32_t rid = hidden::scan_id("NtReadVirtualMemory");
        uint32_t wid = hidden::scan_id("NtWriteVirtualMemory");

        if (rid == 0 || wid == 0)
        {
            cleanup();
            return false;
        }

        read_stub = hidden::alloc_stub(rid);
        write_stub = hidden::alloc_stub(wid);

        if (!read_stub || !write_stub)
        {
            cleanup();
            return false;
        }

        read_syscall = reinterpret_cast<read_fn>(read_stub);
        write_syscall = reinterpret_cast<write_fn>(write_stub);

        ready.store(true, std::memory_order_release);
        return true;
    }

    inline bool initialize() noexcept
    {
        std::unique_lock<std::shared_mutex> lock(init_mutex);
        return initialize_impl();
    }

    template <typename T>
    __forceinline std::optional<T> read_memory(uint64_t address) noexcept
    {
        if (!is_valid_range(address, sizeof(T))) return std::nullopt;

        std::shared_lock<std::shared_mutex> lock(init_mutex);
        if (!ready.load(std::memory_order_acquire))
        {
            lock.unlock();
            std::unique_lock<std::shared_mutex> init_lock(init_mutex);
            if (!ready.load(std::memory_order_relaxed) && !initialize_impl()) return std::nullopt;
            init_lock.unlock();
            lock.lock();
        }
        if (!ready.load(std::memory_order_acquire)) return std::nullopt;
        return detail::read_io<T>(address);
    }

    __forceinline bool write_raw(uint64_t address, void* data, std::size_t size) noexcept
    {
        if (!data || !is_valid_range(address, size)) return false;

        std::shared_lock<std::shared_mutex> lock(init_mutex);
        if (!ready.load(std::memory_order_acquire))
        {
            lock.unlock();
            std::unique_lock<std::shared_mutex> init_lock(init_mutex);
            if (!ready.load(std::memory_order_relaxed) && !initialize_impl()) return false;
            init_lock.unlock();
            lock.lock();
        }
        if (!ready.load(std::memory_order_acquire)) return false;
        return detail::write_io(address, data, size);
    }

    template <typename T>
    __forceinline bool write_memory(uint64_t address, const T& value) noexcept
    {
        T copy = value;
        return write_raw(address, &copy, sizeof(T));
    }

    inline void shutdown_impl() noexcept
    {
        ready.store(false, std::memory_order_release);

        read_syscall = nullptr;
        write_syscall = nullptr;
        if (read_stub) VirtualFree(read_stub, 0, MEM_RELEASE);
        if (write_stub) VirtualFree(write_stub, 0, MEM_RELEASE);
        read_stub = nullptr;
        write_stub = nullptr;
        if (process_handle) CloseHandle(process_handle);
        process_handle = nullptr;

        pid = 0;
        base_addr = 0;
    }

    inline void shutdown() noexcept
    {
        std::unique_lock<std::shared_mutex> lock(init_mutex);
        shutdown_impl();
    }

    // An independent process target. Unlike the legacy free functions, each
    // ProcessSession owns its handle and syscall stubs, so sessions do not
    // replace one another's target.
    class ProcessSession
    {
    public:
        ProcessSession() noexcept = default;
        ~ProcessSession() noexcept { close(); }

        ProcessSession(const ProcessSession&) = delete;
        ProcessSession& operator=(const ProcessSession&) = delete;

        ProcessSession(ProcessSession&& other) noexcept
        {
            std::unique_lock<std::shared_mutex> lock(other.mutex_);
            move_from_locked(other);
        }

        ProcessSession& operator=(ProcessSession&& other) noexcept
        {
            if (this == &other) return *this;
            std::unique_lock<std::shared_mutex> left(mutex_, std::defer_lock);
            std::unique_lock<std::shared_mutex> right(other.mutex_, std::defer_lock);
            std::lock(left, right);
            release_locked();
            move_from_locked(other);
            return *this;
        }

        bool bind(INT32 target_pid) noexcept
        {
            std::unique_lock<std::shared_mutex> lock(mutex_);
            release_locked();
            if (target_pid <= 0) return false;

            HANDLE handle = OpenProcess(TARGET_RIGHTS, FALSE, static_cast<DWORD>(target_pid));
            if (!handle) return false;

            void* read_page = nullptr;
            void* write_page = nullptr;
            auto cleanup = [&]() noexcept
            {
                if (read_page) VirtualFree(read_page, 0, MEM_RELEASE);
                if (write_page) VirtualFree(write_page, 0, MEM_RELEASE);
                CloseHandle(handle);
            };

            HMODULE ntdll = GetModuleHandleA("ntdll.dll");
            const uint32_t read_id = ntdll ? hidden::scan_id("NtReadVirtualMemory") : 0;
            const uint32_t write_id = ntdll ? hidden::scan_id("NtWriteVirtualMemory") : 0;
            if (read_id == 0 || write_id == 0)
            {
                cleanup();
                return false;
            }

            read_page = hidden::alloc_stub(read_id);
            if (read_page) write_page = hidden::alloc_stub(write_id);
            if (!read_page || !write_page)
            {
                cleanup();
                return false;
            }

            HMODULE modules[64]{};
            DWORD needed = 0;
            uintptr_t module_base = 0;
            if (EnumProcessModules(handle, modules, sizeof(modules), &needed) && needed >= sizeof(HMODULE))
                module_base = reinterpret_cast<uintptr_t>(modules[0]);

            handle_ = handle;
            read_stub_ = read_page;
            write_stub_ = write_page;
            read_syscall_ = reinterpret_cast<read_fn>(read_stub_);
            write_syscall_ = reinterpret_cast<write_fn>(write_stub_);
            pid_ = target_pid;
            base_addr_ = module_base;
            return true;
        }

        bool bind(LPCTSTR executable_name) noexcept
        {
            const INT32 target_pid = find_process(executable_name);
            if (!target_pid)
            {
                close();
                return false;
            }
            return bind(target_pid);
        }

        void close() noexcept
        {
            std::unique_lock<std::shared_mutex> lock(mutex_);
            release_locked();
        }

        bool is_open() const noexcept
        {
            std::shared_lock<std::shared_mutex> lock(mutex_);
            return handle_ != nullptr && read_syscall_ != nullptr && write_syscall_ != nullptr;
        }

        INT32 process_id() const noexcept
        {
            std::shared_lock<std::shared_mutex> lock(mutex_);
            return pid_;
        }

        uintptr_t base_address() const noexcept
        {
            std::shared_lock<std::shared_mutex> lock(mutex_);
            return base_addr_;
        }

        template <typename T>
        std::optional<T> read(uint64_t address) const noexcept
        {
            static_assert(std::is_trivially_copyable<T>::value, "ProcessSession::read requires a trivially copyable type");
            if (!is_valid_range(address, sizeof(T))) return std::nullopt;
            std::shared_lock<std::shared_mutex> lock(mutex_);
            if (!is_open_locked()) return std::nullopt;
            T value{};
            SIZE_T transferred = 0;
            const NTSTATUS status = read_syscall_(handle_, reinterpret_cast<PVOID>(address), &value, sizeof(T), &transferred);
            if (status >= 0 && transferred == sizeof(T)) return value;
            return std::nullopt;
        }

        template <typename T>
        bool write(uint64_t address, const T& value) const noexcept
        {
            static_assert(std::is_trivially_copyable<T>::value, "ProcessSession::write requires a trivially copyable type");
            T copy = value;
            return write_raw(address, &copy, sizeof(T));
        }

        bool read_raw(uint64_t address, void* data, std::size_t size) const noexcept
        {
            if (!data || !is_valid_range(address, size)) return false;
            std::shared_lock<std::shared_mutex> lock(mutex_);
            if (!is_open_locked()) return false;
            SIZE_T transferred = 0;
            const NTSTATUS status = read_syscall_(handle_, reinterpret_cast<PVOID>(address), data, size, &transferred);
            return status >= 0 && transferred == size;
        }

        bool write_raw(uint64_t address, const void* data, std::size_t size) const noexcept
        {
            if (!data || !is_valid_range(address, size)) return false;
            std::shared_lock<std::shared_mutex> lock(mutex_);
            if (!is_open_locked()) return false;
            SIZE_T transferred = 0;
            const NTSTATUS status = write_syscall_(handle_, reinterpret_cast<PVOID>(address),
                const_cast<void*>(data), size, &transferred);
            return status >= 0 && transferred == size;
        }

    private:
        bool is_open_locked() const noexcept
        {
            return handle_ != nullptr && read_syscall_ != nullptr && write_syscall_ != nullptr;
        }

        void release_locked() noexcept
        {
            if (read_stub_) VirtualFree(read_stub_, 0, MEM_RELEASE);
            if (write_stub_) VirtualFree(write_stub_, 0, MEM_RELEASE);
            if (handle_) CloseHandle(handle_);
            read_stub_ = nullptr;
            write_stub_ = nullptr;
            read_syscall_ = nullptr;
            write_syscall_ = nullptr;
            handle_ = nullptr;
            pid_ = 0;
            base_addr_ = 0;
        }

        void move_from_locked(ProcessSession& other) noexcept
        {
            handle_ = other.handle_;
            read_stub_ = other.read_stub_;
            write_stub_ = other.write_stub_;
            read_syscall_ = other.read_syscall_;
            write_syscall_ = other.write_syscall_;
            pid_ = other.pid_;
            base_addr_ = other.base_addr_;
            other.handle_ = nullptr;
            other.read_stub_ = nullptr;
            other.write_stub_ = nullptr;
            other.read_syscall_ = nullptr;
            other.write_syscall_ = nullptr;
            other.pid_ = 0;
            other.base_addr_ = 0;
        }

        mutable std::shared_mutex mutex_;
        HANDLE handle_ = nullptr;
        void* read_stub_ = nullptr;
        void* write_stub_ = nullptr;
        read_fn read_syscall_ = nullptr;
        write_fn write_syscall_ = nullptr;
        INT32 pid_ = 0;
        uintptr_t base_addr_ = 0;
    };

    class ScopedSession
    {
    public:
        ScopedSession() = default;

        explicit ScopedSession(INT32 target_pid) noexcept
        {
            std::unique_lock<std::shared_mutex> lock(init_mutex);
            shutdown_impl();
            pid = target_pid;
            active = initialize_impl();
        }

        explicit ScopedSession(LPCTSTR target_name) noexcept
        {
            std::unique_lock<std::shared_mutex> lock(init_mutex);
            shutdown_impl();
            INT32 found = find_process(target_name);
            if (found != 0)
            {
                pid = found;
                active = initialize_impl();
            }
        }

        ~ScopedSession() noexcept
        {
            if (active) shutdown();
        }

        ScopedSession(const ScopedSession&) = delete;
        ScopedSession& operator=(const ScopedSession&) = delete;
        ScopedSession(ScopedSession&&) = delete;
        ScopedSession& operator=(ScopedSession&&) = delete;

        bool bind(INT32 target_pid) noexcept
        {
            std::unique_lock<std::shared_mutex> lock(init_mutex);
            shutdown_impl();
            pid = target_pid;
            active = initialize_impl();
            return active;
        }

        bool bind(LPCTSTR target_name) noexcept
        {
            std::unique_lock<std::shared_mutex> lock(init_mutex);
            shutdown_impl();
            INT32 found = find_process(target_name);
            if (found == 0) return false;
            pid = found;
            active = initialize_impl();
            return active;
        }

        explicit operator bool() const noexcept
        {
            return is_ready();
        }

    private:
        bool active = false;
    };
}
