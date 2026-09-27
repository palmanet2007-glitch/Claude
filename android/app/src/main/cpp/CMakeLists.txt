cmake_minimum_required(VERSION 3.22.1)
project(hearamp_android LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Núcleo DSP partilhado com os testes de bancada (../../../../../core).
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../../../../core ${CMAKE_BINARY_DIR}/hearamp_core)

find_package(oboe REQUIRED CONFIG)

add_library(hearamp_jni SHARED
  AudioEngine.cpp
  jni_bridge.cpp
)
target_compile_options(hearamp_jni PRIVATE -Wall -Wextra -O3 -ffast-math)
target_link_libraries(hearamp_jni PRIVATE hearamp oboe::oboe log)
