#include <main.inl>

#if DAXA_SHADER_STAGE == DAXA_SHADER_STAGE_VERTEX

void main() {
    switch (gl_VertexIndex) {
    case 0: gl_Position = vec4(-1, -1, 0, 1); break;
    case 1: gl_Position = vec4(+3, -1, 0, 1); break;
    case 2: gl_Position = vec4(-1, +3, 0, 1); break;
    }
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

#include <common>

#include <user_code>

layout(location = 0) out vec4 _daxa_color_output;

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
#if CUBEMAP
    iResolution = vec3(1024, 1024, 1);
#endif

    vec4 frag_color = vec4(0);

#if CUBEMAP
    vec2 pixel_i = vec2(gl_FragCoord.xy);
    vec2 uv = pixel_i / 1024.0 * 2.0 - 1.0;

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

    mainCubemap(frag_color, pixel_i, vec3(0, 0, 0), ray_dir);
#else
    vec2 frame_dim = iResolution.xy;
    vec2 pixel_i = vec2(gl_FragCoord.xy);
#if MAIN_IMAGE
    vec2 fragCoord = vec2(pixel_i.x, frame_dim.y - pixel_i.y);
#else
    vec2 fragCoord = pixel_i;
#endif
    mainImage(frag_color, fragCoord);
#endif

    _daxa_color_output = frag_color;
}

#endif
