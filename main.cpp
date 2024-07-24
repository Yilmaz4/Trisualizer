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

#ifdef _DEBUG
void GLAPIENTRY glMessageCallback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* message, const void* userParam) {
    if (type != GL_DEBUG_TYPE_ERROR) return;
    fprintf(stderr, "GL CALLBACK: %s type = 0x%x, severity = 0x%x, message = %s\n", "** GL ERROR **", type, severity, message);
}
#endif

float quad[12] = {
    -1.0f, -1.0f, -1.0f,  1.0f, 1.0f,  1.0f,
    -1.0f, -1.0f,  1.0f,  1.0f, 1.0f, -1.0f
};
std::vector<vec4> colors = {
    vec4(0.000f, 0.500f, 1.000f, 1.f),
    vec4(0.823f, 0.000f, 0.000f, 1.f),
    vec4(0.924f, 0.395f, 0.000f, 1.f),
    vec4(0.058f, 0.570f, 0.000f, 1.f),
    vec4(0.496f, 0.000f, 0.652f, 1.f),
};

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

struct Slider {
    float value;
    float min, max;
    char symbol;
};

enum GraphType {
    UserDefined,
    TangentPlane,
};

class Graph {
public:
    GLuint computeProgram = NULL, SSBO, EBO;
    int idx;
    bool enabled;
    bool valid;
    char* infoLog = new char[512]{};
    int type;
    int grid_res;
    std::vector<unsigned int> indices;

    std::string defn = std::string(100, '\0');
    vec4 color;

    Graph(int idx, int type, std::string definition, int res, vec4 color, bool enabled, GLuint SSBO, GLuint EBO)
        : type(type), idx(idx), grid_res(res), color(color), enabled(enabled), SSBO(SSBO), EBO(EBO), defn(definition) {}

    void setup(bool upload_defn = true) {
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
        if (upload_defn) upload_definition();
        else valid = false;
    }
    
    void upload_definition() {
        int success;

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
            valid = enabled = false;
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
        valid = true;
    }

    void use_compute(float zoom, vec2 centerPos) const {
        glUseProgram(computeProgram);
        glUniform1f(glGetUniformLocation(computeProgram, "zoom"), zoom);
        glUniform1i(glGetUniformLocation(computeProgram, "grid_res"), grid_res);
        glUniform2fv(glGetUniformLocation(computeProgram, "centerPos"), 1, value_ptr(centerPos));
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
    std::vector<Slider> sliders;

    float theta = 45, phi = 45;
    float zoom = 8.f;
    float graph_size = 1.3f;
    bool gridLines = true;
    float gridLineDensity = 3.f;
    bool autoRotate = false;
    bool trace = true;
    bool tangent_plane = false, apply_tangent_plane = false;
    bool gradient_vector = false;
    vec2 centerPos = vec2(0.f);

    vec2 mousePos = vec2(0.f);
    int sidebarWidth = 300;
    bool updateBufferSize = false;

    GLuint shaderProgram;
    GLuint VAO, VBO, EBO;
    GLuint FBO, gridSSBO;
    GLuint depthMap, frameTex, posBuffer;

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

        window = glfwCreateWindow(1000, 600, "Trisualizer", NULL, NULL);
        if (window == nullptr) {
            std::cerr << "Failed to create OpenGL window" << std::endl;
            return;
        }
        glfwSetWindowUserPointer(window, this);
        glfwSwapInterval(1);
        glfwMakeContextCurrent(window);

        glfwSetCursorPosCallback(window, on_mouseMove);
        glfwSetScrollCallback(window, on_mouseScroll);
        glfwSetWindowSizeCallback(window, on_windowResize);
        glfwSetMouseButtonCallback(window, on_mouseButton);

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO(); (void)io;
        io.Fonts->AddFontDefault();
        static const ImWchar ranges[] = {
            0x0020, 0x00FF,
            0x2202, 0x2202, // ∂
            0x2207, 0x2207, // ∇
        };
        font_title = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\consola.ttf", 11.f, nullptr, ranges);
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
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glEnable(GL_BLEND);
#ifdef _DEBUG
        glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
        glDebugMessageCallback(glMessageCallback, nullptr);
#endif
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
        glBufferData(GL_SHADER_STORAGE_BUFFER, 6 * 600 * 600 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, posBuffer);
        glShaderStorageBlockBinding(shaderProgram, glGetProgramResourceIndex(shaderProgram, GL_SHADER_STORAGE_BLOCK, "posbuffer"), 1);

        glUniform1f(glGetUniformLocation(shaderProgram, "zoom"), zoom);
        glUniform1f(glGetUniformLocation(shaderProgram, "graph_size"), graph_size);
        glUniform1f(glGetUniformLocation(shaderProgram, "ambientStrength"), 0.2f);
        glUniform1f(glGetUniformLocation(shaderProgram, "gridLineDensity"), gridLineDensity);
        glUniform2i(glGetUniformLocation(shaderProgram, "windowSize"), 1000, 600);
        glUniform3f(glGetUniformLocation(shaderProgram, "lightPos"), 0.f, 6.f, 0.f);
        glUniform3f(glGetUniformLocation(shaderProgram, "centerPos"), 0.f, 0.f, 0.f);

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
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, 1000, 600, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthMap, 0);

        glGenTextures(1, &frameTex);
        glBindTexture(GL_TEXTURE_2D, frameTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, 1000, 600, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, frameTex, 0);

        graphs.push_back(Graph(0, TangentPlane, "plane_params[0]+plane_params[1]*(x-plane_params[2])+plane_params[3]*(y-plane_params[4])", 25, vec4(0.f), false, gridSSBO, EBO));
        graphs[0].setup(true);
        graphs.push_back(Graph(1, UserDefined, "sin(x * y)", 500, colors[0], true, gridSSBO, EBO));
        graphs[1].setup(true);

        mainloop();
    }

private:
    static inline void on_windowResize(GLFWwindow* window, int width, int height) {
        Trisualizer* app = static_cast<Trisualizer*>(glfwGetWindowUserPointer(window));
        glBindTexture(GL_TEXTURE_2D, app->depthMap);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, width, height, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
        glBindTexture(GL_TEXTURE_2D, app->frameTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, app->posBuffer);
        glBufferData(GL_SHADER_STORAGE_BUFFER, 6 * (width - app->sidebarWidth) * height * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
        glUniform2i(glGetUniformLocation(app->shaderProgram, "windowSize"), width, height);
    }

    static inline void on_mouseButton(GLFWwindow* window, int button, int action, int mods) {
        Trisualizer* app = static_cast<Trisualizer*>(glfwGetWindowUserPointer(window));
        switch (button) {
        case GLFW_MOUSE_BUTTON_LEFT:
            if (action == GLFW_RELEASE && app->updateBufferSize) {
                int width, height;
                glfwGetWindowSize(window, &width, &height);
                glBindBuffer(GL_SHADER_STORAGE_BUFFER, app->posBuffer);
                glBufferData(GL_SHADER_STORAGE_BUFFER, 6 * (width - app->sidebarWidth) * height * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
                app->updateBufferSize = false;
            }
            if (action == GLFW_PRESS && app->tangent_plane) {
                app->apply_tangent_plane = true;
            }
        }
    }

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

            int wWidth, wHeight;
            glfwGetWindowSize(window, &wWidth, &wHeight);

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
                    ImGui::DockBuilderAddNode(dockspace_id, dockspace_flags | ImGuiDockNodeFlags_DockSpace | ImGuiDockNodeFlags_NoTabBar | ImGuiDockNodeFlags_NoResize | ImGuiDockNodeFlags_NoResizeX);
                    ImGui::DockBuilderSetNodeSize(dockspace_id, viewport->Size);

                    auto dock_id_left = ImGui::DockBuilderSplitNode(dockspace_id, ImGuiDir_Left, 0.3f, nullptr, &dockspace_id);
                    auto dock_id_middle = ImGui::DockBuilderSplitNode(dock_id_left, ImGuiDir_Down, 0.5f, nullptr, &dock_id_left);
                    auto dock_id_down = ImGui::DockBuilderSplitNode(dock_id_middle, ImGuiDir_Down, 0.5f, nullptr, &dock_id_middle);

                    ImGui::DockBuilderDockWindow("Symbolic View", dock_id_left);
                    ImGui::DockBuilderDockWindow("Tools", dock_id_down);
                    ImGui::DockBuilderDockWindow("Sliders", dock_id_middle);
                    ImGui::DockBuilderFinish(dockspace_id);
                }
            }

            ImGui::End();

            ImVec2 vMin, vMax;

            ImGuiWindowClass window_class;
            window_class.DockNodeFlagsOverrideSet = ImGuiDockNodeFlags_NoTabBar;
            ImGui::SetNextWindowClass(&window_class);
            ImGui::Begin("Symbolic View", nullptr, ImGuiWindowFlags_NoMove);

            float swidth = ImGui::GetWindowSize().x;
            if (sidebarWidth != swidth) {
                updateBufferSize = true;
                sidebarWidth = swidth;
            }
            if (ImGui::Button("New", ImVec2(50, 0))) {
                int i = graphs.size() - 1;
                graphs.push_back(Graph(graphs.size(), UserDefined, "", 500, colors[i % colors.size()], false, gridSSBO, EBO));
                graphs[graphs.size() - 1].setup(false);
            }
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.4f, 0.4f, 1.f));
            int nVertices = 0;
            for (const Graph& g : graphs) if (g.enabled) nVertices += g.grid_res * g.grid_res;
            ImGui::Text("FPS:%.1f  Number of vertices: %d", 1.0 / timeStep, nVertices);
            ImGui::PopStyleColor();
            for (int i = 0; i < graphs.size(); i++) {
                if (graphs[i].type != UserDefined) continue;
                ImGui::BeginChild(std::format("##child{}", i).c_str(), ImVec2(0, 0), ImGuiChildFlags_Border | ImGuiChildFlags_AlwaysAutoResize | ImGuiChildFlags_AutoResizeY);
                ImVec2 vMin = ImGui::GetWindowContentRegionMin() + ImGui::GetWindowPos();
                ImVec2 vMax = ImGui::GetWindowContentRegionMax() + ImGui::GetWindowPos();
                ImGui::BeginDisabled(!graphs[i].valid);
                ImGui::Checkbox(std::format("##check{}", i).c_str(), &graphs[i].enabled);
                ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::ColorEdit4(std::format("##color{}", i).c_str(), value_ptr(graphs[i].color), ImGuiColorEditFlags_NoInputs);
                ImGui::SameLine();
                ImGui::PushItemWidth((vMax.x - vMin.x) - 65);
                if (ImGui::InputText(std::format("##defn{}", i).c_str(), graphs[i].defn.data(), 512, ImGuiInputTextFlags_EnterReturnsTrue)) {
                    graphs[i].upload_definition();
                    if (graphs[i].valid) graphs[i].enabled = true;
                }
                ImGui::SameLine();
                if (ImGui::Button("X", ImVec2(16, 0))) {
                    graphs.erase(graphs.begin() + i);
                }
                else if (!graphs[i].valid && strlen(graphs[i].infoLog)) {
                    ImGui::InputTextMultiline("##errorlist", graphs[i].infoLog, 512, ImVec2((vMax.x - vMin.x), 0), ImGuiInputTextFlags_ReadOnly);
                }
                ImGui::EndChild();
            }
            ImGui::End();

            ImGui::SetNextWindowClass(&window_class);
            ImGui::Begin("Sliders", nullptr, ImGuiWindowFlags_NoMove);
            vMin = ImGui::GetWindowContentRegionMin() + ImGui::GetWindowPos();
            vMax = ImGui::GetWindowContentRegionMax() + ImGui::GetWindowPos();
            ImGui::End();

            ImGui::SetNextWindowClass(&window_class);
            ImGui::Begin("Tools", nullptr, ImGuiWindowFlags_NoMove);
            vMin = ImGui::GetWindowContentRegionMin() + ImGui::GetWindowPos();
            vMax = ImGui::GetWindowContentRegionMax() + ImGui::GetWindowPos();
            static vec2 gotoLocation;
            ImGui::InputFloat2("##goto", value_ptr(gotoLocation));
            ImGui::SameLine();
            if (ImGui::Button("Jump", ImVec2(vMax.x - vMin.x - ImGui::GetCursorPosX() + 5, 0))) {
                centerPos = gotoLocation;
                glUniform2fv(glGetUniformLocation(shaderProgram, "centerPos"), 1, value_ptr(centerPos));
            }
            float buttonWidth = (vMax.x - vMin.x - 20.f) / 4.f;
            if (tangent_plane) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.30f, 0.32f, 0.33f, 1.00f));
            if (ImGui::Button("Tangent\n Plane", ImVec2(buttonWidth, 60))) {
                tangent_plane ^= 1;
                if (!tangent_plane) ImGui::PopStyleColor();
            }
            else if (tangent_plane) ImGui::PopStyleColor();
            ImGui::SameLine();
            if (ImGui::Button("Gradient\n Vector", ImVec2(buttonWidth, 60))) {
                
            }
            ImGui::SameLine();
            if (ImGui::Button("Min/Max", ImVec2(buttonWidth, 60))) {

            }
            ImGui::SameLine();
            if (ImGui::Button("Integral", ImVec2(buttonWidth, 60))) {

            }
            ImGui::SameLine();
            ImGui::End();

            double x, y;
            glfwGetCursorPos(window, &x, &y);
            if (graphs.size() > 0 && !ImGui::GetIO().WantCaptureMouse && x - sidebarWidth > 0. && x - sidebarWidth < (wWidth - sidebarWidth) && y > 0. && y < wHeight) {
                float data[1];
                glBindFramebuffer(GL_FRAMEBUFFER, FBO);
                glBindTexture(GL_TEXTURE_2D, depthMap);
                glReadPixels(x, wHeight - y, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, data);
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
                    vec4 fragPos = vec4(p[static_cast<int>(6 * (wHeight * (wHeight - y) + (x - sidebarWidth)) + 0)],
                                        p[static_cast<int>(6 * (wHeight * (wHeight - y) + (x - sidebarWidth)) + 1)],
                                        p[static_cast<int>(6 * (wHeight * (wHeight - y) + (x - sidebarWidth)) + 2)],
                                        p[static_cast<int>(6 * (wHeight * (wHeight - y) + (x - sidebarWidth)) + 3)]);
                    vec2 gradvec = vec2(p[static_cast<int>(6 * (wHeight * (wHeight - y) + (x - sidebarWidth)) + 4)],
                                        p[static_cast<int>(6 * (wHeight * (wHeight - y) + (x - sidebarWidth)) + 5)]);
                    glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);

                    vec4 c = graphs[*reinterpret_cast<int*>(&fragPos.w)].color * 1.3f;
                    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 9.f);
                    ImGui::ColorEdit4("##infocolor", value_ptr(c), ImGuiColorEditFlags_NoInputs);
                    ImGui::SameLine();
                    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 11.f);
                    ImGui::Text(u8"X=%6.3f\nY=%6.3f\nZ=%6.3f", fragPos.x, fragPos.y, fragPos.z);
                    ImGui::SameLine();
                    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 5.f);
                    ImGui::Text(u8"\u2202z/\u2202x=%6.3f\n\u2202z/\u2202y=%6.3f", gradvec.x, gradvec.y);
                    ImGui::End();

                    GLfloat params[5] = { fragPos.z, gradvec.x, fragPos.x, gradvec.y, fragPos.y };
                    if (tangent_plane) {
                        glUseProgram(graphs[0].computeProgram);
                        glUniform1fv(glGetUniformLocation(graphs[0].computeProgram, "plane_params"), 5, params);
                        graphs[0].enabled = true;
                        vec4 nc = colors[(graphs.size() - 1) % colors.size()];
                        graphs[0].color = vec4(nc.r, nc.g, nc.b, 0.4f);
                        glUseProgram(shaderProgram);
                    }
                    if (apply_tangent_plane) {
                        const char* eq = "(%.6f)+(%.6f)*(x-(%.6f))+(%.6f)*(y-(%.6f))";
                        char eqf[88]{};
                        sprintf_s(eqf, eq, params[0], params[1], params[2], params[3], params[4]);
                        graphs.push_back(Graph(graphs.size(), UserDefined, eqf, 25, colors[(graphs.size() - 1) % colors.size()], true, gridSSBO, EBO));
                        graphs[graphs.size() - 1].setup(true);
                        apply_tangent_plane = false;
                        tangent_plane = false;
                        graphs[0].enabled = false;
                    }
                } else goto mouse_not_on_graph;
            } else {
            mouse_not_on_graph:
                graphs[0].enabled = false;
                apply_tangent_plane = false;
            }
            ImGui::PopFont();

            if (autoRotate)
                phi += timeStep * 5.f;

            int h, w;
            glfwGetWindowSize(window, &w, &h);
            auto cameraPos = vec3(sin(glm::radians(theta)) * cos(glm::radians(phi)), cos(glm::radians(theta)), sin(glm::radians(theta)) * sin(glm::radians(phi)));
            view = lookAt(cameraPos, vec3(0.f), { 0.f, 1.f, 0.f });
            proj = ortho(-1.f, 1.f, -(float)wHeight / (float)(wWidth - sidebarWidth), (float)wHeight / (float)(wWidth - sidebarWidth), -10.f, 10.f);
            glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "vpmat"), 1, GL_FALSE, value_ptr(proj * view));
            glUniform3fv(glGetUniformLocation(shaderProgram, "cameraPos"), 1, value_ptr(cameraPos));
            glUniform2i(glGetUniformLocation(shaderProgram, "regionSize"), wWidth - sidebarWidth, wHeight);

            ImGui::Render();

            glClearColor(0.05f, 0.05f, 0.05f, 1.f);
            glBindFramebuffer(GL_FRAMEBUFFER, NULL);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            glBindFramebuffer(GL_FRAMEBUFFER, FBO);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            
            
            glViewport(sidebarWidth, 0, wWidth - sidebarWidth, wHeight);

            static bool yes = false;
            for (int i = 1; i <= graphs.size(); i++) {
                if (i == graphs.size()) i = 0;
                const Graph& g = graphs[i];
                if (!g.enabled) {
                    if (i == 0) break;
                    continue;
                }
                g.use_compute(zoom, centerPos);
                glDispatchCompute(g.grid_res, g.grid_res, 1);
                glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
                glUseProgram(shaderProgram);
                g.use_shader();
                glUniform1i(glGetUniformLocation(shaderProgram, "index"), i);
                glUniform4f(glGetUniformLocation(shaderProgram, "color"), g.color.r, g.color.g, g.color.b, g.color.w);
                glUniform1i(glGetUniformLocation(shaderProgram, "grid_res"), g.grid_res);
                glUniform1i(glGetUniformLocation(shaderProgram, "tangent_plane"), g.type == TangentPlane);
                glBindVertexArray(VAO);
                glUniform1i(glGetUniformLocation(shaderProgram, "quad"), false);
                glDrawElements(GL_TRIANGLE_STRIP, g.indices.size(), GL_UNSIGNED_INT, 0);
                if (i == 0) break;
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