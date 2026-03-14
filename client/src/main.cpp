// clang-format off
#ifdef _WIN32
#include <windows.h>
#endif

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include "app.hpp"
#include "config.hpp"
#include "log.hpp"
#include "ui.hpp"

#ifdef __APPLE__
#include <OpenGL/gl3.h>
#elif !defined(_WIN32)
#include <GL/gl.h>
#endif

#include <GLFW/glfw3.h>
// clang-format on

int main() {
    auto config = Config::load_default();

    if (!glfwInit()) {
        LOG_ERROR() << "glfwInit failed";
        return 1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif

    const char* glsl_version = "#version 150";
    GLFWwindow* window = glfwCreateWindow(960, 640, "Driscord", nullptr, nullptr);

#ifndef __APPLE__
    if (!window) {
        LOG_WARNING() << "OpenGL 3.2 Core not available, falling back to 2.1";
        glfwDefaultWindowHints();
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
        glsl_version = "#version 120";
        window = glfwCreateWindow(960, 640, "Driscord", nullptr, nullptr);
    }
#endif

    if (!window) {
        LOG_ERROR() << "glfwCreateWindow failed";
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    LOG_INFO() << "OpenGL: " << glGetString(GL_VERSION);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;

    UIRenderer::apply_discord_theme();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    App app(config);
    UIRenderer ui(config);

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ui.render(app);

        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.11f, 0.12f, 0.13f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
