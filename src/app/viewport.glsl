// Robustness options implemented by our glslang fork (deps/glslang) to match how WebGL runs shadertoy code:
// - Variables declared without an initializer are zero initialized (reading them is otherwise undefined).
// - Dynamic indices into fixed size arrays, matrices and vectors are clamped in-bounds (out-of-bounds
//   accesses are undefined and can crash the GPU).
// - Every loop breaks after a fixed number of iterations, so loops whose trip count comes from undefined
//   behavior (e.g. int(1.0/0.0)) can't hang the GPU.
// - Global consts with non-constant initializers (e.g. calling a user function) become regular globals.
// For diagnosing GPU crashes, each can be disabled with the DESKTOP_SHADERTOY_DISABLE environment variable
// (see Viewport::load_shadertoy_json).
#if !defined(_DS_DISABLE_ZERO_INIT)
#pragma zero_initialize_variables
#endif
#if !defined(_DS_DISABLE_CLAMP_INDICES)
#pragma clamp_dynamic_indices
#endif
#if !defined(_DS_DISABLE_LOOP_LIMIT)
#pragma loop_iteration_limit(262144)
#endif
#if !defined(_DS_DISABLE_RELAXED_CONST)
#pragma relaxed_global_const_initializers
#endif

#include <app/viewport.inl>

#if DAXA_SHADER_STAGE == DAXA_SHADER_STAGE_VERTEX

void main() {
    vec2 uv = vec2(gl_VertexIndex & 1, (gl_VertexIndex >> 1) & 1);
    gl_Position = vec4(uv * 4 - 1, 0, 1);
}

#elif DAXA_SHADER_STAGE == DAXA_SHADER_STAGE_FRAGMENT

// clang-format off
daxa_f32vec3 iResolution;
daxa_f32     iTime;
daxa_f32     iTimeDelta;
daxa_f32     iFrameRate;
daxa_i32     iFrame;
daxa_f32     iChannelTime[4];
daxa_f32vec3 iChannelResolution[4];
daxa_f32vec4 iMouse;
daxa_f32vec4 iDate;
daxa_f32     iSampleRate;
// clang-format on

// overloads
vec4 texture(CombinedImageSampler2D c, vec2 p) {
    if (c.convert_int_to_float == 1) {
        return vec4(texture(daxa_isampler2D(c.image_view_id, c.sampler_id), p));
    } else {
        return texture(daxa_sampler2D(c.image_view_id, c.sampler_id), p);
    }
}
vec4 texture(CombinedImageSampler2D c, vec2 p, float bias) {
    if (c.convert_int_to_float == 1) {
        return vec4(texture(daxa_isampler2D(c.image_view_id, c.sampler_id), p, bias));
    } else {
        return texture(daxa_sampler2D(c.image_view_id, c.sampler_id), p, bias);
    }
}
ivec4 texture(CombinedImageSampler2D_int c, vec2 p) { return texture(daxa_isampler2D(c.image_view_id, c.sampler_id), p); }
ivec4 texture(CombinedImageSampler2D_int c, vec2 p, float bias) { return texture(daxa_isampler2D(c.image_view_id, c.sampler_id), p, bias); }
vec4 texture(CombinedImageSamplerCube c, vec3 p) { return texture(daxa_samplerCube(c.image_view_id, c.sampler_id), p); }
vec4 texture(CombinedImageSamplerCube c, vec3 p, float bias) { return texture(daxa_samplerCube(c.image_view_id, c.sampler_id), p, bias); }
vec4 texture(CombinedImageSampler3D c, vec3 p) { return texture(daxa_sampler3D(c.image_view_id, c.sampler_id), p); }
vec4 texture(CombinedImageSampler3D c, vec3 p, float bias) { return texture(daxa_sampler3D(c.image_view_id, c.sampler_id), p, bias); }

ivec2 textureSize(CombinedImageSampler2D c, int lod) {
    if (c.convert_int_to_float == 1) {
        return textureSize(daxa_isampler2D(c.image_view_id, c.sampler_id), lod);
    } else {
        return textureSize(daxa_sampler2D(c.image_view_id, c.sampler_id), lod);
    }
}
ivec2 textureSize(CombinedImageSampler2D_int c, int lod) { return textureSize(daxa_isampler2D(c.image_view_id, c.sampler_id), lod); }
ivec2 textureSize(CombinedImageSamplerCube c, int lod) { return textureSize(daxa_samplerCube(c.image_view_id, c.sampler_id), lod); }
ivec3 textureSize(CombinedImageSampler3D c, int lod) { return textureSize(daxa_sampler3D(c.image_view_id, c.sampler_id), lod); }

int textureQueryLevels(CombinedImageSampler2D c) {
    if (c.convert_int_to_float == 1) {
        return textureQueryLevels(daxa_isampler2D(c.image_view_id, c.sampler_id));
    } else {
        return textureQueryLevels(daxa_sampler2D(c.image_view_id, c.sampler_id));
    }
}
int textureQueryLevels(CombinedImageSampler2D_int c) { return textureQueryLevels(daxa_isampler2D(c.image_view_id, c.sampler_id)); }
int textureQueryLevels(CombinedImageSamplerCube c) { return textureQueryLevels(daxa_samplerCube(c.image_view_id, c.sampler_id)); }
int textureQueryLevels(CombinedImageSampler3D c) { return textureQueryLevels(daxa_sampler3D(c.image_view_id, c.sampler_id)); }

vec4 texelFetch(CombinedImageSampler2D c, ivec2 p, int lod) {
    if (c.convert_int_to_float == 1) {
        return vec4(texelFetch(daxa_isampler2D(c.image_view_id, c.sampler_id), p, lod));
    } else {
        return texelFetch(daxa_texture2D(c.image_view_id), p, lod);
    }
}
ivec4 texelFetch(CombinedImageSampler2D_int c, ivec2 p, int lod) { return texelFetch(daxa_itexture2D(c.image_view_id), p, lod); }
vec4 texelFetch(CombinedImageSampler3D c, ivec3 p, int lod) { return texelFetch(daxa_texture3D(c.image_view_id), p, lod); }

vec4 textureLod(CombinedImageSampler2D c, vec2 p, float lod) {
    if (c.convert_int_to_float == 1) {
        return vec4(textureLod(daxa_isampler2D(c.image_view_id, c.sampler_id), p, lod));
    } else {
        return textureLod(daxa_sampler2D(c.image_view_id, c.sampler_id), p, lod);
    }
}
ivec4 textureLod(CombinedImageSampler2D_int c, vec2 p, float lod) { return textureLod(daxa_isampler2D(c.image_view_id, c.sampler_id), p, lod); }
vec4 textureLod(CombinedImageSamplerCube c, vec3 p, float lod) { return textureLod(daxa_samplerCube(c.image_view_id, c.sampler_id), p, lod); }
vec4 textureLod(CombinedImageSampler3D c, vec3 p, float lod) { return textureLod(daxa_sampler3D(c.image_view_id, c.sampler_id), p, lod); }

vec4 textureGrad(CombinedImageSampler2D c, vec2 p, vec2 dTdx, vec2 dTdy) {
    if (c.convert_int_to_float == 1) {
        return vec4(textureGrad(daxa_isampler2D(c.image_view_id, c.sampler_id), p, dTdx, dTdy));
    } else {
        return textureGrad(daxa_sampler2D(c.image_view_id, c.sampler_id), p, dTdx, dTdy);
    }
}
ivec4 textureGrad(CombinedImageSampler2D_int c, vec2 p, vec2 dTdx, vec2 dTdy) { return textureGrad(daxa_isampler2D(c.image_view_id, c.sampler_id), p, dTdx, dTdy); }
vec4 textureGrad(CombinedImageSamplerCube c, vec3 p, vec3 dTdx, vec3 dTdy) { return textureGrad(daxa_samplerCube(c.image_view_id, c.sampler_id), p, dTdx, dTdy); }
vec4 textureGrad(CombinedImageSampler3D c, vec3 p, vec3 dTdx, vec3 dTdy) { return textureGrad(daxa_sampler3D(c.image_view_id, c.sampler_id), p, dTdx, dTdy); }

// The remaining GLSL ES 3.00 texture functions.
// GLSL requires texel offsets to be compile time constants, which can't be passed through a function. So offsets
// are applied by shifting the coordinates by whole texels (exact for texelFetchOffset, and for the other
// functions exact when sampling mip 0).
vec2 _ds_texel_offset(CombinedImageSampler2D c, ivec2 offset, int lod) { return vec2(offset) / vec2(textureSize(c, lod)); }
vec3 _ds_texel_offset(CombinedImageSampler3D c, ivec3 offset, int lod) { return vec3(offset) / vec3(textureSize(c, lod)); }

vec4 textureProj(CombinedImageSampler2D c, vec3 p) { return texture(c, p.xy / p.z); }
vec4 textureProj(CombinedImageSampler2D c, vec3 p, float bias) { return texture(c, p.xy / p.z, bias); }
vec4 textureProj(CombinedImageSampler2D c, vec4 p) { return texture(c, p.xy / p.w); }
vec4 textureProj(CombinedImageSampler2D c, vec4 p, float bias) { return texture(c, p.xy / p.w, bias); }
vec4 textureProj(CombinedImageSampler3D c, vec4 p) { return texture(c, p.xyz / p.w); }
vec4 textureProj(CombinedImageSampler3D c, vec4 p, float bias) { return texture(c, p.xyz / p.w, bias); }

vec4 textureProjLod(CombinedImageSampler2D c, vec3 p, float lod) { return textureLod(c, p.xy / p.z, lod); }
vec4 textureProjLod(CombinedImageSampler2D c, vec4 p, float lod) { return textureLod(c, p.xy / p.w, lod); }
vec4 textureProjLod(CombinedImageSampler3D c, vec4 p, float lod) { return textureLod(c, p.xyz / p.w, lod); }

vec4 textureProjGrad(CombinedImageSampler2D c, vec3 p, vec2 dTdx, vec2 dTdy) { return textureGrad(c, p.xy / p.z, dTdx, dTdy); }
vec4 textureProjGrad(CombinedImageSampler2D c, vec4 p, vec2 dTdx, vec2 dTdy) { return textureGrad(c, p.xy / p.w, dTdx, dTdy); }
vec4 textureProjGrad(CombinedImageSampler3D c, vec4 p, vec3 dTdx, vec3 dTdy) { return textureGrad(c, p.xyz / p.w, dTdx, dTdy); }

vec4 textureOffset(CombinedImageSampler2D c, vec2 p, ivec2 offset) { return texture(c, p + _ds_texel_offset(c, offset, 0)); }
vec4 textureOffset(CombinedImageSampler2D c, vec2 p, ivec2 offset, float bias) { return texture(c, p + _ds_texel_offset(c, offset, 0), bias); }
vec4 textureOffset(CombinedImageSampler3D c, vec3 p, ivec3 offset) { return texture(c, p + _ds_texel_offset(c, offset, 0)); }
vec4 textureOffset(CombinedImageSampler3D c, vec3 p, ivec3 offset, float bias) { return texture(c, p + _ds_texel_offset(c, offset, 0), bias); }

vec4 textureProjOffset(CombinedImageSampler2D c, vec3 p, ivec2 offset) { return textureOffset(c, p.xy / p.z, offset); }
vec4 textureProjOffset(CombinedImageSampler2D c, vec3 p, ivec2 offset, float bias) { return textureOffset(c, p.xy / p.z, offset, bias); }
vec4 textureProjOffset(CombinedImageSampler2D c, vec4 p, ivec2 offset) { return textureOffset(c, p.xy / p.w, offset); }
vec4 textureProjOffset(CombinedImageSampler2D c, vec4 p, ivec2 offset, float bias) { return textureOffset(c, p.xy / p.w, offset, bias); }
vec4 textureProjOffset(CombinedImageSampler3D c, vec4 p, ivec3 offset) { return textureOffset(c, p.xyz / p.w, offset); }
vec4 textureProjOffset(CombinedImageSampler3D c, vec4 p, ivec3 offset, float bias) { return textureOffset(c, p.xyz / p.w, offset, bias); }

vec4 textureLodOffset(CombinedImageSampler2D c, vec2 p, float lod, ivec2 offset) { return textureLod(c, p + _ds_texel_offset(c, offset, int(lod)), lod); }
vec4 textureLodOffset(CombinedImageSampler3D c, vec3 p, float lod, ivec3 offset) { return textureLod(c, p + _ds_texel_offset(c, offset, int(lod)), lod); }

vec4 textureProjLodOffset(CombinedImageSampler2D c, vec3 p, float lod, ivec2 offset) { return textureLodOffset(c, p.xy / p.z, lod, offset); }
vec4 textureProjLodOffset(CombinedImageSampler2D c, vec4 p, float lod, ivec2 offset) { return textureLodOffset(c, p.xy / p.w, lod, offset); }
vec4 textureProjLodOffset(CombinedImageSampler3D c, vec4 p, float lod, ivec3 offset) { return textureLodOffset(c, p.xyz / p.w, lod, offset); }

vec4 textureGradOffset(CombinedImageSampler2D c, vec2 p, vec2 dTdx, vec2 dTdy, ivec2 offset) { return textureGrad(c, p + _ds_texel_offset(c, offset, 0), dTdx, dTdy); }
vec4 textureGradOffset(CombinedImageSampler3D c, vec3 p, vec3 dTdx, vec3 dTdy, ivec3 offset) { return textureGrad(c, p + _ds_texel_offset(c, offset, 0), dTdx, dTdy); }

vec4 textureProjGradOffset(CombinedImageSampler2D c, vec3 p, vec2 dTdx, vec2 dTdy, ivec2 offset) { return textureGradOffset(c, p.xy / p.z, dTdx, dTdy, offset); }
vec4 textureProjGradOffset(CombinedImageSampler2D c, vec4 p, vec2 dTdx, vec2 dTdy, ivec2 offset) { return textureGradOffset(c, p.xy / p.w, dTdx, dTdy, offset); }
vec4 textureProjGradOffset(CombinedImageSampler3D c, vec4 p, vec3 dTdx, vec3 dTdy, ivec3 offset) { return textureGradOffset(c, p.xyz / p.w, dTdx, dTdy, offset); }

vec4 texelFetchOffset(CombinedImageSampler2D c, ivec2 p, int lod, ivec2 offset) { return texelFetch(c, p + offset, lod); }
vec4 texelFetchOffset(CombinedImageSampler3D c, ivec3 p, int lod, ivec3 offset) { return texelFetch(c, p + offset, lod); }

// Not GLSL ES 3.00, but used by some shaders. The component must be a constant for the native function.
vec4 textureGather(CombinedImageSampler2D c, vec2 p, int comp) {
    switch (comp) {
    case 1: return textureGather(daxa_sampler2D(c.image_view_id, c.sampler_id), p, 1);
    case 2: return textureGather(daxa_sampler2D(c.image_view_id, c.sampler_id), p, 2);
    case 3: return textureGather(daxa_sampler2D(c.image_view_id, c.sampler_id), p, 3);
    default: return textureGather(daxa_sampler2D(c.image_view_id, c.sampler_id), p, 0);
    }
}
vec4 textureGather(CombinedImageSampler2D c, vec2 p) { return textureGather(c, p, 0); }

layout(location = 0) out vec4 _daxa_color_output;

// The user's mainImage/mainCubemap are called through these wrappers, which are defined after the user code.
// This way mainImage may also be a macro (a common code golfing trick), and its parameter qualifiers
// (e.g. "inout", "highp") don't have to match a forward declaration.
void _ds_call_mainImage(inout vec4 fragColor, vec2 fragCoord);
void _ds_call_mainCubemap(inout vec4 fragColor, vec2 fragCoord, vec3 rayOri, vec3 rayDir);

int _daxa_st_assert_index = -1;

void st_assert(bool condition, int index) {
    if (condition) {
        _daxa_st_assert_index = clamp(index, _daxa_st_assert_index, 3);
    }
}

void st_assert(bool condition) { st_assert(condition, 0); }

void main() {
    // clang-format off
    iResolution           = deref(daxa_push_constant.gpu_input).Resolution;
    iTime                 = deref(daxa_push_constant.gpu_input).Time;
    iTimeDelta            = deref(daxa_push_constant.gpu_input).TimeDelta;
    iFrameRate            = deref(daxa_push_constant.gpu_input).FrameRate;
    iFrame                = deref(daxa_push_constant.gpu_input).Frame;
    iChannelTime[0]       = deref(daxa_push_constant.gpu_input).ChannelTime[0];
    iChannelTime[1]       = deref(daxa_push_constant.gpu_input).ChannelTime[1];
    iChannelTime[2]       = deref(daxa_push_constant.gpu_input).ChannelTime[2];
    iChannelTime[3]       = deref(daxa_push_constant.gpu_input).ChannelTime[3];
    iChannelResolution[0] = deref(daxa_push_constant.gpu_input).ChannelResolution[0];
    iChannelResolution[1] = deref(daxa_push_constant.gpu_input).ChannelResolution[1];
    iChannelResolution[2] = deref(daxa_push_constant.gpu_input).ChannelResolution[2];
    iChannelResolution[3] = deref(daxa_push_constant.gpu_input).ChannelResolution[3];
    iMouse                = deref(daxa_push_constant.gpu_input).Mouse;
    iDate                 = deref(daxa_push_constant.gpu_input).Date;
    iSampleRate           = deref(daxa_push_constant.gpu_input).SampleRate;
    // clang-format on
#if _DS_CUBEMAP
    iResolution = vec3(1024, 1024, 1);
#endif

    vec4 frag_color = vec4(0);

#if _DS_CUBEMAP
    vec2 fragCoord = vec2(gl_FragCoord.xy);
    vec2 uv = fragCoord / 1024.0 * 2.0 - 1.0;

    vec3 ray_dir;
    switch (daxa_push_constant.face_index) {
    case 0: ray_dir = vec3(+1.0, -uv.y, -uv.x); break; // right
    case 1: ray_dir = vec3(-1.0, -uv.y, +uv.x); break; // left
    case 2: ray_dir = vec3(+uv.x, +1.0, +uv.y); break; // up
    case 3: ray_dir = vec3(+uv.x, -1.0, -uv.y); break; // down
    case 4: ray_dir = vec3(+uv.x, -uv.y, +1.0); break; // front
    case 5: ray_dir = vec3(-uv.x, -uv.y, -1.0); break; // back
    }
    ray_dir = normalize(ray_dir);

    _ds_call_mainCubemap(frag_color, fragCoord, vec3(0, 0, 0), ray_dir);
#else
    vec2 frame_dim = iResolution.xy;
    vec2 fragCoord = vec2(gl_FragCoord.xy);
    _ds_call_mainImage(frag_color, fragCoord);

    if (_daxa_st_assert_index != -1) {
        switch (_daxa_st_assert_index) {
        case 0: frag_color = vec4(1, 0, 0, 1); break;
        case 1: frag_color = vec4(0, 1, 0, 1); break;
        case 2: frag_color = vec4(0, 0, 1, 1); break;
        case 3: frag_color = vec4(1, 1, 0, 1); break;
        }
    }
#endif

    _daxa_color_output = frag_color;
}

// The common code is included by user_code, after the pass inputs (iChannel0-3).
#include <user_code>

#if _DS_CUBEMAP
void _ds_call_mainCubemap(inout vec4 _ds_fragColor, vec2 _ds_fragCoord, vec3 _ds_rayOri, vec3 _ds_rayDir) {
    mainCubemap(_ds_fragColor, _ds_fragCoord, _ds_rayOri, _ds_rayDir);
}
#else
void _ds_call_mainImage(inout vec4 _ds_fragColor, vec2 _ds_fragCoord) {
#if defined(color)
    mainImage(_ds_fragColor, _ds_fragCoord);
#else
    // Named like the variable in shadertoy's main(), as some mainImage macros refer to it.
    vec4 color = _ds_fragColor;
    mainImage(color, _ds_fragCoord);
    _ds_fragColor = color;
#endif
}
#endif

#endif
