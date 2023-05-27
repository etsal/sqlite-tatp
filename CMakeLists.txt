cmake_minimum_required(VERSION 3.16)
project(sqlite_performance)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
cmake_path(GET CMAKE_CURRENT_LIST_DIR PARENT_PATH BASE_DIR)
set(SQLITE_BUILDDIR ${BASE_DIR}/sqlite/build)

include(FetchContent)

find_package(Threads REQUIRED)

FetchContent_Declare(
        cxxopts
        GIT_REPOSITORY https://github.com/jarro2783/cxxopts.git
        GIT_TAG v2.2.1
)
set(CXXOPTS_BUILD_EXAMPLES OFF)
set(CXXOPTS_BUILD_TESTS OFF)
set(CXXOPTS_ENABLE_INSTALL OFF)
set(CXXOPTS_ENABLE_WARNINGS OFF)
FetchContent_MakeAvailable(cxxopts)

FetchContent_Declare(
        dbbench
        GIT_REPOSITORY https://github.com/kpgaffney/dbbench.git
        GIT_TAG main
)
set(DBBENCH_BUILD_EXAMPLES OFF)
FetchContent_MakeAvailable(dbbench)

FetchContent_Declare(
        sqlite3cpp
        GIT_REPOSITORY https://github.com/etsal/sqlite3cpp.git
        GIT_TAG main
)
set(SQLITE3CPP_BUILD_TESTS OFF)
FetchContent_MakeAvailable(sqlite3cpp)

add_library(
        sqlite3
        ${SQLITE_BUILDDIR}/sqlite3.c
        ${SQLITE_BUILDDIR}/sqlite3.h
)
target_compile_options(
        sqlite3
        PRIVATE
        -DSQLITE_DQS=0
        -DSQLITE_THREADSAFE=0
        -DSQLITE_DEFAULT_MEMSTATUS=0
        -DSQLITE_DEFAULT_WAL_SYNCHRONOUS=1
        -DSQLITE_LIKE_DOESNT_MATCH_BLOBS
        -DSQLITE_MAX_EXPR_DEPTH=0
        -DSQLITE_OMIT_DECLTYPE
        -DSQLITE_OMIT_DEPRECATED
        -DSQLITE_OMIT_PROGRESS_CALLBACK
        -DSQLITE_OMIT_SHARED_CACHE
        -DSQLITE_USE_ALLOCA
        -DSQLITE_OMIT_AUTOINIT
)

add_library(
        sqlite3_vdbe_profile
        ${SQLITE_BUILDDIR}/sqlite3.c
        ${SQLITE_BUILDDIR}/sqlite3.h
)
target_compile_options(
        sqlite3_vdbe_profile
        PRIVATE
        -DSQLITE_DQS=0
        -DSQLITE_THREADSAFE=0
        -DSQLITE_DEFAULT_MEMSTATUS=0
        -DSQLITE_DEFAULT_WAL_SYNCHRONOUS=1
        -DSQLITE_LIKE_DOESNT_MATCH_BLOBS
        -DSQLITE_MAX_EXPR_DEPTH=0
        -DSQLITE_OMIT_DECLTYPE
        -DSQLITE_OMIT_DEPRECATED
        -DSQLITE_OMIT_PROGRESS_CALLBACK
        -DSQLITE_OMIT_SHARED_CACHE
        -DSQLITE_USE_ALLOCA
        -DSQLITE_OMIT_AUTOINIT
        -DSQLITE_PERFORMANCE_TRACE
        -DSQLITE_HWTIME_USE_INTRINSIC
)

add_executable(
        sqlite3_shell
        ${SQLITE_BUILDDIR}/shell.c
        ${SQLITE_BUILDDIR}/sqlite3.c
        ${SQLITE_BUILDDIR}/sqlite3.h
)
target_compile_options(
        sqlite3_shell
        PRIVATE
        -DSQLITE_THREADSAFE=0
        -DSQLITE_OMIT_LOAD_EXTENSION
)

# TATP executable.

add_executable(tatp_sqlite3 src/tatp/tatp_sqlite3.cpp)
target_include_directories(tatp_sqlite3 PRIVATE src ${SQLITE_BUILDDIR})
target_link_libraries(tatp_sqlite3 cxxopts dbbench_tatp sqlite3 sqlite3cpp)
set_target_properties(
        tatp_sqlite3
        PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/tatp
)

# Blob executable.

add_executable(blob_sqlite3 src/blob/blob_sqlite3.cpp)
target_include_directories(blob_sqlite3 PRIVATE src ${SQLITE_BUILDDIR})
target_link_libraries(blob_sqlite3 cxxopts dbbench_core sqlite3 sqlite3cpp)
set_target_properties(
        blob_sqlite3
        PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/blob
)

# Scripts.
configure_file(scripts/benchmarks/tatp.sh ${CMAKE_CURRENT_BINARY_DIR}/tatp/tatp.sh COPYONLY)
configure_file(scripts/benchmarks/blob.sh ${CMAKE_CURRENT_BINARY_DIR}/blob/blob.sh COPYONLY)
configure_file(scripts/all.sh ${CMAKE_CURRENT_BINARY_DIR}/all.sh COPYONLY)
