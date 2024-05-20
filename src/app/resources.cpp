#include <app/resources.hpp>

#if defined(__linux__)
#include <linux/limits.h>
#include <unistd.h>
#elif defined(_WIN32)
#include <windows.h>
#endif

#include <filesystem>
#include <array>
#include <iostream>
#include <span>
#include <optional>

auto search_for_path_to_fix_working_directory(std::filesystem::path current_path, std::span<std::filesystem::path const> test_paths) -> std::optional<std::filesystem::path> {
    while (true) {
        for (auto const &test_path : test_paths) {
            if (std::filesystem::exists(current_path / test_path)) {
                return current_path;
            }
        }
        if (!current_path.has_parent_path()) {
            break;
        }
        current_path = current_path.parent_path();
    }

    return std::nullopt;
}

inline auto get_resource_dir() noexcept -> std::filesystem::path {
    auto result = std::filesystem::current_path();
#if __linux__
    // 'APPDIR' environment variable should be set when running in AppImage under unix systems
    const char *app_dir = getenv("APPDIR");
    if (app_dir == nullptr) {
        // We are not running in AppImage, use the executable path
        auto exe_loc = std::array<char, PATH_MAX>{};
        if (readlink("/proc/self/exe", exe_loc.data(), PATH_MAX) != -1) {
            auto exe_path = std::filesystem::path(exe_loc.data());
            if (exe_path.has_parent_path()) {
                result = exe_path.parent_path();
            }
        }
    } else {

        result = app_dir / std::filesystem::path("usr/share/desktop-shadertoy");
    }
#elif defined(_WIN32)
    auto exe_loc = std::array<char, 512>{};
    GetModuleFileNameA(nullptr, exe_loc.data(), 512);
    auto exe_path = std::filesystem::path(exe_loc.data());
    if (exe_path.has_parent_path()) {
        result = exe_path.parent_path();
    }
#endif

    // Find the media directory
    auto media_dir = search_for_path_to_fix_working_directory(result, std::array{std::filesystem::path{"media"}});
    if (media_dir) {
        result = *media_dir;
    } else {
        std::cerr << "Failed to find media directory. This should never happen, contact Gabe Rundlett" << std::endl;
    }

    return result;
}

const std::filesystem::path resource_dir = get_resource_dir();
