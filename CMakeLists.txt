cmake_minimum_required(VERSION 3.12)

set(CMAKE_CXX_COMPILER g++)
project(MemoryPool CXX)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build." FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_CXX_FLAGS_DEBUG   "-O0 -g -Wall -Wextra -Werror -Wpedantic")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -Wall -Wextra -Werror -Wpedantic")


add_executable(full_test tests/full_test.cpp)
target_include_directories(full_test PRIVATE ${PROJECT_SOURCE_DIR}/include)

add_executable(performance_test tests/performance_test.cpp)
target_include_directories(performance_test PRIVATE ${PROJECT_SOURCE_DIR}/include)
