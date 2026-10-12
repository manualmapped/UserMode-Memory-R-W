#include <windows.h>
#include <psapi.h>
#include <TlHelp32.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>

#include "memory.hpp"

static_assert(sizeof(void*) == 8, "The historical benchmark requires Windows x64.");

namespace
{
    constexpr std::size_t kDataBytes = 16u * 1024u * 1024u;
    constexpr unsigned kSamples = 7;
    alignas(64) unsigned char child_data[kDataBytes];

    struct Child
    {
        PROCESS_INFORMATION process{};
        HANDLE ready_event = nullptr;
        ~Child()
        {
            if (process.hProcess)
            {
                DWORD code = 0;
                if (GetExitCodeProcess(process.hProcess, &code) && code == STILL_ACTIVE)
                {
                    TerminateProcess(process.hProcess, 0);
                    WaitForSingleObject(process.hProcess, INFINITE);
                }
                CloseHandle(process.hProcess);
            }
            if (process.hThread) CloseHandle(process.hThread);
            if (ready_event) CloseHandle(ready_event);
        }
    };

    double now_seconds() noexcept
    {
        LARGE_INTEGER ticks{}, frequency{};
        QueryPerformanceCounter(&ticks);
        QueryPerformanceFrequency(&frequency);
        return static_cast<double>(ticks.QuadPart) / static_cast<double>(frequency.QuadPart);
    }

    template <typename T>
    T read_value(uint64_t address)
    {
        return base::memory::read<T>(address);
    }

    template <std::size_t N>
    struct Buffer { unsigned char bytes[N]; };

    template <std::size_t N>
    bool run_size(HANDLE process, uintptr_t address, const unsigned char* expected)
    {
        Buffer<N> readback{};
        std::vector<unsigned char> source(N, 0x5a);
        const unsigned iterations = (std::max)(20u, static_cast<unsigned>((32u * 1024u * 1024u) / N));
        const unsigned bounded_iterations = (std::min)(iterations, 2000u);
        std::vector<double> reads, writes;
        std::vector<unsigned char> actual(N);
        reads.reserve(kSamples);
        writes.reserve(kSamples);
        for (unsigned sample = 0; sample < kSamples; ++sample)
        {
            readback = read_value<Buffer<N>>(address);
            double start = now_seconds();
            for (unsigned i = 0; i < bounded_iterations; ++i)
                readback = read_value<Buffer<N>>(address);
            reads.push_back((now_seconds() - start) * 1.0e9 / bounded_iterations);
            if (std::memcmp(readback.bytes, expected, N) != 0)
            {
                std::fprintf(stderr, "historical read validation failed sample=%u size=%zu address=%p first=%02x expected=%02x\n",
                    sample, N, reinterpret_cast<void*>(address), readback.bytes[0], expected[0]);
                return false;
            }

            start = now_seconds();
            for (unsigned i = 0; i < bounded_iterations; ++i)
                if (!base::memory::write_bytes(address, source.data(), N))
                {
                    std::fprintf(stderr, "historical write failed at size %zu\n", N);
                    return false;
                }
            writes.push_back((now_seconds() - start) * 1.0e9 / bounded_iterations);

            SIZE_T transferred = 0;
            if (!ReadProcessMemory(process, reinterpret_cast<const void*>(address), actual.data(), N, &transferred) ||
                transferred != N || std::memcmp(actual.data(), source.data(), N) != 0)
            {
                std::fprintf(stderr, "historical write validation failed size=%zu bytes=%zu error=%lu\n",
                    N, static_cast<std::size_t>(transferred), GetLastError());
                return false;
            }
            if (!WriteProcessMemory(process, reinterpret_cast<void*>(address), expected, N, &transferred) ||
                transferred != N)
            {
                std::fprintf(stderr, "historical restore failed size=%zu error=%lu\n", N, GetLastError());
                return false;
            }
        }

        auto summarize = [](std::vector<double> values) {
            std::sort(values.begin(), values.end());
            const double median = values[values.size() / 2];
            const std::size_t p95_rank = static_cast<std::size_t>(std::ceil(0.95 * values.size()));
            const double p95 = values[(std::max)(std::size_t{ 1 }, p95_rank) - 1];
            return std::pair<double, double>{ median, p95 };
        };
        const auto read_stats = summarize(reads);
        const auto write_stats = summarize(writes);
        std::printf("historical_nt,read,%zu,1,%.2f,%.2f,%.2f,%.2f,%u\n", N,
            read_stats.first, read_stats.second, 1.0e9 / read_stats.first,
            (1.0e9 / read_stats.first) * N / (1024.0 * 1024.0), kSamples);
        std::printf("historical_nt,write,%zu,1,%.2f,%.2f,%.2f,%.2f,%u\n", N,
            write_stats.first, write_stats.second, 1.0e9 / write_stats.first,
            (1.0e9 / write_stats.first) * N / (1024.0 * 1024.0), kSamples);
        return true;
    }

    std::string executable_path()
    {
        std::vector<char> path(512);
        for (;;)
        {
            const DWORD capacity = static_cast<DWORD>(path.size());
            const DWORD length = GetModuleFileNameA(nullptr, path.data(), capacity);
            if (!length) return {};
            if (length < capacity) return std::string(path.data(), length);
            if (path.size() >= 32768) return {};
            path.resize((std::min)(path.size() * 2, std::size_t{32768}));
        }
    }
}

int main(int argc, char** argv)
{
    if (argc == 4 && std::strcmp(argv[1], "--child") == 0)
    {
        HANDLE ready = OpenEventA(EVENT_MODIFY_STATE, FALSE, argv[2]);
        if (!ready) return 20;
        SetEvent(ready);
        CloseHandle(ready);
        HANDLE parent = OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(std::strtoul(argv[3], nullptr, 10)));
        if (!parent) return 20;
        const DWORD result = WaitForSingleObject(parent, INFINITE);
        CloseHandle(parent);
        return result == WAIT_OBJECT_0 ? 0 : 20;
    }

    Child child;
    const std::string path = executable_path();
    const std::string event_name = "Local\\umrw-history-ready-" + std::to_string(GetCurrentProcessId());
    child.ready_event = CreateEventA(nullptr, TRUE, FALSE, event_name.c_str());
    if (path.empty() || !child.ready_event) return 21;
    std::string command = "\"" + path + "\" --child \"" + event_name + "\" " +
        std::to_string(GetCurrentProcessId());
    STARTUPINFOA startup{};
    startup.cb = sizeof(startup);
    if (!CreateProcessA(path.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
        nullptr, nullptr, &startup, &child.process) ||
        WaitForSingleObject(child.ready_event, 5000) != WAIT_OBJECT_0) return 22;

    for (std::size_t i = 0; i < kDataBytes; ++i)
        child_data[i] = static_cast<unsigned char>((i * 131u + 17u) & 0xffu);
    HANDLE writer = OpenProcess(PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_VM_OPERATION,
        FALSE, child.process.dwProcessId);
    if (!writer) return 23;
    void* remote_buffer = VirtualAllocEx(writer, nullptr, kDataBytes,
        MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!remote_buffer) return 24;
    const uintptr_t address = reinterpret_cast<uintptr_t>(remote_buffer);
    SIZE_T written = 0;
    if (!WriteProcessMemory(writer, remote_buffer,
        child_data, kDataBytes, &written) || written != kDataBytes) return 25;
    unsigned char first_byte = 0;
    SIZE_T read_count = 0;
    if (!ReadProcessMemory(writer, remote_buffer,
        &first_byte, 1, &read_count) || read_count != 1 || first_byte != child_data[0])
    {
        std::fprintf(stderr, "child fixture preflight failed address=%p first=%02x expected=%02x\n",
            remote_buffer, first_byte, child_data[0]);
        return 28;
    }

    base::memory::PID = static_cast<INT32>(child.process.dwProcessId);
    base::memory::handleProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, child.process.dwProcessId);
    if (!base::memory::handleProcess) { std::fprintf(stderr, "historical process open failed: %lu\n", GetLastError()); return 26; }
#if defined(HISTORICAL_387)
    HMODULE ntdll = GetModuleHandleA("ntdll.dll");
    if (!ntdll) { std::fprintf(stderr, "ntdll lookup failed\n"); return 26; }
    base::memory::NtReadVirtualMemory = reinterpret_cast<decltype(base::memory::NtReadVirtualMemory)>(
        GetProcAddress(ntdll, "NtReadVirtualMemory"));
    base::memory::NtWriteVirtualMemory = reinterpret_cast<decltype(base::memory::NtWriteVirtualMemory)>(
        GetProcAddress(ntdll, "NtWriteVirtualMemory"));
    if (!base::memory::NtReadVirtualMemory || !base::memory::NtWriteVirtualMemory) { std::fprintf(stderr, "Nt function lookup failed\n"); return 26; }
#endif
    base::memory::syscallInitialized = true;

    std::printf("# historical source revision; initialized old global Nt* API state; startup excluded; valid owned child\n");
    std::printf("# this API returns typed values by value; sizes above 64 KiB are excluded to avoid its large stack frame\n");
    std::printf("api,operation,bytes_per_range,ranges,median_ns_per_range,p95_ns_per_range,ops_per_second,mib_per_second,samples\n");
    bool success = run_size<4>(writer, address, child_data) && run_size<8>(writer, address, child_data) &&
        run_size<64>(writer, address, child_data) && run_size<4096>(writer, address, child_data) &&
        run_size<65536>(writer, address, child_data);
    VirtualFreeEx(writer, remote_buffer, 0, MEM_RELEASE);
    CloseHandle(writer);
    if (!success) { std::fprintf(stderr, "historical benchmark data check failed\n"); return 27; }
    std::puts("# validation=completed_on_valid_ranges");
    return 0;
}
