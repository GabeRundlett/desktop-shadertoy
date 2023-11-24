#pragma once

#include <app/core.inl>

struct GpuInput {
    daxa_f32vec3 Resolution;           // viewport resolution (in pixels)
    daxa_f32 Time;                     // shader playback time (in seconds)
    daxa_f32 TimeDelta;                // render time (in seconds)
    daxa_f32 FrameRate;                // shader frame rate
    daxa_i32 Frame;                    // shader playback frame
    daxa_f32 ChannelTime[4];           // channel playback time (in seconds)
    daxa_f32vec3 ChannelResolution[4]; // channel resolution (in pixels)
    daxa_f32vec4 Mouse;                // mouse pixel coords. xy: current (if MLB down), zw: click
    daxa_f32vec4 Date;                 // (year, month, day, time in seconds)
    daxa_f32 SampleRate;               // sound sample rate (i.e., 44100)
};
DAXA_DECL_BUFFER_PTR(GpuInput)

struct CombinedImageSampler2D {
    daxa_ImageViewId image_view_id;
    daxa_SamplerId sampler_id;
    daxa_u32 convert_int_to_float;
};

struct CombinedImageSampler2D_int {
    daxa_ImageViewId image_view_id;
    daxa_SamplerId sampler_id;
};

struct CombinedImageSamplerCube {
    daxa_ImageViewId image_view_id;
    daxa_SamplerId sampler_id;
};

struct CombinedImageSampler3D {
    daxa_ImageViewId image_view_id;
    daxa_SamplerId sampler_id;
};

struct InputImages {
    daxa_ImageViewId Channel[4];
    daxa_SamplerId Channel_sampler[4];
};

struct ShaderToyPush {
    daxa_BufferPtr(GpuInput) gpu_input;
    InputImages input_images;
    daxa_u32 face_index;
};

#if DAXA_SHADER
DAXA_DECL_PUSH_CONSTANT(ShaderToyPush, daxa_push_constant)
#else
static_assert(sizeof(ShaderToyPush) <= 128);
using BDA = daxa::BufferDeviceAddress;
#endif
