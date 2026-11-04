#pragma once

#include <string>
#include <span>

// Rewrites shadertoy (WebGL2 / GLSL ES 3.00) user code so it compiles as Vulkan GLSL 4.60:
// - identifiers that are keywords in Vulkan GLSL but not in WebGL (e.g. "sample", "buffer", "texture2D")
//   are renamed. Legacy texture functions (texture2D, textureCube, ...) map to their modern equivalents,
//   unless the user defines a function with that name.
// - built-in functions that the user redefines (e.g. "packUnorm2x16" polyfills) are renamed.
// - "uniform" declarations become plain globals; redeclarations of shadertoy inputs (iResolution, ...) are removed.
// - precision statements and precision qualifiers are removed, "#version" directives are removed.
//
// `common_code` is the "Common" tab, which is included in every pass. `pass_codes` are the codes of the
// individual passes. All strings are transformed in place.
void transform_shadertoy_code(std::string &common_code, std::span<std::string *const> pass_codes);
