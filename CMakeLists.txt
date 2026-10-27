cmake_minimum_required(VERSION 3.16)
project(FlashForge VERSION 1.0.0 LANGUAGES CXX)

option(FLASHFORGE_SANITIZE "Enable address and undefined behavior sanitizers" OFF)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

add_library(flashforge_lib
  src/checksum.cpp src/chunker.cpp src/thread_pool.cpp src/hash_index.cpp
  src/metadata_store.cpp src/wal.cpp src/garbage_collector.cpp src/storage_engine.cpp)
target_include_directories(flashforge_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_options(flashforge_lib PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(flashforge_lib PUBLIC Threads::Threads)

if(FLASHFORGE_SANITIZE)
  target_compile_options(flashforge_lib PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(flashforge_lib PUBLIC -fsanitize=address,undefined)
endif()

add_executable(flashforge src/main.cpp)
target_link_libraries(flashforge PRIVATE flashforge_lib)
add_executable(flashforge_tests tests/test_main.cpp)
target_link_libraries(flashforge_tests PRIVATE flashforge_lib)
add_executable(flashforge_benchmark benchmark/benchmark.cpp)
target_link_libraries(flashforge_benchmark PRIVATE flashforge_lib)

enable_testing()
add_test(NAME flashforge_tests COMMAND flashforge_tests)

