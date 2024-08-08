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
#include <regex>

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
    char symbol[32];
    bool config = true;
    bool valid = true;
    std::vector<bool> used_in;
    char infoLog[128];

    Slider(float defval, float min, float max, const char* sym) : value(defval), min(min), max(max) {
        strcpy_s(symbol, sym);
    }
};

enum GraphType {
    UserDefined,
    TangentPlane,
};

class Graph {
public:
    GLuint computeProgram = NULL, SSBO, EBO;
    size_t idx;
    bool enabled = false;
    bool valid = false;
    bool advanced_view = false;
    bool grid_lines = true;
    float shininess = 16;
    char* infoLog = new char[512]{};
    int type;
    int grid_res;
    std::vector<unsigned int> indices;

    char defn[256]{};
    vec4 color;
    vec4 secondary_color;

    Graph(size_t idx, int type, const char* definition, int res, vec4 color, vec4 color2, bool enabled, GLuint SSBO, GLuint EBO)
        : type(type), idx(idx), grid_res(res), color(color), secondary_color(color2), enabled(enabled), SSBO(SSBO), EBO(EBO) {
        strcpy_s(defn, definition);
    }

    void setup() {
        indices.clear();
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
    }
    
    void upload_definition(std::vector<Slider>& sliders, const char* regionBool = "true", bool polar = false) {
        int success;

        std::string pdefn = defn;
        pdefn.resize(512);
        for (int i = 0; i < sliders.size(); i++) {
            if (!sliders[i].valid) continue;
            std::string pattern = "\\b";
            pattern.append(sliders[i].symbol);
            pattern.append("\\b");
            std::string temp = std::regex_replace(pdefn, std::regex(pattern), std::format("sliders[{}]", i));
            sliders[i].used_in[idx] = (pdefn != temp);
            pdefn = temp;
        }
        unsigned int computeShader = glCreateShader(GL_COMPUTE_SHADER);
        char* computeSource = read_resource(IDR_CMPT);
        size_t size = strlen(computeSource) + 512;
        char* modifiedSource = new char[size];
        sprintf_s(modifiedSource, size, computeSource, polar ? "" : "//", pdefn.c_str(), regionBool);
        glShaderSource(computeShader, 1, &modifiedSource, NULL);
        glCompileShader(computeShader);
        glGetShaderiv(computeShader, GL_COMPILE_STATUS, &success);
        if (!success) {
            char temp[512];
            glGetShaderInfoLog(computeShader, 512, NULL, temp);
            int k = 0;
            for (int i = 0, j = 0; i < strlen(temp); i++, j++) {
                if (j < 21) continue; // omit GLSL details
                infoLog[k++] = temp[i];
                if (temp[i] == '\n') j = -1;
            }
            infoLog[k] = '\0';
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
        glShaderStorageBlockBinding(computeProgram, glGetProgramResourceIndex(computeProgram, GL_SHADER_STORAGE_BLOCK, "sliderbuffer"), 3);
        if (!valid) enabled = true;
        valid = true;
    }

    void use_compute(float zoomx, float zoomy, float zoomz, vec3 centerPos) const {
        glUseProgram(computeProgram);
        glUniform1f(glGetUniformLocation(computeProgram, "zoomx"), zoomx);
        glUniform1f(glGetUniformLocation(computeProgram, "zoomy"), zoomy);
        glUniform1f(glGetUniformLocation(computeProgram, "zoomz"), zoomz);
        glUniform1i(glGetUniformLocation(computeProgram, "grid_res"), grid_res + 2);
        glUniform3fv(glGetUniformLocation(computeProgram, "centerPos"), 1, value_ptr(centerPos));
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, SSBO);
        glBufferData(GL_SHADER_STORAGE_BUFFER, 2 * pow(grid_res + 2, 2) * (int)sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    }
    void use_shader() const {
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_DYNAMIC_DRAW);
    }
};

enum RegionType {
    CartesianRectangle,
    Type1, 
    Type2,
    Polar,
};

enum ColoringStyle {
    SingleColor,
    TopBottom,
    Elevation,
    Slope,
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
    float zoomx = 8.f;
    float zoomy = 8.f;
    float zoomz = 8.f;
    float graph_size = 1.3f;
    bool gridLines = true;
    float gridLineDensity = 3.f;
    bool shading = true;
    int coloring = SingleColor;
    bool autoRotate = false;
    bool trace = true;
    bool tangent_plane = false, apply_tangent_plane = false;
    bool gradient_vector = false;
    vec2 xrange, yrange, zrange;

    bool integral = false, second_corner = false, apply_integral = false, show_integral_result = false;
    int integrand_index = -1, region_type = CartesianRectangle, integral_precision = 2000, erroring_eq = -1;
    float x_min, x_max, y_min, y_max, theta_min, theta_max;
    char x_min_eq[32], x_max_eq[32], y_min_eq[32], y_max_eq[32], r_min_eq[32], r_max_eq[32], integral_infoLog[512];
    float x_min_eq_min, x_max_eq_max, y_min_eq_min, y_max_eq_max;
    vec3 center_of_region;
    float integral_result, middle_height, dx, dy;

    std::pair<vec3, vec3> integral_limits;
    vec3 centerPos = vec3(0.f);
    vec2 mousePos = vec2(0.f);
    int sidebarWidth = 300;
    bool updateBufferSize = false;
    double lastMousePress = 0.0;
    bool doubleClickPressed = false;
    int ssaa_factor = 3.f; // change to 3.f when enabling SSAA by default
    bool ssaa = true;

    GLuint shaderProgram;
    GLuint VAO, VBO, EBO;
    GLuint FBO, gridSSBO;
    GLuint depthMap, frameTex, prevZBuffer, posBuffer, kernelBuffer, sliderBuffer;

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
            0x0020, 0x2264, 0xFFFF
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
        glBufferData(GL_SHADER_STORAGE_BUFFER, 6 * 700 * 600 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, posBuffer);
        glShaderStorageBlockBinding(shaderProgram, glGetProgramResourceIndex(shaderProgram, GL_SHADER_STORAGE_BLOCK, "posbuffer"), 1);

        const float radius = 3.f;
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
        glUniform1i(glGetUniformLocation(shaderProgram, "radius"), ssaa_factor);

        glGenBuffers(1, &sliderBuffer);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, sliderBuffer);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, sliderBuffer);

        glUniform1f(glGetUniformLocation(shaderProgram, "zoomx"), zoomx);
        glUniform1f(glGetUniformLocation(shaderProgram, "zoomy"), zoomy);
        glUniform1f(glGetUniformLocation(shaderProgram, "zoomz"), zoomz);
        glUniform1f(glGetUniformLocation(shaderProgram, "graph_size"), graph_size);
        glUniform1f(glGetUniformLocation(shaderProgram, "ambientStrength"), 0.2f);
        glUniform1f(glGetUniformLocation(shaderProgram, "gridLineDensity"), gridLineDensity);
        glUniform1i(glGetUniformLocation(shaderProgram, "shading"), shading);
        glUniform3f(glGetUniformLocation(shaderProgram, "lightPos"), 0.f, 50.f, 0.f);
        glUniform3f(glGetUniformLocation(shaderProgram, "centerPos"), 0.f, 0.f, 0.f);
        glUniform1i(glGetUniformLocation(shaderProgram, "coloring"), SingleColor);

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

        glGenTextures(1, &prevZBuffer);
        glBindTexture(GL_TEXTURE_2D, prevZBuffer);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, 1000 * ssaa_factor, 600 * ssaa_factor, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

        graphs.push_back(Graph(0, TangentPlane, "plane_params[0]+plane_params[1]*(x-plane_params[2])+plane_params[3]*(y-plane_params[4])", 100, vec4(0.f), vec4(0.f), false, gridSSBO, EBO));
        graphs[0].setup();
        graphs[0].upload_definition(sliders);
        graphs.push_back(Graph(1, UserDefined, "sin(x * y)", 500, colors[0], colors[1], true, gridSSBO, EBO));
        graphs[1].setup();
        graphs[1].upload_definition(sliders);

        mainloop();
    }

private:
    static inline void on_windowResize(GLFWwindow* window, int width, int height) {
        Trisualizer* app = static_cast<Trisualizer*>(glfwGetWindowUserPointer(window));
        glBindTexture(GL_TEXTURE_2D, app->depthMap);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, width * app->ssaa_factor, height * app->ssaa_factor, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
        glBindTexture(GL_TEXTURE_2D, app->frameTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, width * app->ssaa_factor, height * app->ssaa_factor, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glBindTexture(GL_TEXTURE_2D, app->prevZBuffer);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, width * app->ssaa_factor, height * app->ssaa_factor, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
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
                if (app->tangent_plane)
                    app->apply_tangent_plane = true;
                if (app->integral && !app->show_integral_result)
                    app->apply_integral = true;
                if (glfwGetTime() - app->lastMousePress < 0.2)
                    app->doubleClickPressed = true;
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
            app->zoomSpeed = pow(0.95f, y);
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

    std::pair<float, float> min_max(char var, const char* func, float rbegin, float rend, int samplesize, char* infoLog) {
        unsigned int computeShader = glCreateShader(GL_COMPUTE_SHADER);
        const char* computeSource = R"glsl(
#version 460 core

layout(local_size_x = 1, local_size_y = 1, local_size_z = 1) in;

layout(std430, binding = 4) volatile buffer samplebuffer {
	float samples[];
};
uniform int samplesize;
uniform float rbegin;
uniform float rend;

void main() {
	float %c = rbegin + ((rend - rbegin) / samplesize) * float(gl_GlobalInvocationID.x);
    samples[gl_GlobalInvocationID.x] = float(%s);
})glsl";
        size_t size = strlen(computeSource) + 256;
        char* modifiedSource = new char[size];
        sprintf_s(modifiedSource, size, computeSource, var, func);
        glShaderSource(computeShader, 1, &modifiedSource, NULL);
        glCompileShader(computeShader);
        int success;
        glGetShaderiv(computeShader, GL_COMPILE_STATUS, &success);
        if (!success) {
            char temp[512];
            glGetShaderInfoLog(computeShader, 512, NULL, temp);
            int k = 0;
            for (int i = 0, j = 0; i < strlen(temp); i++, j++) {
                if (j < 21) continue;
                infoLog[k++] = temp[i];
                if (temp[i] == '\n') j = -1;
            }
            infoLog[k] = '\0';
            return std::pair(std::numeric_limits<float>::quiet_NaN(), 0.f);
        }
        GLuint computeProgram = glCreateProgram();
        glAttachShader(computeProgram, computeShader);
        glLinkProgram(computeProgram);
        glDeleteShader(computeShader);
        glUseProgram(computeProgram);
        delete[] modifiedSource;

        GLuint sampleBuffer;
        glGenBuffers(1, &sampleBuffer);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, sampleBuffer);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, sampleBuffer);
        glShaderStorageBlockBinding(computeProgram, glGetProgramResourceIndex(computeProgram, GL_SHADER_STORAGE_BLOCK, "samplebuffer"), 4);
        glBufferData(GL_SHADER_STORAGE_BUFFER, samplesize * sizeof(float), nullptr, GL_STATIC_DRAW);

        glUniform1i(glGetUniformLocation(computeProgram, "samplesize"), samplesize);
        glUniform1f(glGetUniformLocation(computeProgram, "rbegin"), rbegin);
        glUniform1f(glGetUniformLocation(computeProgram, "rend"), rend);

        glDispatchCompute(samplesize, 1, 1);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
        float* data = new float[samplesize]{};
        glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, samplesize * sizeof(float), data);
        float min = std::numeric_limits<float>::max(), max = std::numeric_limits<float>::min();
        for (int i = 0; i < samplesize; i++) {
            float s = data[i];
            if (isnan(s) || isinf(s)) continue;
            if (s < min) min = s;
            if (s > max) max = s;
        }
        delete[] data;
        glDeleteProgram(computeProgram);
        glDeleteBuffers(1, &sampleBuffer);
        return std::pair(min, max);
    }

    float coterminal_angle(float angle) {
        if (angle < 0.f) while (angle < 0.f) angle += 2 * M_PI;
        else if (angle > 2 * M_PI) while (angle > 2 * M_PI) angle -= 2 * M_PI;
        return angle;
    }

    int compute_integral(char* infoLog) {
        Graph g = graphs[integrand_index];
        g.grid_res = integral_precision;
        float xmin, xmax, ymin, ymax;
        char regionBool[256];
        switch (region_type) {
        case CartesianRectangle:
            xmin = x_min, xmax = x_max, ymin = y_min, ymax = y_max;
            sprintf_s(regionBool, "float(%.9f) <= x && x <= float(%.9f) && float(%.9f) <= y && y <= float(%.9f)", xmin, xmax, ymin, ymax);
            break;
        case Type1: {
            xmin = x_min, xmax = x_max;
            std::pair<float, float> yminbounds = min_max('x', y_min_eq, xmin, xmax, g.grid_res, infoLog);
            std::pair<float, float> ymaxbounds = min_max('x', y_max_eq, xmin, xmax, g.grid_res, infoLog);
            if (isnan(yminbounds.first)) return 0;
            if (isnan(ymaxbounds.first)) return 1;
            ymin = y_min_eq_min = yminbounds.first, ymax = y_max_eq_max = ymaxbounds.second;
            sprintf_s(regionBool, "float(%s) <= y && y <= float(%s) && float(%.9f) <= x && x <= float(%.9f)", y_min_eq, y_max_eq, xmin, xmax);
            break;
        }
        case Type2: {
            ymin = y_min, ymax = y_max;
            std::pair<float, float> xminbounds = min_max('y', x_min_eq, ymin, ymax, g.grid_res, infoLog);
            std::pair<float, float> xmaxbounds = min_max('y', x_max_eq, ymin, ymax, g.grid_res, infoLog);
            if (isnan(xminbounds.first)) return 0;
            if (isnan(xmaxbounds.first)) return 1;
            xmin = x_min_eq_min = xminbounds.first, xmax = x_max_eq_max = xmaxbounds.second;
            sprintf_s(regionBool, "float(%s) <= x && x <= float(%s) && float(%.9f) <= y && y <= float(%.9f)", x_min_eq, x_max_eq, ymin, ymax);
            break;
        }
        case Polar: {
            std::pair<float, float> rminbounds = min_max('t', r_min_eq, theta_min, theta_max, g.grid_res, infoLog);
            std::pair<float, float> rmaxbounds = min_max('t', r_max_eq, theta_min, theta_max, g.grid_res, infoLog);
            if (isnan(rminbounds.first)) return 0;
            if (isnan(rmaxbounds.first)) return 1;
            xmax = ymax = rmaxbounds.second;
            xmin = ymin = -rmaxbounds.second;
            sprintf_s(regionBool, "float(%s) <= sqrt(x*x+y*y) && sqrt(x*x+y*y) <= float(%s) && float(%.9f) <= t && t <= float(%.9f)", r_min_eq, r_max_eq, theta_min, theta_max);
        }}
        g.upload_definition(sliders, regionBool, region_type == Polar);

        center_of_region = vec3((xmax + xmin) / 2.f, (ymax + ymin) / 2.f, 0.f);
        glUniform1f(glGetUniformLocation(g.computeProgram, "zoomx"), abs(xmax - xmin));
        glUniform1f(glGetUniformLocation(g.computeProgram, "zoomy"), abs(ymax - ymin));
        glUniform1f(glGetUniformLocation(g.computeProgram, "zoomz"), 1.f);
        glUniform1i(glGetUniformLocation(g.computeProgram, "grid_res"), g.grid_res);
        glUniform3fv(glGetUniformLocation(g.computeProgram, "centerPos"), 1, value_ptr(center_of_region));
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, g.SSBO);
        glBufferData(GL_SHADER_STORAGE_BUFFER, 2 * g.grid_res * g.grid_res * (int)sizeof(float), nullptr, GL_DYNAMIC_DRAW);
        glDispatchCompute(g.grid_res, g.grid_res, 1);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
        float* data = new float[2 * g.grid_res * g.grid_res]{};
        glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, 2 * g.grid_res * g.grid_res * sizeof(float), data);
        dx = abs(xmax - xmin) / g.grid_res;
        dy = abs(ymax - ymin) / g.grid_res;
        integral_result = 0.f;
        for (int i = 0; i < 2 * g.grid_res * g.grid_res; i += 2) {
            float val = data[i];
            bool in_region = static_cast<bool>(data[i + 1]);
            if (isnan(val) || isinf(val) || !in_region) continue;
            integral_result += val * dx * dy;
        }
        middle_height = data[g.grid_res * (g.grid_res / 2) + (g.grid_res / 2)];
        delete[] data;

        graphs[integrand_index].upload_definition(sliders, regionBool, region_type == Polar);
        return -1;
    }
public:
    void mainloop() {
        double prevTime = glfwGetTime();
        mat4 view, proj;

        BITMAP tangentPlane_icon = loadImageFromResource(TNGTPLANE_ICON);
        GLuint tangentPlane_texture = 0;
        glGenTextures(1, &tangentPlane_texture);
        glBindTexture(GL_TEXTURE_2D, tangentPlane_texture);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tangentPlane_icon.bmWidth, tangentPlane_icon.bmHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, (const void*)tangentPlane_icon.bmBits);

        BITMAP gradVec_icon = loadImageFromResource(GRADVEC_ICON);
        GLuint gradVec_texture = 0;
        glGenTextures(1, &gradVec_texture);
        glBindTexture(GL_TEXTURE_2D, gradVec_texture);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, gradVec_icon.bmWidth, gradVec_icon.bmHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, (const void*)gradVec_icon.bmBits);

        BITMAP normVec_icon = loadImageFromResource(NORMVEC_ICON);
        GLuint normVec_texture = 0;
        glGenTextures(1, &normVec_texture);
        glBindTexture(GL_TEXTURE_2D, normVec_texture);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, normVec_icon.bmWidth, normVec_icon.bmHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, (const void*)normVec_icon.bmBits);
        
        BITMAP integral_icon = loadImageFromResource(INTEGRAL_ICON);
        GLuint integral_texture = 0;
        glGenTextures(1, &integral_texture);
        glBindTexture(GL_TEXTURE_2D, integral_texture);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, integral_icon.bmWidth, integral_icon.bmHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, (const void*)integral_icon.bmBits);

        GLuint srcFBO, dstFBO;
        glGenFramebuffers(1, &srcFBO);
        glGenFramebuffers(1, &dstFBO);
        glBindFramebuffer(GL_FRAMEBUFFER, srcFBO);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthMap, 0);
        glBindFramebuffer(GL_FRAMEBUFFER, dstFBO);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, prevZBuffer, 0);

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

            zoomx *= zoomSpeed;
            zoomy *= zoomSpeed;
            zoomz *= zoomSpeed;
            xrange = vec2(zoomx / 2.f, -zoomx / 2.f) + centerPos.x;
            yrange = vec2(zoomy / 2.f, -zoomy / 2.f) + centerPos.y;
            zrange = vec2(zoomz / 2.f, -zoomz / 2.f) + centerPos.z;
            glUniform1f(glGetUniformLocation(shaderProgram, "zoomx"), zoomx);
            glUniform1f(glGetUniformLocation(shaderProgram, "zoomy"), zoomy);
            glUniform1f(glGetUniformLocation(shaderProgram, "zoomz"), zoomz);
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
                    ImGui::SeparatorText("Graphing");
                    if (ImGui::MenuItem("Single Color", nullptr, coloring == SingleColor)) {
                        coloring = SingleColor;
                        glUniform1i(glGetUniformLocation(shaderProgram, "coloring"), coloring);
                    }
                    if (ImGui::MenuItem("Top/Bottom", nullptr, coloring == TopBottom)) {
                        coloring = TopBottom;
                        glUniform1i(glGetUniformLocation(shaderProgram, "coloring"), coloring);
                    }
                    if (ImGui::MenuItem("Elevation", nullptr, coloring == Elevation)) {
                        coloring = Elevation;
                        glUniform1i(glGetUniformLocation(shaderProgram, "coloring"), coloring);
                    }
                    if (ImGui::MenuItem("Slope", nullptr, coloring == Slope)) {
                        coloring = Slope;
                        glUniform1i(glGetUniformLocation(shaderProgram, "coloring"), coloring);
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Anti-aliasing", nullptr, &ssaa)) {
                        if (ssaa) ssaa_factor = 3.f;
                        else ssaa_factor = 1.f;
                        on_windowResize(window, wWidth, wHeight);
                        glUniform1i(glGetUniformLocation(shaderProgram, "radius"), ssaa_factor);
                    }
                    if (ImGui::MenuItem("Shading", nullptr, &shading)) {
                        glUniform1i(glGetUniformLocation(shaderProgram, "shading"), shading);
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
            if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable) {
                ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
                ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), dockspace_flags);

                static bool init = true;
                if (init) {
                    init = false;

                    ImGui::DockBuilderRemoveNode(dockspace_id);
                    ImGui::DockBuilderAddNode(dockspace_id, dockspace_flags | ImGuiDockNodeFlags_DockSpace | ImGuiDockNodeFlags_NoTabBar | ImGuiDockNodeFlags_NoResize | ImGuiDockNodeFlags_NoResizeX);
                    ImGui::DockBuilderSetNodeSize(dockspace_id, viewport->Size);

                    auto dock_id_left = ImGui::DockBuilderSplitNode(dockspace_id, ImGuiDir_Left, 0.3f, nullptr, &dockspace_id);
                    auto dock_id_middle = ImGui::DockBuilderSplitNode(dock_id_left, ImGuiDir_Down, 0.6f, nullptr, &dock_id_left);
                    auto dock_id_down = ImGui::DockBuilderSplitNode(dock_id_middle, ImGuiDir_Down, 0.51f, nullptr, &dock_id_middle);

                    ImGui::DockBuilderDockWindow("Symbolic View", dock_id_left);
                    ImGui::DockBuilderDockWindow("Variables", dock_id_middle);
                    ImGui::DockBuilderDockWindow("Tools", dock_id_down);
                    ImGui::DockBuilderFinish(dockspace_id);
                }
            }
            ImGui::End();

            ImVec2 vMin, vMax;

            ImGuiWindowClass window_class;
            window_class.DockNodeFlagsOverrideSet = ImGuiDockNodeFlags_NoTabBar;
            ImGui::SetNextWindowClass(&window_class);
            ImGui::Begin("Symbolic View", nullptr, ImGuiWindowFlags_NoMove);

            float sw = ImGui::GetWindowSize().x;
            if (sw != sidebarWidth) {
                sidebarWidth = sw;
                updateBufferSize = true;
            }
            bool set_focus = false;
            if (ImGui::Button("New function", ImVec2(100, 0))) {
                size_t i = graphs.size() - 1;
                graphs.push_back(Graph(graphs.size(), UserDefined, "", 500, colors[i % colors.size()], colors[(i + 1) % colors.size()], false, gridSSBO, EBO));
                graphs[graphs.size() - 1].setup();
                for (Slider& s : sliders) {
                    s.used_in.push_back(false);
                }
                set_focus = true;
            }
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.4f, 0.4f, 1.f));
            ImGui::Text("FPS: %.1f", 1.0 / timeStep);
            ImGui::PopStyleColor();
            for (int i = 0; i < graphs.size(); i++) {
                if (graphs[i].type != UserDefined) continue;
                Graph& g = graphs[i];
                ImGui::BeginChild(std::format("##child{}", i).c_str(), ImVec2(0, 0), ImGuiChildFlags_Border | ImGuiChildFlags_AlwaysAutoResize | ImGuiChildFlags_AutoResizeY);
                ImVec2 vMin = ImGui::GetWindowContentRegionMin() + ImGui::GetWindowPos();
                ImVec2 vMax = ImGui::GetWindowContentRegionMax() + ImGui::GetWindowPos();
                ImGui::BeginDisabled(!g.valid);
                ImGui::Checkbox(std::format("##check{}", i).c_str(), &g.enabled);
                ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::ColorEdit4(std::format("##color{}", i).c_str(), value_ptr(g.color), ImGuiColorEditFlags_NoInputs);
                ImGui::SameLine();
                if (coloring != SingleColor) {
                    ImGui::ColorEdit4(std::format("##color2{}", i).c_str(), value_ptr(g.secondary_color), ImGuiColorEditFlags_NoInputs);
                    ImGui::SameLine();
                }
                ImGui::PushItemWidth((vMax.x - vMin.x) - 86 - (coloring != SingleColor ? 21 : 0));
                if (i == graphs.size() - 1 && set_focus) {
                    ImGui::SetKeyboardFocusHere(0);
                }
                if (ImGui::InputText(std::format("##defn{}", i).c_str(), g.defn, 256)) {
                    g.upload_definition(sliders);
                    if (g.valid) g.enabled = true;
                    if (i == integrand_index) {
                        glUniform1i(glGetUniformLocation(shaderProgram, "integral"), false);
                        integral = show_integral_result = apply_integral = second_corner = false;
                        if (integrand_index != -1) graphs[integrand_index].upload_definition(sliders);
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button(g.advanced_view ? u8"˄" : u8"˅", ImVec2(16, 0))) {
                    g.advanced_view ^= 1;
                }
                ImGui::SameLine();
                if (ImGui::Button("x", ImVec2(16, 0))) {
                    if (i == integrand_index) {
                        glUniform1i(glGetUniformLocation(shaderProgram, "integral"), false);
                        integral = show_integral_result = apply_integral = second_corner = false;
                        if (integrand_index != -1) graphs[integrand_index].upload_definition(sliders);
                    }
                    graphs.erase(graphs.begin() + i);
                    for (Slider& s : sliders) {
                        s.used_in.erase(s.used_in.begin() + i);
                    }
                    ImGui::EndChild();
                    continue;
                }
                size_t logLength = strlen(g.infoLog);
                if (!g.valid && logLength > 0) {
                    int nLines = 1;
                    for (int j = 0; j < logLength; j++) if (g.infoLog[j] == '\n') nLines++;
                    if (g.infoLog[logLength - 1] == '\n') g.infoLog[logLength - 1] = '\0';
                    ImGui::InputTextMultiline("##errorlist", g.infoLog, 512, ImVec2((vMax.x - vMin.x), 11 * nLines + 6), ImGuiInputTextFlags_ReadOnly);
                }
                if (g.advanced_view) {
                    ImGui::BeginDisabled(!g.valid);
                    ImGui::SetNextItemWidth(40.f);
                    if (ImGui::DragInt(std::format("Resolution##{}", i).c_str(), &g.grid_res, g.grid_res / 20.f, 10, 1000)) {
                        g.setup();
                    }
                    ImGui::SetNextItemWidth(40.f);
                    ImGui::SameLine();
                    ImGui::DragFloat(std::format("Shininess##{}", i).c_str(), &g.shininess, g.shininess / 40.f, 1.f, 1024.f, "%.0f");
                    ImGui::SameLine();
                    ImGui::BeginDisabled(!gridLines);
                    ImGui::Checkbox(std::format("Grid lines##{}", i).c_str(), &g.grid_lines);
                    ImGui::EndDisabled();
                    ImGui::EndDisabled();
                }
                ImGui::EndChild();
            }
            ImGui::End();

            ImGui::SetNextWindowClass(&window_class);
            ImGui::Begin("Variables", nullptr, ImGuiWindowFlags_NoMove);
            vMin = ImGui::GetWindowContentRegionMin() + ImGui::GetWindowPos();
            vMax = ImGui::GetWindowContentRegionMax() + ImGui::GetWindowPos();
            bool update_all_functions = false;
            float buttonWidth = (vMax.x - vMin.x) / 3.f - 4.f;
            if (ImGui::Button("New variable", ImVec2(buttonWidth, 0))) {
                sliders.push_back({ 0, -5, 5, std::format("v{}", sliders.size() + 1).c_str() });
                for (int i = 0; i < graphs.size(); i++) {
                    sliders[sliders.size() - 1].used_in.push_back(false);
                }
                update_all_functions = true;
            }
            ImGui::SameLine();
            ImGui::BeginDisabled(!sliders.size());
            if (ImGui::Button("Collapse all", ImVec2(buttonWidth, 0))) {
                for (Slider& s : sliders) {
                    s.config = false;
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Expand all", ImVec2(buttonWidth, 0))) {
                for (Slider& s : sliders) {
                    s.config = true;
                }
            }
            ImGui::EndDisabled();
            for (int i = 0; i < sliders.size(); i++) {
                Slider& s = sliders[i];
                ImGui::BeginChild(std::format("##child{}", i).c_str(), ImVec2(0, 0), ImGuiChildFlags_Border | ImGuiChildFlags_AlwaysAutoResize | ImGuiChildFlags_AutoResizeY);
                ImVec2 vMin = ImGui::GetWindowContentRegionMin() + ImGui::GetWindowPos();
                ImVec2 vMax = ImGui::GetWindowContentRegionMax() + ImGui::GetWindowPos();
                ImGui::SetNextItemWidth(vMax.x - vMin.x - 45.f);
                ImGui::BeginDisabled(!s.valid);
                if (ImGui::SliderFloat(std::format("##slider{}", i).c_str(), &s.value, s.min, s.max)) {
                    for (int j = 0; j < graphs.size(); j++) {
                        if (sliders[i].used_in[j] && (integral && second_corner || show_integral_result) && j == integrand_index) {
                            glUniform1i(glGetUniformLocation(shaderProgram, "integral"), false);
                            integral = show_integral_result = apply_integral = second_corner = false;
                            if (integrand_index != -1) graphs[integrand_index].upload_definition(sliders);
                        }
                    }
                }
                ImGui::EndDisabled();
                ImGui::SameLine();
                if (ImGui::Button(s.config ? u8"˄" : u8"˅", ImVec2(16, 0)))
                    s.config ^= 1;
                ImGui::SameLine();
                if (ImGui::Button("x", ImVec2(16, 0))) {
                    sliders.erase(sliders.begin() + i);
                    update_all_functions = true;
                    ImGui::EndChild();
                    continue;
                }
                if (s.config) {
                    float inputWidth = (vMax.x - vMin.x) / 3.f - 3.5f;
                    ImGui::PushItemWidth(inputWidth);
                    auto charFilter = [](ImGuiInputTextCallbackData* data) -> int {
                        const char* forbiddenChars = "!'^+%&/()=?_*-<>£#$½{[]}\\|.:,;\" ";
                        if (strchr(forbiddenChars, data->EventChar)) return 1;
                        return 0;
                    };
                    if (ImGui::InputText(std::format("##sym{}", i).c_str(), s.symbol, 32, ImGuiInputTextFlags_CallbackCharFilter, charFilter)) {
                        s.infoLog[0] = '\0';
                        s.valid = true;
                        if (strlen(s.symbol) == 0) {
                            s.valid = false;
                        }
                        else {
                            for (int j = 0; j < sliders.size(); j++) {
                                if (j == i) continue;
                                const Slider& x = sliders[j];
                                if (!strcmp(x.symbol, s.symbol)) {
                                    strcpy_s(s.infoLog, "cannot have the same symbol as another variable");
                                    s.valid = false;
                                    break;
                                }
                            }
                            if (!strcmp(s.symbol, "x") || !strcmp(s.symbol, "y")) {
                                strcpy_s(s.infoLog, "cannot be \"x\" or \"y\"");
                                s.valid = false;
                            }
                            if (isdigit(s.symbol[0])) {
                                strcpy_s(s.infoLog, "cannot start with a digit");
                                s.valid = false;
                            }
                        }
                        if (strlen(s.infoLog) == 0) update_all_functions = true;
                    }
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_NoSharedDelay))
                        ImGui::SetTooltip("Symbol", ImGui::GetStyle().HoverDelayNormal);
                    ImGui::SameLine();
                    ImGui::InputFloat(std::format("##min{}", i).c_str(), &s.min);
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_NoSharedDelay))
                        ImGui::SetTooltip("Lower limit", ImGui::GetStyle().HoverDelayNormal);
                    ImGui::SameLine();
                    ImGui::InputFloat(std::format("##max{}", i).c_str(), &s.max);
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_NoSharedDelay))
                        ImGui::SetTooltip("Upper limit", ImGui::GetStyle().HoverDelayNormal);
                    ImGui::PopItemWidth();
                }
                if (!s.valid && strlen(s.infoLog) > 0) {
                    ImGui::InputTextMultiline("##errorlist", s.infoLog, 512, ImVec2((vMax.x - vMin.x), 17), ImGuiInputTextFlags_ReadOnly);
                }
                ImGui::EndChild();
            }
            if (update_all_functions) {
                for (Graph& g : graphs) {
                    if (strlen(g.defn) == 0.f) continue;
                    g.upload_definition(sliders);
                }
            }
            ImGui::End();

            ImGui::SetNextWindowClass(&window_class);
            ImGui::Begin("Tools", nullptr, ImGuiWindowFlags_NoMove);
            vMin = ImGui::GetWindowContentRegionMin() + ImGui::GetWindowPos();
            vMax = ImGui::GetWindowContentRegionMax() + ImGui::GetWindowPos();
            ImGui::SetNextItemWidth(vMax.x - vMin.x);
            if (ImGui::InputFloat3("##goto", value_ptr(centerPos)))
                glUniform3fv(glGetUniformLocation(shaderProgram, "centerPos"), 1, value_ptr(centerPos));
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_NoSharedDelay))
                ImGui::SetTooltip("Center position", ImGui::GetStyle().HoverDelayNormal);
            if (ImGui::Button("Center on origin", ImVec2((vMax.x - vMin.x) / 2.f - 5.f, 0))) {
                centerPos = vec3(0.f);
                glUniform3fv(glGetUniformLocation(shaderProgram, "centerPos"), 1, value_ptr(centerPos));
            }
            ImGui::SameLine();
            if (ImGui::Button("Reset zoom", ImVec2((vMax.x - vMin.x) / 2.f - 2.f, 0))) {
                zoomx = zoomy = zoomz = 8.f;
                zoomSpeed = 1.f;
            }
            buttonWidth = (vMax.x - vMin.x - 61.f) / 4.f + 0.7f;

            bool update_zoom = false;
            ImGui::BeginChild(ImGui::GetID("xrange"), ImVec2((vMax.x - vMin.x) / 3.f - 4.f, 0), ImGuiChildFlags_AlwaysAutoResize | ImGuiChildFlags_AutoResizeY);
            float inputWidth = (ImGui::GetWindowContentRegionMax() + ImGui::GetWindowPos() - ImGui::GetWindowContentRegionMin() - ImGui::GetWindowPos()).x;
            ImGui::PushItemWidth(inputWidth);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (inputWidth - 38.f) / 2.f);
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(150, 150, 150, 255));
            ImGui::Text("X-Range");
            ImGui::PopStyleColor();
            update_zoom |= ImGui::InputFloat("##xrange0", &xrange[0], 0.f, 0.f, "% 06.4f");
            update_zoom |= ImGui::InputFloat("##xrange1", &xrange[1], 0.f, 0.f, "% 06.4f");
            ImGui::PopItemWidth();
            ImGui::EndChild();
            ImGui::SameLine();
            ImGui::BeginChild(ImGui::GetID("yrange"), ImVec2((vMax.x - vMin.x) / 3.f - 4.f, 0), ImGuiChildFlags_AlwaysAutoResize | ImGuiChildFlags_AutoResizeY);
            ImGui::PushItemWidth(inputWidth);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (inputWidth - 38.f) / 2.f);
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(150, 150, 150, 255));
            ImGui::Text("Y-Range");
            ImGui::PopStyleColor();
            update_zoom |= ImGui::InputFloat("##yrange0", &yrange[0], 0.f, 0.f, "% 06.4f");
            update_zoom |= ImGui::InputFloat("##yrange1", &yrange[1], 0.f, 0.f, "% 06.4f");
            ImGui::PopItemWidth();
            ImGui::EndChild();
            ImGui::SameLine();
            ImGui::BeginChild(ImGui::GetID("zrange"), ImVec2((vMax.x - vMin.x) / 3.f - 4.f, 0), ImGuiChildFlags_AlwaysAutoResize | ImGuiChildFlags_AutoResizeY);
            ImGui::PushItemWidth(inputWidth);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (inputWidth - 38.f) / 2.f);
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(150, 150, 150, 255));
            ImGui::Text("Z-Range");
            ImGui::PopStyleColor();
            update_zoom |= ImGui::InputFloat("##zrange0", &zrange[0], 0.f, 0.f, "% 06.4f");
            update_zoom |= ImGui::InputFloat("##zrange1", &zrange[1], 0.f, 0.f, "% 06.4f");
            ImGui::PopItemWidth();
            ImGui::EndChild();

            ImGui::SeparatorText("Calculus Tools");
            
            if (update_zoom) {
                if (xrange[0] <= xrange[1]) xrange[0] = xrange[1] + 0.0001f;
                if (yrange[0] <= yrange[1]) yrange[0] = yrange[1] + 0.0001f;
                if (zrange[0] <= zrange[1]) zrange[0] = zrange[1] + 0.0001f;
                zoomx = abs(xrange[0] - xrange[1]);
                centerPos.x = (xrange[0] + xrange[1]) / 2.f;
                zoomy = abs(yrange[0] - yrange[1]);
                centerPos.y = (yrange[0] + yrange[1]) / 2.f;
                zoomz = abs(zrange[0] - zrange[1]);
                centerPos.z = (zrange[0] + zrange[1]) / 2.f;
                glUniform3fv(glGetUniformLocation(shaderProgram, "centerPos"), 1, value_ptr(centerPos));
            }

            if (gradient_vector) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.30f, 0.32f, 0.33f, 1.00f));
            if (ImGui::ImageButton("gradient_vector", (void*)(intptr_t)gradVec_texture, ImVec2(buttonWidth, 30), ImVec2(-(buttonWidth - 30.f) / 60.f, 0.f), ImVec2(1.f + (buttonWidth - 30.f) / 60.f, 1.f), ImVec4(0.0f, 0.0f, 0.0f, 0.0f), ImVec4(1.0f, 1.0f, 1.0f, 1.0f))) {
                gradient_vector ^= 1;
                tangent_plane = false;
                glUniform1i(glGetUniformLocation(shaderProgram, "integral"), false);
                integral = show_integral_result = apply_integral = second_corner = false;
                if (integrand_index != -1) graphs[integrand_index].upload_definition(sliders);
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
                glUniform1i(glGetUniformLocation(shaderProgram, "integral"), false);
                integral = show_integral_result = apply_integral = second_corner = false;
                if (integrand_index != -1) graphs[integrand_index].upload_definition(sliders);
                if (!tangent_plane) ImGui::PopStyleColor();
            }
            else if (tangent_plane) ImGui::PopStyleColor();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_NoSharedDelay))
                ImGui::SetTooltip("Tangent Plane", ImGui::GetStyle().HoverDelayNormal);

            ImGui::SameLine();
            if (ImGui::ImageButton("normal_vector", (void*)(intptr_t)normVec_texture, ImVec2(buttonWidth, 30), ImVec2(-(buttonWidth - 30.f) / 60.f, 0.0f), ImVec2(1.f + (buttonWidth - 30.f) / 60.f, 1.f), ImVec4(0.0f, 0.0f, 0.0f, 0.0f), ImVec4(1.0f, 1.0f, 1.0f, 1.0f))) {
                
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_NoSharedDelay))
                ImGui::SetTooltip("Normal Vector", ImGui::GetStyle().HoverDelayNormal);

            ImGui::SameLine();
            if (integral) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.30f, 0.32f, 0.33f, 1.00f));
            if (ImGui::ImageButton("integral", (void*)(intptr_t)integral_texture, ImVec2(buttonWidth, 30), ImVec2(-(buttonWidth - 30.f) / 60.f, 0.0f), ImVec2(1.f + (buttonWidth - 30.f) / 60.f, 1.f), ImVec4(0.0f, 0.0f, 0.0f, 0.0f), ImVec4(1.0f, 1.0f, 1.0f, 1.0f))) {
                if (!integral) {
                    glUniform1i(glGetUniformLocation(shaderProgram, "integral"), false);
                    integral = show_integral_result = apply_integral = second_corner = false;
                    if (integrand_index != -1) graphs[integrand_index].upload_definition(sliders);
                }
                if (show_integral_result) {
                    show_integral_result = false;
                    ImGui::PopStyleColor();
                    goto skip;
                }
                integral ^= 1;
                if (second_corner) {
                    second_corner = false;
                    glUniform1i(glGetUniformLocation(shaderProgram, "integral"), false);
                }
                tangent_plane = false;
                gradient_vector = false;
                if (!integral) ImGui::PopStyleColor();
            }
            else if (integral) ImGui::PopStyleColor();
        skip:
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_NoSharedDelay))
                ImGui::SetTooltip("Double Integral", ImGui::GetStyle().HoverDelayNormal);  
            ImGui::End();
            
            if ((integral || show_integral_result)) {
                ImGui::SetNextWindowBgAlpha(0.5f);
                ImGui::SetNextWindowPos(ImVec2(sidebarWidth + 10.f, 24.f));
                ImGui::SetNextWindowSize(ImVec2(300.f, 0.f));
                if (ImGui::Begin("##doubleintegral", nullptr,
                    ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav)) {

                    ImGui::BeginChild(ImGui::GetID("region_type"), ImVec2(100, 0), ImGuiChildFlags_Border | ImGuiChildFlags_AlwaysAutoResize | ImGuiChildFlags_AutoResizeY);
                    ImGui::BeginDisabled(show_integral_result || second_corner);
                    if (ImGui::RadioButton("Rectangle", region_type == CartesianRectangle)) region_type = CartesianRectangle;
                    if (ImGui::RadioButton("Type I", region_type == Type1)) region_type = Type1;
                    if (ImGui::RadioButton("Type II", region_type == Type2)) region_type = Type2;
                    if (ImGui::RadioButton("Polar", region_type == Polar)) region_type = Polar;
                    ImGui::EndDisabled();
                    ImGui::EndChild();
                    ImGui::SameLine();
                    ImGui::BeginChild(ImGui::GetID("region_bounds"), ImVec2(vMax.x - vMin.x - 106.f, 0), ImGuiChildFlags_Border | ImGuiChildFlags_AlwaysAutoResize | ImGuiChildFlags_AutoResizeY);
                    ImGui::BeginDisabled(show_integral_result || second_corner);
                    vMin = ImGui::GetWindowContentRegionMin() + ImGui::GetWindowPos();
                    vMax = ImGui::GetWindowContentRegionMax() + ImGui::GetWindowPos();
                    ImGui::PushItemWidth((vMax.x - vMin.x - 42.f) / 2.f);
                    bool ready = true;
                    switch (region_type) {
                    case CartesianRectangle:
                        ImGui::InputFloat(u8"\u2264 x \u2264", &x_min, 0.f, 0.f, "%g");
                        ImGui::SameLine();
                        ImGui::InputFloat("##x_max", &x_max, 0.f, 0.f, "%g");

                        ImGui::InputFloat(u8"\u2264 y \u2264", &y_min, 0.f, 0.f, "%g");
                        ImGui::SameLine();
                        ImGui::InputFloat("##y_max", &y_max, 0.f, 0.f, "%g");
                        break;
                    case Type1:
                        ImGui::InputFloat(u8"\u2264 x \u2264", &x_min, 0.f, 0.f, "%g");
                        ImGui::SameLine();
                        ImGui::InputFloat("##x_max", &x_max, 0.f, 0.f, "%g");

                        ImGui::InputText(u8"\u2264 y \u2264", y_min_eq, 32);
                        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_NoSharedDelay))
                            ImGui::SetTooltip("Enter a function of x", ImGui::GetStyle().HoverDelayNormal);
                        ImGui::SameLine();
                        ImGui::InputText("##y_max", y_max_eq, 32);
                        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_NoSharedDelay))
                            ImGui::SetTooltip("Enter a function of x", ImGui::GetStyle().HoverDelayNormal);
                        if (strlen(y_min_eq) == 0 || strlen(y_max_eq) == 0) ready = false;
                        break;
                    case Type2:
                        ImGui::InputText(u8"\u2264 x \u2264", x_min_eq, 32);
                        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_NoSharedDelay))
                            ImGui::SetTooltip("Enter a function of y", ImGui::GetStyle().HoverDelayNormal);
                        ImGui::SameLine();
                        ImGui::InputText("##x_max", x_max_eq, 32);
                        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_NoSharedDelay))
                            ImGui::SetTooltip("Enter a function of y", ImGui::GetStyle().HoverDelayNormal);
                        if (strlen(x_min_eq) == 0 || strlen(x_max_eq) == 0) ready = false;

                        ImGui::InputFloat(u8"\u2264 y \u2264", &y_min, 0.f, 0.f, "%g");
                        ImGui::SameLine();
                        ImGui::InputFloat("##y_max", &y_max, 0.f, 0.f, "%g");
                        break;
                    case Polar:
                        ImGui::InputFloat(u8"\u2264 \u03b8 \u2264", &theta_min, 0.f, 0.f, "%g");
                        ImGui::SameLine();
                        ImGui::InputFloat("##theta_max", &theta_max, 0.f, 0.f, "%g");

                        ImGui::InputText(u8"\u2264 r \u2264", r_min_eq, 32);
                        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_NoSharedDelay))
                            ImGui::SetTooltip(u8"Enter a function of \u03b8", ImGui::GetStyle().HoverDelayNormal);
                        ImGui::SameLine();
                        ImGui::InputText("##r_max", r_max_eq, 32);
                        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_NoSharedDelay))
                            ImGui::SetTooltip(u8"Enter a function of \u03b8", ImGui::GetStyle().HoverDelayNormal);
                        if (strlen(r_min_eq) == 0 || strlen(r_max_eq) == 0) ready = false;
                        break;
                    }
                    ImGui::PopItemWidth();
                    ImGui::SetNextItemWidth(vMax.x - vMin.x - 62.f);
                    if (ImGui::InputInt("Precision", &integral_precision, 50, 100))
                        if (integral_precision < 50) integral_precision = 50;
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_NoSharedDelay))
                        ImGui::SetTooltip("Precision, higher the better", ImGui::GetStyle().HoverDelayNormal);
                    ImGui::EndDisabled();
                    ImGui::BeginDisabled(!ready || show_integral_result || second_corner);
                    if (ImGui::Button("Compute", ImVec2(vMax.x - vMin.x, 0.f))) {
                        integrand_index = 1;
                        glUniform1i(glGetUniformLocation(shaderProgram, "integral"), true);
                        glUniform1i(glGetUniformLocation(shaderProgram, "integrand_idx"), 1);
                        glUniform1i(glGetUniformLocation(shaderProgram, "region_type"), region_type);
                        int error = compute_integral(integral_infoLog);
                        if (error != -1) erroring_eq = error;
                        else {
                            erroring_eq = -1;
                            show_integral_result = true;
                        }
                    }
                    ImGui::EndDisabled();
                    ImGui::EndChild();
                }
                if (erroring_eq != -1) {
                    vMin = ImGui::GetWindowContentRegionMin() + ImGui::GetWindowPos();
                    vMax = ImGui::GetWindowContentRegionMax() + ImGui::GetWindowPos();
                    size_t logLength = strlen(integral_infoLog);
                    int nLines = 1;
                    for (int j = 0; j < logLength; j++) if (integral_infoLog[j] == '\n') nLines++;
                    if (integral_infoLog[logLength - 1] == '\n') integral_infoLog[logLength - 1] = '\0';
                    ImGui::InputTextMultiline("##integralerrorlist", integral_infoLog, 512, ImVec2((vMax.x - vMin.x), 11 * nLines + 6), ImGuiInputTextFlags_ReadOnly);
                }
                ImGui::End();
            }
            
            double x, y;
            glfwGetCursorPos(window, &x, &y);
            if (graphs.size() > 0 && x - sidebarWidth > 0. && x - sidebarWidth < (wWidth - sidebarWidth) && y > 0. && y < wHeight &&
                glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_RELEASE && zoomSpeed == 1.f) {
                float depth[1];
                glBindFramebuffer(GL_FRAMEBUFFER, FBO);
                glBindTexture(GL_TEXTURE_2D, prevZBuffer);
                glReadPixels(ssaa_factor * x, ssaa_factor * (wHeight - y), 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, depth);
                if (depth[0] == 1.f) goto mouse_not_on_graph;

                glBindBuffer(GL_SHADER_STORAGE_BUFFER, posBuffer);
                glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
                float data[6];
                glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, static_cast<int>(6 * (wHeight * y + x - sidebarWidth)) * sizeof(float), 6 * sizeof(float), data);
                vec3 fragPos = { data[0], data[1], data[2] };
                int index = static_cast<int>(data[3]);
                vec2 gradvec = { data[4], data[5] };

                if (index >= graphs.size() || index == 0) {
                    goto mouse_not_on_graph;
                }
                ImGui::Begin("info", nullptr,
                    ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar |
                    ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_AlwaysAutoResize |
                    ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar);
                ImVec2 size = ImGui::GetWindowSize();
                ImVec2 pos = { (float)x + 20.0f, (float)y + 20.f };
                if (size.x > wWidth - pos.x - 5)
                    pos.x = wWidth - size.x - 5;
                if (size.y > wHeight - pos.y - 5)
                    pos.y = wHeight - size.y - 5;
                if (pos.x < 5) pos.x = 5;
                if (pos.y < 5) pos.y = 5;
                ImGui::SetWindowPos(pos);
                vec4 c = graphs[index].color * 1.3f;
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 9.f);
                ImGui::ColorEdit4("##infocolor", value_ptr(c), ImGuiColorEditFlags_NoInputs);
                ImGui::SameLine();
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 11.f);
                ImGui::Text(u8"X=% 06.4f\nY=% 06.4f\nZ=% 06.4f", fragPos.x, fragPos.y, fragPos.z);
                ImGui::SameLine();
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 5.f);
                ImGui::Text(u8"\u2202z/\u2202x=% 06.4f\n\u2202z/\u2202y=% 06.4f", gradvec.x, gradvec.y);
                ImVec2 prevWindowSize = ImGui::GetWindowSize();
                ImGui::End();

                GLfloat params[5] = { fragPos.z, gradvec.x, fragPos.x, gradvec.y, fragPos.y };
                if (tangent_plane) {
                    glUseProgram(graphs[0].computeProgram);
                    glUniform1fv(glGetUniformLocation(graphs[0].computeProgram, "plane_params"), 5, params);
                    graphs[0].enabled = true;
                    vec4 nc1 = colors[(graphs.size() - 1) % colors.size()];
                    vec4 nc2 = colors[(graphs.size()) % colors.size()];
                    graphs[0].color = vec4(nc1.r, nc1.g, nc1.b, 0.4f);
                    graphs[0].secondary_color = vec4(nc2.r, nc2.g, nc2.b, 0.4f);
                    glUseProgram(shaderProgram);
                    ImGui::Begin("tooltip", nullptr,
                        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar |
                        ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_AlwaysAutoResize |
                        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar);
                    ImVec2 size = ImGui::GetWindowSize();
                    ImVec2 pos = { (float)x + 20.0f, (float)y + 20.f + prevWindowSize.y + 3.f };
                    if (size.x > wWidth - pos.x - 5)
                        pos.x = wWidth - size.x - 5;
                    if (size.y > wHeight - pos.y - 5)
                        pos.y = wHeight - size.y - 5;
                    if (pos.x < 5) pos.x = 5;
                    if (pos.y < 5) pos.y = 5;
                    ImGui::SetWindowPos(pos);
                    ImGui::Text("Left-click to save tangent plane");
                    ImGui::End();
                }
                if (apply_tangent_plane) {
                    const char* eq = "%.6f%+.6f*(x%+.6f)%+.6f*(y%+.6f)";
                    char eqf[88]{};
                    sprintf_s(eqf, eq, params[0], params[1], -params[2], params[3], -params[4]);
                    graphs.push_back(Graph(graphs.size(), UserDefined, eqf, 100, colors[(graphs.size() - 1) % colors.size()], colors[(graphs.size()) % colors.size()], true, gridSSBO, EBO));
                    for (Slider& s : sliders) {
                        s.used_in.push_back(false);
                    }
                    graphs[graphs.size() - 1].setup();
                    graphs[graphs.size() - 1].upload_definition(sliders);
                    apply_tangent_plane = false;
                    tangent_plane = false;
                    graphs[0].enabled = false;
                }
                if (integral && !show_integral_result) {
                    ImGui::Begin("tooltip", nullptr,
                        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar |
                        ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_AlwaysAutoResize |
                        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar);
                    ImVec2 size = ImGui::GetWindowSize();
                    ImVec2 pos = { (float)x + 20.0f, (float)y + 20.f + prevWindowSize.y + 3.f };
                    if (size.x > wWidth - pos.x - 5)
                        pos.x = wWidth - size.x - 5;
                    if (size.y > wHeight - pos.y - 5)
                        pos.y = wHeight - size.y - 5;
                    if (pos.x < 5) pos.x = 5;
                    if (pos.y < 5) pos.y = 5;
                    ImGui::SetWindowPos(pos);
                    ImGui::Text("Left-click to set the %s corner", second_corner ? "2nd" : "1st");
                    ImGui::End();
                    if (second_corner) {
                        glUniform2f(glGetUniformLocation(shaderProgram, "corner2"), fragPos.x, fragPos.y);
                    }
                }
                if (apply_integral) {
                    if (!second_corner) {
                        integral_limits.first = vec3(fragPos.x, fragPos.y, fragPos.z);
                        integrand_index = index;
                        glUniform1i(glGetUniformLocation(shaderProgram, "integral"), true);
                        glUniform1i(glGetUniformLocation(shaderProgram, "integrand_idx"), index);
                        glUniform1i(glGetUniformLocation(shaderProgram, "region_type"), -1);
                        glUniform2f(glGetUniformLocation(shaderProgram, "corner1"), fragPos.x, fragPos.y);
                        glUniform2f(glGetUniformLocation(shaderProgram, "corner2"), fragPos.x, fragPos.y);
                        second_corner = true;
                        region_type = CartesianRectangle;
                    }
                    else {
                        integral_limits.second = vec3(fragPos.x, fragPos.y, fragPos.z);
                        integral = false;
                        second_corner = false;
                        x_min = min(integral_limits.first.x, integral_limits.second.x);
                        x_max = max(integral_limits.first.x, integral_limits.second.x);
                        y_min = min(integral_limits.first.y, integral_limits.second.y);
                        y_max = max(integral_limits.first.y, integral_limits.second.y);
                        compute_integral(integral_infoLog);
                        show_integral_result = true;
                    }
                    apply_integral = false;
                }
                if (doubleClickPressed) {
                    centerPos = fragPos;
                    glUniform3fv(glGetUniformLocation(shaderProgram, "centerPos"), 1, value_ptr(centerPos));
                    doubleClickPressed = false;
                }
            } else {
            mouse_not_on_graph:
                graphs[0].enabled = false;
                apply_tangent_plane = false;
                apply_integral = false;
                doubleClickPressed = false;
            }

            if (show_integral_result) {
                vec3 v = center_of_region - centerPos;
                const float gridres = graphs[integrand_index].grid_res + 2;
                const float halfres = gridres / 2.f;
                vec4 ndc = proj * view * vec4(graph_size * v.x / zoomx, (middle_height - centerPos.z) / zoomz * graph_size, graph_size * v.y / zoomy, 1.f);
                ndc = ndc / ndc.w;
                ImGui::SetNextWindowPos(ImVec2((ndc.x + 1.f) * (wWidth - sidebarWidth) / 2.f + sidebarWidth, (wHeight - (ndc.y + 1.f) * wHeight / 2.f)));
                static bool result_window = true;
                ImGui::Begin("Volume under surface", &result_window,
                    ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                    ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove);
                switch (region_type) {
                case CartesianRectangle:
                    ImGui::Text(u8"%.4g \u2264 x \u2264 %.4g, %.4g \u2264 y \u2264 %.4g", x_min, x_max, y_min, y_max);
                    break;
                case Type1:
                    ImGui::Text(u8"%.4g \u2264 x \u2264 %.4g, %s \u2264 y \u2264 %s", x_min, x_max, y_min_eq, y_max_eq);
                    break;
                case Type2:
                    ImGui::Text(u8"%.4g \u2264 y \u2264 %.4g, %s \u2264 x \u2264 %s", y_min, y_max, x_min_eq, x_max_eq);
                    break;
                case Polar:
                    ImGui::Text(u8"%.4g \u2264 \u03b8 \u2264 %.4g, %s \u2264 r \u2264 %s", theta_min, theta_max, r_min_eq, r_max_eq);
                }
                ImGui::Text("Signed volume \u2248 %.9f", integral_result);
                ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(120, 120, 120, 255));
                ImGui::Text(u8"\u2206x = %.3e, \u2206y = %.3e", dx, dy);
                ImGui::PopStyleColor();
                ImGui::End();
                if (result_window == false) {
                    show_integral_result = apply_integral = second_corner = false;
                    glUniform1i(glGetUniformLocation(shaderProgram, "integral"), false);
                    if (integrand_index != -1) graphs[integrand_index].upload_definition(sliders);
                    result_window = true;
                }
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

            ImGui::PopFont();

            if (autoRotate)
                phi += timeStep * 5.f;

            auto cameraPos = vec3(sin(glm::radians(theta)) * cos(glm::radians(phi)), cos(glm::radians(theta)), sin(glm::radians(theta)) * sin(glm::radians(phi)));
            view = lookAt(cameraPos, vec3(0.f), { 0.f, 1.f, 0.f });
            proj = ortho(-1.f, 1.f, -(float)wHeight / (float)(wWidth - sidebarWidth), (float)wHeight / (float)(wWidth - sidebarWidth), -10.f, 10.f);
            glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "vpmat"), 1, GL_FALSE, value_ptr(proj * view));
            glUniform3fv(glGetUniformLocation(shaderProgram, "cameraPos"), 1, value_ptr(cameraPos));

            ImGui::Render();

            glBindBuffer(GL_SHADER_STORAGE_BUFFER, posBuffer);
            glClearBufferData(GL_SHADER_STORAGE_BUFFER, GL_R32F, GL_RED, GL_FLOAT, nullptr);

            glClearColor(0.f, 0.f, 0.f, 1.f);
            glBindFramebuffer(GL_FRAMEBUFFER, NULL);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            glBindFramebuffer(GL_FRAMEBUFFER, FBO);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            
            glViewport(sidebarWidth * ssaa_factor, 0, (wWidth - sidebarWidth) * ssaa_factor, wHeight * ssaa_factor);
            glUniform2i(glGetUniformLocation(shaderProgram, "regionSize"), wWidth * ssaa_factor - sidebarWidth * ssaa_factor, wHeight * ssaa_factor);
            glUniform2i(glGetUniformLocation(shaderProgram, "windowSize"), wWidth * ssaa_factor, wHeight * ssaa_factor);

            glBindBuffer(GL_SHADER_STORAGE_BUFFER, sliderBuffer);
            std::vector<float> values(sliders.size());
            for (int i = 0; i < values.size(); i++)
                values[i] = sliders[i].value;
            glBufferData(GL_SHADER_STORAGE_BUFFER, values.size() * sizeof(float), values.data(), GL_DYNAMIC_DRAW);

            auto render_graph = [&](int i) {
                const Graph& g = graphs[i];
                g.use_compute(zoomx, zoomy, zoomz, centerPos);
                glDispatchCompute(g.grid_res + 2, g.grid_res + 2, 1);
                glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
                glUseProgram(shaderProgram);
                g.use_shader();
                glUniform1i(glGetUniformLocation(shaderProgram, "index"), i);
                glUniform4fv(glGetUniformLocation(shaderProgram, "color"), 1, value_ptr(g.color));
                glUniform4fv(glGetUniformLocation(shaderProgram, "secondary_color"), 1, value_ptr(g.secondary_color));
                glUniform1i(glGetUniformLocation(shaderProgram, "grid_res"), g.grid_res);
                glUniform1i(glGetUniformLocation(shaderProgram, "tangent_plane"), g.type == TangentPlane);
                glUniform1f(glGetUniformLocation(shaderProgram, "shininess"), g.shininess);
                if (gridLines)
                    glUniform1f(glGetUniformLocation(shaderProgram, "gridLineDensity"), g.grid_lines ? gridLineDensity : 0.f);
                glBindVertexArray(VAO);
                glUniform1i(glGetUniformLocation(shaderProgram, "quad"), false);
                glActiveTexture(GL_TEXTURE1);
                glBindTexture(GL_TEXTURE_2D, prevZBuffer);
                glDrawElements(GL_TRIANGLE_STRIP, (GLsizei)g.indices.size(), GL_UNSIGNED_INT, 0);
            };
            
            if (integral && second_corner || show_integral_result)
                render_graph(integrand_index);
            for (int i = 1; i < graphs.size(); i++) {
                const Graph& g = graphs[i];
                if (!g.enabled) continue;
                if (i == integrand_index && (integral && second_corner || show_integral_result)) continue;
                render_graph(i);
            }
            glBindFramebuffer(GL_READ_FRAMEBUFFER, srcFBO);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dstFBO);
            glBlitFramebuffer(
                0, 0, ssaa_factor* wWidth, ssaa_factor* wHeight,
                0, 0, ssaa_factor* wWidth, ssaa_factor* wHeight,
                GL_DEPTH_BUFFER_BIT, GL_NEAREST
            );
            glBindFramebuffer(GL_FRAMEBUFFER, FBO);
            if (graphs[0].enabled) render_graph(0);

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