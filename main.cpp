#define VERSION "0.1"

#ifndef _DEBUG
#pragma comment(linker, "/SUBSYSTEM:windows /ENTRY:mainCRTStartup")
#endif

#pragma warning(disable: 26495)
#pragma warning(disable: 6387)
#pragma warning(disable: 4018)
#pragma warning(disable: 4244)

#define _USE_MATH_DEFINES
#define IMGUI_DEFINE_MATH_OPERATORS
#define STB_IMAGE_WRITE_IMPLEMENTATION

#include <Windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include <shlwapi.h>

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

#include <iostream>
#include <vector>
#include <iomanip>
#include <ctime>
#include <cmath>
#include <string>

#include "resource.h"

#pragma comment(lib, "Gdiplus.lib")
#pragma comment(lib, "Shlwapi.lib")

using namespace Gdiplus;
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
    vec4(0.924f, 0.395f, 0.000f, 1.f),
    vec4(0.823f, 0.000f, 0.000f, 1.f),
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

HMODULE getCurrentModule() {
    HMODULE hModule = NULL;
    GetModuleHandleEx(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
        (LPCTSTR)getCurrentModule,
        &hModule);
    return hModule;
}

BITMAP loadImageFromResource(int resourceID) {
    HBITMAP hbitmap = NULL;
    ULONG_PTR token;
    Gdiplus::GdiplusStartupInput tmp;
    Gdiplus::GdiplusStartup(&token, &tmp, NULL);
    if (auto hres = FindResource(getCurrentModule(), MAKEINTRESOURCE(resourceID), RT_RCDATA))
        if (auto size = SizeofResource(getCurrentModule(), hres))
            if (auto data = LockResource(LoadResource(getCurrentModule(), hres)))
                if (auto stream = SHCreateMemStream((BYTE*)data, size))
                {
                    Gdiplus::Bitmap bmp(stream);
                    stream->Release();
                    bmp.GetHBITMAP(Gdiplus::Color::Transparent, &hbitmap);
                }
    Gdiplus::GdiplusShutdown(token);
    BITMAP bitmap;
    GetObject(hbitmap, sizeof(BITMAP), &bitmap);
    return bitmap;
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
    size_t idx;
    bool enabled;
    bool valid;
    bool advanced_view = false;
    char* infoLog = new char[512]{};
    int type;
    int grid_res;
    std::vector<unsigned int> indices;

    std::string defn = std::string(256, '\0');
    vec4 color;

    Graph(size_t idx, int type, std::string definition, int res, vec4 color, bool enabled, GLuint SSBO, GLuint EBO)
        : type(type), idx(idx), grid_res(res), color(color), enabled(enabled), SSBO(SSBO), EBO(EBO) {
        defn = definition;
    }

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

    void use_compute(float zoom, vec3 centerPos) const {
        glUseProgram(computeProgram);
        glUniform1f(glGetUniformLocation(computeProgram, "zoom"), zoom);
        glUniform1i(glGetUniformLocation(computeProgram, "grid_res"), grid_res);
        glUniform3fv(glGetUniformLocation(computeProgram, "centerPos"), 1, value_ptr(centerPos));
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, SSBO);
        glBufferData(GL_SHADER_STORAGE_BUFFER, pow(grid_res, 2) * (int)sizeof(float), nullptr, GL_DYNAMIC_DRAW);
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
    double zoomTimestamp = 0.f;
    float zoomSpeed = 1.f;
    float zoom = 8.f;
    float graph_size = 1.3f;
    bool gridLines = true;
    float gridLineDensity = 3.f;
    bool autoRotate = false;
    bool trace = true;
    bool tangent_plane = false, apply_tangent_plane = false;
    bool gradient_vector = false;
    vec3 centerPos = vec3(0.f);
    vec2 mousePos = vec2(0.f);
    int sidebarWidth = 300;
    bool updateBufferSize = false;
    double lastMousePress = 0.0;
    bool doubleClickPressed = false;
    int ssaa_factor = 3.f;
    bool ssaa = true;

    GLuint shaderProgram;
    GLuint VAO, VBO, EBO;
    GLuint FBO, gridSSBO;
    GLuint depthMap, frameTex, posBuffer, kernelBuffer;

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
            0x02C4, 0x02C4,
            0x02C5, 0x02C5,
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

        if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))) {
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
        glUniform3f(glGetUniformLocation(shaderProgram, "lightPos"), 0.f, 6.f, 0.f);
        glUniform3f(glGetUniformLocation(shaderProgram, "centerPos"), 0.f, 0.f, 0.f);

        const float radius = ssaa_factor;
        auto gaussian = [](float x, float mu, float sigma) -> float {
            const float a = (x - mu) / sigma;
            return std::exp(-0.5 * a * a);
        };
        const float sigma = radius / 2.f;
        int rowLength = 2 * radius + 1;
        std::vector<float> kernel(rowLength * rowLength);
        float sum = 0;
        for (uint64_t row = 0; row < rowLength; row++) {
            for (uint64_t col = 0; col < rowLength; col++) {
                float x = gaussian(row, radius, sigma) * gaussian(col, radius, sigma);
                kernel[row * rowLength + col] = x;
                sum += x;
            }
        }
        for (uint64_t row = 0; row < rowLength; row++) {
            for (uint64_t col = 0; col < rowLength; col++) {
                kernel[row * rowLength + col] /= sum;
            }
        }
        glGenBuffers(1, &kernelBuffer);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, kernelBuffer);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, kernelBuffer);
        glShaderStorageBlockBinding(shaderProgram, glGetProgramResourceIndex(shaderProgram, GL_SHADER_STORAGE_BLOCK, "kernel"), 2);
        glBufferData(GL_SHADER_STORAGE_BUFFER, kernel.size() * sizeof(float), kernel.data(), GL_STATIC_DRAW);
        glUniform1i(glGetUniformLocation(shaderProgram, "radius"), radius);

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
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, 1000 * ssaa_factor, 600 * ssaa_factor, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthMap, 0);

        glGenTextures(1, &frameTex);
        glBindTexture(GL_TEXTURE_2D, frameTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, 1000 * ssaa_factor, 600 * ssaa_factor, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, frameTex, 0);

        graphs.push_back(Graph(0, TangentPlane, "plane_params[0]+plane_params[1]*(x-plane_params[2])+plane_params[3]*(y-plane_params[4])", 100, vec4(0.f), false, gridSSBO, EBO));
        graphs[0].setup(true);
        graphs.push_back(Graph(1, UserDefined, "sin(x * y)", 500, colors[0], true, gridSSBO, EBO));
        graphs[1].setup(true);

        mainloop();
    }

private:
    static inline void on_windowResize(GLFWwindow* window, int width, int height) {
        Trisualizer* app = static_cast<Trisualizer*>(glfwGetWindowUserPointer(window));
        glBindTexture(GL_TEXTURE_2D, app->depthMap);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, width * app->ssaa_factor, height * app->ssaa_factor, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
        glBindTexture(GL_TEXTURE_2D, app->frameTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, width * app->ssaa_factor, height * app->ssaa_factor, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, app->posBuffer);
        glBufferData(GL_SHADER_STORAGE_BUFFER, 6 * (width - app->sidebarWidth) * height * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    }

    static inline void on_mouseButton(GLFWwindow* window, int button, int action, int mods) {
        Trisualizer* app = static_cast<Trisualizer*>(glfwGetWindowUserPointer(window));
        switch (button) {
        case GLFW_MOUSE_BUTTON_LEFT:
            switch (action) {
            case GLFW_RELEASE:
                if (app->updateBufferSize) {
                    int width, height;
                    glfwGetWindowSize(window, &width, &height);
                    glBindBuffer(GL_SHADER_STORAGE_BUFFER, app->posBuffer);
                    glBufferData(GL_SHADER_STORAGE_BUFFER, 6 * (width - app->sidebarWidth) * height * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
                    app->updateBufferSize = false;
                }
                break;
            case GLFW_PRESS:
                if (app->tangent_plane) {
                    app->apply_tangent_plane = true;
                }
                if (glfwGetTime() - app->lastMousePress < 0.2) {
                    app->doubleClickPressed = true;
                }
                app->lastMousePress = glfwGetTime();
                break;
            }
        }
    }

    static inline void on_mouseScroll(GLFWwindow* window, double x, double y) {
        Trisualizer* app = static_cast<Trisualizer*>(glfwGetWindowUserPointer(window));
        if (ImGui::GetIO().WantCaptureMouse) return;
        if (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS) {
            app->graph_size *= pow(0.9f, -y);
            glUniform1f(glGetUniformLocation(app->shaderProgram, "graph_size"), app->graph_size);
        }
        else {
            app->zoomSpeed = pow(0.9f, y);
            app->zoomTimestamp = glfwGetTime();
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
        else if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS) {
            float xoffset = x - app->mousePos.x;
            float yoffset = y - app->mousePos.y;
        }
        app->mousePos = { x, y };
    }
public:
    void mainloop() {
        double prevTime = glfwGetTime();
        mat4 view, proj;

        BITMAP tangentPlane_icon = loadImageFromResource(TNGTPLANE_ICON);
        unsigned int tangentPlane_texture = 0;
        glGenTextures(1, &tangentPlane_texture);
        glBindTexture(GL_TEXTURE_2D, tangentPlane_texture);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tangentPlane_icon.bmWidth, tangentPlane_icon.bmHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, (const void*)tangentPlane_icon.bmBits);

        BITMAP gradVec_icon = loadImageFromResource(GRADVEC_ICON);
        unsigned int gradVec_texture = 0;
        glGenTextures(1, &gradVec_texture);
        glBindTexture(GL_TEXTURE_2D, gradVec_texture);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, gradVec_icon.bmWidth, gradVec_icon.bmHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, (const void*)gradVec_icon.bmBits);

        
        BITMAP integral_icon = loadImageFromResource(INTEGRAL_ICON);
        unsigned int integral_texture = 0;
        glGenTextures(1, &integral_texture);
        glBindTexture(GL_TEXTURE_2D, integral_texture);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, integral_icon.bmWidth, integral_icon.bmHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, (const void*)integral_icon.bmBits);

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

            zoom *= zoomSpeed;
            glUniform1f(glGetUniformLocation(shaderProgram, "zoom"), zoom);
            zoomSpeed -= (zoomSpeed - 1.f) * min(timeStep * 10.f, 1.0);
            if (currentTime - zoomTimestamp > 1.0) zoomSpeed = 1.f; 

            ImGui::PushFont(font_title);

            bool aboutTrisualizerPopup = false;

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
                    ImGui::SeparatorText("Graphics");
                    if (ImGui::MenuItem("Anti-aliasing", nullptr, &ssaa)) {
                        if (ssaa) ssaa_factor = 3.f;
                        else ssaa_factor = 1.f;
                        on_windowResize(window, wWidth, wHeight);
                        glUniform1i(glGetUniformLocation(shaderProgram, "radius"), ssaa_factor);
                    }
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Help")) {
                    if (ImGui::MenuItem("About")) {
                        aboutTrisualizerPopup = true;
                    }
                    ImGui::EndMenu();
                }
                ImGui::EndMainMenuBar();
            }

            if (aboutTrisualizerPopup) {
                ImGui::OpenPopup("About Trisualizer");
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
            bool set_focus = false;
            if (ImGui::Button("New", ImVec2(50, 0))) {
                size_t i = graphs.size() - 1;
                graphs.push_back(Graph(graphs.size(), UserDefined, "", 500, colors[i % colors.size()], false, gridSSBO, EBO));
                graphs[graphs.size() - 1].setup(false);
                set_focus = true;
            }
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.4f, 0.4f, 1.f));
            ImGui::Text("FPS: %.1f", 1.0 / timeStep);
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
                ImGui::PushItemWidth((vMax.x - vMin.x) - 85);
                if (i == graphs.size() - 1 && set_focus) {
                    ImGui::SetKeyboardFocusHere(0);
                }
                if (ImGui::InputText(std::format("##defn{}", i).c_str(), graphs[i].defn.data(), 256, ImGuiInputTextFlags_EnterReturnsTrue)) {
                    graphs[i].upload_definition();
                    if (graphs[i].valid) graphs[i].enabled = true;
                }
                ImGui::SameLine();
                if (ImGui::Button(graphs[i].advanced_view ? u8"˄" : u8"˅", ImVec2(16, 0))) {
                    graphs[i].advanced_view ^= 1;
                }
                ImGui::SameLine();
                size_t logLength = strlen(graphs[i].infoLog);
                if (ImGui::Button("x", ImVec2(16, 0))) {
                    graphs.erase(graphs.begin() + i);
                }
                else if (!graphs[i].valid && logLength > 0) {
                    int nLines = 1;
                    for (int j = 0; j < logLength; j++) if (graphs[i].infoLog[j] == '\n') nLines++;
                    if (graphs[i].infoLog[logLength - 1] == '\n') graphs[i].infoLog[logLength - 1] = '\0';
                    ImGui::InputTextMultiline("##errorlist", graphs[i].infoLog, 512, ImVec2((vMax.x - vMin.x), 11 * nLines + 6), ImGuiInputTextFlags_ReadOnly);
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
            static vec3 gotoLocation;
            ImGui::InputFloat3("##goto", value_ptr(gotoLocation));
            ImGui::SameLine();
            if (ImGui::Button("Jump", ImVec2(vMax.x - vMin.x - ImGui::GetCursorPosX() + 5, 0))) {
                centerPos = gotoLocation;
                glUniform3fv(glGetUniformLocation(shaderProgram, "centerPos"), 1, value_ptr(centerPos));
            }
            float buttonWidth = (vMax.x - vMin.x - 61.f) / 4.f;

            if (gradient_vector) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.30f, 0.32f, 0.33f, 1.00f));
            if (ImGui::ImageButton("gradient_vector", (void*)(intptr_t)gradVec_texture, ImVec2(buttonWidth, 30), ImVec2(-(buttonWidth - 30.f) / 60.f, 0.f), ImVec2(1.f + (buttonWidth - 30.f) / 60.f, 1.f), ImVec4(0.0f, 0.0f, 0.0f, 0.0f), ImVec4(1.0f, 1.0f, 1.0f, 1.0f))) {
                gradient_vector ^= 1;
                tangent_plane = false;
                if (!gradient_vector) ImGui::PopStyleColor();
            }
            else if (gradient_vector) ImGui::PopStyleColor();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_NoSharedDelay))
                ImGui::SetTooltip("Gradient Vector", ImGui::GetStyle().HoverDelayNormal);

            ImGui::SameLine();
            if (tangent_plane) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.30f, 0.32f, 0.33f, 1.00f));
            if (ImGui::ImageButton("tangent_plane", (void*)(intptr_t)tangentPlane_texture, ImVec2(buttonWidth, 30), ImVec2(-(buttonWidth - 30.f) / 60.f, 0.0f), ImVec2(1.f + (buttonWidth - 30.f) / 60.f, 1.f), ImVec4(0.0f, 0.0f, 0.0f, 0.0f), ImVec4(1.0f, 1.0f, 1.0f, 1.0f))) {
                tangent_plane ^= 1;
                gradient_vector = false;
                if (!tangent_plane) ImGui::PopStyleColor();
            }
            else if (tangent_plane) ImGui::PopStyleColor();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_NoSharedDelay))
                ImGui::SetTooltip("Tangent Plane", ImGui::GetStyle().HoverDelayNormal);

            ImGui::SameLine();
            if (ImGui::ImageButton("min_max", (void*)(intptr_t)tangentPlane_texture, ImVec2(buttonWidth, 30), ImVec2(-(buttonWidth - 30.f) / 60.f, 0.0f), ImVec2(1.f + (buttonWidth - 30.f) / 60.f, 1.f), ImVec4(0.0f, 0.0f, 0.0f, 0.0f), ImVec4(1.0f, 1.0f, 1.0f, 1.0f))) {

            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_NoSharedDelay))
                ImGui::SetTooltip("Local Minima/Maxima", ImGui::GetStyle().HoverDelayNormal);
            ImGui::SameLine();
            if (ImGui::ImageButton("integral", (void*)(intptr_t)integral_texture, ImVec2(buttonWidth, 30), ImVec2(-(buttonWidth - 30.f) / 60.f, 0.0f), ImVec2(1.f + (buttonWidth - 30.f) / 60.f, 1.f), ImVec4(0.0f, 0.0f, 0.0f, 0.0f), ImVec4(1.0f, 1.0f, 1.0f, 1.0f))) {

            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_NoSharedDelay))
                ImGui::SetTooltip("Double Integral", ImGui::GetStyle().HoverDelayNormal);

            ImGui::End();

            double x, y;
            glfwGetCursorPos(window, &x, &y);
            if (graphs.size() > 0 && x - sidebarWidth > 0. && x - sidebarWidth < (wWidth - sidebarWidth) && y > 0. && y < wHeight) {
                float data[1];
                glBindFramebuffer(GL_FRAMEBUFFER, FBO);
                glBindTexture(GL_TEXTURE_2D, depthMap);
                glReadPixels(x * ssaa_factor, (wHeight - y) * ssaa_factor, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, data);
                glBindFramebuffer(GL_FRAMEBUFFER, NULL);

                if (data[0] != 1.f) {
                    glBindBuffer(GL_SHADER_STORAGE_BUFFER, posBuffer);
                    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
                    float data[6];
                    glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, static_cast<int>(6 * (wHeight * (wHeight - y) + x)) * sizeof(float), 6 * sizeof(float), data);
                    vec3 fragPos = { data[0], data[1], data[2] };
                    int index = static_cast<int>(data[3]);
                    vec2 gradvec = { data[4], data[5] };

                    if (index >= graphs.size()) {
                        goto mouse_not_on_graph;
                    }
                    ImGui::SetNextWindowPos(ImVec2(x + 10.f, y));
                    ImGui::Begin("info", nullptr,
                        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar |
                        ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_AlwaysAutoResize |
                        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar);

                    vec4 c = graphs[index].color * 1.3f;
                    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 9.f);
                    ImGui::ColorEdit4("##infocolor", value_ptr(c), ImGuiColorEditFlags_NoInputs);
                    ImGui::SameLine();
                    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 11.f);
                    ImGui::Text(u8"X=%6.3f\nY=%6.3f\nZ=%6.3f", fragPos.x, fragPos.y, fragPos.z);
                    ImGui::SameLine();
                    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 5.f);
                    ImGui::Text(u8"\u2202z/\u2202x=%6.3f\n\u2202z/\u2202y=%6.3f", gradvec.x, gradvec.y);
                    ImVec2 prevWindowSize = ImGui::GetWindowSize();
                    ImGui::End();

                    GLfloat params[5] = { fragPos.z, gradvec.x, fragPos.x, gradvec.y, fragPos.y };
                    if (tangent_plane) {
                        glUseProgram(graphs[0].computeProgram);
                        glUniform1fv(glGetUniformLocation(graphs[0].computeProgram, "plane_params"), 5, params);
                        graphs[0].enabled = true;
                        vec4 nc = colors[(graphs.size() - 1) % colors.size()];
                        graphs[0].color = vec4(nc.r, nc.g, nc.b, 0.4f);
                        glUseProgram(shaderProgram);
                        ImGui::SetNextWindowPos(ImVec2(x + 10.f, y + prevWindowSize.y + 3.f));
                        ImGui::Begin("tooltip", nullptr,
                            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar |
                            ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_AlwaysAutoResize |
                            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar);
                        ImGui::Text("Left-click to save tangent plane");
                        ImGui::End();
                    }
                    if (apply_tangent_plane) {
                        const char* eq = "(%.6f)+(%.6f)*(x-(%.6f))+(%.6f)*(y-(%.6f))";
                        char eqf[88]{};
                        sprintf_s(eqf, eq, params[0], params[1], params[2], params[3], params[4]);
                        graphs.push_back(Graph(graphs.size(), UserDefined, eqf, 100, colors[(graphs.size() - 1) % colors.size()], true, gridSSBO, EBO));
                        graphs[graphs.size() - 1].setup(true);
                        apply_tangent_plane = false;
                        tangent_plane = false;
                        graphs[0].enabled = false;
                    }
                    if (doubleClickPressed) {
                        centerPos = fragPos;
                        glUniform3fv(glGetUniformLocation(shaderProgram, "centerPos"), 1, value_ptr(centerPos));
                        doubleClickPressed = false;
                    }
                } else goto mouse_not_on_graph;
            } else {
            mouse_not_on_graph:
                graphs[0].enabled = false;
                apply_tangent_plane = false;
                doubleClickPressed = false;
            }

            if (ImGui::BeginPopupModal("About Trisualizer", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
                ImGui::Text("Version v" VERSION " (Build date: " __DATE__ " " __TIME__ ")\n\nTrisualizer is a two-variable function grapher");
                ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(150, 150, 150, 255));
                ImGui::Text(u8"Copyright © 2017-2024 Yilmaz Alpaslan");
                ImGui::PopStyleColor();
                if (ImGui::Button("Open GitHub Page"))
                    ShellExecuteW(0, 0, L"https://github.com/Yilmaz4/Trisualizer", 0, 0, SW_SHOW);
                ImGui::SameLine();
                if (ImGui::Button("Close"))
                    ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }

            //ImGui::ShowDemoWindow();

            ImGui::PopFont();

            if (autoRotate)
                phi += timeStep * 5.f;

            auto cameraPos = vec3(sin(glm::radians(theta)) * cos(glm::radians(phi)), cos(glm::radians(theta)), sin(glm::radians(theta)) * sin(glm::radians(phi)));
            view = lookAt(cameraPos, vec3(0.f), { 0.f, 1.f, 0.f });
            proj = ortho(-1.f, 1.f, -(float)wHeight / (float)(wWidth - sidebarWidth), (float)wHeight / (float)(wWidth - sidebarWidth), -10.f, 10.f);
            glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "vpmat"), 1, GL_FALSE, value_ptr(proj * view));
            glUniform3fv(glGetUniformLocation(shaderProgram, "cameraPos"), 1, value_ptr(cameraPos));

            ImGui::Render();

            glClearColor(0.05f, 0.05f, 0.05f, 1.f);
            glBindFramebuffer(GL_FRAMEBUFFER, NULL);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            glBindFramebuffer(GL_FRAMEBUFFER, FBO);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            
            glViewport(sidebarWidth * ssaa_factor, 0, (wWidth - sidebarWidth) * ssaa_factor, wHeight * ssaa_factor);
            glUniform2i(glGetUniformLocation(shaderProgram, "regionSize"), wWidth * ssaa_factor - sidebarWidth * ssaa_factor, wHeight * ssaa_factor);
            glUniform2i(glGetUniformLocation(shaderProgram, "windowSize"), wWidth * ssaa_factor, wHeight * ssaa_factor);

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
                glDrawElements(GL_TRIANGLE_STRIP, (GLsizei)g.indices.size(), GL_UNSIGNED_INT, 0);
                if (i == 0) break;
            }

            glViewport(sidebarWidth, 0, wWidth - sidebarWidth, wHeight);

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