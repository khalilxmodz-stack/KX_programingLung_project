#include <jni.h>
#include <sstream>
#include <string>
#include "kxcompiler/kx_compiler_api.hpp"

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
    JNIEnv* env, jobject, jstring) {
    // Deliberately fail closed: KX v0.21 emits LLVM IR, but does not include
    // an in-process Android ARM native-code generator or execution host.
    return toJString(env,
        "NATIVE RUNTIME NOT AVAILABLE\n\n"
        "The source can be compiled to validated LLVM IR, but this build does "
        "not yet contain the native ARMv7 code generator, linker, or runtime "
        "loader required to execute KX programs on Android.\n\n"
        "No Preview VM or interpreter is used. Run is intentionally blocked "
        "until the real native runtime is integrated.");
}
