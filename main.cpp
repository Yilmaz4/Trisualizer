#define VERSION "0.1"

#ifndef _DEBUG
#pragma comment(linker, "/SUBSYSTEM:windows /ENTRY:mainCRTStartup")
#endif

#pragma warning(disable: 26495)

#define IMGUI_DEFINE_MATH_OPERATORS
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define NOMINMAX

#include <Windows.h>
#include "resource.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <imgui_internal.h>
#include <imgui_theme.h>

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <stb/stb_image_write.h>
#include <tinyfiledialogs/tinyfiledialogs.h>
#include <chromium/cubic_bezier.h>

#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <iomanip>
#include <ctime>
#include <string>
#include <filesystem>
#include <functional>
#include <algorithm>
#include <regex>

using namespace glm;

void GLAPIENTRY glMessageCallback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* message, const void* userParam) {
	if (type != GL_DEBUG_TYPE_ERROR) return;
	fprintf(stderr, "GL CALLBACK: %s type = 0x%x, severity = 0x%x, message = %s\n", "** GL ERROR **", type, severity, message);
}

static gfx::CubicBezier fast_out_slow_in(0.4, 0.0, 0.2, 1.0);
static float bezier(float t) {
    return fast_out_slow_in.Solve(t);
}

namespace ImGui {
    ImFont* font;

    // credit: https://github.com/zfedoran
    bool BufferingBar(const char* label, float value, const ImVec2& size_arg, const ImU32& bg_col, const ImU32& fg_col) {
        ImGuiWindow* window = GetCurrentWindow();
        if (window->SkipItems)
            return false;

        ImGuiContext& g = *GImGui;
        const ImGuiStyle& style = g.Style;
        const ImGuiID id = window->GetID(label);

        ImVec2 pos = window->DC.CursorPos;
        ImVec2 size = size_arg;
        size.x -= style.FramePadding.x * 2;

        const ImRect bb(pos, ImVec2(pos.x + size.x, pos.y + size.y));
        ItemSize(bb, style.FramePadding.y);
        if (!ItemAdd(bb, id))
            return false;

        window->DrawList->AddRectFilled(bb.Min, bb.Max, bg_col);
        window->DrawList->AddRectFilled(bb.Min, ImVec2(bb.Min.x + value * size.x, bb.Max.y), fg_col);
    }

    // credit: https://github.com/hofstee
    constexpr static auto lerp(float x0, float x1) {
        return [=](float t) {
            return (1 - t) * x0 + t * x1;
        };
    }

    constexpr static float lerp(float x0, float x1, float t) {
        return lerp(x0, x1)(t);
    }

    static auto interval(float T0, float T1, std::function<float(float)> tween = lerp(0.0, 1.0)) {
        return [=](float t) {
            return t < T0 ? 0.0f : t > T1 ? 1.0f : tween((t - T0) / (T1 - T0));
            };
    }

    template <int T> float sawtooth(float t) {
        return ImFmod(((float)T) * t, 1.0f);
    }

    bool Spinner(const char* label, float radius, int thickness, const ImU32& color) {
        ImGuiWindow* window = GetCurrentWindow();
        if (window->SkipItems) return false;

        ImGuiContext& g = *GImGui;
        const ImGuiStyle& style = g.Style;
        const ImGuiID id = window->GetID(label);

        ImVec2 pos = window->DC.CursorPos;
        ImVec2 size((radius) * 2, (radius + style.FramePadding.y) * 2);

        const ImRect bb(pos, ImVec2(pos.x + size.x, pos.y + size.y));
        ItemSize(bb, style.FramePadding.y);
        if (!ItemAdd(bb, id)) return false;

        const ImVec2 center = ImVec2(pos.x + radius, pos.y + radius + thickness + style.FramePadding.y);

        const float start_angle = -IM_PI / 2.0f;         // Start at the top
        const int num_detents = 5;                       // how many rotations we want before a repeat
        const int skip_detents = 3;                      // how many steps we skip each rotation
        const float period = 5.0f;                       // in seconds
        const float t = ImFmod(g.Time, period) / period; // map period into [0, 1]

        auto stroke_head_tween = [](float t) {
            t = sawtooth<num_detents>(t);
            return interval(0.0, 0.5, bezier)(t);
            };

        auto stroke_tail_tween = [](float t) {
            t = sawtooth<num_detents>(t);
            return interval(0.5, 1.0, bezier)(t);
            };

        auto step_tween = [=](float t) {
            return floor(lerp(0.0, (float)num_detents, t));
            };

        auto rotation_tween = sawtooth<num_detents>;

        const float head_value = stroke_head_tween(t);
        const float tail_value = stroke_tail_tween(t);
        const float step_value = step_tween(t);
        const float rotation_value = rotation_tween(t);

        const float min_arc = 30.0f / 360.0f * 2.0f * IM_PI;
        const float max_arc = 270.0f / 360.0f * 2.0f * IM_PI;
        const float step_offset = skip_detents * 2.0f * IM_PI / num_detents;
        const float rotation_compensation = ImFmod(4.0 * IM_PI - step_offset - max_arc, 2 * IM_PI);

        const float a_min = start_angle + tail_value * max_arc + rotation_value * rotation_compensation - step_value * step_offset;
        const float a_max = a_min + (head_value - tail_value) * max_arc + min_arc;

        window->DrawList->PathClear();

        int num_segments = 24;
        for (int i = 0; i < num_segments; i++) {
            const float a = a_min + ((float)i / (float)num_segments) * (a_max - a_min);
            window->DrawList->PathLineTo(ImVec2(center.x + ImCos(a) * radius,
                center.y + ImSin(a) * radius));
        }

        window->DrawList->PathStroke(color, false, thickness);

        return true;
    }

    // from imgui demo app
    static void HelpMarker(const char* desc) {
        ImGui::TextDisabled("(?)");
        if (ImGui::BeginItemTooltip()) {
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
            ImGui::TextUnformatted(desc);
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
    }
}

class Trisualizer {
    GLFWwindow* window = nullptr;
    ImFont* font_title = nullptr;

    int grid_height = 100, grid_width = 100;
    std::vector<double> grid = std::vector(grid_height * grid_width, 0.0);

    GLuint shaderProgram;
    GLuint VAO, VBO;
public:
	Trisualizer() {
        glfwInit();

        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

        const GLFWvidmode* mode = glfwGetVideoMode(glfwGetPrimaryMonitor());

        glfwWindowHint(GLFW_RED_BITS, mode->redBits);
        glfwWindowHint(GLFW_GREEN_BITS, mode->greenBits);
        glfwWindowHint(GLFW_BLUE_BITS, mode->blueBits);
        glfwWindowHint(GLFW_REFRESH_RATE, mode->refreshRate);

        window = glfwCreateWindow(800, 600, "Trisualizer", NULL, NULL);
        if (window == nullptr) {
            std::cerr << "Failed to create OpenGL window" << std::endl;
            return;
        }
        glfwSetWindowUserPointer(window, this);
        glfwSwapInterval(1);
        glfwMakeContextCurrent(window);

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO(); (void)io;
        io.Fonts->AddFontDefault();
        font_title = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\consola.ttf", 11.f);
        IM_ASSERT(font_title != NULL);
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.IniFilename = NULL;
        io.LogFilename = NULL;

        ImGui::StyleColorsDark();
        ImGui::LoadTheme();

        ImGui_ImplGlfw_InitForOpenGL(window, true);
        ImGui_ImplOpenGL3_Init("#version 460");

        if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
            std::cerr << "Failed to create OpenGL window" << std::endl;
            return;
        }

        int success;
        char infoLog[512];

        unsigned int vertexShader;
        vertexShader = glCreateShader(GL_VERTEX_SHADER);
        char* vertexSource = read_resource(IDR_VRTX);
        glShaderSource(vertexShader, 1, &vertexSource, NULL);
        glCompileShader(vertexShader);
        glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &success);
        if (!success) {
            glGetShaderInfoLog(vertexShader, 512, NULL, infoLog);
            std::cerr << infoLog << std::endl;
            return;
        }

        unsigned int fragmentShader;
        fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
        char* fragmentSource = read_resource(IDR_FRAG);
        glShaderSource(fragmentShader, 1, &fragmentSource, NULL);
        glCompileShader(fragmentShader);
        glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &success);
        if (!success) {
            glGetShaderInfoLog(fragmentShader, 512, NULL, infoLog);
            std::cerr << infoLog << std::endl;
            return;
        }

        shaderProgram = glCreateProgram();
        glAttachShader(shaderProgram, vertexShader);
        glAttachShader(shaderProgram, fragmentShader);
        glLinkProgram(shaderProgram);
        glDeleteShader(fragmentShader);

        glGenVertexArrays(1, &VAO);
        glGenBuffers(1, &VBO);
        glBindVertexArray(VAO);
        glBindBuffer(GL_ARRAY_BUFFER, VBO);
        glBufferData(GL_ARRAY_BUFFER, grid.size(), grid.data(), GL_STATIC_DRAW);
        glVertexAttribLPointer(0, 1, GL_DOUBLE, 0, nullptr);
        glEnableVertexAttribArray(0);

        glUseProgram(shaderProgram);

        mainloop();
	}
private:
    static inline char* read_resource(int name) {
        HMODULE handle = GetModuleHandleW(NULL);
        HRSRC rc = FindResourceW(handle, MAKEINTRESOURCE(name), MAKEINTRESOURCE(TEXTFILE));
        if (rc == NULL) return nullptr;
        HGLOBAL rcData = LoadResource(handle, rc);
        if (rcData == NULL) return nullptr;
        DWORD size = SizeofResource(handle, rc);
        char* res = new char[size + 1];
        memcpy(res, static_cast<const char*>(LockResource(rcData)), size);
        res[size] = '\0';
        return res;
    }
public:
    void mainloop() {
        do {
            glfwPollEvents();
            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();

            ImGui::Render();
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            glfwSwapBuffers(window);

        } while (!glfwWindowShouldClose(window));
    }
};

int main() {
    Trisualizer app;

    return 0;
}