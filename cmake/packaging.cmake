set(APPDIR_PATH "${CMAKE_BINARY_DIR}/appimage/AppDir")

install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION ".")
install(DIRECTORY "${CMAKE_SOURCE_DIR}/media" DESTINATION "." PATTERN ".git" EXCLUDE)
install(
    DIRECTORY "${CMAKE_SOURCE_DIR}/src"
    DESTINATION "."
    FILES_MATCHING
    PATTERN "*.glsl"
    PATTERN "*.inl"
    PATTERN "*.rcss"
    PATTERN "*.rml"
)
install(FILES "${CMAKE_SOURCE_DIR}/deps/Daxa/include/daxa/daxa.inl" "${CMAKE_SOURCE_DIR}/deps/Daxa/include/daxa/daxa.glsl" DESTINATION "src/daxa")
install(FILES "${CMAKE_SOURCE_DIR}/deps/Daxa/include/daxa/utils/task_graph.inl" DESTINATION "src/daxa/utils")
install(FILES "${CMAKE_SOURCE_DIR}/appicon.png" "${CMAKE_SOURCE_DIR}/default-shader.json" DESTINATION ".")
install(FILES $<TARGET_RUNTIME_DLLS:${PROJECT_NAME}> DESTINATION ".")

install(
    DIRECTORY "${_VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}$<$<CONFIG:Debug>:/debug>/bin/"
    DESTINATION "."
    FILES_MATCHING
    PATTERN "*.dll"
)

set(CPACK_PACKAGE_NAME "DesktopShadertoy")
set(CPACK_PACKAGE_VENDOR "Gabe-Rundlett")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Desktop Shadertoy is a desktop client for the web app 'Shadertoy'")
set(CPACK_PACKAGE_DESCRIPTION "Desktop Shadertoy was made for allowing people to use tools like Renderdoc and NSight with existing Shadertoy projects.")
set(CPACK_PACKAGE_ICON "${CMAKE_SOURCE_DIR}/appicon.png")

set(CPACK_GENERATOR ZIP)

include(InstallRequiredSystemLibraries)
include(CPack)

set(ENABLE_BUILD_APPIMAGE OFF CACHE BOOL "Enable building appimage package")
if(UNIX AND NOT APPLE AND ${ENABLE_BUILD_APPIMAGE})
    include("${CMAKE_SOURCE_DIR}/cmake/appimage.cmake")
endif()
