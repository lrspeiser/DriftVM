cmake_minimum_required(VERSION 3.20)
project(DriftVM VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(driftvm src/main.cpp)
if (MSVC)
  target_compile_options(driftvm PRIVATE /W4 /permissive-)
else()
  target_compile_options(driftvm PRIVATE -Wall -Wextra -Wpedantic -O3)
endif()
