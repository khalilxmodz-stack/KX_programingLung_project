#include "kx_llvm_jit.hpp"

#if defined(KX_WITH_LLVM)
#include <llvm-c/Core.h>
#include <llvm-c/ExecutionEngine.h>
#include <llvm-c/IRReader.h>
#include <llvm-c/Target.h>
#include <llvm-c/TargetMachine.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

namespace kx {
namespace {
void initializeARMBackend() {
    static bool initialized = false;
    if (initialized) return;
    LLVMLinkInMCJIT();
    LLVMInitializeARMTargetInfo();
    LLVMInitializeARMTarget();
    LLVMInitializeARMTargetMC();
    LLVMInitializeARMAsmPrinter();
    initialized = true;
}

void registerAndroidSymbols() {
    // KX's current LLVM emitter uses these C runtime entry points.
    LLVMAddSymbol("printf", reinterpret_cast<void*>(static_cast<int (*)(const char*, ...)>(std::printf)));
    LLVMAddSymbol("malloc", reinterpret_cast<void*>(static_cast<void* (*)(size_t)>(std::malloc)));
    LLVMAddSymbol("free", reinterpret_cast<void*>(static_cast<void (*)(void*)>(std::free)));
}
} // namespace

NativeExecutionResult executeLLVMIR(const std::string& llvmIR) {
    NativeExecutionResult result;
    initializeARMBackend();
    registerAndroidSymbols();

    LLVMContextRef context = LLVMContextCreate();
    if (!context) {
        result.output = "LLVM ERROR: unable to create IR context";
        return result;
    }

    LLVMMemoryBufferRef buffer = LLVMCreateMemoryBufferWithMemoryRangeCopy(
        llvmIR.data(), llvmIR.size(), "kx-program.ll");
    LLVMModuleRef module = nullptr;
    char* message = nullptr;
    if (LLVMParseIRInContext(context, buffer, &module, &message) != 0) {
        result.output = std::string("LLVM IR parse failed: ") + (message ? message : "unknown error");
        if (message) LLVMDisposeMessage(message);
        LLVMContextDispose(context);
        return result;
    }

    LLVMMCJITCompilerOptions options;
    LLVMInitializeMCJITCompilerOptions(&options, sizeof(options));
    options.OptLevel = 2;
    LLVMExecutionEngineRef engine = nullptr;
    if (LLVMCreateMCJITCompilerForModule(&engine, module, &options, sizeof(options), &message) != 0) {
        result.output = std::string("LLVM MCJIT creation failed: ") + (message ? message : "unknown error");
        if (message) LLVMDisposeMessage(message);
        LLVMDisposeModule(module);
        LLVMContextDispose(context);
        return result;
    }

    const uint64_t address = LLVMGetFunctionAddress(engine, "main");
    if (address == 0) {
        result.output = "LLVM JIT ERROR: entry function 'main' was not found";
        LLVMDisposeExecutionEngine(engine);
        LLVMContextDispose(context);
        return result;
    }

    using KxMain = int (*)();
    KxMain entry = reinterpret_cast<KxMain>(static_cast<uintptr_t>(address));

    // Capture printf output so the Android editor can display program output.
    FILE* capture = std::tmpfile();
    if (!capture) {
        result.output = "Native runtime error: unable to create output capture file";
        LLVMDisposeExecutionEngine(engine);
        LLVMContextDispose(context);
        return result;
    }
    std::fflush(stdout);
    const int savedStdout = dup(STDOUT_FILENO);
    if (savedStdout < 0 || dup2(fileno(capture), STDOUT_FILENO) < 0) {
        if (savedStdout >= 0) close(savedStdout);
        std::fclose(capture);
        result.output = "Native runtime error: unable to redirect program output";
        LLVMDisposeExecutionEngine(engine);
        LLVMContextDispose(context);
        return result;
    }

    result.exitCode = entry();

    std::fflush(stdout);
    dup2(savedStdout, STDOUT_FILENO);
    close(savedStdout);
    std::rewind(capture);
    char chunk[1024];
    while (const size_t count = std::fread(chunk, 1, sizeof(chunk), capture))
        result.output.append(chunk, count);
    std::fclose(capture);

    result.success = true;
    if (result.output.empty()) result.output = "(program produced no stdout output)";
    result.output += "\n\n[Native ARM execution completed; exit code ";
    result.output += std::to_string(result.exitCode);
    result.output += "]";
    LLVMDisposeExecutionEngine(engine);
    LLVMContextDispose(context);
    return result;
}
} // namespace kx

#else
namespace kx {
NativeExecutionResult executeLLVMIR(const std::string&) {
    return {false, -1,
        "LLVM ARM backend is not packaged in this build. Install the CI-built ARMv7 APK containing libLLVM.so."};
}
} // namespace kx
#endif
