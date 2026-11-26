cmake_minimum_required(VERSION 3.22.1)
project(kxstudio LANGUAGES C CXX)

add_library(kxstudio SHARED
    native-lib.cpp
    kx_llvm_jit.cpp
)

target_compile_features(kxstudio PRIVATE cxx_std_17)
target_compile_options(kxstudio PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(kxstudio PRIVATE log android dl)

# The CI pipeline builds LLVM 18 for the Android ARMv7 target and stages its
# C headers and shared library here. A local frontend-only build remains
# possible, but native execution is enabled only when the real backend exists.
set(KX_LLVM_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/llvm-include")
set(KX_LLVM_LIBRARY "${CMAKE_CURRENT_SOURCE_DIR}/../jniLibs/${ANDROID_ABI}/libLLVM.so")

if(EXISTS "${KX_LLVM_INCLUDE_DIR}/llvm-c/Core.h" AND EXISTS "${KX_LLVM_LIBRARY}")
    message(STATUS "KX: enabling LLVM ARM MCJIT runtime")
    target_include_directories(kxstudio PRIVATE "${KX_LLVM_INCLUDE_DIR}")
    target_compile_definitions(kxstudio PRIVATE KX_WITH_LLVM=1)
    target_link_libraries(kxstudio PRIVATE "${KX_LLVM_LIBRARY}")
else()
    message(WARNING "KX: LLVM ARM runtime is not staged; Run will report unavailable")
endif()
