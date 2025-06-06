﻿#define VERSION "0.1"

#ifdef PLATFORM_WINDOWS
    #pragma comment(linker, "/ENTRY:mainCRTStartup")
#endif

#define _USE_MATH_DEFINES
#define IMGUI_DEFINE_MATH_OPERATORS
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define GLFW_EXPOSE_NATIVE_WIN32
#define GLFW_EXPOSE_NATIVE_WGL
#define GLFW_NATIVE_INCLUDE_NONE

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <imgui_internal.h>
#include <imgui_theme.h>

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#ifdef PLATFORM_WINDOWS
    #include <Windows.h>
    #include <dwmapi.h>
    #include <GLFW/glfw3native.h>
#endif

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/rotate_vector.hpp>
#include <glm/gtx/vector_angle.hpp>
#include <glm/gtx/string_cast.hpp>

#include <boxer/boxer.h>
#include <battery/embed.hpp>
#include <lodepng.h>
#include <bmp_read.hpp>
#include <nlohmann/json.hpp>

#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <vector>
#include <iomanip>
#include <ctime>
#include <cmath>
#include <string>
#include <regex>
#include <bitset>

#ifdef PLATFORM_WINDOWS
    #pragma comment(lib, "Gdiplus.lib")
#endif
using namespace glm;

#ifdef _DEBUG
void GLAPIENTRY glMessageCallback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* message, const void* userParam) {
    if (type != GL_DEBUG_TYPE_ERROR) return;
    fprintf(stderr, "GL CALLBACK: %s type = 0x%x, severity = 0x%x, message = %s\n", "** GL ERROR **", type, severity, message);
}
#endif

#define U8(t) reinterpret_cast<const char*>(t)

class compilation_error : public std::exception {
    char* message;
public:
    compilation_error(char* msg) : message(msg) {}
    const char* what() const throw() {
        return message;
    }
};

float quad[12] = {
    -1.0f, -1.0f, -1.0f,  1.0f, 1.0f,  1.0f,
    -1.0f, -1.0f,  1.0f,  1.0f, 1.0f, -1.0f
};
std::vector<vec4> colors = {
    vec4(0.000f, 0.500f, 1.000f, 1.f),
    vec4(0.924f, 0.395f, 0.000f, 1.f),
    vec4(0.058f, 0.570f, 0.000f, 1.f),
    vec4(0.906f ,0.757f, 0.000f, 1.f),
    vec4(0.496f, 0.000f, 0.652f, 1.f),
    vec4(0.823f, 0.000f, 0.000f, 1.f),
};

namespace ImGui {
    ImFont* font;

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

struct Slider {
    float value;
    float min, max;
    char symbol[32];
    bool config = true;
    bool valid = true;
    std::vector<bool> used_in;
    char infoLog[128];

    Slider() = default;
    Slider(float defval, float min, float max, const char* sym) : value(defval), min(min), max(max) {
        strcpy(symbol, sym);
    }

    Slider& operator=(const Slider& other) {
        if (this != &other) {
            value = other.value;
            min = other.min;
            max = other.max;
            memcpy(symbol, other.symbol, 32);
            valid = other.valid;
        }
        return *this;
    }
};

enum GraphType {
    UserDefined,
    TangentPlane,
};

class Graph {
public:
    GLuint computeProgram = 0, SSBO, EBO;
    size_t idx;
    bool enabled = false;
    bool valid = false;
    bool advanced_view = false;
    bool grid_lines = false;
    float shininess = 16;
    char* infoLog = new char[512]{};
    int type;
    int grid_res;
    std::vector<unsigned int> indices;

    char defn[256]{};
    vec4 color;
    vec4 secondary_color;

    Graph() = default;
    Graph(size_t idx, int type, const char* definition, int res, vec4 color, vec4 color2, bool enabled, GLuint SSBO, GLuint EBO)
        : type(type), idx(idx), grid_res(res), color(color), secondary_color(color2), enabled(enabled), SSBO(SSBO), EBO(EBO) {
        strcpy(defn, definition);
    }

    Graph& operator=(const Graph& other) {
        if (this != &other) {
            idx = other.idx;
            enabled = other.enabled;
            grid_lines = other.grid_lines;
            shininess = other.shininess;
            type = other.type;
            grid_res = other.grid_res;
            memcpy(defn, other.defn, 256);
            color = other.color;
            secondary_color = other.secondary_color;
        }
        return *this;
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

    void upload_definition(std::vector<Slider>& sliders, const char* regionBool = "true", const char* scalarField = "z", bool polar = false, bool partialderivatives = false) {
        int success;

        auto replace = [&](std::string& source, std::string x, std::string y) {
            std::string pattern = "\\b";
            pattern.append(x);
            pattern.append("\\b");
            return std::regex_replace(source, std::regex(pattern), y);
        };

        std::string pdefn = defn;
        pdefn.resize(512);
        for (int i = 0; i < sliders.size(); i++) {
            if (!sliders[i].valid) continue;
            std::string temp = replace(pdefn, sliders[i].symbol, std::format("sliders[{}]", i));
            sliders[i].used_in[idx] = (pdefn != temp);
            pdefn = temp;
        }

        b::EmbedInternal::EmbeddedFile embed;
        const char* content;
        int length;

        unsigned int computeShader = glCreateShader(GL_COMPUTE_SHADER);
        embed = b::embed<"shaders/compute.glsl">();
        content = embed.data();
        length = embed.length();
        size_t size = length + pdefn.capacity() + 7;
        char* modifiedSource = new char[size];
        snprintf(modifiedSource, size, content, pdefn.c_str(), partialderivatives ? "true" : "false", polar ? "" : "//", scalarField, regionBool);
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
        delete[] modifiedSource;

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
    NormalMap,
};

enum ExpressionType {
    Explicit,
    Implicit,
};

enum IntegralType {
    None,
    DoubleIntegral,
    SurfaceIntegral,
    LineIntegral,
};

// https://www.youtube.com/watch?v=KvwVYJY_IZ4
const int triang[256 * 15]{
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,0, 8, 3, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,0, 1, 9, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,1, 8, 3, 9, 8, 1, -1, -1, -1, -1, -1, -1, -1, -1, -1,1, 2, 10, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,0, 8, 3, 1, 2, 10, -1, -1, -1, -1, -1, -1, -1, -1, -1,9, 2, 10, 0, 2, 9, -1, -1, -1, -1, -1, -1, -1, -1, -1,2, 8, 3, 2, 10, 8, 10, 9, 8, -1, -1, -1, -1, -1, -1,3, 11, 2, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,0, 11, 2, 8, 11, 0, -1, -1, -1, -1, -1, -1, -1, -1, -1,1, 9, 0, 2, 3, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1,1, 11, 2, 1, 9, 11, 9, 8, 11, -1, -1, -1, -1, -1, -1,3, 10, 1, 11, 10, 3, -1, -1, -1, -1, -1, -1, -1, -1, -1,0, 10, 1, 0, 8, 10, 8, 11, 10, -1, -1, -1, -1, -1, -1,3, 9, 0, 3, 11, 9, 11, 10, 9, -1, -1, -1, -1, -1, -1,9, 8, 10, 10, 8, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1,4, 7, 8, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,4, 3, 0, 7, 3, 4, -1, -1, -1, -1, -1, -1, -1, -1, -1,0, 1, 9, 8, 4, 7, -1, -1, -1, -1, -1, -1, -1, -1, -1,4, 1, 9, 4, 7, 1, 7, 3, 1, -1, -1, -1, -1, -1, -1,1, 2, 10, 8, 4, 7, -1, -1, -1, -1, -1, -1, -1, -1, -1,3, 4, 7, 3, 0, 4, 1, 2, 10, -1, -1, -1, -1, -1, -1,9, 2, 10, 9, 0, 2, 8, 4, 7, -1, -1, -1, -1, -1, -1,2, 10, 9, 2, 9, 7, 2, 7, 3, 7, 9, 4, -1, -1, -1,8, 4, 7, 3, 11, 2, -1, -1, -1, -1, -1, -1, -1, -1, -1,11, 4, 7, 11, 2, 4, 2, 0, 4, -1, -1, -1, -1, -1, -1,9, 0, 1, 8, 4, 7, 2, 3, 11, -1, -1, -1, -1, -1, -1,4, 7, 11, 9, 4, 11, 9, 11, 2, 9, 2, 1, -1, -1, -1,3, 10, 1, 3, 11, 10, 7, 8, 4, -1, -1, -1, -1, -1, -1,1, 11, 10, 1, 4, 11, 1, 0, 4, 7, 11, 4, -1, -1, -1,4, 7, 8, 9, 0, 11, 9, 11, 10, 11, 0, 3, -1, -1, -1,4, 7, 11, 4, 11, 9, 9, 11, 10, -1, -1, -1, -1, -1, -1,9, 5, 4, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,9, 5, 4, 0, 8, 3, -1, -1, -1, -1, -1, -1, -1, -1, -1,0, 5, 4, 1, 5, 0, -1, -1, -1, -1, -1, -1, -1, -1, -1,8, 5, 4, 8, 3, 5, 3, 1, 5, -1, -1, -1, -1, -1, -1,1, 2, 10, 9, 5, 4, -1, -1, -1, -1, -1, -1, -1, -1, -1,3, 0, 8, 1, 2, 10, 4, 9, 5, -1, -1, -1, -1, -1, -1,5, 2, 10, 5, 4, 2, 4, 0, 2, -1, -1, -1, -1, -1, -1,2, 10, 5, 3, 2, 5, 3, 5, 4, 3, 4, 8, -1, -1, -1,9, 5, 4, 2, 3, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1,0, 11, 2, 0, 8, 11, 4, 9, 5, -1, -1, -1, -1, -1, -1,0, 5, 4, 0, 1, 5, 2, 3, 11, -1, -1, -1, -1, -1, -1,2, 1, 5, 2, 5, 8, 2, 8, 11, 4, 8, 5, -1, -1, -1,10, 3, 11, 10, 1, 3, 9, 5, 4, -1, -1, -1, -1, -1, -1,4, 9, 5, 0, 8, 1, 8, 10, 1, 8, 11, 10, -1, -1, -1,5, 4, 0, 5, 0, 11, 5, 11, 10, 11, 0, 3, -1, -1, -1,5, 4, 8, 5, 8, 10, 10, 8, 11, -1, -1, -1, -1, -1, -1,9, 7, 8, 5, 7, 9, -1, -1, -1, -1, -1, -1, -1, -1, -1,9, 3, 0, 9, 5, 3, 5, 7, 3, -1, -1, -1, -1, -1, -1,0, 7, 8, 0, 1, 7, 1, 5, 7, -1, -1, -1, -1, -1, -1,1, 5, 3, 3, 5, 7, -1, -1, -1, -1, -1, -1, -1, -1, -1,9, 7, 8, 9, 5, 7, 10, 1, 2, -1, -1, -1, -1, -1, -1,10, 1, 2, 9, 5, 0, 5, 3, 0, 5, 7, 3, -1, -1, -1,8, 0, 2, 8, 2, 5, 8, 5, 7, 10, 5, 2, -1, -1, -1,2, 10, 5, 2, 5, 3, 3, 5, 7, -1, -1, -1, -1, -1, -1,7, 9, 5, 7, 8, 9, 3, 11, 2, -1, -1, -1, -1, -1, -1,9, 5, 7, 9, 7, 2, 9, 2, 0, 2, 7, 11, -1, -1, -1,2, 3, 11, 0, 1, 8, 1, 7, 8, 1, 5, 7, -1, -1, -1,11, 2, 1, 11, 1, 7, 7, 1, 5, -1, -1, -1, -1, -1, -1,9, 5, 8, 8, 5, 7, 10, 1, 3, 10, 3, 11, -1, -1, -1,5, 7, 0, 5, 0, 9, 7, 11, 0, 1, 0, 10, 11, 10, 0,11, 10, 0, 11, 0, 3, 10, 5, 0, 8, 0, 7, 5, 7, 0,11, 10, 5, 7, 11, 5, -1, -1, -1, -1, -1, -1, -1, -1, -1,10, 6, 5, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,0, 8, 3, 5, 10, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1,9, 0, 1, 5, 10, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1,1, 8, 3, 1, 9, 8, 5, 10, 6, -1, -1, -1, -1, -1, -1,1, 6, 5, 2, 6, 1, -1, -1, -1, -1, -1, -1, -1, -1, -1,1, 6, 5, 1, 2, 6, 3, 0, 8, -1, -1, -1, -1, -1, -1,9, 6, 5, 9, 0, 6, 0, 2, 6, -1, -1, -1, -1, -1, -1,5, 9, 8, 5, 8, 2, 5, 2, 6, 3, 2, 8, -1, -1, -1,2, 3, 11, 10, 6, 5, -1, -1, -1, -1, -1, -1, -1, -1, -1,11, 0, 8, 11, 2, 0, 10, 6, 5, -1, -1, -1, -1, -1, -1,0, 1, 9, 2, 3, 11, 5, 10, 6, -1, -1, -1, -1, -1, -1,5, 10, 6, 1, 9, 2, 9, 11, 2, 9, 8, 11, -1, -1, -1,6, 3, 11, 6, 5, 3, 5, 1, 3, -1, -1, -1, -1, -1, -1,0, 8, 11, 0, 11, 5, 0, 5, 1, 5, 11, 6, -1, -1, -1,3, 11, 6, 0, 3, 6, 0, 6, 5, 0, 5, 9, -1, -1, -1,6, 5, 9, 6, 9, 11, 11, 9, 8, -1, -1, -1, -1, -1, -1,5, 10, 6, 4, 7, 8, -1, -1, -1, -1, -1, -1, -1, -1, -1,4, 3, 0, 4, 7, 3, 6, 5, 10, -1, -1, -1, -1, -1, -1,1, 9, 0, 5, 10, 6, 8, 4, 7, -1, -1, -1, -1, -1, -1,10, 6, 5, 1, 9, 7, 1, 7, 3, 7, 9, 4, -1, -1, -1,6, 1, 2, 6, 5, 1, 4, 7, 8, -1, -1, -1, -1, -1, -1,1, 2, 5, 5, 2, 6, 3, 0, 4, 3, 4, 7, -1, -1, -1,8, 4, 7, 9, 0, 5, 0, 6, 5, 0, 2, 6, -1, -1, -1,7, 3, 9, 7, 9, 4, 3, 2, 9, 5, 9, 6, 2, 6, 9,3, 11, 2, 7, 8, 4, 10, 6, 5, -1, -1, -1, -1, -1, -1,5, 10, 6, 4, 7, 2, 4, 2, 0, 2, 7, 11, -1, -1, -1,0, 1, 9, 4, 7, 8, 2, 3, 11, 5, 10, 6, -1, -1, -1,9, 2, 1, 9, 11, 2, 9, 4, 11, 7, 11, 4, 5, 10, 6,8, 4, 7, 3, 11, 5, 3, 5, 1, 5, 11, 6, -1, -1, -1,5, 1, 11, 5, 11, 6, 1, 0, 11, 7, 11, 4, 0, 4, 11,0, 5, 9, 0, 6, 5, 0, 3, 6, 11, 6, 3, 8, 4, 7,6, 5, 9, 6, 9, 11, 4, 7, 9, 7, 11, 9, -1, -1, -1,10, 4, 9, 6, 4, 10, -1, -1, -1, -1, -1, -1, -1, -1, -1,4, 10, 6, 4, 9, 10, 0, 8, 3, -1, -1, -1, -1, -1, -1,10, 0, 1, 10, 6, 0, 6, 4, 0, -1, -1, -1, -1, -1, -1,8, 3, 1, 8, 1, 6, 8, 6, 4, 6, 1, 10, -1, -1, -1,1, 4, 9, 1, 2, 4, 2, 6, 4, -1, -1, -1, -1, -1, -1,3, 0, 8, 1, 2, 9, 2, 4, 9, 2, 6, 4, -1, -1, -1,0, 2, 4, 4, 2, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1,8, 3, 2, 8, 2, 4, 4, 2, 6, -1, -1, -1, -1, -1, -1,10, 4, 9, 10, 6, 4, 11, 2, 3, -1, -1, -1, -1, -1, -1,0, 8, 2, 2, 8, 11, 4, 9, 10, 4, 10, 6, -1, -1, -1,3, 11, 2, 0, 1, 6, 0, 6, 4, 6, 1, 10, -1, -1, -1,6, 4, 1, 6, 1, 10, 4, 8, 1, 2, 1, 11, 8, 11, 1,9, 6, 4, 9, 3, 6, 9, 1, 3, 11, 6, 3, -1, -1, -1,8, 11, 1, 8, 1, 0, 11, 6, 1, 9, 1, 4, 6, 4, 1,3, 11, 6, 3, 6, 0, 0, 6, 4, -1, -1, -1, -1, -1, -1,6, 4, 8, 11, 6, 8, -1, -1, -1, -1, -1, -1, -1, -1, -1,7, 10, 6, 7, 8, 10, 8, 9, 10, -1, -1, -1, -1, -1, -1,0, 7, 3, 0, 10, 7, 0, 9, 10, 6, 7, 10, -1, -1, -1,10, 6, 7, 1, 10, 7, 1, 7, 8, 1, 8, 0, -1, -1, -1,10, 6, 7, 10, 7, 1, 1, 7, 3, -1, -1, -1, -1, -1, -1,1, 2, 6, 1, 6, 8, 1, 8, 9, 8, 6, 7, -1, -1, -1,2, 6, 9, 2, 9, 1, 6, 7, 9, 0, 9, 3, 7, 3, 9,7, 8, 0, 7, 0, 6, 6, 0, 2, -1, -1, -1, -1, -1, -1,7, 3, 2, 6, 7, 2, -1, -1, -1, -1, -1, -1, -1, -1, -1,2, 3, 11, 10, 6, 8, 10, 8, 9, 8, 6, 7, -1, -1, -1,2, 0, 7, 2, 7, 11, 0, 9, 7, 6, 7, 10, 9, 10, 7,1, 8, 0, 1, 7, 8, 1, 10, 7, 6, 7, 10, 2, 3, 11,11, 2, 1, 11, 1, 7, 10, 6, 1, 6, 7, 1, -1, -1, -1,8, 9, 6, 8, 6, 7, 9, 1, 6, 11, 6, 3, 1, 3, 6,0, 9, 1, 11, 6, 7, -1, -1, -1, -1, -1, -1, -1, -1, -1,7, 8, 0, 7, 0, 6, 3, 11, 0, 11, 6, 0, -1, -1, -1,7, 11, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,7, 6, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,3, 0, 8, 11, 7, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1,0, 1, 9, 11, 7, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1,8, 1, 9, 8, 3, 1, 11, 7, 6, -1, -1, -1, -1, -1, -1,10, 1, 2, 6, 11, 7, -1, -1, -1, -1, -1, -1, -1, -1, -1,1, 2, 10, 3, 0, 8, 6, 11, 7, -1, -1, -1, -1, -1, -1,2, 9, 0, 2, 10, 9, 6, 11, 7, -1, -1, -1, -1, -1, -1,6, 11, 7, 2, 10, 3, 10, 8, 3, 10, 9, 8, -1, -1, -1,7, 2, 3, 6, 2, 7, -1, -1, -1, -1, -1, -1, -1, -1, -1,7, 0, 8, 7, 6, 0, 6, 2, 0, -1, -1, -1, -1, -1, -1,2, 7, 6, 2, 3, 7, 0, 1, 9, -1, -1, -1, -1, -1, -1,1, 6, 2, 1, 8, 6, 1, 9, 8, 8, 7, 6, -1, -1, -1,10, 7, 6, 10, 1, 7, 1, 3, 7, -1, -1, -1, -1, -1, -1,10, 7, 6, 1, 7, 10, 1, 8, 7, 1, 0, 8, -1, -1, -1,0, 3, 7, 0, 7, 10, 0, 10, 9, 6, 10, 7, -1, -1, -1,7, 6, 10, 7, 10, 8, 8, 10, 9, -1, -1, -1, -1, -1, -1,6, 8, 4, 11, 8, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1,3, 6, 11, 3, 0, 6, 0, 4, 6, -1, -1, -1, -1, -1, -1,8, 6, 11, 8, 4, 6, 9, 0, 1, -1, -1, -1, -1, -1, -1,9, 4, 6, 9, 6, 3, 9, 3, 1, 11, 3, 6, -1, -1, -1,6, 8, 4, 6, 11, 8, 2, 10, 1, -1, -1, -1, -1, -1, -1,1, 2, 10, 3, 0, 11, 0, 6, 11, 0, 4, 6, -1, -1, -1,4, 11, 8, 4, 6, 11, 0, 2, 9, 2, 10, 9, -1, -1, -1,10, 9, 3, 10, 3, 2, 9, 4, 3, 11, 3, 6, 4, 6, 3,8, 2, 3, 8, 4, 2, 4, 6, 2, -1, -1, -1, -1, -1, -1,0, 4, 2, 4, 6, 2, -1, -1, -1, -1, -1, -1, -1, -1, -1,1, 9, 0, 2, 3, 4, 2, 4, 6, 4, 3, 8, -1, -1, -1,1, 9, 4, 1, 4, 2, 2, 4, 6, -1, -1, -1, -1, -1, -1,8, 1, 3, 8, 6, 1, 8, 4, 6, 6, 10, 1, -1, -1, -1,10, 1, 0, 10, 0, 6, 6, 0, 4, -1, -1, -1, -1, -1, -1,4, 6, 3, 4, 3, 8, 6, 10, 3, 0, 3, 9, 10, 9, 3,10, 9, 4, 6, 10, 4, -1, -1, -1, -1, -1, -1, -1, -1, -1,4, 9, 5, 7, 6, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1,0, 8, 3, 4, 9, 5, 11, 7, 6, -1, -1, -1, -1, -1, -1,5, 0, 1, 5, 4, 0, 7, 6, 11, -1, -1, -1, -1, -1, -1,11, 7, 6, 8, 3, 4, 3, 5, 4, 3, 1, 5, -1, -1, -1,9, 5, 4, 10, 1, 2, 7, 6, 11, -1, -1, -1, -1, -1, -1,6, 11, 7, 1, 2, 10, 0, 8, 3, 4, 9, 5, -1, -1, -1,7, 6, 11, 5, 4, 10, 4, 2, 10, 4, 0, 2, -1, -1, -1,3, 4, 8, 3, 5, 4, 3, 2, 5, 10, 5, 2, 11, 7, 6,7, 2, 3, 7, 6, 2, 5, 4, 9, -1, -1, -1, -1, -1, -1,9, 5, 4, 0, 8, 6, 0, 6, 2, 6, 8, 7, -1, -1, -1,3, 6, 2, 3, 7, 6, 1, 5, 0, 5, 4, 0, -1, -1, -1,6, 2, 8, 6, 8, 7, 2, 1, 8, 4, 8, 5, 1, 5, 8,9, 5, 4, 10, 1, 6, 1, 7, 6, 1, 3, 7, -1, -1, -1,1, 6, 10, 1, 7, 6, 1, 0, 7, 8, 7, 0, 9, 5, 4,4, 0, 10, 4, 10, 5, 0, 3, 10, 6, 10, 7, 3, 7, 10,7, 6, 10, 7, 10, 8, 5, 4, 10, 4, 8, 10, -1, -1, -1,6, 9, 5, 6, 11, 9, 11, 8, 9, -1, -1, -1, -1, -1, -1,3, 6, 11, 0, 6, 3, 0, 5, 6, 0, 9, 5, -1, -1, -1,0, 11, 8, 0, 5, 11, 0, 1, 5, 5, 6, 11, -1, -1, -1,6, 11, 3, 6, 3, 5, 5, 3, 1, -1, -1, -1, -1, -1, -1,1, 2, 10, 9, 5, 11, 9, 11, 8, 11, 5, 6, -1, -1, -1,0, 11, 3, 0, 6, 11, 0, 9, 6, 5, 6, 9, 1, 2, 10,11, 8, 5, 11, 5, 6, 8, 0, 5, 10, 5, 2, 0, 2, 5,6, 11, 3, 6, 3, 5, 2, 10, 3, 10, 5, 3, -1, -1, -1,5, 8, 9, 5, 2, 8, 5, 6, 2, 3, 8, 2, -1, -1, -1,9, 5, 6, 9, 6, 0, 0, 6, 2, -1, -1, -1, -1, -1, -1,1, 5, 8, 1, 8, 0, 5, 6, 8, 3, 8, 2, 6, 2, 8,1, 5, 6, 2, 1, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1,1, 3, 6, 1, 6, 10, 3, 8, 6, 5, 6, 9, 8, 9, 6,10, 1, 0, 10, 0, 6, 9, 5, 0, 5, 6, 0, -1, -1, -1,0, 3, 8, 5, 6, 10, -1, -1, -1, -1, -1, -1, -1, -1, -1,10, 5, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,11, 5, 10, 7, 5, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1,11, 5, 10, 11, 7, 5, 8, 3, 0, -1, -1, -1, -1, -1, -1,5, 11, 7, 5, 10, 11, 1, 9, 0, -1, -1, -1, -1, -1, -1,10, 7, 5, 10, 11, 7, 9, 8, 1, 8, 3, 1, -1, -1, -1,11, 1, 2, 11, 7, 1, 7, 5, 1, -1, -1, -1, -1, -1, -1,0, 8, 3, 1, 2, 7, 1, 7, 5, 7, 2, 11, -1, -1, -1,9, 7, 5, 9, 2, 7, 9, 0, 2, 2, 11, 7, -1, -1, -1,7, 5, 2, 7, 2, 11, 5, 9, 2, 3, 2, 8, 9, 8, 2,2, 5, 10, 2, 3, 5, 3, 7, 5, -1, -1, -1, -1, -1, -1,8, 2, 0, 8, 5, 2, 8, 7, 5, 10, 2, 5, -1, -1, -1,9, 0, 1, 5, 10, 3, 5, 3, 7, 3, 10, 2, -1, -1, -1,9, 8, 2, 9, 2, 1, 8, 7, 2, 10, 2, 5, 7, 5, 2,1, 3, 5, 3, 7, 5, -1, -1, -1, -1, -1, -1, -1, -1, -1,0, 8, 7, 0, 7, 1, 1, 7, 5, -1, -1, -1, -1, -1, -1,9, 0, 3, 9, 3, 5, 5, 3, 7, -1, -1, -1, -1, -1, -1,9, 8, 7, 5, 9, 7, -1, -1, -1, -1, -1, -1, -1, -1, -1,5, 8, 4, 5, 10, 8, 10, 11, 8, -1, -1, -1, -1, -1, -1,5, 0, 4, 5, 11, 0, 5, 10, 11, 11, 3, 0, -1, -1, -1,0, 1, 9, 8, 4, 10, 8, 10, 11, 10, 4, 5, -1, -1, -1,10, 11, 4, 10, 4, 5, 11, 3, 4, 9, 4, 1, 3, 1, 4,2, 5, 1, 2, 8, 5, 2, 11, 8, 4, 5, 8, -1, -1, -1,0, 4, 11, 0, 11, 3, 4, 5, 11, 2, 11, 1, 5, 1, 11,0, 2, 5, 0, 5, 9, 2, 11, 5, 4, 5, 8, 11, 8, 5,9, 4, 5, 2, 11, 3, -1, -1, -1, -1, -1, -1, -1, -1, -1,2, 5, 10, 3, 5, 2, 3, 4, 5, 3, 8, 4, -1, -1, -1,5, 10, 2, 5, 2, 4, 4, 2, 0, -1, -1, -1, -1, -1, -1,3, 10, 2, 3, 5, 10, 3, 8, 5, 4, 5, 8, 0, 1, 9,5, 10, 2, 5, 2, 4, 1, 9, 2, 9, 4, 2, -1, -1, -1,8, 4, 5, 8, 5, 3, 3, 5, 1, -1, -1, -1, -1, -1, -1,0, 4, 5, 1, 0, 5, -1, -1, -1, -1, -1, -1, -1, -1, -1,8, 4, 5, 8, 5, 3, 9, 0, 5, 0, 3, 5, -1, -1, -1,9, 4, 5, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,4, 11, 7, 4, 9, 11, 9, 10, 11, -1, -1, -1, -1, -1, -1,0, 8, 3, 4, 9, 7, 9, 11, 7, 9, 10, 11, -1, -1, -1,1, 10, 11, 1, 11, 4, 1, 4, 0, 7, 4, 11, -1, -1, -1,3, 1, 4, 3, 4, 8, 1, 10, 4, 7, 4, 11, 10, 11, 4,4, 11, 7, 9, 11, 4, 9, 2, 11, 9, 1, 2, -1, -1, -1,9, 7, 4, 9, 11, 7, 9, 1, 11, 2, 11, 1, 0, 8, 3,11, 7, 4, 11, 4, 2, 2, 4, 0, -1, -1, -1, -1, -1, -1,11, 7, 4, 11, 4, 2, 8, 3, 4, 3, 2, 4, -1, -1, -1,2, 9, 10, 2, 7, 9, 2, 3, 7, 7, 4, 9, -1, -1, -1,9, 10, 7, 9, 7, 4, 10, 2, 7, 8, 7, 0, 2, 0, 7,3, 7, 10, 3, 10, 2, 7, 4, 10, 1, 10, 0, 4, 0, 10,1, 10, 2, 8, 7, 4, -1, -1, -1, -1, -1, -1, -1, -1, -1,4, 9, 1, 4, 1, 7, 7, 1, 3, -1, -1, -1, -1, -1, -1,4, 9, 1, 4, 1, 7, 0, 8, 1, 8, 7, 1, -1, -1, -1,4, 0, 3, 7, 4, 3, -1, -1, -1, -1, -1, -1, -1, -1, -1,4, 8, 7, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,9, 10, 8, 10, 11, 8, -1, -1, -1, -1, -1, -1, -1, -1, -1,3, 0, 9, 3, 9, 11, 11, 9, 10, -1, -1, -1, -1, -1, -1,0, 1, 10, 0, 10, 8, 8, 10, 11, -1, -1, -1, -1, -1, -1,3, 1, 10, 11, 3, 10, -1, -1, -1, -1, -1, -1, -1, -1, -1,1, 2, 11, 1, 11, 9, 9, 11, 8, -1, -1, -1, -1, -1, -1,3, 0, 9, 3, 9, 11, 1, 2, 9, 2, 11, 9, -1, -1, -1,0, 2, 11, 8, 0, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1,3, 2, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,2, 3, 8, 2, 8, 10, 10, 8, 9, -1, -1, -1, -1, -1, -1,9, 10, 2, 0, 9, 2, -1, -1, -1, -1, -1, -1, -1, -1, -1,2, 3, 8, 2, 8, 10, 0, 1, 8, 1, 10, 8, -1, -1, -1,1, 10, 2, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,1, 3, 8, 9, 1, 8, -1, -1, -1, -1, -1, -1, -1, -1, -1,0, 9, 1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,0, 3, 8, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
};

class Trisualizer {
    GLFWwindow* window = nullptr;
    ImFont* font_title = nullptr;
public:
    std::vector<Graph> graphs;
    std::vector<Slider> sliders;

    float theta = 135, phi = 45;
    vec2 cameraVelocity = vec2(0.f);
    std::bitset<4> keys{ 0x0 };
    double zoomTimestamp = 0.f;
    float zoomSpeed = 1.f;
    float zoomx = 8.f;
    float zoomy = 8.f;
    float zoomz = 8.f;
    float graph_size = 1.3f;
    float gridLineDensity = 3.f;
    bool shading = true;
    int coloring = SingleColor;
    bool autoRotate = false;
    bool tangent_plane = false, apply_tangent_plane = false;
    bool gradient_vector = false;
    bool normal_vector = false;
    bool show_axes = false;
    vec2 xrange, yrange, zrange;
    vec3 light_pos = vec3(0.f, 50.f, 0.f);

    bool dintegral = false;
    bool integral = false, second_corner = false, apply_integral = false, show_integral_result = false;
    int integrand_index = 1, region_type = CartesianRectangle, integral_precision = 2000, erroring_eq = -1;
    float x_min{}, x_max{}, y_min{}, y_max{}, theta_min{}, theta_max{}, t_min{}, t_max{};
    char x_min_eq[32]{}, x_max_eq[32]{}, y_min_eq[32]{}, y_max_eq[32]{}, r_min_eq[32]{}, r_max_eq[32]{}, x_param_eq[32]{}, y_param_eq[32]{}, scalar_field_eq[32]{}, integral_infoLog[512]{};
    float x_min_eq_min{}, x_max_eq_max{}, y_min_eq_min{}, y_max_eq_max{};
    vec3 center_of_region;
    float integral_result, dx, dy, dt;
    IntegralType last_integration_type;
    float* li_data = nullptr;
    float* li_data_ws = nullptr;
    size_t li_samplecount = 0;

    vec3 vector_start = vec3(0.f), vector_end = vec3(0.f, 0.5f, 0.f);

    bool cursor_on_point;
    int graph_index;
    vec3 fragPos;
    vec2 gradient;

    std::pair<vec3, vec3> integral_limits;
    vec3 centerPos = vec3(0.f);
    vec3 next_centerPos = vec3(0.f);
    vec3 temp_centerPos;
    double moveTimestamp;
    vec2 mousePos = vec2(0.f);
    ivec2 prevWindowPos = ivec2(200, 200);
    ivec2 prevWindowSize = ivec2(1000, 600);
    int sidebarWidth = 0;
    bool updateBufferSize = false;
    double lastMousePress = 0.0;
    bool doubleClickPressed = false;
    bool rightClickPending = false;
    bool rightClickPressed = false;
    float dpi_scale = 1.f;
    int ssaa_factor = 3; // change to 3 when enabling SSAA by default
    bool ssaa = true;
    bool ssaa_enabled_by_user = false;
    int frameCount = 0;
    std::vector<double> fps_history = std::vector<double>(5, 0.0);

    GLuint shaderProgram;
    GLuint VAO, VBO, EBO;
    GLuint FBO, gridSSBO;
    GLuint depthMap, frameTex, prevZBuffer, posBuffer, kernelBuffer, sliderBuffer;

    void check_for_errors(GLuint shader) {
        int success;
        char infoLog[1024];
        glGetShaderiv(shader, GL_COMPILE_STATUS, &success);

        if (!success) {
            glGetShaderInfoLog(shader, 1024, NULL, infoLog);
            throw compilation_error(infoLog);
        }
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
        glfwWindowHintString(GLFW_WAYLAND_APP_ID, "trisualizer");
        glfwWindowHint(GLFW_SAMPLES, 4);

        const char* session = std::getenv("XDG_SESSION_DESKTOP");
        const char* hyprSig = std::getenv("HYPRLAND_INSTANCE_SIGNATURE");
        if ((session && std::string(session) == "Hyprland") || (hyprSig != nullptr)) {
            system("hyprctl keyword windowrulev2 float, class:trisualizer");
        }

        window = glfwCreateWindow(1000, 600, "Trisualizer", NULL, NULL);
        if (window == nullptr) {
            throw std::runtime_error("Failed to create window.");
        }

#ifdef PLATFORM_LINUX
        const char* wayland_display = std::getenv("WAYLAND_DISPLAY");
        const char* x11_display = std::getenv("DISPLAY");

        if (wayland_display) {
            // Fix for scaling in Hyprland specifically
            if ((session && std::string(session) == "Hyprland") || (hyprSig != nullptr)) {
                auto exec_command = [](const char* cmd) {
                    std::array<char, 128> buffer;
                    std::stringstream result;
                    FILE* pipe = popen(cmd, "r");
                    if (!pipe) return std::string();
                    while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
                        result << buffer.data();
                    }
                    pclose(pipe);
                    return result.str();
                };
                std::string json = exec_command("hyprctl monitors -j");
                if (!json.empty()) {
                    auto parsedjson = nlohmann::json::parse(json);
                    std::string monitor = nlohmann::json::parse(exec_command("hyprctl activeworkspace -j"))["monitor"];
                    for (const auto& m : parsedjson) {
                        if (m["name"] == monitor) {
                            dpi_scale = m["scale"].get<float>();
                            break;
                        }
                    }
                }
            }
            else {
                float xscale, yscale;
                glfwGetWindowContentScale(window, &xscale, &yscale);
                dpi_scale = xscale;
            }
        }
#endif
        glfwSetWindowUserPointer(window, this);
        glfwSwapInterval(1);
        glfwMakeContextCurrent(window);

        glfwSetCursorPosCallback(window, on_mouseMove);
        glfwSetScrollCallback(window, on_mouseScroll);
        glfwSetWindowSizeCallback(window, on_windowResize);
        glfwSetMouseButtonCallback(window, on_mouseButton);
        glfwSetKeyCallback(window, on_keyPress);

        auto icon = b::embed<"assets/main.bmp">();
        uint8_t pixels[32 * 32 * 4];
        parseBMP(reinterpret_cast<const uint8_t*>(icon.data()), icon.size(), pixels);
        GLFWimage icons[1];
        icons[0].width = 32;
        icons[0].height = 32;
        icons[0].pixels = pixels;
        glfwSetWindowIcon(window, 1, icons);

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO(); (void)io;

        io.Fonts->Clear();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        io.IniFilename = NULL;
        io.LogFilename = NULL;
        static const ImWchar ranges[] = {
            0x0020, 0x2264, 0x2264, 0xFFFF
        };
#ifndef PLATFORM_WINDOWS
        auto font = b::embed<"assets/consola.ttf">();
        font_title = io.Fonts->AddFontFromMemoryTTF((void*)font.data(), font.size(), 11.f, nullptr, ranges);
#else
        font_title = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\consola.ttf", 11.f, nullptr, ranges);
        BOOL use_dark_mode = true;
        DwmSetWindowAttribute(glfwGetWin32Window(window), 20, &use_dark_mode, sizeof(use_dark_mode));
#endif
        IM_ASSERT(font_title != NULL);

        ImGuiStyle& style = ImGui::GetStyle();
        ImGui::StyleColorsDark();
        ImGui::LoadTheme();

        ImGui_ImplGlfw_InitForOpenGL(window, true);
        ImGui_ImplOpenGL3_Init("#version 460");

        if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))) {
            throw std::runtime_error("Failed to create OpenGL context. Make sure your GPU supports OpenGL 4.6");
        }

        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glEnable(GL_BLEND);
#ifdef _DEBUG
        glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
        glDebugMessageCallback(glMessageCallback, nullptr);
#endif
        b::EmbedInternal::EmbeddedFile embed;
        const char* content;
        int length;

        unsigned int vertexShader = glCreateShader(GL_VERTEX_SHADER);
        embed = b::embed<"shaders/vertex.glsl">();
        content = embed.data();
        length = embed.length();
        glShaderSource(vertexShader, 1, &content, NULL);
        glCompileShader(vertexShader);
        check_for_errors(vertexShader);

        unsigned int fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
        embed = b::embed<"shaders/fragment.glsl">();
        content = embed.data();
        length = embed.length();
        glShaderSource(fragmentShader, 1, &content, NULL);
        glCompileShader(fragmentShader);
        check_for_errors(fragmentShader);

        shaderProgram = glCreateProgram();
        glAttachShader(shaderProgram, vertexShader);
        glAttachShader(shaderProgram, fragmentShader);
        glLinkProgram(shaderProgram);
        glDeleteShader(fragmentShader);
        glDeleteShader(vertexShader);
        glUseProgram(shaderProgram);

        glGenBuffers(1, &gridSSBO);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, gridSSBO);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, gridSSBO);
        glShaderStorageBlockBinding(shaderProgram, glGetProgramResourceIndex(shaderProgram, GL_SHADER_STORAGE_BLOCK, "gridbuffer"), 0);

        glGenBuffers(1, &posBuffer);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, posBuffer);
        glBufferData(GL_SHADER_STORAGE_BUFFER, 6 * 700 * dpi_scale * 600 * dpi_scale * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
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
        glUniform3fv(glGetUniformLocation(shaderProgram, "lightPos"), 1, value_ptr(light_pos));
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
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, 1000 * ssaa_factor * dpi_scale, 600 * ssaa_factor * dpi_scale, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthMap, 0);

        glGenTextures(1, &frameTex);
        glBindTexture(GL_TEXTURE_2D, frameTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, 1000 * ssaa_factor * dpi_scale, 600 * ssaa_factor * dpi_scale, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, frameTex, 0);

        glGenTextures(1, &prevZBuffer);
        glBindTexture(GL_TEXTURE_2D, prevZBuffer);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, 1000 * ssaa_factor * dpi_scale, 600 * ssaa_factor * dpi_scale, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
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
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, width * app->ssaa_factor * app->dpi_scale, height * app->ssaa_factor * app->dpi_scale, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
        glBindTexture(GL_TEXTURE_2D, app->frameTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, width * app->ssaa_factor * app->dpi_scale, height * app->ssaa_factor * app->dpi_scale, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glBindTexture(GL_TEXTURE_2D, app->prevZBuffer);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, width * app->ssaa_factor * app->dpi_scale, height * app->ssaa_factor * app->dpi_scale, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, app->posBuffer);
        glBufferData(GL_SHADER_STORAGE_BUFFER, 6 * (size_t)(width - app->sidebarWidth) * app->dpi_scale * height * app->dpi_scale * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    }

    static inline void on_mouseButton(GLFWwindow* window, int button, int action, int mods) {
        Trisualizer* app = static_cast<Trisualizer*>(glfwGetWindowUserPointer(window));
        switch (button) {
        case GLFW_MOUSE_BUTTON_LEFT:
            app->rightClickPressed = false;
            switch (action) {
            case GLFW_RELEASE:
                if (app->updateBufferSize) {
                    int width, height;
                    glfwGetWindowSize(window, &width, &height);
                    glBindBuffer(GL_SHADER_STORAGE_BUFFER, app->posBuffer);
                    glBufferData(GL_SHADER_STORAGE_BUFFER, 6ull * (width - app->sidebarWidth) * app->dpi_scale * height * app->dpi_scale * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
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
            break;
        case GLFW_MOUSE_BUTTON_RIGHT:
            switch (action) {
            case GLFW_PRESS:
                app->rightClickPending = true;
                break;
            case GLFW_RELEASE:
                app->rightClickPressed = app->rightClickPending;
            }
        }
    }

    static inline void on_mouseScroll(GLFWwindow* window, double x, double y) {
        Trisualizer* app = static_cast<Trisualizer*>(glfwGetWindowUserPointer(window));
        if (ImGui::GetIO().WantCaptureMouse) return;
        if (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS) {
            app->graph_size *= pow(0.9f, -y);
            glUniform1f(glGetUniformLocation(app->shaderProgram, "graph_size"), app->graph_size);
        } else {
            float factor = glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ? 0.985f : 0.95f;
            app->zoomSpeed = pow(factor, y);
            app->zoomTimestamp = glfwGetTime();
        }
    }

    static inline void on_mouseMove(GLFWwindow* window, double x, double y) {
        Trisualizer* app = static_cast<Trisualizer*>(glfwGetWindowUserPointer(window));
        if (ImGui::GetIO().WantCaptureMouse && !app->rightClickPressed) return;
        if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS) {
            float xoffset = x - app->mousePos.x;
            float yoffset = y - app->mousePos.y;
            app->theta += yoffset * 0.5f;
            app->phi -= xoffset * 0.5f;
            if (app->theta > 179.9f) app->theta = 179.9f;
            if (app->theta < 0.1f) app->theta = 0.1f;
            app->rightClickPending = false;
            app->rightClickPressed = false;
        }
        app->mousePos = { x, y };
    }

    static inline void on_keyPress(GLFWwindow* window, int key, int scancode, int action, int mods) {
        Trisualizer* app = static_cast<Trisualizer*>(glfwGetWindowUserPointer(window));
        float angle_r = app->phi * M_PI / 180.f;
        switch (action) {
        case GLFW_PRESS:
            if (!ImGui::GetIO().WantCaptureKeyboard) app->keys |= ((int)(key == GLFW_KEY_W) | (int)(key == GLFW_KEY_A) << 1 | (int)(key == GLFW_KEY_S) << 2 | (int)(key == GLFW_KEY_D) << 3);
            switch (key) {
            case GLFW_KEY_F11:
                if (glfwGetWindowMonitor(window) == nullptr) {
                    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
                    const GLFWvidmode* mode = glfwGetVideoMode(monitor);
                    glfwGetWindowPos(window, &app->prevWindowPos.x, &app->prevWindowPos.y);
                    glfwGetWindowSize(window, &app->prevWindowSize.x, &app->prevWindowSize.y);
                    glfwSetWindowMonitor(window, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
                }
                else {
                    glfwSetWindowMonitor(window, nullptr, app->prevWindowPos.x, app->prevWindowPos.y, app->prevWindowSize.x, app->prevWindowSize.y, 0);
                }
                break;
            }
            break;
        case GLFW_RELEASE:
            app->keys &= ((int)(key != GLFW_KEY_W) | (int)(key != GLFW_KEY_A) << 1 | (int)(key != GLFW_KEY_S) << 2 | (int)(key != GLFW_KEY_D) << 3);
        }
    }

    void save_file() {
        // OPENFILENAMEA ofn{};
        // auto t = std::time(nullptr);
        // std::tm bt{};
        // localtime_s(&bt, &t);
        // std::ostringstream oss;
        // oss << std::put_time(&bt, "Trisualizer Snapshot %d-%m-%Y %H-%M-%S.tris");
        // std::string filename = oss.str();

        // char szFileName[MAX_PATH]{};
        // char szFileTitle[MAX_PATH]{};
        // char filePath[MAX_PATH]{};
        // char szFile[MAX_PATH];

        // filename.copy(szFile, filename.size());
        // szFile[filename.size()] = '\0';

        // ofn.lpstrFile = szFile;
        // ofn.lStructSize = sizeof(OPENFILENAME);
        // ofn.hwndOwner = GetFocus();
        // ofn.lpstrFilter = "Trisualizer Snapshot (*.tris)\0*.tris\0All Files (*.*)\0*.*\0";
        // ofn.lpstrCustomFilter = NULL;
        // ofn.nMaxCustFilter = NULL;
        // ofn.nFilterIndex = NULL;
        // ofn.nMaxFile = MAX_PATH;
        // ofn.lpstrInitialDir = ".";
        // ofn.lpstrFileTitle = szFileTitle;
        // ofn.nMaxFileTitle = MAX_PATH;
        // ofn.lpstrTitle = "Save the current configuration to a file...";
        // ofn.lpstrDefExt = "*.tris";

        // ofn.Flags = OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY;

        // if (!GetSaveFileNameA(reinterpret_cast<LPOPENFILENAMEA>(&ofn)))
        //     return;
        // std::ofstream out;
        // out.open(ofn.lpstrFile, std::ios::out | std::ios::binary | std::ofstream::trunc);

        // out.write(CNFGVER, 1);
        // auto cast = []<typename T>(T d) {
        //     return reinterpret_cast<const char*>(d);
        // };
        // size_t n = graphs.size();
        // out.write(cast(&n), sizeof(size_t));
        // n = sliders.size();
        // out.write(cast(&n), sizeof(size_t));

        // out.write(cast(graphs.data()), graphs.size() * sizeof(Graph));
        // out.write(cast(sliders.data()), sliders.size() * sizeof(Slider));

        // out.write(cast(value_ptr(xrange)), 2 * sizeof(float));
        // out.write(cast(value_ptr(yrange)), 2 * sizeof(float));
        // out.write(cast(value_ptr(zrange)), 2 * sizeof(float));
        // out.write(cast(&coloring), sizeof(int));
        // out.write(cast(&shading), sizeof(bool));

        // out.close();
    }

    void open_file() {
        // OPENFILENAMEA ofn{};

        // char szFileName[MAX_PATH]{};
        // char szFileTitle[MAX_PATH]{};

        // ofn.lStructSize = sizeof(OPENFILENAME);
        // ofn.hwndOwner = GetFocus();
        // ofn.lpstrFilter = "Trisualizer Snapshot (*.tris)\0*.tris\0All Files (*.*)\0*.*\0";
        // ofn.lpstrCustomFilter = NULL;
        // ofn.nMaxCustFilter = NULL;
        // ofn.nFilterIndex = NULL;
        // ofn.lpstrFile = szFileName;
        // ofn.nMaxFile = MAX_PATH;
        // ofn.lpstrInitialDir = ".";
        // ofn.lpstrFileTitle = szFileTitle;
        // ofn.nMaxFileTitle = MAX_PATH;
        // ofn.lpstrTitle = "Load a Trisualizer configuration...";
        // ofn.lpstrDefExt = "*.tris";
        // ofn.Flags = OFN_FILEMUSTEXIST;

        // if (!GetOpenFileNameA(reinterpret_cast<LPOPENFILENAMEA>(&ofn)))
        //     return;
        // std::ifstream in;
        // in.open(ofn.lpstrFile, std::ios::in | std::ios::binary);

        // char ver[1]{};
        // in.read(&ver[0], 1);
        // if (ver[0] != CNFGVER[0]) {
        //     MessageBoxA(NULL, std::format("This file is from a previous version of Trisualizer ({}.0) and will not work on this version.", ver[0]).c_str(), "Incompatible version", MB_ICONERROR | MB_OK);
        //     return;
        // }
        // int num_graphs, num_sliders;
        // char buf[8]{};
        // in.read(&buf[0], sizeof(size_t));
        // num_graphs = *reinterpret_cast<size_t*>(buf);
        // in.read(&buf[0], sizeof(size_t));
        // num_sliders = *reinterpret_cast<size_t*>(buf);

        // char* graphs_buf = new char[num_graphs * sizeof(Graph)];
        // in.read(graphs_buf, num_graphs * sizeof(Graph));
        // graphs.clear();
        // for (size_t i = 0; i < num_graphs; i++) {
        //     graphs.push_back(Graph());
        //     graphs[i] = reinterpret_cast<Graph*>(graphs_buf)[i];
        // }
        // delete[] graphs_buf;

        // char* sliders_buf = new char[num_sliders * sizeof(Slider)];
        // in.read(sliders_buf, num_sliders * sizeof(Slider));
        // sliders.clear();
        // for (size_t i = 0; i < num_sliders; i++) {
        //     sliders.push_back(Slider());
        //     sliders[i] = reinterpret_cast<Slider*>(sliders_buf)[i];
        // }
        // delete[] sliders_buf;

        // for (Slider& s : sliders) {
        //     s.used_in.resize(graphs.size(), false);
        // }
        // for (Graph& g : graphs) {
        //     g.SSBO = gridSSBO;
        //     g.EBO = EBO;
        //     g.upload_definition(sliders);
        //     g.setup();
        // }

        // in.read(&buf[0], 2 * sizeof(float));
        // xrange[0] = *reinterpret_cast<float*>(buf);
        // xrange[1] = *reinterpret_cast<float*>(buf + sizeof(float));
        // in.read(&buf[0], 2 * sizeof(float));
        // yrange[0] = *reinterpret_cast<float*>(buf);
        // yrange[1] = *reinterpret_cast<float*>(buf + sizeof(float));
        // in.read(&buf[0], 2 * sizeof(float));
        // zrange[0] = *reinterpret_cast<float*>(buf);
        // zrange[1] = *reinterpret_cast<float*>(buf + sizeof(float));

        // glUseProgram(shaderProgram);

        // zoomx = abs(xrange[0] - xrange[1]);
        // centerPos.x = (xrange[0] + xrange[1]) / 2.f;
        // zoomy = abs(yrange[0] - yrange[1]);
        // centerPos.y = (yrange[0] + yrange[1]) / 2.f;
        // zoomz = abs(zrange[0] - zrange[1]);
        // centerPos.z = (zrange[0] + zrange[1]) / 2.f;
        // glUniform3fv(glGetUniformLocation(shaderProgram, "centerPos"), 1, value_ptr(centerPos));

        // in.read(&buf[0], sizeof(int));
        // coloring = *reinterpret_cast<int*>(buf);
        // glUniform1i(glGetUniformLocation(shaderProgram, "coloring"), coloring);
        // in.read(&buf[0], sizeof(bool));
        // shading = *reinterpret_cast<bool*>(buf);
        // glUniform1i(glGetUniformLocation(shaderProgram, "shading"), shading);

        // in.close();
    }

    void draw_lineintegral(vec3 color, mat4 view, mat4 proj) {
        b::EmbedInternal::EmbeddedFile embed;
        const char* content;
        int length;

        unsigned int vertexShader = glCreateShader(GL_VERTEX_SHADER);
        const char* vertexSource = R"glsl(
#version 460 core

layout (location = 0) in vec3 aPos;

uniform mat4 view;
uniform mat4 proj;

out vec3 fragPos;
out vec3 normal;

void main() {
    fragPos = aPos;
    gl_Position = proj * view * vec4(aPos, 1.f);
})glsl";
        glShaderSource(vertexShader, 1, &vertexSource, NULL);
        glCompileShader(vertexShader);
        check_for_errors(vertexShader);

        unsigned int fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
        const char* fragmentSource = R"glsl(
#version 460 core

in vec3 fragPos;
out vec4 fragColor;

uniform vec3 color;

void main() {
    fragColor = vec4(color, 1.0);
})glsl";
        glShaderSource(fragmentShader, 1, &fragmentSource, NULL);
        glCompileShader(fragmentShader);
        check_for_errors(fragmentShader);

        GLuint vectorShaderProgram = glCreateProgram();
        glAttachShader(vectorShaderProgram, vertexShader);
        glAttachShader(vectorShaderProgram, fragmentShader);
        glLinkProgram(vectorShaderProgram);
        glDeleteShader(fragmentShader);
        glDeleteShader(vertexShader);
        glUseProgram(vectorShaderProgram);

        GLuint vao, vbo;
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glBindVertexArray(vao);


        for (int i = 0; i < li_samplecount; i++) {
            vec3 cvu = to_worldspace(vec3(li_data[i * 3], li_data[i * 3 + 1], li_data[i * 3 + 2]));
            vec3 cvd = to_worldspace(vec3(li_data[i * 3], li_data[i * 3 + 1], 0.f));

            li_data_ws[i * 6 + 0] = cvu.x;
            li_data_ws[i * 6 + 1] = cvu.y;
            li_data_ws[i * 6 + 2] = cvu.z;
            li_data_ws[i * 6 + 3] = cvd.x;
            li_data_ws[i * 6 + 4] = cvd.y;
            li_data_ws[i * 6 + 5] = cvd.z;
        }

        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, li_samplecount * 6 * sizeof(float), li_data_ws, GL_STATIC_DRAW);

        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
        glEnableVertexAttribArray(0);

        glUniformMatrix4fv(glGetUniformLocation(vectorShaderProgram, "view"), 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(glGetUniformLocation(vectorShaderProgram, "proj"), 1, GL_FALSE, glm::value_ptr(proj));

        glUniform3fv(glGetUniformLocation(vectorShaderProgram, "color"), 1, value_ptr(color));

        glDrawArrays(GL_TRIANGLE_STRIP, 0, li_samplecount * 2);

        glDeleteProgram(vectorShaderProgram);
        glDeleteBuffers(1, &vbo);
        glDeleteVertexArrays(1, &vao);

        glUseProgram(shaderProgram);
        glBindVertexArray(VAO);
        glBindBuffer(GL_ARRAY_BUFFER, VBO);
    }

    // TODO: add shadows under arrow
    void draw_vector(vec3 start, vec3 end, vec3 color, mat4 view, mat4 proj, float thickness = 1.f) {
        const float factor = graph_size / 1.3f;
        float magnitude = distance(start, end);
        float tip_height = clamp(magnitude / 2.f, 0.01f, 0.1f * factor) * thickness;
        float bottom_radius = 0.01f * factor * thickness;
        float top_radius = 0.03f * factor * thickness;
        int segments = 20 * factor;
        auto push = [](std::vector<float>& arr, vec3 v, vec3 normal) {
            arr.push_back(v.x);
            arr.push_back(v.y);
            arr.push_back(v.z);
            arr.push_back(normal.x);
            arr.push_back(normal.y);
            arr.push_back(normal.z);
        };
        std::vector<float> bottom_circle;
        std::vector<float> cylinder;
        std::vector<float> top_circle;
        std::vector<float> conic_head;

        vec3 bottom_normal = vec3(0,-1,0);
        push(bottom_circle, vec3(0.f), bottom_normal);
        for (float a = 0.f; a < 2.f * M_PI; a += 2.f * M_PI / segments) {
            push(bottom_circle, vec3(bottom_radius * cos(a), 0.f, bottom_radius * sin(a)), bottom_normal);
        }
        push(bottom_circle, vec3(bottom_radius, 0.f, 0.f), bottom_normal);

        for (float a = 0.f; a <= 2.f * M_PI; a += 2.f * M_PI / segments) {
            vec3 normal = vec3(cos(a), 0.f, sin(a));
            push(cylinder, bottom_radius * vec3(cos(a), 0.f, sin(a)), normal);
            push(cylinder, vec3(bottom_radius * cos(a), magnitude - tip_height, bottom_radius * sin(a)), normal);
        }
        push(cylinder, vec3(bottom_radius, 0.f, 0.f), vec3(1.f, 0.f, 0.f));
        push(cylinder, vec3(bottom_radius, magnitude - tip_height, 0.f), vec3(1.f, 0.f, 0.f));

        vec3 top_normal = vec3(0, -1, 0);
        push(top_circle, vec3(0.f, magnitude - tip_height, 0.f), top_normal);
        for (float a = 0.f; a < 2.f * M_PI; a += 2.f * M_PI / segments) {
            push(top_circle, vec3(top_radius * cos(a), magnitude - tip_height, top_radius * sin(a)), top_normal);
        }
        push(top_circle, vec3(top_radius, magnitude - tip_height, 0.f), top_normal);

        auto normal_vec = [&](float a) {
            float inc = atan(tip_height / top_radius);
            return vec3(sin(inc) * cos(a), cos(inc), sin(inc) * sin(a));
        };
        for (float a = 0.f; a < 2.f * M_PI; a += 2.f * M_PI / segments) {
            push(conic_head, vec3(0.f, magnitude, 0.f), normal_vec(a));
            push(conic_head, vec3(top_radius * cos(a), magnitude - tip_height, top_radius * sin(a)), normal_vec(a));
        }
        push(conic_head, vec3(0.f, magnitude, 0.f), normal_vec(0.f));
        push(conic_head, vec3(top_radius, magnitude - tip_height, 0.f), normal_vec(0.f));


        int success;
        char infoLog[512];

        unsigned int vertexShader = glCreateShader(GL_VERTEX_SHADER);
        const char* vertexSource = R"glsl(
#version 460 core

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;

uniform mat4 model;
uniform mat4 view;
uniform mat4 proj;

out vec3 fragPos;
out vec3 normal;

void main() {
    fragPos = vec3(model * vec4(aPos, 1.f));
    normal = mat3(transpose(inverse(model))) * aNormal;
    gl_Position = proj * view * vec4(fragPos, 1.f);
})glsl";
        glShaderSource(vertexShader, 1, &vertexSource, NULL);
        glCompileShader(vertexShader);
        check_for_errors(vertexShader);

        unsigned int fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
        const char* fragmentSource = R"glsl(
#version 460 core

in vec3 fragPos;
in vec3 normal;

out vec4 fragColor;

uniform vec3 color;
uniform vec3 lightPos;

void main() {
    float ambientStrength = 0.3f;
    vec3 ambient = ambientStrength * color;

    vec3 norm = normalize(normal);
    vec3 lightDir = normalize(lightPos - fragPos);
    float diff = max(dot(norm, lightDir), 0.f);
    vec3 diffuse = diff * color;

    fragColor = vec4(ambient + diffuse, 1.f);
})glsl";
        glShaderSource(fragmentShader, 1, &fragmentSource, NULL);
        glCompileShader(fragmentShader);
        check_for_errors(fragmentShader);

        GLuint vectorShaderProgram = glCreateProgram();
        glAttachShader(vectorShaderProgram, vertexShader);
        glAttachShader(vectorShaderProgram, fragmentShader);
        glLinkProgram(vectorShaderProgram);
        glDeleteShader(fragmentShader);
        glDeleteShader(vertexShader);
        glUseProgram(vectorShaderProgram);

        vec3 direction = normalize(end - start);
        vec3 defdir = vec3(0.f, 1.f, 0.f);
        vec3 rotationAxis = cross(defdir, direction);
        if (length(rotationAxis) < 1e-6) rotationAxis = vec3(1.0f, 0.0f, 0.0f);
        float angle = acos(clamp(dot(defdir, direction), -1.0f, 1.0f));
        mat4 rotationMatrix = rotate(mat4(1.f), angle, normalize(rotationAxis));
        mat4 modelMatrix = translate(mat4(1.f), start) * rotationMatrix;
        glUniformMatrix4fv(glGetUniformLocation(vectorShaderProgram, "model"), 1, GL_FALSE, glm::value_ptr(modelMatrix));
        glUniformMatrix4fv(glGetUniformLocation(vectorShaderProgram, "view"), 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(glGetUniformLocation(vectorShaderProgram, "proj"), 1, GL_FALSE, glm::value_ptr(proj));

        glUniform3fv(glGetUniformLocation(vectorShaderProgram, "color"), 1, value_ptr(color));
        glUniform3fv(glGetUniformLocation(vectorShaderProgram, "lightPos"), 1, value_ptr(light_pos));

        unsigned int vao, vbo;
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);

        glBindVertexArray(vao);

        glBindBuffer(GL_ARRAY_BUFFER, vbo);

        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), nullptr);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
        glEnableVertexAttribArray(1);

        glBufferData(GL_ARRAY_BUFFER, bottom_circle.size() * sizeof(float), bottom_circle.data(), GL_STATIC_DRAW);
        glDrawArrays(GL_TRIANGLE_FAN, 0, bottom_circle.size() / 6);

        glBufferData(GL_ARRAY_BUFFER, cylinder.size() * sizeof(float), cylinder.data(), GL_STATIC_DRAW);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, cylinder.size() / 6);

        glBufferData(GL_ARRAY_BUFFER, top_circle.size() * sizeof(float), top_circle.data(), GL_STATIC_DRAW);
        glDrawArrays(GL_TRIANGLE_FAN, 0, top_circle.size() / 6);

        glBufferData(GL_ARRAY_BUFFER, conic_head.size() * sizeof(float), conic_head.data(), GL_STATIC_DRAW);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, conic_head.size() / 6);

        glDeleteProgram(vectorShaderProgram);
        glDeleteBuffers(1, &vbo);
        glDeleteVertexArrays(1, &vao);

        glUseProgram(shaderProgram);
        glBindVertexArray(VAO);
        glBindBuffer(GL_ARRAY_BUFFER, VBO);
    }

    std::pair<float, float> min_max(char var, const char* func, float rbegin, float rend, int samplesize, char* infoLog) {
        unsigned int computeShader = glCreateShader(GL_COMPUTE_SHADER);
        const char* computeSource = R"glsl(
#version 460 core

layout(local_size_x = 1, local_size_y = 1, local_size_z = 1) in;

layout(std430, binding = 4) volatile buffer sbuf1 {
	float samples[];
};
uniform int samplesize;
uniform float rbegin;
uniform float rend;

float cot(float x) {
	return 1.f / tan(x);
}
float sec(float x) {
	return 1.f / cos(x);
}
float csc(float x) {
	return 1.f / sin(x);
}

void main() {
	float %c = rbegin + ((rend - rbegin) / samplesize) * float(gl_GlobalInvocationID.x);
    samples[gl_GlobalInvocationID.x] = float(%s);
})glsl";
        size_t size = strlen(computeSource) + strlen(func) + 1;
        char* modifiedSource = new char[size];
        snprintf(modifiedSource, size, computeSource, var, func);
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
        glShaderStorageBlockBinding(computeProgram, glGetProgramResourceIndex(computeProgram, GL_SHADER_STORAGE_BLOCK, "sbuf1"), 4);
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

    int compute_boundary(Graph& g, char regionBool[256], float& xmin, float& xmax, float& ymin, float& ymax, char* infoLog) {
        switch (region_type) {
        case CartesianRectangle:
            xmin = x_min, xmax = x_max, ymin = y_min, ymax = y_max;
            snprintf(regionBool, 256, "float(%.9f) <= x && x <= float(%.9f) && float(%.9f) <= y && y <= float(%.9f)", xmin, xmax, ymin, ymax);
            break;
        case Type1: {
            xmin = x_min, xmax = x_max;
            std::pair<float, float> yminbounds = min_max('x', y_min_eq, xmin, xmax, g.grid_res, infoLog);
            std::pair<float, float> ymaxbounds = min_max('x', y_max_eq, xmin, xmax, g.grid_res, infoLog);
            if (isnan(yminbounds.first)) return 0;
            if (isnan(ymaxbounds.first)) return 1;
            ymin = y_min_eq_min = yminbounds.first, ymax = y_max_eq_max = ymaxbounds.second;
            snprintf(regionBool, 256, "float(%s) <= y && y <= float(%s) && float(%.9f) <= x && x <= float(%.9f)", y_min_eq, y_max_eq, xmin, xmax);
            break;
        }
        case Type2: {
            ymin = y_min, ymax = y_max;
            std::pair<float, float> xminbounds = min_max('y', x_min_eq, ymin, ymax, g.grid_res, infoLog);
            std::pair<float, float> xmaxbounds = min_max('y', x_max_eq, ymin, ymax, g.grid_res, infoLog);
            if (isnan(xminbounds.first)) return 0;
            if (isnan(xmaxbounds.first)) return 1;
            xmin = x_min_eq_min = xminbounds.first, xmax = x_max_eq_max = xmaxbounds.second;
            snprintf(regionBool, 256, "float(%s) <= x && x <= float(%s) && float(%.9f) <= y && y <= float(%.9f)", x_min_eq, x_max_eq, ymin, ymax);
            break;
        }
        case Polar: {
            std::pair<float, float> rminbounds = min_max('t', r_min_eq, theta_min, theta_max, g.grid_res, infoLog);
            std::pair<float, float> rmaxbounds = min_max('t', r_max_eq, theta_min, theta_max, g.grid_res, infoLog);
            if (isnan(rminbounds.first)) return 0;
            if (isnan(rmaxbounds.first)) return 1;
            xmax = ymax = rmaxbounds.second;
            xmin = ymin = -rmaxbounds.second;
            snprintf(regionBool, 256, "float(%s) <= sqrt(x*x+y*y) && sqrt(x*x+y*y) <= float(%s) && float(%.9f) <= t && t <= float(%.9f)", r_min_eq, r_max_eq, theta_min, theta_max);
        } }
        return -1;
    }

    int compute_doubleintegral(char* infoLog) {
        Graph g = graphs[integrand_index];
        g.grid_res = integral_precision;
        float xmin{}, xmax{}, ymin{}, ymax{};
        char regionBool[256];

        if (int err = compute_boundary(g, regionBool, xmin, xmax, ymin, ymax, infoLog) != -1) return err;
        g.upload_definition(sliders, regionBool, "z", region_type == Polar);

        center_of_region = vec3((xmax + xmin) / 2.f, (ymax + ymin) / 2.f, 0.f);
        glUniform1f(glGetUniformLocation(g.computeProgram, "zoomx"), abs(xmax - xmin));
        glUniform1f(glGetUniformLocation(g.computeProgram, "zoomy"), abs(ymax - ymin));
        glUniform1f(glGetUniformLocation(g.computeProgram, "zoomz"), 1.f);
        glUniform1i(glGetUniformLocation(g.computeProgram, "grid_res"), g.grid_res);
        glUniform3fv(glGetUniformLocation(g.computeProgram, "centerPos"), 1, value_ptr(center_of_region));
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, g.SSBO);
        glBufferData(GL_SHADER_STORAGE_BUFFER, 2ull * g.grid_res * g.grid_res * (int)sizeof(float), nullptr, GL_DYNAMIC_DRAW);
        glDispatchCompute(g.grid_res, g.grid_res, 1);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
        float* data = new float[2ull * g.grid_res * g.grid_res] {};
        glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, 2ull * g.grid_res * g.grid_res * sizeof(float), data);
        dx = abs(xmax - xmin) / g.grid_res;
        dy = abs(ymax - ymin) / g.grid_res;
        integral_result = 0.f;
        for (int i = 0; i < 2 * g.grid_res * g.grid_res; i += 2) {
            float x = (graph_size / g.grid_res) * (fmod(i / 2.f, g.grid_res) - g.grid_res / 2.f);
            float y = (graph_size / g.grid_res) * ((g.grid_res - floor((i / 2.f) / g.grid_res)) - g.grid_res / 2.f);
            float val = data[i];
            bool in_region = static_cast<bool>(data[i + 1]);
            if (isnan(val) || isinf(val) || !in_region) continue;
            integral_result += val * dx * dy;
        }
        delete[] data;
        graphs[integrand_index].upload_definition(sliders, regionBool, "z", region_type == Polar);
        return -1;
    }

    int compute_surfaceintegral(char* infoLog) {
        Graph g = graphs[integrand_index];
        g.grid_res = integral_precision;
        float xmin{}, xmax{}, ymin{}, ymax{};
        char regionBool[256];

        compute_boundary(g, regionBool, xmin, xmax, ymin, ymax, infoLog);
        char scalarField[128];
        snprintf(scalarField, 128, "(%s) * sqrt(px * px + py * py + 1)", scalar_field_eq);
        g.upload_definition(sliders, regionBool, scalarField, region_type == Polar, true);

        center_of_region = vec3((xmax + xmin) / 2.f, (ymax + ymin) / 2.f, 0.f);
        glUniform1f(glGetUniformLocation(g.computeProgram, "zoomx"), abs(xmax - xmin));
        glUniform1f(glGetUniformLocation(g.computeProgram, "zoomy"), abs(ymax - ymin));
        glUniform1f(glGetUniformLocation(g.computeProgram, "zoomz"), 1.f);
        glUniform1i(glGetUniformLocation(g.computeProgram, "grid_res"), g.grid_res);
        glUniform3fv(glGetUniformLocation(g.computeProgram, "centerPos"), 1, value_ptr(center_of_region));
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, g.SSBO);
        glBufferData(GL_SHADER_STORAGE_BUFFER, 2ull * g.grid_res * g.grid_res * (int)sizeof(float), nullptr, GL_DYNAMIC_DRAW);
        glDispatchCompute(g.grid_res, g.grid_res, 1);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
        float* data = new float[2ull * g.grid_res * g.grid_res] {};
        glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, 2ull * g.grid_res * g.grid_res * sizeof(float), data);
        dx = abs(xmax - xmin) / g.grid_res;
        dy = abs(ymax - ymin) / g.grid_res;
        integral_result = 0.f;
        for (int i = 0; i < 2 * g.grid_res * g.grid_res; i += 2) {
            float x = (graph_size / g.grid_res) * (fmod(i / 2.f, g.grid_res) - g.grid_res / 2.f);
            float y = (graph_size / g.grid_res) * ((g.grid_res - floor((i / 2.f) / g.grid_res)) - g.grid_res / 2.f);
            float val = data[i];
            bool in_region = static_cast<bool>(data[i + 1]);
            if (isnan(val) || isinf(val) || !in_region) continue;
            integral_result += val * dx * dy;
        }
        delete[] data;
        graphs[integrand_index].upload_definition(sliders, regionBool, "z", region_type == Polar);
        return -1;
    }

    int compute_lineintegral(char* infoLog) {
        Graph g = graphs[integrand_index];
        unsigned int computeShader = glCreateShader(GL_COMPUTE_SHADER);
        const char* computeSource = R"glsl(
#version 460 core

layout(local_size_x = 1, local_size_y = 1, local_size_z = 1) in;

layout(std430, binding = 6) volatile buffer sbuf3 {
	float samples[];
};
uniform int samplesize;
uniform float tbegin;
uniform float tend;

float cot(float x) {
	return 1.f / tan(x);
}
float sec(float x) {
	return 1.f / cos(x);
}
float csc(float x) {
	return 1.f / sin(x);
}

float to_trange(float t) {
    return tbegin + t / samplesize * (tend - tbegin);
}

void main() {
    float next_t = to_trange(float(gl_GlobalInvocationID.x + 1));
    float t = to_trange(float(gl_GlobalInvocationID.x));
    float dt = next_t - t;

    float x = (%s);
    float y = (%s);
    t = next_t;
    float dx = ((%s) - x);
    float dy = ((%s) - y);

    samples[gl_GlobalInvocationID.x * 4] = float(%s);
    samples[gl_GlobalInvocationID.x * 4 + 1] = length(vec2(dx / dt, dy / dt));
    samples[gl_GlobalInvocationID.x * 4 + 2] = x;
    samples[gl_GlobalInvocationID.x * 4 + 3] = y;
})glsl";
        size_t size = strlen(computeSource) + sizeof(x_param_eq) * 4 + sizeof(Graph::defn);
        char* modifiedSource = new char[size];
        snprintf(modifiedSource, size, computeSource, x_param_eq, y_param_eq, x_param_eq, y_param_eq, g.defn);
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
            return 1;
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
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 6, sampleBuffer);
        glShaderStorageBlockBinding(computeProgram, glGetProgramResourceIndex(computeProgram, GL_SHADER_STORAGE_BLOCK, "sbuf3"), 6);
        glBufferData(GL_SHADER_STORAGE_BUFFER, integral_precision * 4ull * sizeof(float), nullptr, GL_STATIC_DRAW);

        glUniform1i(glGetUniformLocation(computeProgram, "samplesize"), integral_precision);
        glUniform1f(glGetUniformLocation(computeProgram, "tbegin"), t_min);
        glUniform1f(glGetUniformLocation(computeProgram, "tend"), t_max);

        glDispatchCompute(integral_precision, 1, 1);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

        if (li_data) delete[] li_data;
        li_data = new float[integral_precision * 3];
        li_data_ws = new float[integral_precision * 6];
        li_samplecount = integral_precision;

        float* data = new float[integral_precision * 4];
        glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, integral_precision * 4ull * sizeof(float), data);
        dt = (t_max - t_min) / integral_precision;
        integral_result = 0.f;
        center_of_region = vec3(0.f);

        auto get_data = [&](int i) {
            return vec4(data[i * 4 + 2], data[i * 4 + 3], data[i * 4], data[i * 4 + 1]);
        };
        auto add_vertex = [&](int i) {
            vec3 cv = get_data(i);

            li_data[i * 3 + 0] = cv.x;
            li_data[i * 3 + 1] = cv.y;
            li_data[i * 3 + 2] = cv.z;
        };

        for (int i = 0; i < integral_precision; i++) {
            vec4 d = get_data(i);
            integral_result += d.z * d.w * dt;
            center_of_region += vec3(d) / static_cast<float>(integral_precision);
            add_vertex(i);
        }
        delete[] data;
        glDeleteProgram(computeProgram);
        glDeleteBuffers(1, &sampleBuffer);
        return 0;
    }

    // v: vector in cartesian space
    vec3 to_worldspace(vec3 v) const {
        v -= centerPos;
        return vec3(graph_size * v.x / zoomx, v.z / zoomz * graph_size, graph_size * v.y / zoomy);
    }
    // v: vector in cartesian space
    vec3 to_screenspace(vec3 v, ivec2 viewportSize, mat4 view, mat4 proj) const {
        v -= centerPos;
        vec4 ndc = proj * view * vec4(graph_size * v.x / zoomx, v.z / zoomz * graph_size, graph_size * v.y / zoomy, 1.f);
        ndc = ndc / ndc.w;
        return vec3((ndc.x + 1.f) * (viewportSize.x - sidebarWidth) / 2.f + sidebarWidth, (viewportSize.y - (ndc.y + 1.f) * viewportSize.y / 2.f), ndc.z);
    }

    void move_to(vec3 pos) {
        next_centerPos = pos;
        temp_centerPos = centerPos;
        moveTimestamp = glfwGetTime();
    }

public:
    void mainloop() {
        double prevTime = glfwGetTime();
        mat4 view{}, proj{};

        b::EmbedInternal::EmbeddedFile icon;
        uint8_t pixels[30 * 30 * 4];

        icon = b::embed<"assets/tangent_plane.png">();
        parsePNG(reinterpret_cast<const uint8_t*>(icon.data()), icon.size(), pixels);
        GLuint tangentPlane_texture = 0;
        glGenTextures(1, &tangentPlane_texture);
        glBindTexture(GL_TEXTURE_2D, tangentPlane_texture);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 30, 30, 0, GL_RGBA, GL_UNSIGNED_BYTE, (const void*)pixels);

        icon = b::embed<"assets/gradient_vector.png">();
        parsePNG(reinterpret_cast<const uint8_t*>(icon.data()), icon.size(), pixels);
        GLuint gradVec_texture = 0;
        glGenTextures(1, &gradVec_texture);
        glBindTexture(GL_TEXTURE_2D, gradVec_texture);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 30, 30, 0, GL_RGBA, GL_UNSIGNED_BYTE, (const void*)pixels);

        icon = b::embed<"assets/normal_vector.png">();
        parsePNG(reinterpret_cast<const uint8_t*>(icon.data()), icon.size(), pixels);
        GLuint normVec_texture = 0;
        glGenTextures(1, &normVec_texture);
        glBindTexture(GL_TEXTURE_2D, normVec_texture);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 30, 30, 0, GL_RGBA, GL_UNSIGNED_BYTE, (const void*)pixels);

        icon = b::embed<"assets/integral.png">();
        parsePNG(reinterpret_cast<const uint8_t*>(icon.data()), icon.size(), pixels);
        GLuint dintegral_texture = 0;
        glGenTextures(1, &dintegral_texture);
        glBindTexture(GL_TEXTURE_2D, dintegral_texture);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 30, 30, 0, GL_RGBA, GL_UNSIGNED_BYTE, (const void*)pixels);

        icon = b::embed<"assets/surface_integral.png">();
        parsePNG(reinterpret_cast<const uint8_t*>(icon.data()), icon.size(), pixels);
        GLuint sintegral_texture = 0;
        glGenTextures(1, &sintegral_texture);
        glBindTexture(GL_TEXTURE_2D, sintegral_texture);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 30, 30, 0, GL_RGBA, GL_UNSIGNED_BYTE, (const void*)pixels);

        icon = b::embed<"assets/line_integral.png">();
        parsePNG(reinterpret_cast<const uint8_t*>(icon.data()), icon.size(), pixels);
        GLuint lintegral_texture = 0;
        glGenTextures(1, &lintegral_texture);
        glBindTexture(GL_TEXTURE_2D, lintegral_texture);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 30, 30, 0, GL_RGBA, GL_UNSIGNED_BYTE, (const void*)pixels);

        GLuint integral_texture = dintegral_texture;

        GLuint srcFBO, dstFBO;
        glGenFramebuffers(1, &srcFBO);
        glGenFramebuffers(1, &dstFBO);
        glBindFramebuffer(GL_FRAMEBUFFER, srcFBO);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthMap, 0);
        glBindFramebuffer(GL_FRAMEBUFFER, dstFBO);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, prevZBuffer, 0);
        
        do {
            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            glfwPollEvents();
            ImGui::NewFrame();

            int wWidth, wHeight;
            glfwGetWindowSize(window, &wWidth, &wHeight);

            double currentTime = glfwGetTime();
            float timeStep = currentTime - prevTime;
            prevTime = currentTime;

            cameraVelocity = vec2(
                (keys[0] && !keys[2] ? zoomx / 4.f : (keys[2] && !keys[0] ? -zoomx / 4.f : cameraVelocity.x * pow(0.0001f, timeStep))),
                (keys[1] && !keys[3] ? zoomy / 4.f : (keys[3] && !keys[1] ? -zoomy / 4.f : cameraVelocity.y * pow(0.0001f, timeStep)))
            );
            float yaw = phi * M_PI / 180.f;
            centerPos += vec3(
                cameraVelocity.x * timeStep * vec2(cos(yaw), sin(yaw)) +
                cameraVelocity.y * timeStep * vec2(cos(yaw + M_PI / 2.f), sin(yaw + M_PI / 2.f)), 0.f
            );
            if (length(cameraVelocity) > length(vec2(zoomx, zoomy)) * 0.001f) next_centerPos = centerPos;
            else cameraVelocity = vec2(0.f);

            zoomx *= pow(zoomSpeed, timeStep / 0.007f);
            zoomy *= pow(zoomSpeed, timeStep / 0.007f);
            zoomz *= pow(zoomSpeed, timeStep / 0.007f);
            xrange = vec2(zoomx / 2.f, -zoomx / 2.f) + centerPos.x;
            yrange = vec2(zoomy / 2.f, -zoomy / 2.f) + centerPos.y;
            zrange = vec2(zoomz / 2.f, -zoomz / 2.f) + centerPos.z;
            glUniform1f(glGetUniformLocation(shaderProgram, "zoomx"), zoomx);
            glUniform1f(glGetUniformLocation(shaderProgram, "zoomy"), zoomy);
            glUniform1f(glGetUniformLocation(shaderProgram, "zoomz"), zoomz);
            zoomSpeed -= (zoomSpeed - 1.f) * min(timeStep * 10.f, 1.f);
            if (currentTime - zoomTimestamp > 0.8) zoomSpeed = 1.f;

            if (centerPos != next_centerPos) {
                vec3 v = next_centerPos - temp_centerPos;
                float step = smoothstep(0.0, 0.3, currentTime - moveTimestamp);
                centerPos = temp_centerPos + v * step;
                if (step == 1.f) centerPos = next_centerPos;
            }
            glUniform3fv(glGetUniformLocation(shaderProgram, "centerPos"), 1, value_ptr(centerPos));

            frameCount++;
            fps_history[frameCount % 5] = 1.0 / timeStep;
            if (frameCount >= 20) {
                double avg = 0.;
                for (int i = 0; i < 5; i++)
                    avg += fps_history[i] / 5;
                if (avg < 20 && ssaa && !ssaa_enabled_by_user) {
                    ssaa = false;
                    ssaa_factor = 1.f;
                    on_windowResize(window, wWidth, wHeight);
                    glUniform1i(glGetUniformLocation(shaderProgram, "radius"), ssaa_factor);
                }
            }

            ImGui::PushFont(font_title);

            bool aboutTrisualizerPopup = false;

            if (ImGui::BeginMainMenuBar()) {
                if (ImGui::BeginMenu("File")) {
                    if (ImGui::MenuItem("Open", "Ctrl+O")) {
                        open_file();
                    }
                    if (ImGui::MenuItem("Save", "Ctrl+S")) {
                        save_file();
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Exit", "Alt+F4")) {
                        std::exit(0);
                    }
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Graph")) {
                    ImGui::MenuItem("Auto-rotate", nullptr, &autoRotate);
                    ImGui::MenuItem("Show main axes", nullptr, &show_axes);
                    if (ImGui::BeginMenu("Grid density")) {
                        if (ImGui::MenuItem("Low", nullptr, gridLineDensity == 2.f)) gridLineDensity = 2.f;
                        if (ImGui::MenuItem("Moderate", nullptr, gridLineDensity == 3.f)) gridLineDensity = 3.f;
                        if (ImGui::MenuItem("High", nullptr, gridLineDensity == 4.f)) gridLineDensity = 4.f;
                        ImGui::EndMenu();
                    }
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
                    if (ImGui::MenuItem("Normal map", nullptr, coloring == NormalMap)) {
                        coloring = NormalMap;
                        glUniform1i(glGetUniformLocation(shaderProgram, "coloring"), coloring);
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Anti-aliasing", nullptr, &ssaa)) {
                        if (ssaa) {
                            ssaa_factor = 3.f;
                            ssaa_enabled_by_user = true;
                        }
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
            window_flags |= ImGuiWindowFlags_NoNavInputs | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

            if (dockspace_flags & ImGuiDockNodeFlags_PassthruCentralNode)
                window_flags |= ImGuiWindowFlags_NoBackground;

            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
            ImGui::Begin("DockSpace", nullptr, window_flags);
            ImGui::PopStyleVar(3);

            ImGuiIO& io = ImGui::GetIO();
            if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable) {
                ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
                ImGui::DockSpace(dockspace_id, ImVec2(0.f, 0.f), dockspace_flags);

                static bool init = true;
                if (init) {
                    init = false;

                    ImGui::DockBuilderRemoveNode(dockspace_id);
                    ImGui::DockBuilderAddNode(dockspace_id, dockspace_flags | ImGuiDockNodeFlags_DockSpace | ImGuiDockNodeFlags_NoTabBar | ImGuiDockNodeFlags_NoResize | ImGuiDockNodeFlags_NoResizeX);
                    ImGui::DockBuilderSetNodeSize(dockspace_id, viewport->Size);

                    auto dock_id_left = ImGui::DockBuilderSplitNode(dockspace_id, ImGuiDir_Left, 0.3f, nullptr, &dockspace_id);
                    auto dock_id_down = ImGui::DockBuilderSplitNode(dock_id_left, ImGuiDir_Down, 0.3f, nullptr, &dock_id_left);
                    auto dock_id_middle = ImGui::DockBuilderSplitNode(dock_id_left, ImGuiDir_Down, 0.4f, nullptr, &dock_id_left);

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
            ImGui::Begin("Symbolic View", nullptr, ImGuiWindowFlags_NoNavInputs | ImGuiWindowFlags_NoMove);

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
            // ImGui::SameLine();
            // ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.4f, 0.4f, 1.f));
            // ImGui::Text("FPS: %.1f", 1.0 / timeStep);
            // ImGui::PopStyleColor();
            for (int i = 0; i < graphs.size(); i++) {
                if (graphs[i].type != UserDefined) continue;
                Graph& g = graphs[i];
                ImGui::BeginChild(std::format("##child{}", i).c_str(), ImVec2(0, 0), ImGuiChildFlags_Border | ImGuiChildFlags_AlwaysAutoResize | ImGuiChildFlags_AutoResizeY);
                ImVec2 vMin = ImGui::GetWindowContentRegionMin() + ImGui::GetWindowPos();
                ImVec2 vMax = ImGui::GetWindowContentRegionMax() + ImGui::GetWindowPos();
                ImGui::BeginDisabled(!g.valid);
                if (ImGui::Checkbox(std::format("##check{}", i).c_str(), &g.enabled)) {
                    bool none_active = true;
                    int first_active = -1;
                    for (int j = 1; j < graphs.size(); j++)
                        if (j != i && graphs[j].enabled) {
                            if (first_active == -1) first_active = j;
                            none_active = false;
                        }
                    if (i == integrand_index) {
                        glUniform1i(glGetUniformLocation(shaderProgram, "integral"), false);
                        if (none_active) {
                            integral = show_integral_result = apply_integral = second_corner = false;
                            graphs[integrand_index].upload_definition(sliders);
                        }
                        else {
                            show_integral_result = apply_integral = second_corner = false;
                            integrand_index = first_active;
                        }
                    }
                }
                ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::ColorEdit4(std::format("##color{}", i).c_str(), value_ptr(g.color), ImGuiColorEditFlags_NoInputs);
                ImGui::SameLine();
                if (coloring != SingleColor && coloring != NormalMap) {
                    ImGui::ColorEdit4(std::format("##color2{}", i).c_str(), value_ptr(g.secondary_color), ImGuiColorEditFlags_NoInputs);
                    ImGui::SameLine();
                }
                int offset = 53 + ((coloring != SingleColor && coloring != NormalMap) ? 24 : 0);
                ImGui::PushItemWidth((vMax.x - vMin.x) - offset - 34);
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
                if (ImGui::Button(g.advanced_view ? U8(u8"↑") : U8(u8"↓"), ImVec2(16, 0))) {
                    g.advanced_view ^= 1;
                }
                ImGui::SameLine();
                if (ImGui::Button("x", ImVec2(16, 0))) {
                    bool none_active = true;
                    int first_active = -1;
                    for (int j = 1; j < graphs.size(); j++)
                        if (j != i && graphs[j].enabled) {
                            if (first_active == -1) first_active = j;
                            none_active = false;
                        }
                    if (i == integrand_index) {
                        glUniform1i(glGetUniformLocation(shaderProgram, "integral"), false);
                        if (none_active) {
                            integral = show_integral_result = apply_integral = second_corner = false;
                            graphs[integrand_index].upload_definition(sliders);
                        }
                        else {
                            show_integral_result = apply_integral = second_corner = false;
                            integrand_index = first_active;
                        }
                    }

                    if (none_active) {
                        gradient_vector = false;
                        normal_vector = false;
                        integral = show_integral_result = apply_integral = second_corner = false;
                        glUniform1i(glGetUniformLocation(shaderProgram, "integral"), false);
                        tangent_plane = false;
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
                    ImGui::Checkbox(std::format("Grid##{}", i).c_str(), &g.grid_lines);
                    ImGui::EndDisabled();
                }
                ImGui::EndChild();
            }
            ImGui::End();

            ImGui::SetNextWindowClass(&window_class);
            ImGui::Begin("Variables", nullptr, ImGuiWindowFlags_NoNavInputs | ImGuiWindowFlags_NoMove);
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
                if (ImGui::Button(s.config ? U8(u8"↑") : U8(u8"↓"), ImVec2(16, 0)))
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
                                    strcpy(s.infoLog, "cannot have the same symbol as another variable");
                                    s.valid = false;
                                    break;
                                }
                            }
                            if (!strcmp(s.symbol, "x") || !strcmp(s.symbol, "y")) {
                                strcpy(s.infoLog, "cannot be \"x\" or \"y\"");
                                s.valid = false;
                            }
                            if (isdigit(s.symbol[0])) {
                                strcpy(s.infoLog, "cannot start with a digit");
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
            ImGui::Begin("Tools", nullptr, ImGuiWindowFlags_NoNavInputs | ImGuiWindowFlags_NoMove);
            vMin = ImGui::GetWindowContentRegionMin() + ImGui::GetWindowPos();
            vMax = ImGui::GetWindowContentRegionMax() + ImGui::GetWindowPos();
            ImGui::SetNextItemWidth(vMax.x - vMin.x);
            if (ImGui::InputFloat3("##goto", value_ptr(next_centerPos))) {
                move_to(next_centerPos);
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_NoSharedDelay))
                ImGui::SetTooltip("Center position", ImGui::GetStyle().HoverDelayNormal);
            if (ImGui::Button("Center on origin", ImVec2((vMax.x - vMin.x) / 2.f - 5.f, 0))) {
                move_to(vec3(0.f));
            }
            ImGui::SameLine();
            if (ImGui::Button("Reset zoom", ImVec2((vMax.x - vMin.x) / 2.f - 2.f, 0))) {
                zoomx = zoomy = zoomz = 8.f;
                zoomSpeed = 1.f;
            }

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

            bool none_active = true;
            for (int j = 1; j < graphs.size(); j++)
                if (graphs[j].enabled) none_active = false;
            if (none_active) {
                gradient_vector = false;
                tangent_plane = false;
                normal_vector = false;
                glUniform1i(glGetUniformLocation(shaderProgram, "integral"), false);
                integral = show_integral_result = apply_integral = second_corner = false;
                if (integrand_index != -1 && integrand_index < graphs.size()) graphs[integrand_index].upload_definition(sliders);
            }
            buttonWidth = (vMax.x - vMin.x - 61.f) / 4.f;
            ImGui::BeginDisabled(none_active);
            if (gradient_vector) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.30f, 0.32f, 0.33f, 1.00f));
            if (ImGui::ImageButton("gradient_vector", gradVec_texture, ImVec2(buttonWidth, 30), ImVec2(0.5f - buttonWidth / 60, 0.f), ImVec2(0.5f + buttonWidth / 60, 1.f), ImVec4(0.0f, 0.0f, 0.0f, 0.0f), ImVec4(1.0f, 1.0f, 1.0f, 1.0f))) {
                gradient_vector ^= 1;
                tangent_plane = false;
                normal_vector = false;
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
            if (ImGui::ImageButton("tangent_plane", tangentPlane_texture, ImVec2(buttonWidth, 30), ImVec2(0.5f - buttonWidth / 60, 0.f), ImVec2(0.5f + buttonWidth / 60, 1.f), ImVec4(0.0f, 0.0f, 0.0f, 0.0f), ImVec4(1.0f, 1.0f, 1.0f, 1.0f))) {
                tangent_plane ^= 1;
                gradient_vector = false;
                normal_vector = false;
                glUniform1i(glGetUniformLocation(shaderProgram, "integral"), false);
                integral = show_integral_result = apply_integral = second_corner = false;
                if (integrand_index != -1) graphs[integrand_index].upload_definition(sliders);
                if (!tangent_plane) ImGui::PopStyleColor();
            }
            else if (tangent_plane) ImGui::PopStyleColor();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_NoSharedDelay))
                ImGui::SetTooltip("Tangent Plane", ImGui::GetStyle().HoverDelayNormal);

            ImGui::SameLine();
            if (normal_vector) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.30f, 0.32f, 0.33f, 1.00f));
            if (ImGui::ImageButton("normal_vector", normVec_texture, ImVec2(buttonWidth, 30), ImVec2(0.5f - buttonWidth / 60, 0.f), ImVec2(0.5f + buttonWidth / 60, 1.f), ImVec4(0.0f, 0.0f, 0.0f, 0.0f), ImVec4(1.0f, 1.0f, 1.0f, 1.0f))) {
                normal_vector ^= 1;
                tangent_plane = false;
                gradient_vector = false;
                glUniform1i(glGetUniformLocation(shaderProgram, "integral"), false);
                integral = show_integral_result = apply_integral = second_corner = false;
                if (integrand_index != -1) graphs[integrand_index].upload_definition(sliders);
                if (!normal_vector) ImGui::PopStyleColor();
            }
            else if (normal_vector) ImGui::PopStyleColor();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_NoSharedDelay))
                ImGui::SetTooltip("Normal Vector", ImGui::GetStyle().HoverDelayNormal);

            ImGui::SameLine();
            if (integral) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.30f, 0.32f, 0.33f, 1.00f));
            if (ImGui::ImageButton("integral", integral_texture, ImVec2(buttonWidth, 30), ImVec2(0.5f - buttonWidth / 60, 0.f), ImVec2(0.5f + buttonWidth / 60, 1.f), ImVec4(0.0f, 0.0f, 0.0f, 0.0f), ImVec4(1.0f, 1.0f, 1.0f, 1.0f))) {
                if (!integral) {
                    glUniform1i(glGetUniformLocation(shaderProgram, "integral"), false);
                    integral = show_integral_result = apply_integral = second_corner = false;
                    if (integrand_index != -1) graphs[integrand_index].upload_definition(sliders);
                }
                if (show_integral_result) {
                    glUniform1i(glGetUniformLocation(shaderProgram, "integral"), false);
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
                normal_vector = false;
                if (!integral) ImGui::PopStyleColor();
            }
            else if (integral) ImGui::PopStyleColor();
        skip:
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_NoSharedDelay))
                ImGui::SetTooltip("Integral", ImGui::GetStyle().HoverDelayNormal);
            ImGui::EndDisabled();
            ImGui::End();

            if ((integral || show_integral_result)) {
                ImGui::SetNextWindowBgAlpha(0.8f);
                ImGui::SetNextWindowPos(ImVec2(sidebarWidth + 10.f, 24.f));
                ImGui::SetNextWindowSize(ImVec2(360.f, 0.f));
                if (ImGui::Begin("##integral", nullptr,
                    ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoFocusOnAppearing |
                    ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoNavInputs)) {

                    vMin = ImGui::GetWindowContentRegionMin() + ImGui::GetWindowPos();
                    vMax = ImGui::GetWindowContentRegionMax() + ImGui::GetWindowPos();

                    const char* preview = graphs[integrand_index].defn;
                    auto to_imcol32 = [](const glm::vec4& color) {
                        ImU8 r = static_cast<ImU8>(clamp(color.r, 0.f, 1.f) * 255.f);
                        ImU8 g = static_cast<ImU8>(clamp(color.g, 0.f, 1.f) * 255.f);
                        ImU8 b = static_cast<ImU8>(clamp(color.b, 0.f, 1.f) * 255.f);
                        ImU8 a = static_cast<ImU8>(clamp(color.w, 0.f, 1.f) * 255.f);
                        return IM_COL32(r, g, b, a);
                    };

                    if (ImGui::BeginTabBar("IntegrationTypes")) {
                        if (ImGui::BeginTabItem("Double Integral", nullptr, dintegral ? ImGuiTabItemFlags_SetSelected : 0)) {
                            integral_texture = dintegral_texture;
                            dintegral = false;
                            ImGui::BeginChild(ImGui::GetID("region_type"), ImVec2(100, 0), ImGuiChildFlags_Border | ImGuiChildFlags_AlwaysAutoResize | ImGuiChildFlags_AutoResizeY);
                            ImGui::BeginDisabled(show_integral_result || second_corner);
                            if (ImGui::RadioButton("Rectangle", region_type == CartesianRectangle)) region_type = CartesianRectangle;
                            if (ImGui::RadioButton("Type I", region_type == Type1)) region_type = Type1;
                            if (ImGui::RadioButton("Type II", region_type == Type2)) region_type = Type2;
                            if (ImGui::RadioButton("Polar", region_type == Polar)) region_type = Polar;
                            ImGui::EndDisabled();
                            ImGui::EndChild();
                            ImGui::SameLine();
                            ImGui::BeginChild(ImGui::GetID("region_bounds"), ImVec2(ImGui::GetContentRegionAvail().x, 0), ImGuiChildFlags_Border | ImGuiChildFlags_AlwaysAutoResize | ImGuiChildFlags_AutoResizeY);
                            ImGui::BeginDisabled(show_integral_result || second_corner);
                            vMin = ImGui::GetWindowContentRegionMin() + ImGui::GetWindowPos();
                            vMax = ImGui::GetWindowContentRegionMax() + ImGui::GetWindowPos();
                            ImGui::PushItemWidth((vMax.x - vMin.x - 42.f) / 2.f);
                            bool ready = true;
                            switch (region_type) {
                            case CartesianRectangle:
                                ImGui::InputFloat(U8(u8"\u2264 x \u2264"), &x_min, 0.f, 0.f, "%g");
                                ImGui::SameLine();
                                ImGui::InputFloat("##x_max", &x_max, 0.f, 0.f, "%g");

                                ImGui::InputFloat(U8(u8"\u2264 y \u2264"), &y_min, 0.f, 0.f, "%g");
                                ImGui::SameLine();
                                ImGui::InputFloat("##y_max", &y_max, 0.f, 0.f, "%g");
                                break;
                            case Type1:
                                ImGui::InputFloat(U8(u8"\u2264 x \u2264"), &x_min, 0.f, 0.f, "%g");
                                ImGui::SameLine();
                                ImGui::InputFloat("##x_max", &x_max, 0.f, 0.f, "%g");

                                ImGui::InputText(U8(u8"\u2264 y \u2264"), y_min_eq, 32);
                                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_NoSharedDelay))
                                    ImGui::SetTooltip("Enter a function of x", ImGui::GetStyle().HoverDelayNormal);
                                ImGui::SameLine();
                                ImGui::InputText("##y_max", y_max_eq, 32);
                                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_NoSharedDelay))
                                    ImGui::SetTooltip("Enter a function of x", ImGui::GetStyle().HoverDelayNormal);
                                if (strlen(y_min_eq) == 0 || strlen(y_max_eq) == 0) ready = false;
                                break;
                            case Type2:
                                ImGui::InputText(U8(u8"\u2264 x \u2264"), x_min_eq, 32);
                                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_NoSharedDelay))
                                    ImGui::SetTooltip("Enter a function of y", ImGui::GetStyle().HoverDelayNormal);
                                ImGui::SameLine();
                                ImGui::InputText("##x_max", x_max_eq, 32);
                                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_NoSharedDelay))
                                    ImGui::SetTooltip("Enter a function of y", ImGui::GetStyle().HoverDelayNormal);
                                if (strlen(x_min_eq) == 0 || strlen(x_max_eq) == 0) ready = false;

                                ImGui::InputFloat(U8(u8"\u2264 y \u2264"), &y_min, 0.f, 0.f, "%g");
                                ImGui::SameLine();
                                ImGui::InputFloat("##y_max", &y_max, 0.f, 0.f, "%g");
                                break;
                            case Polar:
                                ImGui::InputFloat(U8(u8"\u2264 \u03b8 \u2264"), &theta_min, 0.f, 0.f, "%g");
                                ImGui::SameLine();
                                ImGui::InputFloat("##theta_max", &theta_max, 0.f, 0.f, "%g");

                                ImGui::InputText(U8(u8"\u2264 r \u2264"), r_min_eq, 32);
                                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_NoSharedDelay))
                                    ImGui::SetTooltip(U8(u8"Enter a function of \u03b8 (alias: t)"), ImGui::GetStyle().HoverDelayNormal);
                                ImGui::SameLine();
                                ImGui::InputText("##r_max", r_max_eq, 32);
                                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_NoSharedDelay))
                                    ImGui::SetTooltip(U8(u8"Enter a function of \u03b8 (alias: t)"), ImGui::GetStyle().HoverDelayNormal);
                                if (strlen(r_min_eq) == 0 || strlen(r_max_eq) == 0) ready = false;
                                break;
                            }
                            ImGui::PopItemWidth();
                            ImGui::SetNextItemWidth(vMax.x - vMin.x - 81.f);

                            if (ImGui::BeginCombo("##integrand", preview)) {
                                for (int n = 1; n < graphs.size(); n++) {
                                    if (!graphs[n].enabled) continue;
                                    const bool is_selected = (integrand_index == n);
                                    ImGui::PushStyleColor(ImGuiCol_Text, to_imcol32(graphs[n].color * 1.1f));
                                    if (ImGui::Selectable(graphs[n].defn, is_selected))
                                        integrand_index = n;
                                    ImGui::PopStyleColor();
                                    if (is_selected) ImGui::SetItemDefaultFocus();
                                }
                                ImGui::EndCombo();
                            }
                            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_NoSharedDelay))
                                ImGui::SetTooltip("Integrand", ImGui::GetStyle().HoverDelayNormal);
                            ImGui::SameLine();

                            ImGui::SetNextItemWidth(75.f);
                            if (ImGui::InputInt("##precision", &integral_precision, 50, 100))
                                if (integral_precision < 50) integral_precision = 50;
                            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_NoSharedDelay))
                                ImGui::SetTooltip("Precision, higher the better", ImGui::GetStyle().HoverDelayNormal);

                            ImGui::EndDisabled();
                            ImGui::BeginDisabled(!ready || show_integral_result || second_corner);
                            if (ImGui::Button("Compute", ImVec2(vMax.x - vMin.x, 0.f))) {
                                last_integration_type = DoubleIntegral;
                                glUniform1i(glGetUniformLocation(shaderProgram, "integral"), DoubleIntegral);
                                glUniform1i(glGetUniformLocation(shaderProgram, "integrand_idx"), integrand_index);
                                glUniform1i(glGetUniformLocation(shaderProgram, "region_type"), region_type);
                                int error = compute_doubleintegral(integral_infoLog);
                                if (error != -1) erroring_eq = error;
                                else {
                                    erroring_eq = -1;
                                    show_integral_result = true;
                                }
                            }
                            ImGui::EndDisabled();
                            ImGui::EndChild();
                            ImGui::EndTabItem();
                        }
                        if (ImGui::BeginTabItem("Surface Integral", nullptr)) {
                            integral_texture = sintegral_texture;
                            ImGui::BeginChild(ImGui::GetID("region_type"), ImVec2(100, 0), ImGuiChildFlags_Border | ImGuiChildFlags_AlwaysAutoResize | ImGuiChildFlags_AutoResizeY);
                            ImGui::BeginDisabled(show_integral_result || second_corner);
                            if (ImGui::RadioButton("Rectangle", region_type == CartesianRectangle)) region_type = CartesianRectangle;
                            if (ImGui::RadioButton("Type I", region_type == Type1)) region_type = Type1;
                            if (ImGui::RadioButton("Type II", region_type == Type2)) region_type = Type2;
                            if (ImGui::RadioButton("Polar", region_type == Polar)) region_type = Polar;
                            ImGui::EndDisabled();
                            ImGui::EndChild();
                            ImGui::SameLine();
                            ImGui::BeginChild(ImGui::GetID("region_bounds"), ImVec2(ImGui::GetContentRegionAvail().x, 0), ImGuiChildFlags_Border | ImGuiChildFlags_AlwaysAutoResize | ImGuiChildFlags_AutoResizeY);
                            ImGui::BeginDisabled(show_integral_result || second_corner);
                            vMin = ImGui::GetWindowContentRegionMin() + ImGui::GetWindowPos();
                            vMax = ImGui::GetWindowContentRegionMax() + ImGui::GetWindowPos();
                            ImGui::PushItemWidth((vMax.x - vMin.x - 42.f) / 2.f);
                            bool ready = true;
                            switch (region_type) {
                            case CartesianRectangle:
                                ImGui::InputFloat(U8(u8"\u2264 x \u2264"), &x_min, 0.f, 0.f, "%g");
                                ImGui::SameLine();
                                ImGui::InputFloat("##x_max", &x_max, 0.f, 0.f, "%g");

                                ImGui::InputFloat(U8(u8"\u2264 y \u2264"), &y_min, 0.f, 0.f, "%g");
                                ImGui::SameLine();
                                ImGui::InputFloat("##y_max", &y_max, 0.f, 0.f, "%g");
                                break;
                            case Type1:
                                ImGui::InputFloat(U8(u8"\u2264 x \u2264"), &x_min, 0.f, 0.f, "%g");
                                ImGui::SameLine();
                                ImGui::InputFloat("##x_max", &x_max, 0.f, 0.f, "%g");

                                ImGui::InputText(U8(u8"\u2264 y \u2264"), y_min_eq, 32);
                                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_NoSharedDelay))
                                    ImGui::SetTooltip("Enter a function of x", ImGui::GetStyle().HoverDelayNormal);
                                ImGui::SameLine();
                                ImGui::InputText("##y_max", y_max_eq, 32);
                                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_NoSharedDelay))
                                    ImGui::SetTooltip("Enter a function of x", ImGui::GetStyle().HoverDelayNormal);
                                if (strlen(y_min_eq) == 0 || strlen(y_max_eq) == 0) ready = false;
                                break;
                            case Type2:
                                ImGui::InputText(U8(u8"\u2264 x \u2264"), x_min_eq, 32);
                                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_NoSharedDelay))
                                    ImGui::SetTooltip("Enter a function of y", ImGui::GetStyle().HoverDelayNormal);
                                ImGui::SameLine();
                                ImGui::InputText("##x_max", x_max_eq, 32);
                                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_NoSharedDelay))
                                    ImGui::SetTooltip("Enter a function of y", ImGui::GetStyle().HoverDelayNormal);
                                if (strlen(x_min_eq) == 0 || strlen(x_max_eq) == 0) ready = false;

                                ImGui::InputFloat(U8(u8"\u2264 y \u2264"), &y_min, 0.f, 0.f, "%g");
                                ImGui::SameLine();
                                ImGui::InputFloat("##y_max", &y_max, 0.f, 0.f, "%g");
                                break;
                            case Polar:
                                ImGui::InputFloat(U8(u8"\u2264 \u03b8 \u2264"), &theta_min, 0.f, 0.f, "%g");
                                ImGui::SameLine();
                                ImGui::InputFloat("##theta_max", &theta_max, 0.f, 0.f, "%g");

                                ImGui::InputText(U8(u8"\u2264 r \u2264"), r_min_eq, 32);
                                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_NoSharedDelay))
                                    ImGui::SetTooltip(U8(u8"Enter a function of \u03b8 (alias: t)"), ImGui::GetStyle().HoverDelayNormal);
                                ImGui::SameLine();
                                ImGui::InputText("##r_max", r_max_eq, 32);
                                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_NoSharedDelay))
                                    ImGui::SetTooltip(U8(u8"Enter a function of \u03b8 (alias: t)"), ImGui::GetStyle().HoverDelayNormal);
                                if (strlen(r_min_eq) == 0 || strlen(r_max_eq) == 0) ready = false;
                                break;
                            }
                            ImGui::PopItemWidth();

                            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 81.f);
                            ImGui::InputText("Scalar field", scalar_field_eq, 32);
                            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_NoSharedDelay))
                                ImGui::SetTooltip("Enter a function of x, y and z", ImGui::GetStyle().HoverDelayNormal);
                            if (strlen(scalar_field_eq) == 0) ready = false;

                            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 81.f);
                            if (ImGui::BeginCombo("##integrand", preview)) {
                                for (int n = 1; n < graphs.size(); n++) {
                                    if (!graphs[n].enabled) continue;
                                    const bool is_selected = (integrand_index == n);
                                    ImGui::PushStyleColor(ImGuiCol_Text, to_imcol32(graphs[n].color * 1.1f));
                                    if (ImGui::Selectable(graphs[n].defn, is_selected))
                                        integrand_index = n;
                                    ImGui::PopStyleColor();
                                    if (is_selected) ImGui::SetItemDefaultFocus();
                                }
                                ImGui::EndCombo();
                            }
                            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_NoSharedDelay))
                                ImGui::SetTooltip("Integrand", ImGui::GetStyle().HoverDelayNormal);
                            ImGui::SameLine();

                            ImGui::SetNextItemWidth(75.f);
                            if (ImGui::InputInt("##precision", &integral_precision, 50, 100))
                                if (integral_precision < 50) integral_precision = 50;
                            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_NoSharedDelay))
                                ImGui::SetTooltip("Precision, higher the better", ImGui::GetStyle().HoverDelayNormal);

                            ImGui::EndDisabled();
                            ImGui::BeginDisabled(!ready || show_integral_result || second_corner);
                            if (ImGui::Button("Compute", ImVec2(vMax.x - vMin.x, 0.f))) {
                                last_integration_type = SurfaceIntegral;
                                glUniform1i(glGetUniformLocation(shaderProgram, "integral"), SurfaceIntegral);
                                glUniform1i(glGetUniformLocation(shaderProgram, "integrand_idx"), integrand_index);
                                glUniform1i(glGetUniformLocation(shaderProgram, "region_type"), region_type);
                                int error = compute_surfaceintegral(integral_infoLog);
                                if (error != -1) erroring_eq = error;
                                else {
                                    erroring_eq = -1;
                                    show_integral_result = true;
                                }
                            }
                            ImGui::EndDisabled();
                            ImGui::EndChild();
                            ImGui::EndTabItem();
                        }
                        if (ImGui::BeginTabItem("Line Integral", nullptr)) {
                            integral_texture = lintegral_texture;
                            ImGui::SetNextItemWidth(140.f);
                            ImGui::BeginDisabled(show_integral_result || second_corner);
                            if (ImGui::BeginCombo("##integrand", preview)) {
                                for (int n = 1; n < graphs.size(); n++) {
                                    if (!graphs[n].enabled) continue;
                                    const bool is_selected = (integrand_index == n);
                                    ImGui::PushStyleColor(ImGuiCol_Text, to_imcol32(graphs[n].color * 1.1f));
                                    if (ImGui::Selectable(graphs[n].defn, is_selected))
                                        integrand_index = n;
                                    ImGui::PopStyleColor();
                                    if (is_selected) ImGui::SetItemDefaultFocus();
                                }
                                ImGui::EndCombo();
                            }
                            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_NoSharedDelay))
                                ImGui::SetTooltip("Integrand", ImGui::GetStyle().HoverDelayNormal);
                            ImGui::SameLine();

                            ImGui::PushItemWidth((ImGui::GetContentRegionAvail().x - 43.f) / 2.f);
                            ImGui::InputFloat(U8(u8"\u2264 t \u2264"), &t_min, 0.f, 0.f, "%g");
                            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_NoSharedDelay))
                                ImGui::SetTooltip("Lower limit for the parameter t", ImGui::GetStyle().HoverDelayNormal);
                            ImGui::SameLine();
                            ImGui::InputFloat("##t_max", &t_max, 0.f, 0.f, "%g");
                            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_NoSharedDelay))
                                ImGui::SetTooltip("Upper limit for the parameter t", ImGui::GetStyle().HoverDelayNormal);
                            ImGui::PopItemWidth();

                            ImGui::PushItemWidth((vMax.x - vMin.x - 67.f) / 2.f);
                            ImGui::InputText("x(t)", x_param_eq, 32);
                            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_NoSharedDelay))
                                ImGui::SetTooltip("Enter a parametric function of t for x", ImGui::GetStyle().HoverDelayNormal);
                            ImGui::SameLine();
                            ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 2.f);
                            ImGui::InputText("y(t)", y_param_eq, 32);
                            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_NoSharedDelay))
                                ImGui::SetTooltip("Enter a parametric function of t for y", ImGui::GetStyle().HoverDelayNormal);
                            ImGui::PopItemWidth();

                            ImGui::SetNextItemWidth(140.f);
                            if (ImGui::InputInt("##precision", &integral_precision, 50, 100))
                                if (integral_precision < 50) integral_precision = 50;
                            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_NoSharedDelay))
                                ImGui::SetTooltip("Precision, higher the better", ImGui::GetStyle().HoverDelayNormal);
                            ImGui::SameLine();

                            ImGui::EndDisabled();
                            ImGui::BeginDisabled(show_integral_result || second_corner || strlen(x_param_eq) == 0 || strlen(y_param_eq) == 0);
                            if (ImGui::Button("Compute", ImVec2(ImGui::GetContentRegionAvail().x, 0))) {
                                glUniform1i(glGetUniformLocation(shaderProgram, "integral"), LineIntegral);
                                glUniform1i(glGetUniformLocation(shaderProgram, "integrand_idx"), integrand_index);
                                glUniform1i(glGetUniformLocation(shaderProgram, "region_type"), -2);
                                int error = compute_lineintegral(integral_infoLog);
                                if (error != 0) erroring_eq = error;
                                else {
                                    erroring_eq = -1;
                                    show_integral_result = true;
                                    last_integration_type = LineIntegral;
                                }
                            }
                            ImGui::EndDisabled();

                            ImGui::EndTabItem();
                        }
                        ImGui::EndTabBar();
                    }
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

            if (coloring == Elevation) {
                ImGui::SetNextWindowBgAlpha(0.5f);
                ImGui::SetNextWindowPos(ImVec2(sidebarWidth + 10.f, wHeight - 130.f));
                ImGui::SetNextWindowSize(ImVec2(0.f, 120.f));
                if (ImGui::Begin("##scale", nullptr,
                    ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking |
                    ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNavInputs | ImGuiWindowFlags_NoNav)) {

                    auto to_imu32 = [](vec4 v) -> ImU32 {
                        ImU32 r = static_cast<ImU32>(v.r * 255.0f);
                        ImU32 g = static_cast<ImU32>(v.g * 255.0f);
                        ImU32 b = static_cast<ImU32>(v.b * 255.0f);
                        ImU32 a = static_cast<ImU32>(v.a * 255.0f);
                        return (a << 24) | (b << 16) | (g << 8) | r;
                    };
                    ImVec2 corner = { ImGui::GetWindowPos().x , ImGui::GetWindowPos().y };
                    ImDrawList* draw_list = ImGui::GetWindowDrawList();
                    int nActive = 0;
                    for (int i = 1; i < graphs.size(); i++) {
                        Graph g = graphs[i];
                        if (!g.enabled) continue;
                        draw_list->AddRectFilledMultiColor(corner + ImVec2(8 + nActive * 12, 6), corner + ImVec2(14 + nActive * 12, 114), to_imu32(g.color), to_imu32(g.color), to_imu32(g.secondary_color), to_imu32(g.secondary_color));
                        nActive += 1;
                    }
                    ImGui::SetCursorPos(ImVec2(nActive * 12 + 4, 5));
                    ImGui::Text("% 04.3f", zrange[0]);
                    ImGui::SetCursorPos(ImVec2(nActive * 12 + 4, 104));
                    ImGui::Text("% 04.3f", zrange[1]);
                    if (cursor_on_point) {
                        float t = (fragPos.z - zrange.x) / (zrange.y - zrange.x);
                        ImVec2 point = corner + ImVec2(10 + nActive * 12, 6) + ImVec2(0, 108 * t);
                        draw_list->AddTriangleFilled(point, point + ImVec2(7, -5), point + ImVec2(7, 5), to_imu32(vec4(0.8f)));
                    }
                }
                ImGui::End();
            }

            double x, y;
            glfwGetCursorPos(window, &x, &y);
            x = round(x);
            y = round(y);
            if (graphs.size() > 0 && x - sidebarWidth > 0. && x - sidebarWidth < (wWidth - sidebarWidth) && y > 0. && y < wHeight &&
                glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_RELEASE && zoomSpeed == 1.f && !ImGui::GetIO().WantCaptureMouse && !autoRotate) {
                // depth check not needed since 977ef16
                float depth[1];
                glBindFramebuffer(GL_FRAMEBUFFER, FBO);
                glBindTexture(GL_TEXTURE_2D, prevZBuffer);
                glReadPixels(ssaa_factor * x * dpi_scale, ssaa_factor * (wHeight - y) * dpi_scale, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, depth);
                if (depth[0] == 0.f) goto mouse_not_on_graph;

                glBindBuffer(GL_SHADER_STORAGE_BUFFER, posBuffer);
                float data[6];
                glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 6 * (floor((wWidth - sidebarWidth) * dpi_scale) * floor(y * dpi_scale) + floor((x - sidebarWidth) * dpi_scale)) * sizeof(float), 6 * sizeof(float), data);
                fragPos = { data[0], data[1], data[2] };
                graph_index = static_cast<int>(data[3]);
                gradient = { data[4], data[5] };

                if (graph_index >= graphs.size() || graph_index == 0) {
                    goto mouse_not_on_graph;
                }
                cursor_on_point = true;
                ImVec2 prevWindowSize;
                if (!rightClickPressed) {
                    ImGui::Begin("info", nullptr,
                        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoNavInputs |
                        ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_AlwaysAutoResize |
                        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoFocusOnAppearing);
                    ImVec2 size = ImGui::GetWindowSize();
                    ImVec2 pos = { (float)x + 20.0f, (float)y + 20.f };
                    if (size.x > wWidth - pos.x - 5)
                        pos.x = wWidth - size.x - 5;
                    if (size.y > wHeight - pos.y - 5)
                        pos.y = wHeight - size.y - 5;
                    if (pos.x < 5) pos.x = 5;
                    if (pos.y < 5) pos.y = 5;
                    ImGui::SetWindowPos(pos);
                    vec4 c = graphs[graph_index].color * 1.3f;
                    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 9.f);
                    ImGui::ColorEdit4("##infocolor", value_ptr(c), ImGuiColorEditFlags_NoInputs);
                    ImGui::SameLine();
                    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 11.f);
                    ImGui::Text("X=% 06.4f\nY=% 06.4f\nZ=% 06.4f", fragPos.x, fragPos.y, fragPos.z);
                    ImGui::SameLine();
                    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 5.f);
                    ImGui::Text(U8(u8"\u2202z/\u2202x=% 06.4f\n\u2202z/\u2202y=% 06.4f"), gradient.x, gradient.y);
                    prevWindowSize = ImGui::GetWindowSize();
                    ImGui::End();
                }

                if (gradient_vector) {
                    vector_start = to_worldspace(fragPos);
                    vector_end = to_worldspace(fragPos + vec3(data[4], data[5], pow(length(gradient), 2)));
                }
                if (normal_vector) {
                    vector_start = to_worldspace(fragPos);
                    vector_end = to_worldspace(fragPos + normalize(vec3(-gradient, 1.f)));
                }

                GLfloat params[5] = { fragPos.z, gradient.x, fragPos.x, gradient.y, fragPos.y };
                if (tangent_plane && !rightClickPressed) {
                    glUseProgram(graphs[0].computeProgram);
                    glUniform1fv(glGetUniformLocation(graphs[0].computeProgram, "plane_params"), 5, params);
                    graphs[0].enabled = true;
                    vec4 nc1 = colors[(graphs.size() - 1) % colors.size()];
                    vec4 nc2 = colors[(graphs.size()) % colors.size()];
                    graphs[0].color = vec4(nc1.r, nc1.g, nc1.b, 0.4f);
                    graphs[0].secondary_color = vec4(nc2.r, nc2.g, nc2.b, 0.4f);
                    graphs[0].grid_lines = graphs[graph_index].grid_lines;
                    glUseProgram(shaderProgram);
                    ImGui::Begin("tooltip", nullptr,
                        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoNavInputs |
                        ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_AlwaysAutoResize |
                        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoFocusOnAppearing);
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
                    snprintf(eqf, 88, eq, params[0], params[1], -params[2], params[3], -params[4]);
                    graphs.push_back(Graph(graphs.size(), UserDefined, eqf, 100, colors[(graphs.size() - 1) % colors.size()], colors[(graphs.size()) % colors.size()], true, gridSSBO, EBO));
                    for (Slider& s : sliders) {
                        s.used_in.push_back(false);
                    }
                    graphs[graphs.size() - 1].setup();
                    graphs[graphs.size() - 1].upload_definition(sliders);
                    graphs[graphs.size() - 1].grid_lines = graphs[graph_index].grid_lines;
                    apply_tangent_plane = false;
                    tangent_plane = false;
                    graphs[0].enabled = false;
                }
                if (integral && !show_integral_result && !rightClickPressed) {
                    ImGui::Begin("tooltip", nullptr,
                        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoNavInputs |
                        ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_AlwaysAutoResize |
                        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoFocusOnAppearing);
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
                        integrand_index = graph_index;
                        last_integration_type = DoubleIntegral;
                        glUniform1i(glGetUniformLocation(shaderProgram, "integral"), DoubleIntegral);
                        glUniform1i(glGetUniformLocation(shaderProgram, "integrand_idx"), graph_index);
                        glUniform1i(glGetUniformLocation(shaderProgram, "region_type"), -1);
                        glUniform2f(glGetUniformLocation(shaderProgram, "corner1"), fragPos.x, fragPos.y);
                        glUniform2f(glGetUniformLocation(shaderProgram, "corner2"), fragPos.x, fragPos.y);
                        second_corner = true;
                        region_type = CartesianRectangle;
                        dintegral = true;
                    }
                    else {
                        integral_limits.second = vec3(fragPos.x, fragPos.y, fragPos.z);
                        integral = false;
                        second_corner = false;
                        x_min = min(integral_limits.first.x, integral_limits.second.x);
                        x_max = max(integral_limits.first.x, integral_limits.second.x);
                        y_min = min(integral_limits.first.y, integral_limits.second.y);
                        y_max = max(integral_limits.first.y, integral_limits.second.y);
                        compute_doubleintegral(integral_infoLog);
                        dintegral = true;
                        show_integral_result = true;
                    }
                    apply_integral = false;
                }
                if (doubleClickPressed) {
                    move_to(fragPos);
                    doubleClickPressed = false;
                }
            } else {
            mouse_not_on_graph:
                graphs[0].enabled = false;
                apply_tangent_plane = false;
                apply_integral = false;
                doubleClickPressed = false;
                cursor_on_point = false;
            }

            if (show_integral_result && !rightClickPressed) {
                vec3 w = to_screenspace(center_of_region, { wWidth, wHeight }, view, proj);
                //ImGui::SetNextWindowPos(ImVec2(w.x, w.y));
                static bool result_window = true;
                std::string x_display, y_display, s_display;
                ImVec2 size;
                ImVec2 pos = { w.x, w.y };
                switch (last_integration_type) {
                case DoubleIntegral:
                    ImGui::Begin("Volume under surface", &result_window,
                        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoNavInputs |
                        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoFocusOnAppearing);
                    size = ImGui::GetWindowSize();
                    if (size.x > wWidth - pos.x - 5)
                        pos.x = wWidth - size.x - 5;
                    if (size.y > wHeight - pos.y - 5)
                        pos.y = wHeight - size.y - 5;
                    if (pos.x < 5) pos.x = 5;
                    if (pos.y < 5) pos.y = 5;
                    ImGui::SetWindowPos(pos);
                    switch (region_type) {
                    case CartesianRectangle:
                        ImGui::Text(U8(u8"%.4g \u2264 x \u2264 %.4g, %.4g \u2264 y \u2264 %.4g"), x_min, x_max, y_min, y_max);
                        break;
                    case Type1:
                        ImGui::Text(U8(u8"%.4g \u2264 x \u2264 %.4g, %s \u2264 y \u2264 %s"), x_min, x_max, y_min_eq, y_max_eq);
                        break;
                    case Type2:
                        ImGui::Text(U8(u8"%.4g \u2264 y \u2264 %.4g, %s \u2264 x \u2264 %s"), y_min, y_max, x_min_eq, x_max_eq);
                        break;
                    case Polar:
                        ImGui::Text(U8(u8"%.4g \u2264 \u03b8 \u2264 %.4g, %s \u2264 r \u2264 %s"), theta_min, theta_max, r_min_eq, r_max_eq);
                    }
                    ImGui::Text(U8(u8"Signed volume \u2248 %.9f"), integral_result);
                    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(120, 120, 120, 255));
                    ImGui::Text(U8(u8"\u2206x = %.3e, \u2206y = %.3e"), dx, dy);
                    ImGui::PopStyleColor();
                    ImGui::End();
                    break;
                case SurfaceIntegral:
                    ImGui::Begin("Surface integral", &result_window,
                        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoNavInputs |
                        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoFocusOnAppearing);
                    size = ImGui::GetWindowSize();
                    if (size.x > wWidth - pos.x - 5)
                        pos.x = wWidth - size.x - 5;
                    if (size.y > wHeight - pos.y - 5)
                        pos.y = wHeight - size.y - 5;
                    if (pos.x < 5) pos.x = 5;
                    if (pos.y < 5) pos.y = 5;
                    ImGui::SetWindowPos(pos);
                    switch (region_type) {
                    case CartesianRectangle:
                        ImGui::Text(U8(u8"%.4g \u2264 x \u2264 %.4g, %.4g \u2264 y \u2264 %.4g"), x_min, x_max, y_min, y_max);
                        break;
                    case Type1:
                        ImGui::Text(U8(u8"%.4g \u2264 x \u2264 %.4g, %s \u2264 y \u2264 %s"), x_min, x_max, y_min_eq, y_max_eq);
                        break;
                    case Type2:
                        ImGui::Text(U8(u8"%.4g \u2264 y \u2264 %.4g, %s \u2264 x \u2264 %s"), y_min, y_max, x_min_eq, x_max_eq);
                        break;
                    case Polar:
                        ImGui::Text(U8(u8"%.4g \u2264 \u03b8 \u2264 %.4g, %s \u2264 r \u2264 %s"), theta_min, theta_max, r_min_eq, r_max_eq);
                    }
                    s_display = scalar_field_eq;
                    s_display.erase(std::remove(s_display.begin(), s_display.end(), ' '), s_display.end());
                    ImGui::Text("Scalar field: %s", s_display.c_str());
                    ImGui::Text(U8(u8"Weighted surface area \u2248 %.9f"), integral_result);
                    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(120, 120, 120, 255));
                    ImGui::Text(U8(u8"\u2206x = %.3e, \u2206y = %.3e"), dx, dy);
                    ImGui::PopStyleColor();
                    ImGui::End();
                    break;
                case LineIntegral:
                    ImGui::Begin("Line integral", &result_window,
                        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoNavInputs |
                        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoFocusOnAppearing);
                    size = ImGui::GetWindowSize();
                    if (size.x > wWidth - pos.x - 5)
                        pos.x = wWidth - size.x - 5;
                    if (size.y > wHeight - pos.y - 5)
                        pos.y = wHeight - size.y - 5;
                    if (pos.x < 5) pos.x = 5;
                    if (pos.y < 5) pos.y = 5;
                    ImGui::SetWindowPos(pos);
                    x_display = x_param_eq;
                    y_display = y_param_eq;
                    x_display.erase(std::remove(x_display.begin(), x_display.end(), ' '), x_display.end());
                    y_display.erase(std::remove(y_display.begin(), y_display.end(), ' '), y_display.end());
                    ImGui::Text(U8(u8"%.5g \u2264 t \u2264 %.5g"), t_min, t_max);
                    ImGui::Text("x = %s  y = %s", x_display.c_str(), y_display.c_str());
                    ImGui::Text(U8(u8"Cross-sectional area \u2248 %.9f"), integral_result);
                    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(120, 120, 120, 255));
                    ImGui::Text(U8(u8"\u2206t = %.3e"), dt);
                    ImGui::PopStyleColor();
                    ImGui::End();
                }

                if (result_window == false) {
                    show_integral_result = apply_integral = second_corner = false;
                    glUniform1i(glGetUniformLocation(shaderProgram, "integral"), false);
                    if (integrand_index != -1) graphs[integrand_index].upload_definition(sliders);
                    result_window = true;
                }
            }

            if (cursor_on_point && rightClickPressed) {
                ImGui::OpenPopup("context_menu");
            }

            if (ImGui::BeginPopup("context_menu", ImGuiWindowFlags_NoFocusOnAppearing)) {
                if (ImGui::MenuItem("Go to here")) {
                    move_to(fragPos);
                }
                if (ImGui::MenuItem("Tangent plane")) {
                    const char* eq = "%.6f%+.6f*(x%+.6f)%+.6f*(y%+.6f)";
                    char eqf[88]{};
                    snprintf(eqf, 88, eq, fragPos.z, gradient.x, -fragPos.x, gradient.y, -fragPos.y);
                    graphs.push_back(Graph(graphs.size(), UserDefined, eqf, 100, colors[(graphs.size() - 1) % colors.size()], colors[(graphs.size()) % colors.size()], true, gridSSBO, EBO));
                    for (Slider& s : sliders) {
                        s.used_in.push_back(false);
                    }
                    graphs[graphs.size() - 1].setup();
                    graphs[graphs.size() - 1].upload_definition(sliders);
                    graphs[graphs.size() - 1].grid_lines = graphs[graph_index].grid_lines;
                }
                ImGui::EndPopup();
            }

            if (ImGui::BeginPopupModal("About Trisualizer", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
                ImGui::Text("Version v" VERSION " (Build date: " __DATE__ " " __TIME__ ")\n\nTrisualizer is a two-variable function grapher");
                ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(150, 150, 150, 255));
                ImGui::Text(U8(u8"Copyright © 2018-2024 Yilmaz Alpaslan"));
                ImGui::PopStyleColor();
                if (ImGui::Button("Open GitHub Page")) {
#ifdef PLATFORM_WINDOWS
                    ShellExecuteW(0, 0, L"https://github.com/Yilmaz4/Trisualizer", 0, 0, SW_SHOW);
#elif defined(PLATFORM_LINUX)
                    system("xdg-open https://github.com/Yilmaz4/Trisualizer");
#endif
                }
                ImGui::SameLine();
                if (ImGui::Button("Close"))
                    ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }

            ImGui::PopFont();

            if (autoRotate)
                phi += timeStep * 5.f;

            auto cameraPos = vec3(sin(radians(theta)) * cos(radians(phi)), cos(radians(theta)), sin(radians(theta)) * sin(radians(phi)));
            view = lookAt(cameraPos, vec3(0.f), { 0.f, 1.f, 0.f });
            proj = ortho(-1.f, 1.f, -(float)wHeight / (float)(wWidth - sidebarWidth), (float)wHeight / (float)(wWidth - sidebarWidth), -5.f, 5.f);
            glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "vpmat"), 1, GL_FALSE, value_ptr(proj * view));
            glUniform3fv(glGetUniformLocation(shaderProgram, "cameraPos"), 1, value_ptr(cameraPos));

            ImGui::Render();

            glBindBuffer(GL_SHADER_STORAGE_BUFFER, posBuffer);
            glClearBufferData(GL_SHADER_STORAGE_BUFFER, GL_R32F, GL_RED, GL_FLOAT, nullptr);

            glClearColor(0.0f, 0.0f, 0.0f, 1.f);
            glClearDepth(0.f);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            glBindFramebuffer(GL_FRAMEBUFFER, FBO);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            
            glViewport(sidebarWidth * ssaa_factor * dpi_scale, 0, (wWidth - sidebarWidth) * ssaa_factor * dpi_scale, wHeight * ssaa_factor * dpi_scale);
            glUniform2i(glGetUniformLocation(shaderProgram, "regionSize"), (wWidth - sidebarWidth) * ssaa_factor * dpi_scale, wHeight * ssaa_factor * dpi_scale);
            glUniform2i(glGetUniformLocation(shaderProgram, "windowSize"), wWidth * ssaa_factor * dpi_scale, wHeight * ssaa_factor * dpi_scale);

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
                glUniform1f(glGetUniformLocation(shaderProgram, "gridLineDensity"), g.grid_lines ? gridLineDensity : 0.f);
                glBindVertexArray(VAO);
                glUniform1i(glGetUniformLocation(shaderProgram, "quad"), false);
                glActiveTexture(GL_TEXTURE1);
                glBindTexture(GL_TEXTURE_2D, prevZBuffer);
                glDrawElements(GL_TRIANGLE_STRIP, (GLsizei)g.indices.size(), GL_UNSIGNED_INT, 0);
            };

            auto write_to_prevzbuf = [&]() {
                glBindFramebuffer(GL_READ_FRAMEBUFFER, srcFBO);
                glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dstFBO);
                glBlitFramebuffer(
                    0, 0, ssaa_factor * wWidth * dpi_scale, ssaa_factor * wHeight * dpi_scale,
                    0, 0, ssaa_factor * wWidth * dpi_scale, ssaa_factor * wHeight * dpi_scale,
                    GL_DEPTH_BUFFER_BIT, GL_NEAREST
                );
                glBindFramebuffer(GL_FRAMEBUFFER, FBO);
            };

            if (show_axes) {
                draw_vector(to_worldspace(clamp({ xrange[1], 0.f, 0.f }, vec3(-FLT_MAX, yrange[1], zrange[1]), vec3(FLT_MAX, yrange[0], zrange[0]))),
                    to_worldspace(clamp({ xrange[0], 0.f, 0.f }, vec3(-FLT_MAX, yrange[1], zrange[1]), vec3(FLT_MAX, yrange[0], zrange[0]))),
                    vec3(0.8f, 0.f, 0.f), view, proj, 0.7f);
                draw_vector(to_worldspace(clamp({ 0.f, yrange[1], 0.f }, vec3(xrange[1], -FLT_MAX, zrange[1]), vec3(xrange[0], FLT_MAX, zrange[0]))),
                    to_worldspace(clamp({ 0.f, yrange[0], 0.f }, vec3(xrange[1], -FLT_MAX, zrange[1]), vec3(xrange[0], FLT_MAX, zrange[0]))),
                    vec3(0.f, 0.7f, 0.f), view, proj, 0.7f);
                draw_vector(to_worldspace(clamp({ 0.f, 0.f, zrange[1] }, vec3(xrange[1], yrange[1], -FLT_MAX), vec3(xrange[0], yrange[0], FLT_MAX))),
                    to_worldspace(clamp({ 0.f, 0.f, zrange[0] }, vec3(xrange[1], yrange[1], -FLT_MAX), vec3(xrange[0], yrange[0], FLT_MAX))),
                    vec3(0.f, 0.5f, 1.f), view, proj, 0.7f);
            }

            if (integral && second_corner || show_integral_result && last_integration_type < 3) {
                render_graph(integrand_index);
                write_to_prevzbuf();
            } else if (show_integral_result && last_integration_type == LineIntegral) {
                draw_lineintegral(graphs[integrand_index].color, view, proj);
                glDisable(GL_DEPTH_TEST);
                render_graph(integrand_index);
                glEnable(GL_DEPTH_TEST);
                write_to_prevzbuf();
            }
            for (int i = 1; i < graphs.size(); i++) {
                const Graph& g = graphs[i];
                if (!g.enabled) continue;
                if (i == integrand_index && (integral && second_corner || show_integral_result)) continue;
                render_graph(i);
            }
            if (!integral || !second_corner && !show_integral_result)
                write_to_prevzbuf();
            if (graphs[0].enabled)
                render_graph(0);
            if ((gradient_vector || normal_vector) && cursor_on_point)
                draw_vector(vector_start, vector_end, graphs[graph_index].secondary_color, view, proj);

            glViewport(sidebarWidth * dpi_scale, 0, (wWidth - sidebarWidth) * dpi_scale, wHeight * dpi_scale);

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, frameTex);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glUniform1i(glGetUniformLocation(shaderProgram, "quad"), true);
            glDrawArrays(GL_TRIANGLES, 0, 6);

            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            glfwSwapBuffers(window);
            glDepthFunc(GL_GREATER);

        } while (!glfwWindowShouldClose(window));
    }
};

int main() {
    try {
        Trisualizer app;
    } catch (const compilation_error& e) {
        boxer::show(e.what(), "Shader compilation error", boxer::Style::Error);
    } catch (const std::runtime_error& e) {
        boxer::show(e.what(), "Runtime error", boxer::Style::Error);
    }
    
    return 0;
}