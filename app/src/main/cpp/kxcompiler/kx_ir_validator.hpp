#pragma once
#include "kx_typed_ir.hpp"
#include <map>
#include <set>
#include <string>

namespace kxir {

inline bool validateTypedNode(const TypedNode& node, std::string& error) {
    const auto fail = [&](const std::string& message) {
        error = "IR validation at line " + std::to_string(node.line) + ":" +
                std::to_string(node.column) + ": " + message;
        return false;
    };
    const auto count = node.children.size();
    if (node.kind.empty()) return fail("node has no kind");

    if (node.type.name.size() && node.type.llvm().empty())
        return fail("node has an invalid type '" + node.type.name + "'");
    if (node.type.pointerDepth > 1)
        return fail("pointer depth exceeds the current IR type model");

    if ((node.kind == "binary" && count != 2) ||
        ((node.kind == "unary" || node.kind == "postfix" || node.kind == "member") && count != 1) ||
        (node.kind == "index" && count != 2) ||
        (node.kind == "conditional" && count != 3) ||
        (node.kind == "expression_statement" && count != 1) ||
        (node.kind == "return" && count > 1) ||
        (node.kind == "if" && (count < 2 || count > 3)) ||
        ((node.kind == "while" || node.kind == "do_while") && count != 2) ||
        (node.kind == "for" && count != 4) ||
        (node.kind == "switch" && count < 1))
        return fail("invalid child count for '" + node.kind + "'");

    static const std::set<std::string> known = {
        "block", "declaration", "variable", "initializer_list", "empty",
        "number", "string", "identifier", "call", "member", "index",
        "unary", "postfix", "binary", "conditional", "expression_statement",
        "return", "if", "while", "do_while", "for", "switch", "case",
        "default", "break", "continue", "legacy_instruction_block",
        "asm_block", "asm"
    };
    if (!known.count(node.kind)) return fail("unknown typed node kind '" + node.kind + "'");

    if ((node.kind == "number" || node.kind == "string" || node.kind == "identifier" ||
         node.kind == "break" || node.kind == "continue" || node.kind == "empty" ||
         node.kind == "legacy_instruction_block" || node.kind == "asm_block" || node.kind == "asm") && count != 0)
        return fail("leaf node '" + node.kind + "' unexpectedly has children");

    if (node.kind == "call" && node.text.empty()) return fail("call node has no callee name");
    if (node.kind == "member" && node.text.find(' ') == std::string::npos)
        return fail("member node has no field name");
    if (node.kind == "declaration") {
        for (const auto& child : node.children)
            if (child.kind != "variable") return fail("declaration contains a non-variable child");
    }
    if (node.kind == "switch") {
        for (size_t i = 1; i < count; ++i)
            if (node.children[i].kind != "case" && node.children[i].kind != "default")
                return fail("switch contains a non-case child");
    }
    for (const auto& child : node.children)
        if (!validateTypedNode(child, error)) return false;
    return true;
}

inline bool validateTypeReference(const Type& type, const Module& module, std::string& error,
                                  const std::string& context) {
    if (type.name.empty() || type.llvm().empty() || type.pointerDepth > 1) {
        error = "IR validation: invalid type in " + context;
        return false;
    }
    if (type.name.rfind("struct ", 0) == 0 && !module.structures.count(type.name.substr(7))) {
        error = "IR validation: unknown struct type '" + type.name + "' in " + context;
        return false;
    }
    if (type.name.rfind("enum ", 0) == 0 && !module.enums.count(type.name.substr(5))) {
        error = "IR validation: unknown enum type '" + type.name + "' in " + context;
        return false;
    }
    return true;
}

inline bool validateNodeTypeReferences(const TypedNode& node, const Module& module, std::string& error) {
    if (!node.type.name.empty() && !validateTypeReference(node.type, module, error,
            "node at line " + std::to_string(node.line) + ":" + std::to_string(node.column)))
        return false;
    for (const auto& child : node.children)
        if (!validateNodeTypeReferences(child, module, error)) return false;
    return true;
}


inline bool validateCalls(const TypedNode& node, const Module& module, std::string& error) {
    if (node.kind == "call") {
        const std::string& name = node.text;
        size_t expected = static_cast<size_t>(-1);
        if (name == "alloc" || name == "arena_alloc" || name == "free" ||
            name == "arena_init" || name == "print") expected = 1;
        else if (name == "arena_reset") expected = 0;
        else if (name == "i8" || name == "i16" || name == "i32" || name == "i64" ||
                 name == "u8" || name == "u16" || name == "u32" || name == "u64" ||
                 name == "bool" || name == "ptr") expected = 1;
        else {
            for (const auto& fn : module.functions) {
                if (fn.name == name) { expected = fn.params.size(); break; }
            }
            if (expected == static_cast<size_t>(-1)) {
                error = "IR validation: call to unknown function '" + name + "'";
                return false;
            }
        }
        if (node.children.size() != expected) {
            error = "IR validation at line " + std::to_string(node.line) + ":" +
                    std::to_string(node.column) + ": wrong argument count for '" + name + "'";
            return false;
        }
    }
    for (const auto& child : node.children)
        if (!validateCalls(child, module, error)) return false;
    return true;
}

inline bool validateModule(const Module& module, std::string& error) {
    std::set<std::string> functionNames;
    for (const auto& structure : module.structures) {
        if (structure.first.empty()) { error = "IR validation: unnamed struct"; return false; }
        std::set<std::string> fields;
        for (const auto& field : structure.second) {
            if (field.first.empty() || !fields.insert(field.first).second) {
                error = "IR validation: empty or duplicate field in struct '" + structure.first + "'";
                return false;
            }
            if (field.second.name == "void" && !field.second.pointer) {
                error = "IR validation: invalid field type in struct '" + structure.first + "'";
                return false;
            }
            if (!validateTypeReference(field.second, module, error, "struct '" + structure.first + "'")) return false;
        }
    }
    for (const auto& enumEntry : module.enums) {
        if (enumEntry.first.empty()) { error = "IR validation: unnamed enum"; return false; }
        if (!enumEntry.second.name.empty() && enumEntry.second.llvm().empty()) {
            error = "IR validation: invalid enum underlying type for '" + enumEntry.first + "'";
            return false;
        }
    }
    for (const auto& function : module.functions) {
        if (function.name.empty() || !functionNames.insert(function.name).second) {
            error = "IR validation: empty or duplicate function name";
            return false;
        }
        if (!validateTypeReference(function.result, module, error, "return type of function '" + function.name + "'")) return false;
        std::set<std::string> parameters;
        for (const auto& parameter : function.params) {
            if (parameter.first.empty() || !parameters.insert(parameter.first).second ||
                (parameter.second.name == "void" && !parameter.second.pointer)) {
                error = "IR validation: invalid or duplicate parameter in function '" + function.name + "'";
                return false;
            }
            if (!validateTypeReference(parameter.second, module, error, "parameter in function '" + function.name + "'")) return false;
        }
        if (function.body.kind != "block") {
            error = "IR validation: function '" + function.name + "' has no block body";
            return false;
        }
        if (!validateTypedNode(function.body, error)) return false;
        if (!validateNodeTypeReferences(function.body, module, error)) return false;
        if (!validateCalls(function.body, module, error)) return false;
    }
    return true;
}

} // namespace kxir
