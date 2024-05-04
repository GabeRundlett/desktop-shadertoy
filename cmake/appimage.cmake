##############################################################################################
## This CMake file provides automated packaging for AppImage                                ##
##                                                                                          ##
## To build the AppImage please follow these steps:                                         ##
## - Reconfigure the CMake with ENABLE_BUILD_APPIMAGE=True in the CMakeCache                ##
##   or by passing -DENABLE_BUILD_APPIMAGE=True                                             ##
## - Build desktop shadertoy as usual                                                       ##
## - Run the following command:                                                             ##
##   'cmake --install . --config [Debug/Release] --component desktop-shadertoy-appimage',   ##
##   where [Debug/Release] is the configuration you built desktop shadertoy with.           ##
## - After the command completes, your AppImage package should be located in                ##
##   '[your-binary-dir]/appimage/desktop-shadertoy-[version]-[arch].appimage'               ##
##############################################################################################

target_compile_definitions(${PROJECT_NAME} PRIVATE LINUX_BUILD_APPIMAGE=1)

# Locate or download appiamgetool
set(APPIMAGETOOL_BINARY_PATH "${CMAKE_BINARY_DIR}/appimage/appimagetool-x86_64.AppImage" CACHE FILEPATH
        "Path for appimagetool. If notset, appimagetool will be acquired from github")
if (NOT EXISTS ${APPIMAGETOOL_BINARY_PATH})
message(STATUS "Could not locate appimagetool for packaging, downloading it now")
file(DOWNLOAD https://github.com/AppImage/AppImageKit/releases/download/continuous/appimagetool-x86_64.AppImage
                ${APPIMAGETOOL_BINARY_PATH})
execute_process(COMMAND chmod +x ${APPIMAGETOOL_BINARY_PATH})
message(STATUS "Download for appimagetool complete")
endif ()

set(COMPONENT_APPIMAGE "${PROJECT_NAME}-appimage")

# Install symlinks
install(DIRECTORY "${CMAKE_SOURCE_DIR}/cmake/appimage/AppDir"
        DESTINATION "${CMAKE_BINARY_DIR}/appimage"
        COMPONENT ${COMPONENT_APPIMAGE}
)

# Install binary
set(APPDIR_PATH "${CMAKE_BINARY_DIR}/appimage/AppDir")
install(TARGETS ${PROJECT_NAME} RUNTIME
        DESTINATION "${APPDIR_PATH}/usr/bin"
        COMPONENT ${COMPONENT_APPIMAGE}
)

# Install icon
install(FILES "${CMAKE_SOURCE_DIR}/appicon.png"
        DESTINATION "${APPDIR_PATH}/usr/share/icons/hicolor/128x128/apps"
        COMPONENT ${COMPONENT_APPIMAGE}
        RENAME "desktop-shadertoy.png"
)

# Install desktop file
install(FILES "${CMAKE_SOURCE_DIR}/cmake/appimage/desktop-shadertoy.desktop"
        DESTINATION "${APPDIR_PATH}/usr/share/applications"
        COMPONENT ${COMPONENT_APPIMAGE}
)

# Install app data
set(APPDIR_SHARE_PATH "${APPDIR_PATH}/usr/share/desktop-shadertoy")
install(DIRECTORY "${CMAKE_SOURCE_DIR}/media"
        DESTINATION ${APPDIR_SHARE_PATH}
        COMPONENT ${COMPONENT_APPIMAGE}
        PATTERN ".git" EXCLUDE
)
install(
        DIRECTORY "${CMAKE_SOURCE_DIR}/src"
        DESTINATION ${APPDIR_SHARE_PATH}
        COMPONENT ${COMPONENT_APPIMAGE}
        FILES_MATCHING
        PATTERN "*.glsl"
        PATTERN "*.inl"
        PATTERN "*.rcss"
        PATTERN "*.rml"
)
install(FILES "${daxa_DIR}/../../include/daxa/daxa.inl" "${daxa_DIR}/../../include/daxa/daxa.glsl"
        DESTINATION "${APPDIR_SHARE_PATH}/src/daxa"
        COMPONENT ${COMPONENT_APPIMAGE}
)
install(FILES "${daxa_DIR}/../../include/daxa/utils/task_graph.inl"
        DESTINATION "${APPDIR_SHARE_PATH}/src/daxa/utils"
        COMPONENT ${COMPONENT_APPIMAGE}
)
install(FILES "${CMAKE_SOURCE_DIR}/appicon.png" "${CMAKE_SOURCE_DIR}/default-shader.json"
        DESTINATION ${APPDIR_SHARE_PATH}
        COMPONENT ${COMPONENT_APPIMAGE}
)

# Copy dependency libraries into AppDir
install(CODE "set(target \"$<TARGET_FILE:${PROJECT_NAME}>\")" COMPONENT ${COMPONENT_APPIMAGE})
install(CODE "set(lib_dest ${APPDIR_PATH}/usr/lib)" COMPONENT ${COMPONENT_APPIMAGE})
install(CODE [[
message(STATUS "Figuring out deps to pack into appimage")
message(STATUS "This might take a while since 'GET_RUNTIME_DEPENDENCIES' is wasen't fixed")
file(GET_RUNTIME_DEPENDENCIES
EXECUTABLES ${target}
RESOLVED_DEPENDENCIES_VAR r_deps
)
foreach(dep ${r_deps})
        message(STATUS "Found dependency: ${dep}")
endforeach()
file(INSTALL ${r_deps} DESTINATION ${lib_dest})
]] COMPONENT ${COMPONENT_APPIMAGE})

# Build appimage using appimagetool
set(APPIMAGE_NAME ${PROJECT_NAME}-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_HOST_SYSTEM_PROCESSOR}.appimage)
install(CODE "execute_process(COMMAND ${APPIMAGETOOL_BINARY_PATH} ${APPDIR_PATH} ${CMAKE_BINARY_DIR}/appimage/${APPIMAGE_NAME})"
        COMPONENT ${COMPONENT_APPIMAGE})
