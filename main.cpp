#define VERSION "0.1"

#ifndef _DEBUG
#pragma comment(linker, "/SUBSYSTEM:windows /ENTRY:mainCRTStartup")
#endif

#pragma warning(disable: 26495)

#define _USE_MATH_DEFINES
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
#include "glm/gtx/string_cast.hpp"
#include <stb/stb_image_write.h>
#include <tinyfiledialogs/tinyfiledialogs.h>

#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <iomanip>
#include <ctime>
#include <cmath>
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

namespace ImGui {
    ImFont* font;

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

inline char* read_resource(int name) {
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

class Graph {
    GLuint computeProgram = NULL, SSBO, EBO;
public:
    int idx;
    bool enabled;
    int grid_res;
    std::vector<unsigned int> indices;

    std::string defn;
    vec3 color;

    Graph(int idx, std::string definition, int res, vec3 color, GLuint SSBO, GLuint EBO)
        : idx(idx), grid_res(res), color(color), SSBO(SSBO), EBO(EBO) {

        for (unsigned int y = 0; y < grid_res - 1; ++y) {
            for (unsigned int x = 0; x < grid_res; ++x) {
                unsigned int idx0 = y * grid_res + x;
                unsigned int idx1 = (y + 1) * grid_res + x;

                indices.push_back(idx0);
                indices.push_back(idx1);
            }
            if (y < grid_res - 2) {
                indices.push_back((y + 1) * grid_res + (grid_res - 1));
                indices.push_back((y + 1) * grid_res);
            }
        }
        defn = definition;
        upload_definition();
    }

    void upload_definition() {
        int success;
        char infoLog[512];

        unsigned int computeShader = glCreateShader(GL_COMPUTE_SHADER);
        char* computeSource = read_resource(IDR_CMPT);
        size_t size = strlen(computeSource) + 512;
        char* modifiedSource = new char[size];
        sprintf_s(modifiedSource, size, computeSource, defn.c_str());
        glShaderSource(computeShader, 1, &modifiedSource, NULL);
        glCompileShader(computeShader);
        glGetShaderiv(computeShader, GL_COMPILE_STATUS, &success);
        if (!success) {
            glGetShaderInfoLog(computeShader, 512, NULL, infoLog);
            std::cerr << infoLog << std::endl;
            return;
        }
        if (computeProgram != 0) glDeleteProgram(computeProgram);
        computeProgram = glCreateProgram();
        glAttachShader(computeProgram, computeShader);
        glLinkProgram(computeProgram);
        glDeleteShader(computeShader);
        glUseProgram(computeProgram);
        delete[] computeSource, modifiedSource;

        glShaderStorageBlockBinding(computeProgram, glGetProgramResourceIndex(computeProgram, GL_SHADER_STORAGE_BLOCK, "gridbuffer"), 0);
    }

    void use_compute(float zoom) const {
        glUseProgram(computeProgram);
        glUniform1f(glGetUniformLocation(computeProgram, "zoom"), zoom);
        glUniform1i(glGetUniformLocation(computeProgram, "grid_res"), grid_res);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, SSBO);
        glBufferData(GL_SHADER_STORAGE_BUFFER, pow(grid_res, 2) * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    }
    void use_shader() const {
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_DYNAMIC_DRAW);
    }
};

class Trisualizer {
    GLFWwindow* window = nullptr;
    ImFont* font_title = nullptr;
public:
    std::vector<Graph> graphs;

    float theta = 45, phi = 45;
    float zoom = 8.f;
    float graph_size = 1.3f;
    bool gridLines = true;
    float gridLineDensity = 3.f;
    bool autoRotate = false;
    int selected = 0;

    vec2 mousePos = vec2(0.f);

    GLuint shaderProgram;
    GLuint VAO, VBO, EBO;
    GLuint FBO, gridSSBO;
    GLuint depthMap, frameTex, posBuffer;

    float quad[12] = {
        -1.0f, -1.0f, -1.0f,  1.0f, 1.0f,  1.0f,
        -1.0f, -1.0f,  1.0f,  1.0f, 1.0f, -1.0f
    };

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

        glfwWindowHint(GLFW_SAMPLES, 4);

        window = glfwCreateWindow(900, 600, "Trisualizer", NULL, NULL);
        if (window == nullptr) {
            std::cerr << "Failed to create OpenGL window" << std::endl;
            return;
        }
        glfwSetWindowUserPointer(window, this);
        glfwSwapInterval(1);
        glfwMakeContextCurrent(window);

        glfwSetCursorPosCallback(window, on_mouseMove);
        glfwSetScrollCallback(window, on_mouseScroll);

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO(); (void)io;
        io.Fonts->AddFontDefault();
        font_title = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\consola.ttf", 11.f);
        IM_ASSERT(font_title != NULL);
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
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

        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glEnable(GL_MULTISAMPLE);

        glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
        glDebugMessageCallback(glMessageCallback, nullptr);

        glViewport(300, 0, 600, 600);

        int success;
        char infoLog[512];

        unsigned int vertexShader = glCreateShader(GL_VERTEX_SHADER);
        char* vertexSource = read_resource(IDR_VRTX);
        glShaderSource(vertexShader, 1, &vertexSource, NULL);
        glCompileShader(vertexShader);
        glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &success);
        if (!success) {
            glGetShaderInfoLog(vertexShader, 512, NULL, infoLog);
            std::cerr << infoLog << std::endl;
            return;
        }

        unsigned int fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
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
        glUseProgram(shaderProgram);
        delete[] vertexSource, fragmentSource;

        glGenBuffers(1, &gridSSBO);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, gridSSBO);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, gridSSBO);
        glShaderStorageBlockBinding(shaderProgram, glGetProgramResourceIndex(shaderProgram, GL_SHADER_STORAGE_BLOCK, "gridbuffer"), 0);

        glGenBuffers(1, &posBuffer);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, posBuffer);
        glBufferData(GL_SHADER_STORAGE_BUFFER, 3 * 600 * 600 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, posBuffer);
        glShaderStorageBlockBinding(shaderProgram, glGetProgramResourceIndex(shaderProgram, GL_SHADER_STORAGE_BLOCK, "posbuffer"), 1);

        glUniform1f(glGetUniformLocation(shaderProgram, "zoom"), zoom);
        glUniform1f(glGetUniformLocation(shaderProgram, "graph_size"), graph_size);
        glUniform1f(glGetUniformLocation(shaderProgram, "ambientStrength"), 0.2f);
        glUniform1f(glGetUniformLocation(shaderProgram, "gridLineDensity"), gridLineDensity);
        glUniform1i(glGetUniformLocation(shaderProgram, "selected"), selected);

        glGenVertexArrays(1, &VAO);
        glBindVertexArray(VAO);
        glGenBuffers(1, &VBO);
        glBindBuffer(GL_ARRAY_BUFFER, VBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(quad), &quad, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);

        glGenBuffers(1, &EBO);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);

        glGenFramebuffers(1, &FBO);
        glBindFramebuffer(GL_FRAMEBUFFER, FBO);

        glGenTextures(1, &depthMap);
        glBindTexture(GL_TEXTURE_2D, depthMap);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, 900, 600, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthMap, 0);

        glGenTextures(1, &frameTex);
        glBindTexture(GL_TEXTURE_2D, frameTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, 900, 600, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, frameTex, 0);

        graphs.push_back({ 0, "sin(x * y)", 800, vec3(0.f, 0.5f, 1.f), gridSSBO, EBO });
        graphs.push_back({ 1, "cos(x * y)", 250, vec3(1.f, 0.5f, 0.f), gridSSBO, EBO });

        mainloop();
    }

private:
    static inline void on_mouseScroll(GLFWwindow* window, double x, double y) {
        Trisualizer* app = static_cast<Trisualizer*>(glfwGetWindowUserPointer(window));
        if (ImGui::GetIO().WantCaptureMouse) return;
        if (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS) {
            app->graph_size *= pow(0.9, -y);
            glUniform1f(glGetUniformLocation(app->shaderProgram, "graph_size"), app->graph_size);
        }
        else {
            app->zoom *= pow(0.9, y);
            glUniform1f(glGetUniformLocation(app->shaderProgram, "zoom"), app->zoom);
        }
    }

    static inline void on_mouseMove(GLFWwindow* window, double x, double y) {
        Trisualizer* app = static_cast<Trisualizer*>(glfwGetWindowUserPointer(window));
        if (ImGui::GetIO().WantCaptureMouse) return;
        if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS) {
            float xoffset = x - app->mousePos.x;
            float yoffset = y - app->mousePos.y;
            app->theta += yoffset * 0.5f;
            app->phi -= xoffset * 0.5f;
            if (app->theta > 179.9f) app->theta = 179.9f;
            if (app->theta < 0.1f) app->theta = 0.1f;
        }
        app->mousePos = { x, y };
    }
public:
    void mainloop() {
        double prevTime = glfwGetTime();
        mat4 view, proj;

        do {
            glfwPollEvents();
            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();

            double currentTime = glfwGetTime();
            double timeStep = currentTime - prevTime;
            prevTime = currentTime;

            ImGui::PushFont(font_title);

            if (ImGui::BeginMainMenuBar()) {
                if (ImGui::BeginMenu("File")) {
                    if (ImGui::MenuItem("Open", "Ctrl+O")) {

                    }
                    if (ImGui::MenuItem("Save", "Ctrl+S")) {

                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Exit", "Alt+F4")) {
                        ExitProcess(0);
                    }
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Graph")) {
                    if (ImGui::MenuItem("Grid lines", nullptr, &gridLines)) {
                        glUniform1f(glGetUniformLocation(shaderProgram, "gridLineDensity"), gridLines ? gridLineDensity : 0.f);
                    }
                    ImGui::MenuItem("Auto-rotate", nullptr, &autoRotate);
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Help")) {
                    if (ImGui::MenuItem("About")) {

                    }
                    ImGui::EndMenu();
                }
                ImGui::EndMainMenuBar();
            }

            static ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_PassthruCentralNode;
            ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;

            ImGuiViewport* viewport = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(viewport->Pos);
            ImGui::SetNextWindowSize(viewport->Size);
            ImGui::SetNextWindowViewport(viewport->ID);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
            window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

            if (dockspace_flags & ImGuiDockNodeFlags_PassthruCentralNode)
                window_flags |= ImGuiWindowFlags_NoBackground;

            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
            ImGui::Begin("DockSpace", nullptr, window_flags);
            ImGui::PopStyleVar(3);

            ImGuiIO& io = ImGui::GetIO();
            if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable)
            {
                ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
                ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), dockspace_flags);

                static bool init = true;
                if (init) {
                    init = false;

                    ImGui::DockBuilderRemoveNode(dockspace_id);
                    ImGui::DockBuilderAddNode(dockspace_id, dockspace_flags | ImGuiDockNodeFlags_DockSpace);
                    ImGui::DockBuilderSetNodeSize(dockspace_id, viewport->Size);

                    auto dock_id_left = ImGui::DockBuilderSplitNode(dockspace_id, ImGuiDir_Left, 0.33333f, nullptr, &dockspace_id);
                    //auto dock_id_down = ImGui::DockBuilderSplitNode(dockspace_id, ImGuiDir_Down, 0.25f, nullptr, &dockspace_id);

                    //ImGui::DockBuilderDockWindow("Down", dock_id_down);
                    ImGui::DockBuilderDockWindow("Symbolic View", dock_id_left);
                    ImGui::DockBuilderFinish(dockspace_id);
                }
            }

            ImGui::End();

            ImGuiWindowClass window_class;
            window_class.DockNodeFlagsOverrideSet = ImGuiDockNodeFlags_NoTabBar;
            ImGui::SetNextWindowClass(&window_class);
            ImGui::Begin("Symbolic View", nullptr, ImGuiWindowFlags_NoMove);

            for (int i = 0; i < graphs.size(); i++) {
                ImGui::BeginChild(std::format("##child{}", i).c_str(), ImVec2(0, 0), ImGuiChildFlags_Border | ImGuiChildFlags_AlwaysAutoResize | ImGuiChildFlags_AutoResizeY);
                ImGui::Checkbox(std::format("##check{}", i).c_str(), &graphs[i].enabled);
                ImGui::SameLine();
                ImGui::ColorEdit3(std::format("##color{}", i).c_str(), value_ptr(graphs[i].color), ImGuiColorEditFlags_NoInputs);
                ImGui::SameLine();
                if (ImGui::InputText(std::format("##defn{}", i).c_str(), graphs[i].defn.data(), 512, ImGuiInputTextFlags_EnterReturnsTrue)) {
                    graphs[i].upload_definition();
                }
                ImGui::EndChild();
            }

            ImGui::End();

            double x, y;
            glfwGetCursorPos(window, &x, &y);
            if (!ImGui::GetIO().WantCaptureMouse && x - 300. > 0. && x - 300. < 600. && y > 0. && y < 600.) {
                float data[1];
                glBindFramebuffer(GL_FRAMEBUFFER, FBO);
                glBindTexture(GL_TEXTURE_2D, depthMap);
                glReadPixels(x, 600 - y, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, data);
                glBindFramebuffer(GL_FRAMEBUFFER, NULL);

                if (data[0] != 1.f) {
                    ImGui::SetNextWindowPos(ImVec2(x + 10.f, y));
                    ImGui::Begin("info", nullptr,
                        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar |
                        ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_AlwaysAutoResize |
                        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar);

                    glBindBuffer(GL_SHADER_STORAGE_BUFFER, posBuffer);
                    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
                    float* p = reinterpret_cast<float*>(glMapBuffer(GL_SHADER_STORAGE_BUFFER, GL_READ_ONLY));
                    vec3 fragPos = vec3(p[static_cast<int>(3 * (600 * (600 - y) + (x - 300)) + 0)],
                                        p[static_cast<int>(3 * (600 * (600 - y) + (x - 300)) + 1)],
                                        p[static_cast<int>(3 * (600 * (600 - y) + (x - 300)) + 2)]);
                    glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);

                    ImGui::Text(u8"X=%6.3f\nY=%6.3f\nZ=%6.3f", fragPos.x, fragPos.y, fragPos.z);

                    ImGui::End();
                }
            }

            ImGui::PopFont();

            if (autoRotate)
                phi += timeStep * 5.f;

            int h, w;
            glfwGetWindowSize(window, &w, &h);
            auto cameraPos = vec3(sin(glm::radians(theta)) * cos(glm::radians(phi)), cos(glm::radians(theta)), sin(glm::radians(theta)) * sin(glm::radians(phi)));
            view = lookAt(cameraPos, vec3(0.f), { 0.f, 1.f, 0.f });
            proj = ortho(-1.f, 1.f, -1.f, 1.f, -10.f, 10.f);
            glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "vpmat"), 1, GL_FALSE, value_ptr(proj * view));
            glUniform3f(glGetUniformLocation(shaderProgram, "lightPos"), 1.f, 2.f, 0.f);

            ImGui::Render();

            glClearColor(0.05f, 0.05f, 0.05f, 1.f);
            glBindFramebuffer(GL_FRAMEBUFFER, NULL);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            glBindFramebuffer(GL_FRAMEBUFFER, FBO);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            static bool yes = false;
            for (int i = 0; i < graphs.size(); i++) {
                const Graph& g = graphs[i];
                if (!g.enabled) continue;
                g.use_compute(zoom);
                glDispatchCompute(g.grid_res, g.grid_res, 1);
                glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
                glUseProgram(shaderProgram);
                g.use_shader();
                glUniform1i(glGetUniformLocation(shaderProgram, "index"), i);
                glUniform3f(glGetUniformLocation(shaderProgram, "color"), g.color.r, g.color.g, g.color.b);
                glUniform1i(glGetUniformLocation(shaderProgram, "grid_res"), g.grid_res);
                glBindVertexArray(VAO);
                glUniform1i(glGetUniformLocation(shaderProgram, "quad"), false);
                glDrawElements(GL_TRIANGLE_STRIP, g.indices.size(), GL_UNSIGNED_INT, 0);
            }
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, frameTex);
            glBindFramebuffer(GL_FRAMEBUFFER, NULL);
            glUniform1i(glGetUniformLocation(shaderProgram, "quad"), true);
            glDrawArrays(GL_TRIANGLES, 0, 6);

            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            glfwSwapBuffers(window);

        } while (!glfwWindowShouldClose(window));
    }
};

int main() {
    Trisualizer app;

    return 0;
}