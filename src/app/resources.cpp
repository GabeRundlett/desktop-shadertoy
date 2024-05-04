#include <app/resources.hpp>

#if defined(__linux__)
#include <linux/limits.h>
#include <unistd.h>
#include <array>
#include <filesystem>
#include <iostream>
#endif

inline auto get_resource_dir() noexcept -> std::string {
#if LINUX_BUILD_APPIMAGE
    // 'APPDIR' environment variable should be set when running in AppImage under unix systems
    const char* app_dir = getenv("APPDIR");
    if (app_dir == nullptr) {
        // We are not running in AppImage, use the executable path
        std::array<char, PATH_MAX> exe_loc{};
        if (readlink("/proc/self/exe", exe_loc.data(), PATH_MAX) == -1) { return ""; }
        std::filesystem::path exe_path(exe_loc.data());
        auto working_path = exe_path.parent_path() / "";

        std::cout << "Using appimage dir " << working_path << std::endl;
        return working_path.string();
    }
    std::cout << "Using appimage dir " << app_dir << std::endl;
    return std::string(app_dir).append("/usr/share/desktop-shadertoy/");
#else
    return "";
#endif
}

const std::string resource_dir = get_resource_dir();
