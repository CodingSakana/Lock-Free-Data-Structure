cmake_minimum_required(VERSION 3.10)
project(MemoryPool LANGUAGES CXX)

# 默认 Release
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  message(STATUS "Setting build type to 'Release' as none was specified.")
  set(CMAKE_BUILD_TYPE Release CACHE STRING
      "Choose the type of build: Debug Release RelWithDebInfo MinSizeRel."
      FORCE)
endif()

# C++20
set(CMAKE_CXX_STANDARD       20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Debug/Release flag
set(CMAKE_CXX_FLAGS_DEBUG   "${CMAKE_CXX_FLAGS_DEBUG}   -g")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3")

# Threads
find_package(Threads REQUIRED)

# 包含目录
set(INC_DIR   ${CMAKE_SOURCE_DIR}/include)
set(TEST_DIR  ${CMAKE_SOURCE_DIR}/tests)
include_directories(${INC_DIR})

# 全局警告
add_compile_options(-Wall -Wextra)

# ───────────────────────────────────────────────────────────────
# 功能测试可执行：full_test
# ───────────────────────────────────────────────────────────────
add_executable(full_test
    ${TEST_DIR}/full_test.cpp
)
target_compile_features(full_test PRIVATE cxx_std_20)
target_compile_options(full_test PRIVATE -Wall)
target_link_libraries(full_test PRIVATE Threads::Threads atomic)

# ───────────────────────────────────────────────────────────────
# 性能测试可执行：performance_test
# ───────────────────────────────────────────────────────────────
add_executable(performance_test
    ${TEST_DIR}/performance_test.cpp
)
target_compile_features(performance_test PRIVATE cxx_std_20)
target_compile_options(performance_test PRIVATE -Wall)
target_link_libraries(performance_test PRIVATE Threads::Threads atomic)

# ───────────────────────────────────────────────────────────────
# 自定义目标：test
# ───────────────────────────────────────────────────────────────
add_custom_target(test
    DEPENDS full_test
    COMMAND full_test
    COMMENT "Run functional tests"
)

# ───────────────────────────────────────────────────────────────
# 自定义目标：perf
# ───────────────────────────────────────────────────────────────
add_custom_target(perf
    DEPENDS performance_test
    COMMAND performance_test
    COMMENT "Run performance benchmark"
)
