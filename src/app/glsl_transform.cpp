#include <app/glsl_transform.hpp>

#include <array>
#include <cctype>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {
    enum struct TokenKind {
        IDENTIFIER,
        NUMBER,
        PUNCTUATION,
        WHITESPACE,
        COMMENT,
    };

    struct Token {
        TokenKind kind;
        std::string text;
    };

    auto is_ident_start(char c) -> bool { return std::isalpha(static_cast<unsigned char>(c)) != 0 || c == '_'; }
    auto is_ident_char(char c) -> bool { return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_'; }

    auto tokenize(std::string_view src) -> std::vector<Token> {
        auto tokens = std::vector<Token>{};
        size_t i = 0;
        while (i < src.size()) {
            auto const start = i;
            char const c = src[i];
            auto kind = TokenKind::PUNCTUATION;
            if (c == '/' && i + 1 < src.size() && src[i + 1] == '/') {
                kind = TokenKind::COMMENT;
                while (i < src.size() && src[i] != '\n') {
                    // A backslash at the end of a line comment continues the comment.
                    i += (src[i] == '\\' && i + 1 < src.size() && src[i + 1] == '\n') ? 2 : 1;
                }
            } else if (c == '/' && i + 1 < src.size() && src[i + 1] == '*') {
                kind = TokenKind::COMMENT;
                auto const end = src.find("*/", i + 2);
                i = (end == std::string_view::npos) ? src.size() : end + 2;
            } else if (std::isspace(static_cast<unsigned char>(c)) != 0) {
                kind = TokenKind::WHITESPACE;
                while (i < src.size() && std::isspace(static_cast<unsigned char>(src[i])) != 0) {
                    ++i;
                }
            } else if (is_ident_start(c)) {
                kind = TokenKind::IDENTIFIER;
                while (i < src.size() && is_ident_char(src[i])) {
                    ++i;
                }
            } else if (std::isdigit(static_cast<unsigned char>(c)) != 0 || (c == '.' && i + 1 < src.size() && std::isdigit(static_cast<unsigned char>(src[i + 1])) != 0)) {
                // Numbers, including suffixes and exponents (e.g. 1.5e-3f, 0x1Fu), so that their letters
                // aren't mistaken for identifiers.
                kind = TokenKind::NUMBER;
                bool const is_hex = c == '0' && i + 1 < src.size() && (src[i + 1] == 'x' || src[i + 1] == 'X');
                while (i < src.size()) {
                    char const d = src[i];
                    if (is_ident_char(d) || d == '.') {
                        ++i;
                    } else if ((d == '+' || d == '-') && !is_hex && (src[i - 1] == 'e' || src[i - 1] == 'E')) {
                        ++i;
                    } else {
                        break;
                    }
                }
            } else {
                ++i;
            }
            tokens.push_back({kind, std::string{src.substr(start, i - start)}});
        }
        return tokens;
    }

    auto is_code(Token const &t) -> bool { return t.kind != TokenKind::WHITESPACE && t.kind != TokenKind::COMMENT; }

    auto next_code(std::vector<Token> const &tokens, size_t i) -> size_t {
        for (++i; i < tokens.size(); ++i) {
            if (is_code(tokens[i])) {
                return i;
            }
        }
        return tokens.size();
    }

    auto prev_code(std::vector<Token> const &tokens, size_t i) -> size_t {
        while (i > 0) {
            --i;
            if (is_code(tokens[i])) {
                return i;
            }
        }
        return tokens.size();
    }

    // Removes a token while keeping its newlines, so that error line numbers stay correct.
    void erase_token(Token &t) {
        auto newlines = std::string{};
        for (char c : t.text) {
            if (c == '\n') {
                newlines += '\n';
            }
        }
        t = {TokenKind::WHITESPACE, newlines.empty() ? std::string{" "} : newlines};
    }

    // clang-format off

    // Keywords (or reserved words) of Vulkan GLSL 4.60 that WebGL accepts as identifiers.
    auto const VULKAN_KEYWORDS = std::unordered_set<std::string_view>{
        "sample", "precise", "patch", "subroutine", "shared", "buffer", "coherent", "volatile", "restrict",
        "readonly", "writeonly", "noperspective", "resource", "filter", "input", "output", "superp", "active",
        "common", "partition", "demote", "attribute", "varying", "sampler", "samplerShadow",
        "texture1D", "texture1DArray", "texture2D", "texture2DArray", "texture2DMS", "texture2DMSArray",
        "texture2DRect", "texture3D", "textureCube", "textureCubeArray", "textureBuffer",
        "itexture1D", "itexture2D", "itexture3D", "itextureCube", "itexture2DArray",
        "utexture1D", "utexture2D", "utexture3D", "utextureCube", "utexture2DArray",
        "subpassInput", "subpassInputMS", "isubpassInput", "usubpassInput",
        "image1D", "image2D", "image3D", "imageCube", "image2DArray", "imageBuffer",
        "iimage2D", "uimage2D", "iimage3D", "uimage3D",
        "dvec2", "dvec3", "dvec4", "dmat2", "dmat3", "dmat4",
        "int64_t", "uint64_t", "i64vec2", "i64vec3", "i64vec4", "u64vec2", "u64vec3", "u64vec4",
    };

    // GLSL ES 1.00 (and EXT_shader_texture_lod) texture functions and their GLSL ES 3.00 equivalents.
    auto const LEGACY_TEXTURE_FUNCTIONS = std::unordered_map<std::string_view, std::string_view>{
        {"texture2D", "texture"},
        {"texture3D", "texture"},
        {"textureCube", "texture"},
        {"texture2DLod", "textureLod"},
        {"texture2DLodEXT", "textureLod"},
        {"texture3DLod", "textureLod"},
        {"textureCubeLod", "textureLod"},
        {"textureCubeLodEXT", "textureLod"},
        {"texture2DProj", "textureProj"},
        {"texture3DProj", "textureProj"},
        {"texture2DProjLod", "textureProjLod"},
        {"texture2DProjLodEXT", "textureProjLod"},
        {"texture2DGradEXT", "textureGrad"},
        {"textureCubeGradEXT", "textureGrad"},
        {"texture2DProjGradEXT", "textureProjGrad"},
    };

    // Built-in functions that shaders commonly polyfill (from WebGL1 times). Redefining a built-in is an
    // error in Vulkan GLSL, so user definitions of these are renamed.
    auto const OVERRIDABLE_BUILTINS = std::unordered_set<std::string_view>{
        "packUnorm2x16", "packSnorm2x16", "packHalf2x16", "unpackUnorm2x16", "unpackSnorm2x16", "unpackHalf2x16",
    };

    // Built-in functions of Vulkan GLSL that don't exist in WebGL GLSL ES 3.00. Shadertoy code can only refer to its
    // own definitions with these names, so all occurrences are renamed when the code defines them.
    auto const DESKTOP_ONLY_BUILTINS = std::unordered_set<std::string_view>{
        "min3", "max3", "mid3", "fma", "frexp", "ldexp", "bitfieldExtract", "bitfieldInsert", "bitfieldReverse",
        "bitCount", "findLSB", "findMSB", "uaddCarry", "usubBorrow", "umulExtended", "imulExtended",
        "noise1", "noise2", "noise3", "noise4", "barrier", "memoryBarrier", "groupMemoryBarrier",
        "dFdxFine", "dFdyFine", "dFdxCoarse", "dFdyCoarse", "fwidthFine", "fwidthCoarse",
        "interpolateAtCentroid", "interpolateAtSample", "interpolateAtOffset", "packDouble2x32", "unpackDouble2x32",
        "packUnorm4x8", "packSnorm4x8", "unpackUnorm4x8", "unpackSnorm4x8", "textureQueryLevels", "textureQueryLod",
        "atomicAdd", "atomicMin", "atomicMax", "atomicAnd", "atomicOr", "atomicXor", "atomicExchange", "atomicCompSwap",
    };

    // Built-in functions of GLSL ES 3.00. Variables (and struct fields) with these names are renamed where they
    // aren't called, as e.g. a global "float textureSize;" is a redefinition error in Vulkan GLSL.
    auto const ES3_BUILTIN_FUNCTIONS = std::unordered_set<std::string_view>{
        "radians", "degrees", "sin", "cos", "tan", "asin", "acos", "atan", "sinh", "cosh", "tanh", "asinh", "acosh",
        "atanh", "pow", "exp", "log", "exp2", "log2", "sqrt", "inversesqrt", "abs", "sign", "floor", "trunc", "round",
        "roundEven", "ceil", "fract", "mod", "modf", "min", "max", "clamp", "mix", "step", "smoothstep", "isnan",
        "isinf", "floatBitsToInt", "floatBitsToUint", "intBitsToFloat", "uintBitsToFloat", "packSnorm2x16",
        "unpackSnorm2x16", "packUnorm2x16", "unpackUnorm2x16", "packHalf2x16", "unpackHalf2x16", "length",
        "distance", "dot", "cross", "normalize", "faceforward", "reflect", "refract", "matrixCompMult",
        "outerProduct", "transpose", "determinant", "inverse", "lessThan", "lessThanEqual", "greaterThan",
        "greaterThanEqual", "equal", "notEqual", "any", "all", "not", "textureSize", "texture", "textureProj",
        "textureLod", "textureOffset", "texelFetch", "texelFetchOffset", "textureProjOffset", "textureLodOffset",
        "textureProjLod", "textureProjLodOffset", "textureGrad", "textureGradOffset", "textureProjGrad",
        "textureProjGradOffset", "dFdx", "dFdy", "fwidth",
    };

    // Shadertoy inputs that are provided by desktop-shadertoy. Declaring them again is an error.
    auto const SHADERTOY_INPUTS = std::unordered_set<std::string_view>{
        "iResolution", "iTime", "iTimeDelta", "iFrameRate", "iFrame", "iChannelTime", "iChannelResolution",
        "iMouse", "iDate", "iSampleRate", "iChannel0", "iChannel1", "iChannel2", "iChannel3",
    };

    // clang-format on

    auto const PRECISION_QUALIFIERS = std::unordered_set<std::string_view>{"lowp", "mediump", "highp"};

    struct Definitions {
        std::unordered_set<std::string> functions; // functions and macros
        std::unordered_set<std::string> variables; // variables, parameters and struct fields
    };

    // Collects the names declared in the code. A name counts as a function when it is preceded by a type (or
    // another identifier) and followed by '(' ("vec4 texture2D(...)"), or when it is #defined. It counts as a
    // variable when it is preceded by a type and followed by one of "=;,[)".
    void collect_definitions(std::vector<Token> const &tokens, Definitions &out) {
        static auto const NON_TYPE_WORDS = std::unordered_set<std::string_view>{"return", "else", "case", "ifdef", "ifndef", "undef", "elif", "if", "define"};
        // Variables are only collected at global scope and in struct bodies, as locals and parameters may
        // shadow built-in functions. braces[i] is true for struct bodies.
        auto braces = std::vector<bool>{};
        bool next_brace_is_struct = false;
        int paren_depth = 0; // parameters (inside parentheses) don't count as variables
        for (size_t i = 0; i < tokens.size(); ++i) {
            if (tokens[i].text == "(") {
                ++paren_depth;
            } else if (tokens[i].text == ")") {
                paren_depth = paren_depth > 0 ? paren_depth - 1 : 0;
            } else if (tokens[i].text == "{") {
                braces.push_back(next_brace_is_struct);
                next_brace_is_struct = false;
            } else if (tokens[i].text == "}") {
                if (!braces.empty()) {
                    braces.pop_back();
                }
            } else if (tokens[i].text == ";") {
                next_brace_is_struct = false;
            }
            if (tokens[i].kind != TokenKind::IDENTIFIER) {
                continue;
            }
            if (tokens[i].text == "struct") {
                next_brace_is_struct = true;
            }
            bool const variable_scope = paren_depth == 0 && (braces.empty() || braces.back());
            auto const p = prev_code(tokens, i);
            if (p == tokens.size() || tokens[p].kind != TokenKind::IDENTIFIER) {
                continue;
            }
            auto const n = next_code(tokens, i);
            auto const next = n < tokens.size() ? std::string_view{tokens[n].text} : std::string_view{};
            if (tokens[p].text == "define" || (next == "(" && !NON_TYPE_WORDS.contains(tokens[p].text))) {
                out.functions.insert(tokens[i].text);
            } else if (variable_scope && !NON_TYPE_WORDS.contains(tokens[p].text) && (next == "=" || next == ";" || next == "," || next == "[" || next == ")")) {
                out.variables.insert(tokens[i].text);
            }
        }
    }

    void remove_version_directives(std::vector<Token> &tokens) {
        for (size_t i = 0; i < tokens.size(); ++i) {
            if (tokens[i].text != "#") {
                continue;
            }
            auto const n = next_code(tokens, i);
            if (n == tokens.size() || tokens[n].text != "version") {
                continue;
            }
            // Erase up to the end of the line.
            for (size_t j = i; j < tokens.size(); ++j) {
                if (tokens[j].kind == TokenKind::WHITESPACE && tokens[j].text.find('\n') != std::string::npos) {
                    break;
                }
                erase_token(tokens[j]);
            }
        }
    }

    void remove_precision(std::vector<Token> &tokens, Definitions const &defined) {
        for (size_t i = 0; i < tokens.size(); ++i) {
            if (tokens[i].kind != TokenKind::IDENTIFIER) {
                continue;
            }
            auto const n = next_code(tokens, i);
            bool const is_precision_statement = tokens[i].text == "precision" && n < tokens.size() && PRECISION_QUALIFIERS.contains(tokens[n].text);
            if (PRECISION_QUALIFIERS.contains(tokens[i].text)) {
                if (!defined.functions.contains(tokens[i].text)) {
                    erase_token(tokens[i]);
                }
            } else if (is_precision_statement) {
                // "precision highp float;" ("precision" alone is a common variable name)
                for (size_t j = i; j < tokens.size(); ++j) {
                    bool const end = tokens[j].text == ";";
                    erase_token(tokens[j]);
                    if (end) {
                        break;
                    }
                }
            }
        }
    }

    // "uniform float time;" -> "float time;" and "uniform vec3 iResolution;" -> "".
    void remove_uniforms(std::vector<Token> &tokens) {
        int brace_depth = 0;
        for (size_t i = 0; i < tokens.size(); ++i) {
            auto const &text = tokens[i].text;
            if (text == "{") {
                ++brace_depth;
            } else if (text == "}") {
                --brace_depth;
            }
            if (brace_depth != 0 || tokens[i].kind != TokenKind::IDENTIFIER || text != "uniform") {
                continue;
            }
            // Find the end of the declaration, and the declared names.
            size_t end = i;
            int paren_depth = 0;
            bool is_block = false;
            bool declares_input = false;
            for (size_t j = next_code(tokens, i); j < tokens.size(); j = next_code(tokens, j)) {
                auto const &t = tokens[j].text;
                if (t == "(" || t == "[") {
                    ++paren_depth;
                } else if (t == ")" || t == "]") {
                    --paren_depth;
                } else if (t == "{") {
                    is_block = true;
                    break;
                } else if (t == ";") {
                    end = j;
                    break;
                }
                if (tokens[j].kind == TokenKind::IDENTIFIER && paren_depth == 0 && SHADERTOY_INPUTS.contains(t)) {
                    declares_input = true;
                }
            }
            if (is_block || end == i) {
                continue;
            }
            if (declares_input) {
                for (size_t j = i; j <= end; ++j) {
                    erase_token(tokens[j]);
                }
            } else {
                erase_token(tokens[i]);
            }
            i = end;
        }
    }

    void rename_identifiers(std::vector<Token> &tokens, Definitions const &defined) {
        for (size_t i = 0; i < tokens.size(); ++i) {
            auto &t = tokens[i];
            if (t.kind != TokenKind::IDENTIFIER) {
                continue;
            }
            bool const defined_function = defined.functions.contains(t.text);
            bool const defined_variable = defined.variables.contains(t.text);
            auto const n = next_code(tokens, i);
            bool const is_call = n < tokens.size() && tokens[n].text == "(";
            auto const legacy = LEGACY_TEXTURE_FUNCTIONS.find(t.text);
            if (VULKAN_KEYWORDS.contains(t.text)) {
                if (!defined_function && is_call && legacy != LEGACY_TEXTURE_FUNCTIONS.end()) {
                    t.text = legacy->second;
                } else {
                    t.text = "ds_" + t.text;
                }
            } else if (legacy != LEGACY_TEXTURE_FUNCTIONS.end()) {
                if (!defined_function && is_call) {
                    t.text = legacy->second;
                }
            } else if (defined_function && OVERRIDABLE_BUILTINS.contains(t.text)) {
                t.text = "ds_user_" + t.text;
            } else if ((defined_function || defined_variable) && DESKTOP_ONLY_BUILTINS.contains(t.text)) {
                t.text = "ds_user_" + t.text;
            } else if (defined_variable && !is_call && ES3_BUILTIN_FUNCTIONS.contains(t.text)) {
                t.text = "ds_user_" + t.text;
            }
        }
    }

    auto join(std::vector<Token> const &tokens) -> std::string {
        auto result = std::string{};
        for (auto const &t : tokens) {
            result += t.text;
        }
        return result;
    }
} // namespace

void transform_shadertoy_code(std::string &common_code, std::span<std::string *const> pass_codes) {
    auto common_tokens = tokenize(common_code);
    auto common_definitions = Definitions{};
    collect_definitions(common_tokens, common_definitions);

    auto transform = [](std::vector<Token> &tokens, Definitions const &definitions) {
        remove_version_directives(tokens);
        remove_precision(tokens, definitions);
        remove_uniforms(tokens);
        rename_identifiers(tokens, definitions);
    };

    for (auto *code : pass_codes) {
        auto tokens = tokenize(*code);
        // The common code is included before every pass, so its definitions are visible in every pass.
        auto definitions = common_definitions;
        collect_definitions(tokens, definitions);
        transform(tokens, definitions);
        *code = join(tokens);
    }
    transform(common_tokens, common_definitions);
    common_code = join(common_tokens);
}
