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
