#pragma once

#include <RmlUi/Core/RenderInterface.h>
#include <RmlUi/Core/Types.h>
#include <daxa/utils/pipeline_manager.hpp>
#include <daxa/command_recorder.hpp>
#include <daxa/daxa.hpp>

class RenderInterface_Daxa : public Rml::RenderInterface {
  public:
    explicit RenderInterface_Daxa(daxa::Device device, daxa::Format format);
    ~RenderInterface_Daxa() override;
    RenderInterface_Daxa(const RenderInterface_Daxa &) = delete;
    RenderInterface_Daxa(RenderInterface_Daxa &&) = delete;
    auto operator=(const RenderInterface_Daxa &) -> RenderInterface_Daxa & = delete;
    auto operator=(RenderInterface_Daxa &&) -> RenderInterface_Daxa & = delete;

    // Sets up OpenGL states for taking rendering commands from RmlUi.
    void begin_frame(daxa::ImageId target_image, daxa::CommandRecorder &recorder);
    void end_frame(daxa::ImageId target_image, daxa::CommandRecorder &recorder);

    // -- Inherited from Rml::RenderInterface --
    void RenderGeometry(Rml::Vertex *vertices, int num_vertices, int *indices, int num_indices, Rml::TextureHandle texture, const Rml::Vector2f &translation) override;
    auto CompileGeometry(Rml::Vertex *vertices, int num_vertices, int *indices, int num_indices, const Rml::TextureHandle texture) -> Rml::CompiledGeometryHandle override;
    void RenderCompiledGeometry(Rml::CompiledGeometryHandle handle, const Rml::Vector2f &translation) override;
    void ReleaseCompiledGeometry(Rml::CompiledGeometryHandle handle) override;
    void EnableScissorRegion(bool enable) override;
    void SetScissorRegion(int x, int y, int width, int height) override;
    auto LoadTexture(Rml::TextureHandle &texture_handle, Rml::Vector2i &texture_dimensions, const Rml::String &source) -> bool override;
    auto GenerateTexture(Rml::TextureHandle &texture_handle, const Rml::byte *source, const Rml::Vector2i &source_dimensions) -> bool override;
    void ReleaseTexture(Rml::TextureHandle texture_handle) override;
    void SetTransform(const Rml::Matrix4f *transform) override;

    daxa::Device device;

  private:
    struct Draw {
        std::vector<Rml::Vertex> vertices{};
        std::vector<int> indices{};
        uint32_t vertex_offset{};
        uint32_t index_offset{};
        Rml::TextureHandle texture{};
        daxa::ImageId actual_image{};
        Rml::Vector2f translation{};
        Rml::Matrix4f transform{};
        std::optional<daxa::Rect2D> scissor{};
    };
    struct ImageUpload {
        daxa::ImageId image_id{};
        size_t data{};
        Rml::Vector2i size{};
    };

    std::vector<Rml::CompiledGeometryHandle> draw_order{};
    std::vector<Rml::CompiledGeometryHandle> deferred_draw_releases{};
    std::vector<Draw> draws{};
    size_t vertex_cache_offset{};
    size_t index_cache_offset{};
    std::vector<Rml::Vertex> vertex_cache{};
    std::vector<int> index_cache{};
    std::vector<ImageUpload> image_uploads{};
    std::vector<Rml::byte> image_upload_data{};
    std::stack<size_t> draw_free_list{};

    daxa::PipelineManager pipeline_manager{};
    std::shared_ptr<daxa::RasterPipeline> raster_pipeline{};
    daxa::BufferId vbuffer{};
    daxa::BufferId ibuffer{};
    daxa::ImageId default_texture{};
    daxa::SamplerId default_sampler{};
    Rml::TextureHandle bound_texture{};
    daxa::Rect2D current_scissor{};
    bool scissor_enabled{};

    Rml::Matrix4f projection{};
    Rml::Matrix4f transform{};
    bool transform_enabled = false;

    void recreate_vbuffer(size_t vbuffer_new_size);
    void recreate_ibuffer(size_t ibuffer_new_size);
};
