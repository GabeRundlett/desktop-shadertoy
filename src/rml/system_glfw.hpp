#pragma once

#include <RmlUi/Core/Input.h>
#include <RmlUi/Core/SystemInterface.h>
#include <RmlUi/Core/Types.h>
#include <GLFW/glfw3.h>

class SystemInterface_GLFW : public Rml::SystemInterface {
  public:
    SystemInterface_GLFW();
    ~SystemInterface_GLFW() override;
    SystemInterface_GLFW(const SystemInterface_GLFW &) = delete;
    SystemInterface_GLFW(SystemInterface_GLFW &&) = delete;
    auto operator=(const SystemInterface_GLFW &) -> SystemInterface_GLFW & = delete;
    auto operator=(SystemInterface_GLFW &&) -> SystemInterface_GLFW & = delete;

    // Optionally, provide or change the window to be used for setting the mouse cursors and clipboard text.
    void SetWindow(GLFWwindow *window);

    // -- Inherited from Rml::SystemInterface  --

    auto GetElapsedTime() -> double override;

    void SetMouseCursor(const Rml::String &cursor_name) override;

    void SetClipboardText(const Rml::String &text) override;
    void GetClipboardText(Rml::String &text) override;

  private:
    GLFWwindow *window = nullptr;

    GLFWcursor *cursor_pointer = nullptr;
    GLFWcursor *cursor_cross = nullptr;
    GLFWcursor *cursor_text = nullptr;
};

/**
    Optional helper functions for the GLFW plaform.
 */
namespace RmlGLFW {
    // The following optional functions are intended to be called from their respective GLFW callback functions. The functions expect arguments passed
    // directly from GLFW, in addition to the RmlUi context to apply the input or sizing event on. The input callbacks return true if the event is
    // propagating, i.e. was not handled by the context.
    auto ProcessKeyCallback(Rml::Context *context, int key, int action, int mods) -> bool;
    auto ProcessCharCallback(Rml::Context *context, unsigned int codepoint) -> bool;
    auto ProcessCursorEnterCallback(Rml::Context *context, int entered) -> bool;
    auto ProcessCursorPosCallback(Rml::Context *context, GLFWwindow *window, double xpos, double ypos, int mods) -> bool;
    auto ProcessMouseButtonCallback(Rml::Context *context, int button, int action, int mods) -> bool;
    auto ProcessScrollCallback(Rml::Context *context, double yoffset, int mods) -> bool;
    void ProcessFramebufferSizeCallback(Rml::Context *context, int width, int height);
    void ProcessContentScaleCallback(Rml::Context *context, float xscale);

    // Converts the GLFW key to RmlUi key.
    auto ConvertKey(int glfw_key) -> Rml::Input::KeyIdentifier;

    // Converts the GLFW key modifiers to RmlUi key modifiers.
    auto ConvertKeyModifiers(int glfw_mods) -> int;
} // namespace RmlGLFW
