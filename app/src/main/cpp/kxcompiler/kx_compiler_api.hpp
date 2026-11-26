#pragma once

// Public integration surface for the KX direct frontend. This header deliberately
// exposes compilation results and diagnostics without exposing CLI implementation.
#include "kx_parser.hpp"
#include "kx_diagnostics.hpp"
#include "kx_frontend_api.hpp"
#include "kx_typed_ir.hpp"
#include "kx_ir_validator.hpp"
#include "kx_llvm_emitter.hpp"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>
#include <fstream>
#include <cerrno>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <filesystem>

namespace kx {

enum class CompileStatus { Success, InvalidOptions, IOFailure, FrontendFailure, SemanticFailure, IRFailure, BackendFailure, VerificationFailure };

struct CompileOptions {
    bool freestanding = false;
    bool target32 = false; // Compatibility option for the existing emitter.
    bool emitDebugInfo = false;
    bool includeTokens = false;
    unsigned maxDiagnostics = 32;
    std::string targetTriple;
    std::string sourceName = "<memory>";
};

struct DebugSourceMapEntry {
    std::string functionName;
    std::string sourceFile;
    int sourceLine = 1;
    int sourceColumn = 1;
    std::string nodeKind;
};

struct CompileResult {
    bool success = false;
    CompileStatus status = CompileStatus::FrontendFailure;
    std::vector<Diagnostic> diagnostics;
    std::vector<kxparse::Token> tokens;
    kxparse::Result parsedSource;
    kxir::Module typedModule;
    std::vector<DebugSourceMapEntry> sourceMap;
    std::string llvmIR;
    std::string targetTriple;
};

inline std::string escapeLLVMString(const std::string& value) {
    std::ostringstream out;
    for (unsigned char c : value) {
        if (c == '"' || c == '\\') out << '\\' << static_cast<char>(c);
        else if (c < 32 || c >= 127) { const char* hex = "0123456789ABCDEF"; out << '\\' << hex[c >> 4] << hex[c & 15]; }
        else out << static_cast<char>(c);
    }
    return out.str();
}

inline bool isDebugMappedStatement(const std::string& kind) {
    return kind == "block" || kind == "declaration" || kind == "return" ||
           kind == "expression_statement" || kind == "if" || kind == "while" ||
           kind == "do_while" || kind == "for" || kind == "break" ||
           kind == "continue" || kind == "switch" || kind == "legacy_instruction_block" ||
           kind == "asm_block" || kind == "asm";
}
inline void collectDebugSourceMap(const kxir::TypedNode& node, const std::string& function,
                                  const std::string& file, std::vector<DebugSourceMapEntry>& out) {
    if (isDebugMappedStatement(node.kind))
        out.push_back({function, file, std::max(1, node.line), std::max(1, node.column), node.kind});
    for (const auto& child : node.children) collectDebugSourceMap(child, function, file, out);
}


class Compiler {
    static bool validateOptions(const CompileOptions& options, std::string& error) {
        if (options.sourceName.empty()) { error = "sourceName must not be empty"; return false; }
        if (options.maxDiagnostics == 0 || options.maxDiagnostics > 4096) {
            error = "maxDiagnostics must be between 1 and 4096"; return false;
        }
        if (options.target32 && options.targetTriple.find("aarch64") != std::string::npos) {
            error = "target32 conflicts with an AArch64 target triple"; return false;
        }
        return true;
    }
public:
    CompileResult compileSource(const std::string& source, const CompileOptions& options = {}) const {
        CompileResult result;
        result.targetTriple = options.targetTriple;
        std::string optionError;
        if (!validateOptions(options, optionError)) {
            result.status = CompileStatus::InvalidOptions;
            DiagnosticsAPI::add(result.diagnostics,
                {Severity::Error, Stage::Parser, "KX0001", optionError, {options.sourceName, 1, 1}}, options.maxDiagnostics ? options.maxDiagnostics : 1);
            return result;
        }
        std::vector<Diagnostic> parseDiagnostics;
        result.parsedSource = LexerParserAPI::parse(source, parseDiagnostics, options.sourceName, options.maxDiagnostics);
        result.diagnostics.insert(result.diagnostics.end(), parseDiagnostics.begin(), parseDiagnostics.end());
        if (DiagnosticsAPI::hasErrors(result.diagnostics)) {
            result.status = CompileStatus::FrontendFailure;
            return result;
        }
        if (options.includeTokens) result.tokens = result.parsedSource.tokens;
        std::string error;
        kxir::Analyzer analyzer(result.parsedSource);
        if (!analyzer.buildTypedModule(result.typedModule, error)) {
            result.status = CompileStatus::SemanticFailure;
            DiagnosticsAPI::add(result.diagnostics,
                {Severity::Error, Stage::Semantic, "KX3001", error, parseLocation(error, options.sourceName)}, options.maxDiagnostics);
            return result;
        }
        if (!kxir::validateModule(result.typedModule, error)) {
            result.status = CompileStatus::IRFailure;
            DiagnosticsAPI::add(result.diagnostics,
                {Severity::Error, Stage::IRValidation, "KX4001", error, parseLocation(error, options.sourceName)}, options.maxDiagnostics);
            return result;
        }
        const bool inferred32 = options.target32 || options.targetTriple.find("armv7") != std::string::npos || options.targetTriple.find("armv7a") != std::string::npos || options.targetTriple.find("i386") != std::string::npos || options.targetTriple.find("i686") != std::string::npos || options.targetTriple.find("wasm32") != std::string::npos;
        kxir::Emitter emitter(result.typedModule, options.freestanding, inferred32);
        if (options.emitDebugInfo) emitter.enableDebugInfo(options.sourceName);
        if (!emitter.emit(result.llvmIR, error)) {
            result.status = CompileStatus::BackendFailure;
            DiagnosticsAPI::add(result.diagnostics,
                {Severity::Error, Stage::LLVMEmission, "KX5001", error, parseLocation(error, options.sourceName)}, options.maxDiagnostics);
            return result;
        }
        if (!options.targetTriple.empty())
            result.llvmIR = "target triple = \"" + escapeLLVMString(options.targetTriple) + "\"\n" + result.llvmIR;
        if (options.emitDebugInfo) {
            for (const auto& fn : result.typedModule.functions)
                collectDebugSourceMap(fn.body, fn.name, options.sourceName, result.sourceMap);
        }
        result.success = true;
        result.status = CompileStatus::Success;
        return result;
    }

    CompileResult compileFile(const std::string& path, CompileOptions options = {}) const {
        std::ifstream input(path, std::ios::binary);
        if (!input) {
            CompileResult result;
            result.status = CompileStatus::IOFailure;
            result.diagnostics.push_back({Severity::Error, Stage::Parser, "KX0002",
                "cannot open source file '" + path + "'", {path, 1, 1}});
            return result;
        }
        std::ostringstream contents; contents << input.rdbuf();
        if (input.bad()) {
            CompileResult result;
            result.status = CompileStatus::IOFailure;
            result.diagnostics.push_back({Severity::Error, Stage::Parser, "KX0003",
                "failed while reading source file '" + path + "'", {path, 1, 1}});
            return result;
        }
        options.sourceName = path;
        return compileSource(contents.str(), options);
    }
};

struct BuildRequest {
    std::string sourcePath;
    std::string sourceText;
    std::string sourceName = "<memory>";
    std::string outputPath;
    std::string intermediatePath;
    std::string clangPath = "clang";
    std::string targetTriple;
    std::string cpu;
    std::string optimization = "-O2";
    std::string floatAbi;
    std::string linkerScript;
    std::string startupFile;
    bool emitObject = false;
    bool freestanding = false;
    bool debugInfo = false;
    bool verifyLLVM = true;
    bool link = false;
    bool keepLLVM = true;
};
struct BuildPlan { std::string compiler = "clang"; std::vector<std::string> arguments; std::string outputPath; };
enum class BuildStatus { Success, InvalidRequest, CompileFailure, IOFailure, VerificationFailure, ToolFailure };
struct BuildResult {
    bool success = false;
    BuildStatus status = BuildStatus::InvalidRequest;
    std::string outputPath;
    std::string llvmPath;
    std::string toolOutput;
    int exitCode = -1;
    CompileResult compilation;
    std::vector<Diagnostic> diagnostics;
};

// BuildManager executes Clang without a shell, so paths and arguments are not
// interpreted as shell syntax. This implementation targets POSIX hosts,
// including Linux and Android.
class BuildManager {
    static int runProcess(const std::vector<std::string>& arguments, std::string& output) {
        if (arguments.empty()) return -1;
        int pipefd[2];
        if (pipe(pipefd) != 0) return -1;
        pid_t pid = fork();
        if (pid < 0) { close(pipefd[0]); close(pipefd[1]); return -1; }
        if (pid == 0) {
            close(pipefd[0]);
            dup2(pipefd[1], STDOUT_FILENO);
            dup2(pipefd[1], STDERR_FILENO);
            close(pipefd[1]);
            std::vector<char*> argv;
            argv.reserve(arguments.size() + 1);
            for (const auto& arg : arguments) argv.push_back(const_cast<char*>(arg.c_str()));
            argv.push_back(nullptr);
            execvp(argv[0], argv.data());
            _exit(errno == ENOENT ? 127 : 126);
        }
        close(pipefd[1]);
        output.clear();
        char buffer[4096];
        ssize_t count;
        while ((count = read(pipefd[0], buffer, sizeof(buffer))) > 0) output.append(buffer, static_cast<size_t>(count));
        close(pipefd[0]);
        int status = 0;
        while (waitpid(pid, &status, 0) < 0) { if (errno != EINTR) return -1; }
        if (WIFEXITED(status)) return WEXITSTATUS(status);
        if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
        return -1;
    }

public:
    static BuildPlan makePlan(const BuildRequest& request, const std::string& llvmPath) {
        BuildPlan plan; plan.compiler = request.clangPath; plan.outputPath = request.outputPath;
        if (!request.targetTriple.empty()) { plan.arguments.push_back("-target"); plan.arguments.push_back(request.targetTriple); }
        if (!request.cpu.empty()) plan.arguments.push_back("-mcpu=" + request.cpu);
        if (request.targetTriple.find("armv7") != std::string::npos || request.targetTriple.find("armv7a") != std::string::npos)
            plan.arguments.push_back(request.targetTriple.find("none-eabi") != std::string::npos ? "-mabi=aapcs" : "-mabi=aapcs-linux");
        if (!request.floatAbi.empty()) plan.arguments.push_back("-mfloat-abi=" + request.floatAbi);
        plan.arguments.push_back(request.optimization);
        plan.arguments.push_back("-Wno-override-module");
        if (request.freestanding) { plan.arguments.push_back("-ffreestanding"); plan.arguments.push_back("-fno-builtin"); }
        if (request.link && request.freestanding) plan.arguments.push_back("-nostdlib");
        if (request.emitObject) plan.arguments.push_back("-c");
        plan.arguments.push_back("-x"); plan.arguments.push_back("ir"); plan.arguments.push_back(llvmPath);
        if (request.link && !request.startupFile.empty()) { plan.arguments.push_back("-x"); plan.arguments.push_back("assembler"); plan.arguments.push_back(request.startupFile); }
        if (request.link && !request.linkerScript.empty()) plan.arguments.push_back("-Wl,-T," + request.linkerScript);
        if (request.link && request.freestanding) plan.arguments.push_back("-Wl,-e,_start");
        plan.arguments.push_back("-o"); plan.arguments.push_back(request.outputPath);
        return plan;
    }

    BuildResult build(const BuildRequest& request) const {
        BuildResult result;
        if (request.sourcePath.empty() && request.sourceText.empty()) {
            result.status = BuildStatus::InvalidRequest;
            result.diagnostics.push_back({Severity::Error, Stage::Build, "KX6001", "sourcePath or sourceText is required", {request.sourceName,1,1}});
            return result;
        }
        if (request.outputPath.empty()) {
            result.status = BuildStatus::InvalidRequest;
            result.diagnostics.push_back({Severity::Error, Stage::Build, "KX6002", "outputPath is required", {request.sourceName,1,1}});
            return result;
        }
        if (request.optimization != "-O0" && request.optimization != "-O1" && request.optimization != "-O2" && request.optimization != "-O3" && request.optimization != "-Os" && request.optimization != "-Oz") {
            result.status = BuildStatus::InvalidRequest;
            result.diagnostics.push_back({Severity::Error, Stage::Build, "KX6003", "unsupported optimization level", {request.sourceName,1,1}});
            return result;
        }
        if (request.link && request.emitObject) {
            result.status = BuildStatus::InvalidRequest;
            result.diagnostics.push_back({Severity::Error, Stage::Build, "KX6004", "link and emitObject cannot both be enabled", {request.sourceName,1,1}});
            return result;
        }
        if (request.freestanding && request.targetTriple.empty()) {
            result.status = BuildStatus::InvalidRequest;
            result.diagnostics.push_back({Severity::Error, Stage::Build, "KX6011", "freestanding builds require an explicit target triple", {request.sourceName,1,1}});
            return result;
        }
        if (request.link && (!request.freestanding || request.targetTriple.empty() || request.linkerScript.empty() || request.startupFile.empty())) {
            result.status = BuildStatus::InvalidRequest;
            result.diagnostics.push_back({Severity::Error, Stage::Build, "KX6012", "freestanding linking requires targetTriple, linkerScript, and startupFile", {request.sourceName,1,1}});
            return result;
        }
        if (!request.floatAbi.empty() && request.floatAbi != "soft" && request.floatAbi != "softfp" && request.floatAbi != "hard") {
            result.status = BuildStatus::InvalidRequest;
            result.diagnostics.push_back({Severity::Error, Stage::Build, "KX6013", "floatAbi must be soft, softfp, or hard", {request.sourceName,1,1}});
            return result;
        }
        if (request.clangPath.empty()) {
            result.status = BuildStatus::InvalidRequest;
            result.diagnostics.push_back({Severity::Error, Stage::Build, "KX6014", "clangPath must not be empty", {request.sourceName,1,1}});
            return result;
        }
        std::string source = request.sourceText;
        std::string sourceName = request.sourceName;
        if (!request.sourcePath.empty()) {
            std::ifstream file(request.sourcePath, std::ios::binary);
            if (!file) {
                result.status = BuildStatus::IOFailure;
                result.diagnostics.push_back({Severity::Error, Stage::Build, "KX6005", "cannot open source file '" + request.sourcePath + "'", {request.sourcePath,1,1}});
                return result;
            }
            std::ostringstream contents; contents << file.rdbuf();
            if (file.bad()) {
                result.status = BuildStatus::IOFailure;
                result.diagnostics.push_back({Severity::Error, Stage::Build, "KX6006", "failed reading source file", {request.sourcePath,1,1}});
                return result;
            }
            source = contents.str(); sourceName = request.sourcePath;
        }
        CompileOptions options;
        options.sourceName = sourceName;
        options.freestanding = request.freestanding;
        options.target32 = request.targetTriple.find("armv7") != std::string::npos || request.targetTriple.find("armv7a") != std::string::npos || request.targetTriple.find("wasm32") != std::string::npos;
        options.targetTriple = request.targetTriple;
        options.emitDebugInfo = request.debugInfo;
        result.compilation = Compiler{}.compileSource(source, options);
        result.diagnostics = result.compilation.diagnostics;
        if (!result.compilation.success) { result.status = BuildStatus::CompileFailure; return result; }

        result.outputPath = request.outputPath;
        result.llvmPath = request.intermediatePath.empty() ? request.outputPath + ".ll" : request.intermediatePath;
        std::error_code directoryError;
        const auto outputParent = std::filesystem::path(result.outputPath).parent_path();
        if (!outputParent.empty()) std::filesystem::create_directories(outputParent, directoryError);
        const auto llvmParent = std::filesystem::path(result.llvmPath).parent_path();
        if (!directoryError && !llvmParent.empty()) std::filesystem::create_directories(llvmParent, directoryError);
        if (directoryError) {
            result.status = BuildStatus::IOFailure;
            result.diagnostics.push_back({Severity::Error, Stage::Build, "KX6015", "cannot create build output directory: " + directoryError.message(), {sourceName,1,1}});
            return result;
        }
        {
            std::ofstream llvm(result.llvmPath, std::ios::binary);
            if (!llvm) {
                result.status = BuildStatus::IOFailure;
                result.diagnostics.push_back({Severity::Error, Stage::Build, "KX6007", "cannot write LLVM IR file", {sourceName,1,1}});
                return result;
            }
            llvm << result.compilation.llvmIR;
            if (!llvm) {
                result.status = BuildStatus::IOFailure;
                result.diagnostics.push_back({Severity::Error, Stage::Build, "KX6008", "failed writing LLVM IR file", {sourceName,1,1}});
                return result;
            }
        }
        if (request.verifyLLVM) {
            std::vector<std::string> verifyArgs{request.clangPath, "-Wno-override-module", "-x", "ir", "-c", result.llvmPath, "-o", "/dev/null"};
            std::string verificationOutput;
            result.exitCode = runProcess(verifyArgs, verificationOutput);
            result.toolOutput += verificationOutput;
            if (result.exitCode != 0) {
                result.status = BuildStatus::VerificationFailure;
                result.diagnostics.push_back({Severity::Error, Stage::LLVMVerification, "KX6009", "LLVM IR verification failed: " + result.toolOutput, {sourceName,1,1}});
                return result;
            }
        }
        BuildPlan plan = makePlan(request, result.llvmPath);
        std::vector<std::string> args{plan.compiler};
        args.insert(args.end(), plan.arguments.begin(), plan.arguments.end());
        std::string buildOutput;
        result.exitCode = runProcess(args, buildOutput);
        result.toolOutput += buildOutput;
        if (result.exitCode != 0) {
            result.status = BuildStatus::ToolFailure;
            result.diagnostics.push_back({Severity::Error, Stage::Build, "KX6010", "Clang build/link failed (exit " + std::to_string(result.exitCode) + "): " + result.toolOutput, {sourceName,1,1}});
            return result;
        }
        result.success = true;
        result.status = BuildStatus::Success;
        if (!request.keepLLVM) unlink(result.llvmPath.c_str());
        return result;
    }
};

inline std::string shellQuote(const std::string& value) {
    std::string out = "'";
    for (char c : value) { if (c == '\'') out += "'\\''"; else out += c; }
    return out + "'";
}

class LLVMVerifier {
public:
    // Uses Clang's LLVM IR parser/verifier. The caller supplies a file path so
    // the API never silently creates temporary files or executes source code.
    static bool verifyFile(const std::string& llvmPath, const std::string& clang = "clang") {
        const std::string command = shellQuote(clang) + " -Wno-override-module -x ir -c -o /dev/null " + shellQuote(llvmPath);
        return std::system(command.c_str()) == 0;
    }
};

struct RuntimeRequest { std::string executablePath; std::vector<std::string> arguments; std::string workingDirectory; unsigned timeoutMs = 10000; };
struct RuntimeResult { int exitCode = -1; bool timedOut = false; std::string standardOutput; std::string standardError; };

// Platform-specific implementations (POSIX/Android host) must provide this.
// No unsafe implicit execution is performed by the compiler library itself.
class RuntimeHost {
public:
    virtual ~RuntimeHost() = default;
    virtual RuntimeResult execute(const RuntimeRequest& request) = 0;
};

} // namespace kx
