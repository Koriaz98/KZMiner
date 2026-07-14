cmake_minimum_required(VERSION 3.18)

project(KZMiner LANGUAGES CXX CUDA)

# Version derivee automatiquement du tag Git le plus recent (ex: v0.7,
# ou v0.7-3-gabc1234 si des commits ont ete faits apres le dernier tag).
# Fallback "dev" si ce n'est pas un depot git ou si git est absent.
execute_process(
    COMMAND git describe --tags --always --dirty
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE KZMINER_GIT_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE KZMINER_GIT_RESULT
    ERROR_QUIET
)
if(NOT KZMINER_GIT_RESULT EQUAL 0 OR KZMINER_GIT_VERSION STREQUAL "")
    set(KZMINER_GIT_VERSION "dev")
endif()
message(STATUS "KZMiner version: ${KZMINER_GIT_VERSION}")

configure_file(
    ${CMAKE_SOURCE_DIR}/src/version.h.in
    ${CMAKE_SOURCE_DIR}/src/version.h
    @ONLY
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CUDA_STANDARD 14)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -march=native -mavx2 -mfma -mpopcnt -flto")

find_package(CURL REQUIRED)

include_directories(
    /usr/include
)

add_subdirectory(third_party/argon2-gpu)

add_executable(
    kzminer

    src/main.cpp
    src/config/config.cpp
    src/cpu/cpu_miner.cpp
    src/gpu/gpu_miner.cpp
    src/argon2/argon2_engine.cpp
    src/network/work_client.cpp
    src/network/solo_job_manager.cpp
    src/network/pool_client.cpp
    src/network/pool_job_manager.cpp
    src/devfee/devfee_source.cpp
    src/update_check.cpp
    src/monitor/system_monitor.cpp
    src/monitor/status_table.cpp
)

target_link_libraries(
    kzminer

    argon2
    pthread
    CURL::libcurl
    argon2-cuda-vendored
)

add_executable(
    gpu-selftest

    src/argon2/argon2_engine.cpp
    src/gpu/gpu_selftest.cpp
    src/gpu/gpu_selftest_main.cpp
)

target_link_libraries(
    gpu-selftest

    argon2
    argon2-cuda-vendored
)
