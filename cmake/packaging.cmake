install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION ".")
install(DIRECTORY "${CMAKE_SOURCE_DIR}/media" DESTINATION ".")
install(
    DIRECTORY "${CMAKE_SOURCE_DIR}/src"
    DESTINATION "."
    FILES_MATCHING
    PATTERN "*.glsl"
    PATTERN "*.inl"
    PATTERN "*.rcss"
    PATTERN "*.rml"
)
install(FILES "${daxa_DIR}/../../include/daxa/daxa.inl" "${daxa_DIR}/../../include/daxa/daxa.glsl" DESTINATION "src/daxa")
install(FILES "${daxa_DIR}/../../include/daxa/utils/task_graph.inl" DESTINATION "src/daxa/utils")
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
