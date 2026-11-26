#include <jni.h>
#include <sstream>
#include <string>
#include "kxcompiler/kx_compiler_api.hpp"
#include "kx_llvm_jit.hpp"

namespace {
std::string fromJString(JNIEnv* env, jstring value) {
    if (!value) return {};
    const char* chars = env->GetStringUTFChars(value, nullptr);
    std::string result = chars ? chars : "";
    if (chars) env->ReleaseStringUTFChars(value, chars);
    return result;
}

jstring toJString(JNIEnv* env, const std::string& value) {
    return env->NewStringUTF(value.c_str());
}

kx::CompileOptions androidOptions() {
    kx::CompileOptions options;
    options.sourceName = "main.kx";
    options.target32 = true;
    options.targetTriple = "armv7a-linux-androideabi24";
    options.emitDebugInfo = true;
    options.includeTokens = false;
    options.maxDiagnostics = 64;
    return options;
}
} // namespace

extern "C" JNIEXPORT jstring JNICALL
Java_com_khalilxmodz_kxstudio_MainActivity_nativeCompile(
    JNIEnv* env, jobject, jstring sourceValue) {
    const std::string source = fromJString(env, sourceValue);
    const kx::CompileResult result = kx::Compiler{}.compileSource(source, androidOptions());

    std::ostringstream out;
    if (!result.success) {
        out << "COMPILE FAILED\n";
        out << kx::DiagnosticsAPI::formatAll(result.diagnostics);
        return toJString(env, out.str());
    }

    out << "KX FRONTEND: SUCCESS\n";
    out << "Typed IR validation: SUCCESS\n";
    out << "LLVM IR emission: SUCCESS\n";
    out << "Target triple: " << result.targetTriple << "\n";
    out << "Debug source-map entries: " << result.sourceMap.size() << "\n\n";
    out << "--- LLVM IR ---\n" << result.llvmIR;

    if (!result.sourceMap.empty()) {
        out << "\n--- SOURCE MAP ---\n";
        for (const auto& entry : result.sourceMap) {
            out << entry.sourceFile << ':' << entry.sourceLine << ':'
                << entry.sourceColumn << "  " << entry.functionName
                << "  [" << entry.nodeKind << "]\n";
        }
    }
    return toJString(env, out.str());
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_khalilxmodz_kxstudio_MainActivity_nativeRun(
    JNIEnv* env, jobject, jstring sourceValue) {
    const std::string source = fromJString(env, sourceValue);
    const kx::CompileResult compilation = kx::Compiler{}.compileSource(source, androidOptions());
    if (!compilation.success) {
        return toJString(env, "COMPILE FAILED\\n" +
            kx::DiagnosticsAPI::formatAll(compilation.diagnostics));
    }

    const kx::NativeExecutionResult execution = kx::executeLLVMIR(compilation.llvmIR);
    std::ostringstream out;
    out << (execution.success ? "NATIVE ARMv7 RUN: SUCCESS\\n" : "NATIVE ARMv7 RUN: FAILED\\n");
    out << execution.output;
    return toJString(env, out.str());
}
