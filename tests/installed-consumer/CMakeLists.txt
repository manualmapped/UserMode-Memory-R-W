cmake_minimum_required(VERSION 3.20)
project(UserModeMemoryInstalledConsumer LANGUAGES CXX)

if(NOT DEFINED MEMORY_INSTALL_PREFIX)
    message(FATAL_ERROR "Pass MEMORY_INSTALL_PREFIX to the installed consumer check.")
endif()
include("${MEMORY_INSTALL_PREFIX}/lib/cmake/UserModeMemoryRW/UserModeMemoryRWTargets.cmake")
add_executable(installed_consumer main.cpp)
target_link_libraries(installed_consumer PRIVATE UserModeMemoryRW::UserModeMemoryRW)
