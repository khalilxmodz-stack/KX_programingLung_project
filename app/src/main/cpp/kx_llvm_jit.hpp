#pragma once

#include <string>

namespace kx {
struct NativeExecutionResult {
    bool success = false;
    int exitCode = -1;
    std::string output;
};

// Executes validated KX LLVM IR with LLVM's ARM MCJIT backend.
// The Android build packages libLLVM with the ARM target enabled.
NativeExecutionResult executeLLVMIR(const std::string& llvmIR);
}
