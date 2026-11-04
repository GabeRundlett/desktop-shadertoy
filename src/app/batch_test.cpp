// Headless batch tester, used by tools/test_shaders.py.
//
//   desktop-shadertoy --batch-test <list.txt> --out <results.jsonl> [--frames N] [--width W] [--height H]
//
// Compiles (and with --frames > 0 also renders) every shadertoy json listed in <list.txt>, one path per line.
// Results are appended to <results.jsonl>, one json object per line. Before a shader is processed, a
// {"event": "start"} line is written and flushed, so that if the process crashes (e.g. device lost) the
// driver script can attribute the crash to that shader and restart after it.

#include <app/viewport.hpp>

#include <daxa/daxa.hpp>
#include <daxa/utils/task_graph.hpp>
#include <nlohmann/json.hpp>

#include <chrono>
#include <fstream>
#include <iostream>
#include <span>
#include <string>
#include <string_view>

namespace {
    struct BatchArgs {
        std::string list_path;
        std::string out_path;
        uint32_t frames = 0;
        uint32_t width = 800;
        uint32_t height = 450;
    };

    auto parse_args(std::span<char const *const> args) -> std::optional<BatchArgs> {
        auto result = BatchArgs{};
        if (args.empty()) {
            return std::nullopt;
        }
        result.list_path = args[0];
        for (size_t i = 1; i + 1 < args.size(); i += 2) {
            auto const key = std::string_view{args[i]};
            auto const value = std::string{args[i + 1]};
            if (key == "--out") {
                result.out_path = value;
            } else if (key == "--frames") {
                result.frames = static_cast<uint32_t>(std::stoul(value));
            } else if (key == "--width") {
                result.width = static_cast<uint32_t>(std::stoul(value));
            } else if (key == "--height") {
                result.height = static_cast<uint32_t>(std::stoul(value));
            } else {
                return std::nullopt;
            }
        }
        if (result.out_path.empty()) {
            return std::nullopt;
        }
        return result;
    }

    void render_frames(daxa::Device &device, Viewport &viewport, BatchArgs const &args) {
        auto const w = static_cast<daxa_f32>(args.width);
        auto const h = static_cast<daxa_f32>(args.height);
        viewport.gpu_input.Resolution = daxa_f32vec3{w, h, 1.0f};
        viewport.gpu_input.ChannelResolution[0] = daxa_f32vec3{w, h, 1.0f};

        auto task_graph = daxa::TaskGraph({.device = device, .name = "batch_tg"});
        viewport.record(task_graph);
        task_graph.submit({});
        task_graph.complete({});

        for (uint32_t frame = 0; frame < args.frames; ++frame) {
            viewport.update();
            viewport.render();
            task_graph.execute({});
            ++viewport.gpu_input.Frame;
            // Waiting each frame keeps the amount of queued GPU work (and thus a potential hang) small,
            // and attributes a device loss to the right frame.
            device.wait_idle();
            device.collect_garbage();
        }
    }
} // namespace

auto run_batch_test(std::span<char const *const> raw_args) -> int {
    auto const args_opt = parse_args(raw_args);
    if (!args_opt) {
        std::cerr << "usage: desktop-shadertoy --batch-test <list.txt> --out <results.jsonl> [--frames N] [--width W] [--height H]\n";
        return 2;
    }
    auto const &args = *args_opt;

    auto instance = daxa::create_instance({});
    auto device = [&]() {
        auto device_info = daxa::DeviceInfo2{};
        device_info.name = "Desktop Shadertoy batch test";
        device_info.explicit_features = daxa::ExplicitFeatureFlagBits::ROBUSTNESS_2;
        device_info = instance.choose_device({}, device_info);
        return instance.create_device_2(device_info);
    }();

    auto errors = std::string{};
    core::error_sink = &errors;

    auto out = std::ofstream(args.out_path, std::ios::app);
    auto list = std::ifstream(args.list_path);
    {
        auto viewport = Viewport(device);
        auto path = std::string{};
        while (std::getline(list, path)) {
            if (!path.empty() && path.back() == '\r') {
                path.pop_back();
            }
            if (path.empty()) {
                continue;
            }
            out << nlohmann::json{{"file", path}, {"event", "start"}}.dump() << std::endl;

            errors.clear();
            auto result = nlohmann::json{{"file", path}, {"event", "result"}};
            auto const t0 = std::chrono::steady_clock::now();
            try {
                auto json = nlohmann::json::parse(std::ifstream(path));
                if (json.contains("Shader")) {
                    json = json["Shader"];
                }
                viewport.load_shadertoy_json(json);
                auto const t1 = std::chrono::steady_clock::now();
                result["compile_seconds"] = std::chrono::duration<double>(t1 - t0).count();
                if (viewport.load_failed) {
                    result["status"] = "compile_error";
                } else if (args.frames > 0) {
                    render_frames(device, viewport, args);
                    result["render_seconds"] = std::chrono::duration<double>(std::chrono::steady_clock::now() - t1).count();
                    result["status"] = "ok";
                } else {
                    result["status"] = "ok";
                }
            } catch (std::exception const &e) {
                result["status"] = "exception";
                errors += e.what();
            } catch (...) {
                result["status"] = "exception";
                errors += "unknown exception";
            }
            if (!errors.empty()) {
                result["errors"] = errors;
            }
            out << result.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace) << std::endl;
        }
    }

    core::error_sink = nullptr;
    device.wait_idle();
    device.collect_garbage();
    return 0;
}
