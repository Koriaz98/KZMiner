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

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -march=x86-64-v2 -mpopcnt -flto")

find_package(CURL REQUIRED)

# Compile libargon2 en deux variantes (portable + AVX2, symboles AVX2
# renommes) au moment de la configuration, pour que KZMiner puisse
# choisir automatiquement la bonne variante au demarrage selon le CPU
# reel de la machine (voir src/argon2/argon2_engine.cpp).
set(ARGON2_DUAL_DIR ${CMAKE_BINARY_DIR}/argon2-dual)
file(MAKE_DIRECTORY ${ARGON2_DUAL_DIR})

execute_process(
    COMMAND ${CMAKE_SOURCE_DIR}/third_party/argon2-build-script/build_argon2_dual.sh
            ${CMAKE_SOURCE_DIR}/third_party/argon2
            ${ARGON2_DUAL_DIR}
    RESULT_VARIABLE ARGON2_DUAL_RESULT
)
if(NOT ARGON2_DUAL_RESULT EQUAL 0)
    message(FATAL_ERROR "Echec de la compilation double de libargon2 (voir ci-dessus)")
endif()

include_directories(
    ${ARGON2_DUAL_DIR}
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
    src/algo/argon2id/argon2id_algorithm.cpp
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

    ${ARGON2_DUAL_DIR}/libargon2_avx2_renamed.a
    ${ARGON2_DUAL_DIR}/libargon2_ref.a
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

    ${ARGON2_DUAL_DIR}/libargon2_avx2_renamed.a
    ${ARGON2_DUAL_DIR}/libargon2_ref.a
    argon2-cuda-vendored
)
