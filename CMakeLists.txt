cmake_minimum_required(VERSION 3.20)

project(UserModeMemoryRW LANGUAGES CXX)

if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "UserMode-Memory-R-W requires a 64-bit Windows target.")
endif()

add_library(UserModeMemoryRW INTERFACE)
add_library(UserModeMemoryRW::UserModeMemoryRW ALIAS UserModeMemoryRW)
target_compile_features(UserModeMemoryRW INTERFACE cxx_std_17)
target_link_libraries(UserModeMemoryRW INTERFACE Psapi)
target_include_directories(UserModeMemoryRW INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>
)

install(TARGETS UserModeMemoryRW EXPORT UserModeMemoryRWTargets)
install(FILES memory.h DESTINATION include)
install(EXPORT UserModeMemoryRWTargets
    NAMESPACE UserModeMemoryRW::
    DESTINATION lib/cmake/UserModeMemoryRW
)

include(CTest)
if(BUILD_TESTING)
    add_executable(process_session_integration tests/process_session_integration.cpp)
    target_link_libraries(process_session_integration PRIVATE UserModeMemoryRW::UserModeMemoryRW)
    add_test(NAME process_session_integration COMMAND process_session_integration)
    set_tests_properties(process_session_integration PROPERTIES TIMEOUT 30)
endif()

option(UMRW_BUILD_BENCHMARKS "Build opt-in process-transfer benchmarks" OFF)
if(UMRW_BUILD_BENCHMARKS)
    enable_language(C)
    add_executable(process_transfer_bench benchmarks/process_transfer_bench.cpp)
    target_link_libraries(process_transfer_bench PRIVATE UserModeMemoryRW::UserModeMemoryRW Advapi32)

    set(UMRW_LIBVAS_SOURCE_DIR "" CACHE PATH "Optional external libvas checkout at the pinned revision")
    set(UMRW_LIBVAS_GENERATED_DIR "" CACHE PATH "Directory containing generated libvas headers")
    if(UMRW_LIBVAS_SOURCE_DIR)
        if(NOT EXISTS "${UMRW_LIBVAS_SOURCE_DIR}/win32/vas.c" OR NOT EXISTS "${UMRW_LIBVAS_GENERATED_DIR}/vas.h")
            message(FATAL_ERROR "libvas source and generated header directories are both required.")
        endif()
        add_library(umrw_bench_libvas STATIC "${UMRW_LIBVAS_SOURCE_DIR}/win32/vas.c")
        target_include_directories(umrw_bench_libvas PRIVATE
            "${UMRW_LIBVAS_SOURCE_DIR}" "${UMRW_LIBVAS_GENERATED_DIR}")
        target_link_libraries(umrw_bench_libvas PRIVATE Psapi)
        target_include_directories(process_transfer_bench PRIVATE "${UMRW_LIBVAS_GENERATED_DIR}")
        target_compile_definitions(process_transfer_bench PRIVATE UMRW_BENCH_LIBVAS=1)
        target_link_libraries(process_transfer_bench PRIVATE umrw_bench_libvas)
    endif()

    set(UMRW_LIBMEM_SOURCE_DIR "" CACHE PATH "Optional external libmem checkout at the pinned revision")
    if(UMRW_LIBMEM_SOURCE_DIR)
        if(NOT EXISTS "${UMRW_LIBMEM_SOURCE_DIR}/src/win/memory.c" OR
            NOT EXISTS "${UMRW_LIBMEM_SOURCE_DIR}/internal/winutils/winutils.c")
            message(FATAL_ERROR "UMRW_LIBMEM_SOURCE_DIR does not contain a Windows libmem source tree.")
        endif()
        add_library(umrw_bench_libmem STATIC
            "${UMRW_LIBMEM_SOURCE_DIR}/src/win/memory.c"
            "${UMRW_LIBMEM_SOURCE_DIR}/src/win/osprot.c"
            "${UMRW_LIBMEM_SOURCE_DIR}/internal/winutils/winutils.c")
        target_include_directories(umrw_bench_libmem PRIVATE
            "${UMRW_LIBMEM_SOURCE_DIR}/include" "${UMRW_LIBMEM_SOURCE_DIR}/internal")
        if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
            target_compile_options(umrw_bench_libmem PRIVATE -Wno-error=int-conversion)
        endif()
        target_link_libraries(umrw_bench_libmem PRIVATE Psapi ntdll)
        target_include_directories(process_transfer_bench PRIVATE "${UMRW_LIBMEM_SOURCE_DIR}/include")
        target_compile_definitions(process_transfer_bench PRIVATE UMRW_BENCH_LIBMEM=1)
        target_link_libraries(process_transfer_bench PRIVATE umrw_bench_libmem)
    endif()
endif()
