#pragma once
#include "kx_parser.hpp"
#include "kx_diagnostics.hpp"
#include <string>
#include <vector>

namespace kx {

class LexerParserAPI {
public:
    static std::vector<kxparse::Token> lex(const std::string& source, std::vector<Diagnostic>& diagnostics,
                                           const std::string& file = "<memory>", unsigned limit = 32) {
        std::string error;
        kxparse::Lexer lexer(source);
        auto tokens = lexer.run(error);
        if (!error.empty()) DiagnosticsAPI::add(diagnostics,
            {Severity::Error, Stage::Lexer, "KX1001", error, parseLocation(error, file)}, limit);
        return tokens;
    }

    static kxparse::Result parseTokens(const std::vector<kxparse::Token>& tokens,
                                       std::vector<Diagnostic>& diagnostics,
                                       const std::string& file = "<memory>", unsigned limit = 32) {
        kxparse::Parser parser(tokens);
        auto result = parser.run();
        if (!result.error.empty()) DiagnosticsAPI::add(diagnostics,
            {Severity::Error, Stage::Parser, "KX2001", result.error, parseLocation(result.error, file)}, limit);
        return result;
    }

    static kxparse::Result parse(const std::string& source, std::vector<Diagnostic>& diagnostics,
                                 const std::string& file = "<memory>", unsigned limit = 32) {
        std::string lexError;
        kxparse::Lexer lexer(source);
        auto tokens = lexer.run(lexError);
        if (!lexError.empty()) {
            DiagnosticsAPI::add(diagnostics,
                {Severity::Error, Stage::Lexer, "KX1001", lexError, parseLocation(lexError, file)}, limit);
            kxparse::Result failed; failed.error = lexError; return failed;
        }
        return parseTokens(tokens, diagnostics, file, limit);
    }
};

} // namespace kx
