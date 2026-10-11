#include "memory.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <atomic>
#include <utility>

static_assert(sizeof(void*) == 8, "This integration test requires Windows x64.");

namespace
{
    int child_value = 42;

    struct ChildProcess
    {
        PROCESS_INFORMATION process{};
        HANDLE ready_event = nullptr;

        ~ChildProcess()
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

    struct NoAccessRegion
    {
        HANDLE process = nullptr;
        LPVOID address = nullptr;

        ~NoAccessRegion()
        {
            if (address) VirtualFreeEx(process, address, 0, MEM_RELEASE);
            if (process) CloseHandle(process);
        }
    };

    int fail(int code, const char* message)
    {
        std::fprintf(stderr, "FAIL %d: %s\n", code, message);
        return code;
    }
}

int main(int argc, char** argv)
{
    if (argc == 3 && std::strcmp(argv[1], "--child") == 0)
    {
        HANDLE ready = OpenEventA(EVENT_MODIFY_STATE, FALSE, argv[2]);
        if (!ready) return fail(90, "child could not open its readiness event");
        SetEvent(ready);
        CloseHandle(ready);
        for (;;) Sleep(1000);
    }

    if (!(mem::is_valid_range(mem::MIN_ADDRESS, 1) &&
        !mem::is_valid_range(mem::MIN_ADDRESS - 1, 1) &&
        !mem::is_valid_range(mem::MAX_ADDRESS, 2) &&
        !mem::is_valid_range(mem::MIN_ADDRESS, 0)))
        return fail(1, "address range validation failed");

    char executable[MAX_PATH]{};
    if (!GetModuleFileNameA(nullptr, executable, MAX_PATH))
        return fail(2, "could not locate the integration executable");

    const std::string event_name = "Local\\umrw-ready-" + std::to_string(GetCurrentProcessId());
    ChildProcess child;
    child.ready_event = CreateEventA(nullptr, TRUE, FALSE, event_name.c_str());
    if (!child.ready_event) return fail(3, "could not create child readiness event");

    std::string command = std::string("\"") + executable + "\" --child \"" + event_name + "\"";
    STARTUPINFOA startup{};
    startup.cb = sizeof(startup);
    if (!CreateProcessA(executable, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
        nullptr, nullptr, &startup, &child.process))
        return fail(4, "could not launch the test child");
    if (WaitForSingleObject(child.ready_event, 5000) != WAIT_OBJECT_0)
        return fail(5, "test child did not become ready");

    NoAccessRegion inaccessible_region;
    inaccessible_region.process = OpenProcess(PROCESS_VM_OPERATION,
        FALSE, child.process.dwProcessId);
    if (!inaccessible_region.process)
        return fail(30, "could not open the owned child to prepare an inaccessible test page");
    inaccessible_region.address = VirtualAllocEx(inaccessible_region.process, nullptr, 4096,
        MEM_RESERVE | MEM_COMMIT, PAGE_NOACCESS);
    if (!inaccessible_region.address)
        return fail(31, "could not allocate the owned child’s no-access test page");

    mem::ProcessSession own_reader;
    if (!own_reader.bind(static_cast<INT32>(GetCurrentProcessId()), mem::AccessMode::read_only))
        return fail(6, "read-only self bind failed");
    int own_value = 0;
    const auto own_read = own_reader.read_result(reinterpret_cast<uintptr_t>(&child_value), own_value);
    if (!own_read.complete() || own_value != 42)
        return fail(7, "read-only session failed to read its own test data");
    if (own_reader.write_result(reinterpret_cast<uintptr_t>(&child_value), 73).status != mem::TransferStatus::access_denied)
        return fail(8, "read-only session did not reject a write");

    mem::ProcessSession child_reader;
    mem::ProcessSession child_writer;
    if (!child_reader.bind(static_cast<INT32>(child.process.dwProcessId), mem::AccessMode::read_only) ||
        !child_writer.bind(static_cast<INT32>(child.process.dwProcessId), mem::AccessMode::read_write))
        return fail(9, "could not open child sessions with their selected access modes");

    const uintptr_t local_base = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
    const uintptr_t value_address = child_reader.base_address() +
        (reinterpret_cast<uintptr_t>(&child_value) - local_base);
    int value = 0;
    auto read = child_reader.read_result(value_address, value);
    if (!read.complete() || value != 42)
        return fail(10, "child read did not return the initialized value");

    mem::MemoryRegion region{};
    if (child_reader.query_region(value_address, region) != ERROR_SUCCESS ||
        region.state != MEM_COMMIT || value_address < region.base_address ||
        value_address - region.base_address >= region.size)
        return fail(11, "advisory region query did not describe the child value");

    int replacement = 73;
    auto write = child_writer.write_result(value_address, replacement);
    if (!write.complete() || write.transferred != sizeof(replacement))
        return fail(12, "child write did not report a complete transfer");
    read = child_reader.read_result(value_address, value);
    if (!read.complete() || value != replacement)
        return fail(13, "child did not contain the written test value");

    unsigned char bytes[sizeof(int)]{};
    mem::ReadRequest requests[] = {
        { value_address, &value, sizeof(value) },
        { mem::MAX_ADDRESS, bytes, sizeof(bytes) },
        { value_address, bytes, sizeof(bytes) },
    };
    mem::TransferResult results[3]{};
    if (!child_reader.read_many(requests, results, 3) || !results[0].complete() ||
        results[1].status != mem::TransferStatus::invalid_range || !results[2].complete())
        return fail(14, "bounded batch did not report one independent result per range");

    mem::ReadRequest oversized[mem::MAX_READ_MANY_RANGES + 1]{};
    mem::TransferResult oversized_results[mem::MAX_READ_MANY_RANGES + 1]{};
    if (child_reader.read_many(oversized, oversized_results, mem::MAX_READ_MANY_RANGES + 1))
        return fail(15, "batch accepted more ranges than its documented cap");

    auto bad_range = child_reader.read_raw_result(mem::MAX_ADDRESS, bytes, sizeof(bytes));
    if (bad_range.status != mem::TransferStatus::invalid_range || bad_range.transferred != 0)
        return fail(16, "numeric invalid range did not produce a structured error");
    auto bad_buffer = child_reader.read_raw_result(value_address, nullptr, sizeof(value));
    if (bad_buffer.status != mem::TransferStatus::invalid_buffer)
        return fail(17, "null buffer did not produce a structured error");
    const auto inaccessible_address = reinterpret_cast<uintptr_t>(inaccessible_region.address);
    mem::MemoryRegion inaccessible_info{};
    if (child_reader.query_region(inaccessible_address, inaccessible_info) != ERROR_SUCCESS ||
        inaccessible_info.state != MEM_COMMIT || inaccessible_info.protection != PAGE_NOACCESS)
        return fail(32, "test child’s reserved page did not retain no-access protection");
    auto inaccessible = child_reader.read_raw_result(inaccessible_address, bytes, sizeof(bytes));
    if (inaccessible.complete() || inaccessible.os_error == ERROR_SUCCESS)
        return fail(18, "inaccessible child address did not preserve a Win32 error");
    auto inaccessible_write = child_writer.write_raw_result(inaccessible_address, bytes, sizeof(bytes));
    if (inaccessible_write.complete() || inaccessible_write.os_error == ERROR_SUCCESS)
        return fail(26, "failed child write did not preserve a Win32 error");

    mem::ReadRequest over_bytes{ value_address, &value, mem::MAX_READ_MANY_BYTES + 1 };
    mem::TransferResult untouched{ 1, 1, ERROR_SUCCESS, mem::TransferStatus::complete };
    if (child_reader.read_many(&over_bytes, &untouched, 1) || !untouched.complete())
        return fail(27, "batch byte cap was not rejected without changing results");

    mem::ProcessSession independent;
    if (!independent.bind(static_cast<INT32>(child.process.dwProcessId), mem::AccessMode::read_only) ||
        !independent.read<int>(value_address))
        return fail(19, "independent session could not read the same child");

    std::atomic<int> read_failures{0};
    auto read_loop = [&](const mem::ProcessSession& session)
    {
        for (int i = 0; i < 1000; ++i)
        {
            const auto current = session.read<int>(value_address);
            if (!current || *current != replacement) ++read_failures;
        }
    };
    std::thread first_reader([&]() { read_loop(child_reader); });
    std::thread second_reader([&]() { read_loop(independent); });
    first_reader.join();
    second_reader.join();
    if (read_failures.load() != 0)
        return fail(25, "concurrent independent session reads were inconsistent");

    mem::ProcessSession moved = std::move(child_reader);
    if (child_reader.is_open() || !moved.is_open() || !independent.is_open())
        return fail(20, "move construction did not preserve independent handle ownership");
    mem::ProcessSession assigned;
    if (!assigned.bind(static_cast<INT32>(GetCurrentProcessId()), mem::AccessMode::read_only))
        return fail(28, "move-assignment destination setup failed");
    assigned = std::move(moved);
    if (moved.is_open() || !assigned.is_open() || !independent.is_open())
        return fail(29, "move assignment did not transfer the child session");
    assigned.close();
    if (assigned.is_open() || !independent.is_open())
        return fail(21, "closing one session affected another session");
    if (!assigned.bind(static_cast<INT32>(child.process.dwProcessId), mem::AccessMode::read_only) ||
        !assigned.read<int>(value_address))
        return fail(22, "closed session could not rebind and read");

    TerminateProcess(child.process.hProcess, 0);
    if (WaitForSingleObject(child.process.hProcess, 5000) != WAIT_OBJECT_0)
        return fail(23, "test child did not exit");
    const auto after_exit = assigned.read_result(value_address, value);
    if (after_exit.complete() || after_exit.transferred > after_exit.requested)
        return fail(24, "transfer against an exited child reported success");

    assigned.close();
    independent.close();
    child_writer.close();
    own_reader.close();
    std::puts("ProcessSession Win32 integration checks passed.");
    return 0;
}
