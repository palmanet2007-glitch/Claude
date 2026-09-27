cmake_minimum_required(VERSION 3.16)
project(hearamp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_subdirectory(core)

option(HEARAMP_BUILD_TESTS "Compilar testes e ferramentas de bancada" ON)
if(HEARAMP_BUILD_TESTS)
  enable_testing()
  add_executable(hearamp_tests core/tests/test_main.cpp)
  target_link_libraries(hearamp_tests PRIVATE hearamp)
  add_test(NAME hearamp_tests COMMAND hearamp_tests)

  add_executable(hearamp_wav core/tools/hearamp_wav.cpp)
  target_link_libraries(hearamp_wav PRIVATE hearamp)
endif()
