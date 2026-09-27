# Biblioteca DSP portátil (C++17, sem alocação no percurso de áudio, sem dependências).
# Usada pela app Android (NDK) e portável para SoC embebidos (Cortex-M/A, DSP com FPU).
add_library(hearamp STATIC
  src/processor.cpp
  src/feedback.cpp
  src/fitting.cpp
)
target_include_directories(hearamp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(hearamp PRIVATE -Wall -Wextra -Wpedantic -Wshadow)
endif()
