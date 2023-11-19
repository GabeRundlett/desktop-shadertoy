#pragma once

#include <daxa/daxa.hpp>
#include <daxa/utils/task_graph.hpp>

struct PingPongImage_impl {
    using ResourceType = daxa::ImageId;
    using ResourceInfoType = daxa::ImageInfo;
    using TaskResourceType = daxa::TaskImage;
    using TaskResourceInfoType = daxa::TaskImageInfo;

    static auto create(daxa::Device &device, ResourceInfoType const &info) -> ResourceType {
        return device.create_image(info);
    }
    static void destroy(daxa::Device &device, ResourceType rsrc_id) {
        device.destroy_image(rsrc_id);
    }
    static auto create_task_resource(ResourceType rsrc_id, std::string const &name) -> TaskResourceType {
        return TaskResourceType(TaskResourceInfoType{.initial_images = {std::array{rsrc_id}}, .name = name});
    }
};

struct PingPongBuffer_impl {
    using ResourceType = daxa::BufferId;
    using ResourceInfoType = daxa::BufferInfo;
    using TaskResourceType = daxa::TaskBuffer;
    using TaskResourceInfoType = daxa::TaskBufferInfo;

    static auto create(daxa::Device &device, ResourceInfoType const &info) -> ResourceType {
        return device.create_buffer(info);
    }
    static void destroy(daxa::Device &device, ResourceType rsrc_id) {
        device.destroy_buffer(rsrc_id);
    }
    static auto create_task_resource(ResourceType rsrc_id, std::string const &name) -> TaskResourceType {
        return TaskResourceType(TaskResourceInfoType{.initial_buffers = {std::array{rsrc_id}}, .name = name});
    }
};

template <typename Impl>
struct PingPongResource {
    using ResourceType = typename Impl::ResourceType;
    using ResourceInfoType = typename Impl::ResourceInfoType;
    using TaskResourceType = typename Impl::TaskResourceType;
    using TaskResourceInfoType = typename Impl::TaskResourceInfoType;

    struct Resources {
        Resources() = default;
        Resources(Resources const &) = delete;
        Resources(Resources &&other) noexcept {
            *this = std::move(other);
        }
        auto operator=(Resources const &) -> Resources & = delete;
        auto operator=(Resources &&other) noexcept -> Resources & {
            std::swap(this->device, other.device);
            std::swap(this->resource_a, other.resource_a);
            std::swap(this->resource_b, other.resource_b);
            return *this;
        }

        daxa::Device device{};
        ResourceType resource_a{};
        ResourceType resource_b{};

        ~Resources() {
            if (!resource_a.is_empty()) {
                Impl::destroy(device, resource_a);
                Impl::destroy(device, resource_b);
            }
        }
    };
    struct TaskResources {
        TaskResourceType output_resource;
        TaskResourceType history_resource;
    };
    Resources resources;
    TaskResources task_resources;

    auto get(daxa::Device a_device, ResourceInfoType const &a_info) -> std::pair<TaskResourceType &, TaskResourceType &> {
        if (!resources.device.is_valid()) {
            resources.device = a_device;
        }
        // Can't compare managed_ptr
        // assert(resources.device == a_device);
        if (resources.resource_a.is_empty()) {
            auto info_a = a_info;
            auto info_b = a_info;
            info_a.name = std::string(info_a.name.view()) + "_a";
            info_b.name = std::string(info_b.name.view()) + "_b";
            resources.resource_a = Impl::create(a_device, info_a);
            resources.resource_b = Impl::create(a_device, info_b);
            task_resources.output_resource = Impl::create_task_resource(resources.resource_a, std::string(a_info.name.view()));
            task_resources.history_resource = Impl::create_task_resource(resources.resource_b, std::string(a_info.name.view()) + "_hist");
        }
        return {task_resources.output_resource, task_resources.history_resource};
    }

    void swap() {
        task_resources.output_resource.swap_images(task_resources.history_resource);
    }
};

using PingPongImage = PingPongResource<PingPongImage_impl>;
using PingPongBuffer = PingPongResource<PingPongBuffer_impl>;