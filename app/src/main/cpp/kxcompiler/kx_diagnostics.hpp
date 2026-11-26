#pragma once
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace kx {

enum class Severity { Note, Warning, Error };
enum class Stage { Lexer, Parser, Semantic, IRValidation, LLVMEmission, LLVMVerification, Build, Runtime };

struct SourceLocation {
    std::string file;
    int line = 1;
    int column = 1;
};

struct Diagnostic {
    Severity severity = Severity::Error;
    Stage stage = Stage::Parser;
    std::string code;
    std::string message;
    SourceLocation location;
};

inline SourceLocation parseLocation(const std::string& message, const std::string& file) {
    SourceLocation loc; loc.file = file;
    const std::string marker = "line ";
    const auto p = message.find(marker);
    if (p == std::string::npos) return loc;
    const auto colon = message.find(':', p + marker.size());
    const auto second = colon == std::string::npos ? colon : message.find(':', colon + 1);
    if (colon == std::string::npos || second == std::string::npos) return loc;
    try {
        loc.line = std::stoi(message.substr(p + marker.size(), colon - (p + marker.size())));
        loc.column = std::stoi(message.substr(colon + 1, second - colon - 1));
    } catch (...) { loc.line = 1; loc.column = 1; }
    return loc;
}

inline std::string formatDiagnostic(const Diagnostic& diagnostic) {
    std::ostringstream out;
    if (!diagnostic.location.file.empty()) out << diagnostic.location.file << ':';
    out << diagnostic.location.line << ':' << diagnostic.location.column << ": ";
    if (diagnostic.severity == Severity::Warning) out << "warning";
    else if (diagnostic.severity == Severity::Note) out << "note";
    else out << "error";
    if (!diagnostic.code.empty()) out << '[' << diagnostic.code << ']';
    out << ": " << diagnostic.message;
    return out.str();
}

class DiagnosticsAPI {
public:
    static void add(std::vector<Diagnostic>& out, Diagnostic diagnostic, unsigned limit = 32) {
        if (out.size() < limit) out.push_back(std::move(diagnostic));
    }
    static bool hasErrors(const std::vector<Diagnostic>& diagnostics) {
        for (const auto& d : diagnostics) if (d.severity == Severity::Error) return true;
        return false;
    }
    static std::string formatAll(const std::vector<Diagnostic>& diagnostics) {
        std::ostringstream out;
        for (size_t i = 0; i < diagnostics.size(); ++i) {
            if (i) out << '\n';
            out << formatDiagnostic(diagnostics[i]);
        }
        return out.str();
    }
};

} // namespace kx
