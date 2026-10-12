#include "memory.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <numeric>
#include <random>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include <winreg.h>

#if defined(UMRW_BENCH_LIBVAS)
#include "vas.h"
#endif
#if defined(UMRW_BENCH_LIBMEM)
#include <libmem/libmem.h>
#endif

static_assert(sizeof(void*) == 8, "The transfer benchmark requires Windows x64.");

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

    unsigned iterations_for(std::size_t bytes) noexcept
    {
        const std::size_t budget = 32u * 1024u * 1024u;
        const std::size_t count = bytes ? budget / bytes : 1;
        return static_cast<unsigned>((std::max)(std::size_t{ 20 }, (std::min)(std::size_t{ 2000 }, count)));
    }

    double percentile(std::vector<double> values, double p)
    {
        std::sort(values.begin(), values.end());
        if (values.empty()) return 0.0;
        const std::size_t rank = static_cast<std::size_t>(std::ceil(p * values.size()));
        const std::size_t index = (std::max)(std::size_t{ 1 }, rank) - 1;
        return values[(std::min)(index, values.size() - 1)];
    }

    void print_measurement(const char* api, const char* op, std::size_t size,
        std::size_t ranges, const std::vector<double>& ns_per_range)
    {
        const double median = percentile(ns_per_range, 0.50);
        const double p95 = percentile(ns_per_range, 0.95);
        const double ops = median > 0 ? 1.0e9 / median : 0;
        const double mib = ops * static_cast<double>(size) / (1024.0 * 1024.0);
        std::printf("%s,%s,%zu,%zu,%.2f,%.2f,%.2f,%.2f,%u\n", api, op, size,
            ranges, median, p95, ops, mib, kSamples);
    }

    std::string executable_path()
    {
        std::vector<char> path(512);
        for (;;)
        {
            const DWORD capacity = static_cast<DWORD>(path.size());
            const DWORD length = GetModuleFileNameA(nullptr, path.data(), capacity);
            if (length == 0) return {};
            if (length < capacity) return std::string(path.data(), length);
            if (path.size() >= 32768) return {};
            path.resize((std::min)(path.size() * 2, std::size_t{ 32768 }));
        }
    }

    bool launch_child(Child& child)
    {
        const std::string path = executable_path();
        if (path.empty()) return false;
        const std::string event_name = "Local\\umrw-bench-ready-" + std::to_string(GetCurrentProcessId());
        child.ready_event = CreateEventA(nullptr, TRUE, FALSE, event_name.c_str());
        if (!child.ready_event) return false;
        std::string command = "\"" + path + "\" --child \"" + event_name + "\" " +
            std::to_string(GetCurrentProcessId());
        STARTUPINFOA startup{};
        startup.cb = sizeof(startup);
        if (!CreateProcessA(path.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
            nullptr, nullptr, &startup, &child.process)) return false;
        return WaitForSingleObject(child.ready_event, 5000) == WAIT_OBJECT_0;
    }

    bool raw_read(HANDLE process, uintptr_t address, void* buffer, std::size_t size)
    {
        SIZE_T transferred = 0;
        return ReadProcessMemory(process, reinterpret_cast<LPCVOID>(address), buffer, size, &transferred) &&
            transferred == size;
    }

    bool raw_write(HANDLE process, uintptr_t address, const void* buffer, std::size_t size)
    {
        SIZE_T transferred = 0;
        return WriteProcessMemory(process, reinterpret_cast<LPVOID>(address), buffer, size, &transferred) &&
            transferred == size;
    }

    template <typename Call>
    std::vector<double> sample_calls(Call&& call, unsigned iterations, std::size_t ranges,
        std::size_t bytes_per_range)
    {
        std::vector<double> values;
        values.reserve(kSamples);
        std::mt19937 random(0x5eedu);
        for (unsigned sample = 0; sample < kSamples; ++sample)
        {
            if (!call(2)) return {};
            const double start = now_seconds();
            for (unsigned i = 0; i < iterations; ++i)
                if (!call(i)) return {};
            const double elapsed = now_seconds() - start;
            const double operations = static_cast<double>(iterations) * static_cast<double>(ranges);
            values.push_back(elapsed * 1.0e9 / operations);
            std::shuffle(values.begin(), values.end(), random);
            (void)bytes_per_range;
        }
        return values;
    }

    template <typename Call>
    bool measure_one_sample(Call&& call, unsigned iterations, std::size_t ranges, double& ns_per_range)
    {
        if (!call(0)) return false;
        const double start = now_seconds();
        for (unsigned i = 0; i < iterations; ++i)
            if (!call(i)) return false;
        const double elapsed = now_seconds() - start;
        ns_per_range = elapsed * 1.0e9 /
            (static_cast<double>(iterations) * static_cast<double>(ranges));
        return true;
    }

    bool validate_read(HANDLE process, uintptr_t address, const unsigned char* result, std::size_t size)
    {
        std::vector<unsigned char> expected(size);
        if (!raw_read(process, address, expected.data(), size)) return false;
        return std::memcmp(expected.data(), result, size) == 0;
    }

    template <std::size_t N>
    struct TypedPayload { unsigned char bytes[N]; };

    template <std::size_t N>
    bool benchmark_typed_write(mem::ProcessSession& session, HANDLE process, uintptr_t address)
    {
        auto payload = std::make_unique<TypedPayload<N>>();
        std::memset(payload.get(), 0xA5, sizeof(*payload));
        auto samples = sample_calls([&](unsigned) {
            return session.write_result(address, *payload).complete();
        }, iterations_for(N), 1, N);
        if (samples.empty() || !validate_read(process, address, payload->bytes, N)) return false;
        print_measurement("ProcessSession", "typed_write", N, 1, samples);
        return raw_write(process, address, child_data, N);
    }

    std::string registry_string(HKEY root, const char* key_path, const char* value_name)
    {
        HKEY key = nullptr;
        if (RegOpenKeyExA(root, key_path, 0, KEY_READ, &key) != ERROR_SUCCESS) return "unknown";
        char value[256]{};
        DWORD type = 0;
        DWORD bytes = sizeof(value);
        const LONG status = RegQueryValueExA(key, value_name, nullptr, &type,
            reinterpret_cast<BYTE*>(value), &bytes);
        RegCloseKey(key);
        if (status != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ)) return "unknown";
        return value;
    }

    void print_environment()
    {
        SYSTEM_INFO info{};
        GetSystemInfo(&info);
        std::printf("# cpu=%s\n", registry_string(HKEY_LOCAL_MACHINE,
            "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", "ProcessorNameString").c_str());
        std::printf("# windows_build=%s logical_processors=%lu page_size=%lu\n",
            registry_string(HKEY_LOCAL_MACHINE,
                "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", "CurrentBuildNumber").c_str(),
            static_cast<unsigned long>(info.dwNumberOfProcessors), static_cast<unsigned long>(info.dwPageSize));
#if defined(_MSC_VER)
        std::printf("# compiler=MSVC %d\n", _MSC_VER);
#elif defined(__GNUC__)
        std::printf("# compiler=GCC %d.%d.%d\n", __GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__);
#else
        std::puts("# compiler=unknown");
#endif
    }

    int run_benchmarks(DWORD pid)
    {
        for (std::size_t i = 0; i < kDataBytes; ++i)
            child_data[i] = static_cast<unsigned char>((i * 131u + 17u) & 0xffu);

        HANDLE process = OpenProcess(PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_VM_OPERATION |
            PROCESS_QUERY_INFORMATION, FALSE, pid);
        if (!process) return 2;
        void* remote_buffer = VirtualAllocEx(process, nullptr, kDataBytes,
            MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        const uintptr_t remote_data_address = reinterpret_cast<uintptr_t>(remote_buffer);
        if (!remote_buffer || !raw_write(process, remote_data_address, child_data, kDataBytes))
        {
            CloseHandle(process);
            return 4;
        }

        mem::ProcessSession session;
        if (!session.bind(static_cast<INT32>(pid), mem::AccessMode::read_write))
        {
            CloseHandle(process);
            return 3;
        }
        if (!session.base_address())
        {
            CloseHandle(process);
            return 4;
        }

#if defined(UMRW_BENCH_LIBVAS)
        vas_t* vas = vas_open(static_cast<pid_t>(pid), VAS_O_NOP);
        if (!vas)
        {
            CloseHandle(process);
            return 14;
        }
#endif
#if defined(UMRW_BENCH_LIBMEM)
        lm_process_t libmem_process{};
        libmem_process.pid = static_cast<lm_pid_t>(pid);
#endif

        std::printf("# Windows x64 process-transfer benchmark; source=ProcessSession vs direct Win32\n");
        std::printf("# warmup included; validation outside timing; only owned child data is written\n");
        print_environment();
        std::printf("api,operation,bytes_per_range,ranges,median_ns_per_range,p95_ns_per_range,ops_per_second,mib_per_second,samples\n");

        const std::size_t sizes[] = { 4, 8, 64, 4096, 65536, 1048576 };
        std::mt19937 order_random(0x1234u);
        for (const std::size_t size : sizes)
        {
            std::vector<unsigned char> buffer(size);
            unsigned iterations = iterations_for(size);
            enum class Api { session, win32
#if defined(UMRW_BENCH_LIBVAS)
                , vas
#endif
#if defined(UMRW_BENCH_LIBMEM)
                , libmem
#endif
            };
            struct Method { const char* name; bool write; Api api; };
            std::vector<Method> methods = {
                { "ProcessSession", false, Api::session }, { "ReadProcessMemory", false, Api::win32 },
                { "ProcessSession", true, Api::session }, { "WriteProcessMemory", true, Api::win32 }
            };
#if defined(UMRW_BENCH_LIBVAS)
            methods.push_back({ "libvas", false, Api::vas });
            methods.push_back({ "libvas", true, Api::vas });
#endif
#if defined(UMRW_BENCH_LIBMEM)
            methods.push_back({ "libmem", false, Api::libmem });
            methods.push_back({ "libmem", true, Api::libmem });
#endif
            std::vector<unsigned> order(methods.size());
            std::iota(order.begin(), order.end(), 0u);
            std::vector<std::vector<double>> method_samples(methods.size());
            for (unsigned sample = 0; sample < kSamples; ++sample)
            {
                std::shuffle(order.begin(), order.end(), order_random);
                for (const unsigned method_index : order)
                {
                    const Method& method = methods[method_index];
                    const uintptr_t address = remote_data_address;
                    auto invoke = [&](unsigned) {
                        if (method.api == Api::session)
                            return method.write
                                ? session.write_raw_result(address, buffer.data(), size).complete()
                                : session.read_raw_result(address, buffer.data(), size).complete();
                        if (method.api == Api::win32)
                            return method.write ? raw_write(process, address, buffer.data(), size)
                                : raw_read(process, address, buffer.data(), size);
#if defined(UMRW_BENCH_LIBVAS)
                        if (method.api == Api::vas)
                            return method.write ? vas_write(vas, address, buffer.data(), size) == static_cast<int>(size)
                                : vas_read(vas, address, buffer.data(), size) == static_cast<int>(size);
#endif
#if defined(UMRW_BENCH_LIBMEM)
                        if (method.api == Api::libmem)
                            return method.write ? LM_WriteMemoryEx(&libmem_process, address, buffer.data(), size) == size
                                : LM_ReadMemoryEx(&libmem_process, address, buffer.data(), size) == size;
#endif
                        return false;
                    };
                    double ns_per_range = 0;
                    if (!measure_one_sample(invoke, iterations, 1, ns_per_range))
                    {
                        CloseHandle(process);
                        return 5;
                    }
                    method_samples[method_index].push_back(ns_per_range);
                }

            }
            if (!validate_read(process, remote_data_address, buffer.data(), size))
            {
                CloseHandle(process);
                return 6;
            }
            for (std::size_t method_index = 0; method_index < methods.size(); ++method_index)
            {
                if (method_samples[method_index].size() != kSamples)
                {
                    CloseHandle(process);
                    return 7;
                }
                const Method& method = methods[method_index];
                print_measurement(method.name, method.write ? "write" : "read", size, 1,
                    method_samples[method_index]);
            }

            if (size == sizeof(int))
            {
                int typed_value = 0;
                const uintptr_t address = remote_data_address;
                auto typed_reads = sample_calls([&](unsigned) {
                    return session.read_result(address, typed_value).complete();
                }, iterations, 1, size);
                auto typed_writes = sample_calls([&](unsigned) {
                    return session.write_result(address, typed_value).complete();
                }, iterations, 1, size);
                if (typed_reads.empty() || typed_writes.empty())
                {
                    CloseHandle(process);
                    return 10;
                }
                print_measurement("ProcessSession", "typed_read", size, 1, typed_reads);
                print_measurement("ProcessSession", "typed_write", size, 1, typed_writes);
            }
        }

        const uintptr_t base_address = remote_data_address;
        if (!benchmark_typed_write<64>(session, process, base_address) ||
            !benchmark_typed_write<4096>(session, process, base_address) ||
            !benchmark_typed_write<65536>(session, process, base_address))
        {
            CloseHandle(process);
            return 15;
        }

        std::vector<unsigned char> batch_buffer(mem::MAX_READ_MANY_BYTES);
        for (const std::size_t size : { std::size_t{4}, std::size_t{64}, std::size_t{4096}, std::size_t{65536} })
        {
            for (const std::size_t ranges : { std::size_t{1}, std::size_t{16}, std::size_t{64}, std::size_t{256} })
            {
                if (ranges > batch_buffer.size() / size || ranges * size > mem::MAX_READ_MANY_BYTES) continue;
                for (const bool disjoint : { false, true })
                {
                    const std::size_t stride = size + (disjoint ? 64 : 0);
                    if ((ranges - 1) * stride + size > kDataBytes) continue;
                    std::vector<mem::ReadRequest> requests(ranges);
                    std::vector<mem::TransferResult> results(ranges);
                    for (std::size_t i = 0; i < ranges; ++i)
                        requests[i] = { remote_data_address + i * stride, batch_buffer.data() + i * size, size };
                    const unsigned iterations = (std::min)(iterations_for(size * ranges),
                        (std::max)(10u, 5000u / static_cast<unsigned>(ranges)));
                    auto run_batch = [&](unsigned) {
                        return session.read_many(requests.data(), results.data(), ranges) &&
                            std::all_of(results.begin(), results.end(), [](const mem::TransferResult& result) { return result.complete(); });
                    };
                    auto run_repeated = [&](unsigned) {
                        for (std::size_t i = 0; i < ranges; ++i)
                            if (!session.read_raw_result(requests[i].address, requests[i].buffer, size).complete()) return false;
                        return true;
                    };
                    std::vector<double> batch_samples, repeated_samples;
                    batch_samples.reserve(kSamples);
                    repeated_samples.reserve(kSamples);
                    std::array<unsigned, 2> variants{ 0, 1 };
                    for (unsigned sample = 0; sample < kSamples; ++sample)
                    {
                        std::shuffle(variants.begin(), variants.end(), order_random);
                        for (const unsigned variant : variants)
                        {
                            double ns_per_range = 0;
                            const bool ok = variant == 0
                                ? measure_one_sample(run_batch, iterations, ranges, ns_per_range)
                                : measure_one_sample(run_repeated, iterations, ranges, ns_per_range);
                            if (!ok)
                            {
                                CloseHandle(process);
                                return 8;
                            }
                            (variant == 0 ? batch_samples : repeated_samples).push_back(ns_per_range);
                        }
                    }
                    for (std::size_t i = 0; i < ranges; ++i)
                    {
                        if (!validate_read(process, requests[i].address,
                            static_cast<const unsigned char*>(requests[i].buffer), size))
                        {
                            CloseHandle(process);
                            return 9;
                        }
                    }
                    print_measurement("ProcessSession", disjoint ? "read_many_disjoint" : "read_many_contiguous",
                        size, ranges, batch_samples);
                    print_measurement("ProcessSession", disjoint ? "repeated_disjoint" : "repeated_contiguous",
                        size, ranges, repeated_samples);
                }
            }
        }

        for (const unsigned thread_count : { 1u, 2u, 4u, 8u })
        {
            const uintptr_t address = remote_data_address;
            auto parallel_samples = sample_calls([&](unsigned) {
                constexpr unsigned ops_per_thread = 10000;
                std::atomic<unsigned> ready{ 0 };
                std::atomic<unsigned> failures{ 0 };
                std::atomic<bool> go{ false };
                std::vector<std::thread> workers;
                workers.reserve(thread_count);
                for (unsigned thread_index = 0; thread_index < thread_count; ++thread_index)
                {
                    workers.emplace_back([&] {
                        unsigned char local[64]{};
                        ready.fetch_add(1, std::memory_order_release);
                        while (!go.load(std::memory_order_acquire)) std::this_thread::yield();
                        for (unsigned i = 0; i < ops_per_thread; ++i)
                            if (!session.read_raw_result(address, local, sizeof(local)).complete())
                                failures.fetch_add(1, std::memory_order_relaxed);
                    });
                }
                while (ready.load(std::memory_order_acquire) != thread_count) std::this_thread::yield();
                const double start = now_seconds();
                go.store(true, std::memory_order_release);
                for (auto& worker : workers) worker.join();
                const double elapsed = now_seconds() - start;
                return failures.load(std::memory_order_relaxed) == 0 && elapsed > 0;
            }, 1, thread_count * 10000u, 64);
            if (parallel_samples.empty())
            {
                CloseHandle(process);
                return 12;
            }
            print_measurement("ProcessSession", "parallel_read_64B", 64, thread_count, parallel_samples);
        }

        std::vector<double> bind_samples;
        bind_samples.reserve(kSamples);
        for (unsigned sample = 0; sample < kSamples; ++sample)
        {
            constexpr unsigned bind_count = 25;
            const double start = now_seconds();
            for (unsigned i = 0; i < bind_count; ++i)
            {
                session.close();
                if (!session.bind(static_cast<INT32>(pid), mem::AccessMode::read_write))
                {
                    CloseHandle(process);
                    return 13;
                }
            }
            bind_samples.push_back((now_seconds() - start) * 1.0e9 / bind_count);
        }
        print_measurement("ProcessSession", "bind_plus_base", 0, 1, bind_samples);

        session.close();
#if defined(UMRW_BENCH_LIBVAS)
        vas_close(vas);
#endif
        VirtualFreeEx(process, remote_buffer, 0, MEM_RELEASE);
        CloseHandle(process);
        std::puts("# validation=passed");
        return 0;
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
    if (!launch_child(child))
    {
        std::fprintf(stderr, "Could not launch the owned benchmark child. Win32 error=%lu\n", GetLastError());
        return 21;
    }
    return run_benchmarks(child.process.dwProcessId);
}
