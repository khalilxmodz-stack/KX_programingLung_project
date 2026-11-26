#pragma once
#include <regex>
#include <string>
#include <sstream>

namespace kxcl {
// KX's C-like surface syntax is deliberately close to C11. KX function
// declarations use `fn name(args) -> type`; the body uses C-like syntax.
// Clang supplies the mature C parser/type checker and emits LLVM IR, which
// is then consumed by the same LLVM backend path as the instruction frontend.
inline std::string translateToC(const std::string& source, bool freestanding,
                                std::string& error) {
    std::string body = source;
    const std::regex fn(R"(\bfn\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(([^()]*)\)\s*->\s*(i8|i16|i32|i64|u8|u16|u32|u64|bool|void)\s*\{)");
    std::string converted;
    std::sregex_iterator it(body.begin(), body.end(), fn), end;
    size_t last = 0;
    bool foundMain = false;
    for (; it != end; ++it) {
        const auto& m = *it;
        converted.append(body, last, static_cast<size_t>(m.position()) - last);
        std::string name = m[1].str();
        if (name == "main") foundMain = true;
        if (freestanding && name == "main") name = "kx_kernel_main";
        auto mapType = [](const std::string& t) {
            if (t == "i8") return std::string("int8_t");
            if (t == "i16") return std::string("int16_t");
            if (t == "i32") return std::string("int32_t");
            if (t == "i64") return std::string("int64_t");
            if (t == "u8") return std::string("uint8_t");
            if (t == "u16") return std::string("uint16_t");
            if (t == "u32") return std::string("uint32_t");
            if (t == "u64") return std::string("uint64_t");
            if (t == "bool") return std::string("_Bool");
            return t;
        };
        converted += mapType(m[3].str()) + " " + name + "(";
        std::string params = m[2].str();
        if (params.find_first_not_of(" \t\r\n") == std::string::npos) params = "void";
        // KX scalar aliases in parameter declarations are C-compatible typedefs.
        converted += params + ") {";
        last = static_cast<size_t>(m.position() + m.length());
    }
    if (it == end && last == 0) {
        error = "expected at least one function declaration: fn name(...) -> type { ... }";
        return {};
    }
    converted.append(body, last, std::string::npos);
    if (!foundMain) {
        error = "program must define fn main(...) -> type";
        return {};
    }
    std::ostringstream out;
    out << "/* KX C-like translation unit */\n"
        << "typedef signed char int8_t; typedef short int16_t; typedef int int32_t; typedef long long int64_t;\n"
        << "typedef unsigned char uint8_t; typedef unsigned short uint16_t; typedef unsigned int uint32_t; typedef unsigned long long uint64_t;\n"
        << "typedef int8_t i8; typedef int16_t i16; typedef int32_t i32; typedef int64_t i64;\n"
        << "typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64; typedef void* ptr;\n"
        << "typedef _Bool bool;\n#define true 1\n#define false 0\n#define kx_asm(s) __asm__ volatile(s)\n";
    if (!freestanding) {
        out << "extern int printf(const char *, ...);\n"
            << "static void print(int x) { (void)printf(\"%d\\n\", x); }\n";
    }
    out << converted;
    return out.str();
}
}
