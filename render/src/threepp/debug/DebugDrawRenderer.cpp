#include "threepp/debug/DebugDrawRenderer.hpp"

#include <glad/glad.h>

#include <cstddef>
#include <stdexcept>
#include <string>
#include <vector>

namespace threepp {
    namespace {

        unsigned int compileShader(GLenum type, const char* source) {
            const unsigned int shader = glCreateShader(type);
            glShaderSource(shader, 1, &source, nullptr);
            glCompileShader(shader);
            GLint compiled = GL_FALSE;
            glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
            if (compiled != GL_TRUE) {
                GLint length = 0;
                glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
                std::string log(static_cast<std::size_t>(length > 0 ? length : 1), '\0');
                glGetShaderInfoLog(shader, length, nullptr, log.data());
                glDeleteShader(shader);
                throw std::runtime_error("DebugDrawRenderer: shader compile failed: " + log);
            }
            return shader;
        }

        unsigned int buildProgram() {
            constexpr const char* vertexSource = R"(
                #version 330 core
                layout(location = 0) in vec3 aPosition;
                layout(location = 1) in vec4 aColor;
                uniform mat4 uViewProjection;
                out vec4 vColor;
                void main() {
                    gl_Position = uViewProjection * vec4(aPosition, 1.0);
                    vColor = aColor;
                }
            )";
            constexpr const char* fragmentSource = R"(
                #version 330 core
                in vec4 vColor;
                out vec4 FragColor;
                void main() {
                    FragColor = vColor;
                }
            )";

            const unsigned int vertex = compileShader(GL_VERTEX_SHADER, vertexSource);
            const unsigned int fragment = compileShader(GL_FRAGMENT_SHADER, fragmentSource);
            const unsigned int program = glCreateProgram();
            glAttachShader(program, vertex);
            glAttachShader(program, fragment);
            glLinkProgram(program);
            glDeleteShader(vertex);
            glDeleteShader(fragment);

            GLint linked = GL_FALSE;
            glGetProgramiv(program, GL_LINK_STATUS, &linked);
            if (linked != GL_TRUE) {
                GLint length = 0;
                glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
                std::string log(static_cast<std::size_t>(length > 0 ? length : 1), '\0');
                glGetProgramInfoLog(program, length, nullptr, log.data());
                glDeleteProgram(program);
                throw std::runtime_error("DebugDrawRenderer: program link failed: " + log);
            }
            return program;
        }

    }// namespace

    DebugDrawRenderer::DebugDrawRenderer() {
        program_ = buildProgram();
        glGenVertexArrays(1, &vao_);
        glGenBuffers(1, &vbo_);
        glBindVertexArray(vao_);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(float) * 7,
                              reinterpret_cast<void*>(0));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(float) * 7,
                              reinterpret_cast<void*>(sizeof(float) * 3));
        glBindVertexArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }

    DebugDrawRenderer::~DebugDrawRenderer() {
        dispose();
    }

    void DebugDrawRenderer::dispose() {
        if (vbo_ != 0) {
            glDeleteBuffers(1, &vbo_);
            vbo_ = 0;
        }
        if (vao_ != 0) {
            glDeleteVertexArrays(1, &vao_);
            vao_ = 0;
        }
        if (program_ != 0) {
            glDeleteProgram(program_);
            program_ = 0;
        }
    }

    void DebugDrawRenderer::render(const chrononmotion::preview::DebugDraw& draw,
                                   const std::array<float, 16>& viewProjection,
                                   bool depthTest) {
        if (!valid() || draw.empty()) return;

        const GLboolean depthWas = glIsEnabled(GL_DEPTH_TEST);
        const GLboolean blendWas = glIsEnabled(GL_BLEND);
        GLint oldProgram = 0;
        GLint oldVao = 0;
        GLint oldArrayBuffer = 0;
        GLint blendSrc = GL_SRC_ALPHA;
        GLint blendDst = GL_ONE_MINUS_SRC_ALPHA;
        GLboolean depthMask = GL_TRUE;
        glGetIntegerv(GL_CURRENT_PROGRAM, &oldProgram);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &oldVao);
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &oldArrayBuffer);
        glGetIntegerv(GL_BLEND_SRC_ALPHA, &blendSrc);
        glGetIntegerv(GL_BLEND_DST_ALPHA, &blendDst);
        glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);

        if (depthTest) glEnable(GL_DEPTH_TEST);
        else glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        std::vector<float> vertices;
        vertices.reserve(draw.vertices().size() * 7);
        for (const chrononmotion::preview::DebugVertex& vertex : draw.vertices()) {
            vertices.push_back(vertex.position.x);
            vertices.push_back(vertex.position.y);
            vertices.push_back(vertex.position.z);
            vertices.push_back(vertex.color.r);
            vertices.push_back(vertex.color.g);
            vertices.push_back(vertex.color.b);
            vertices.push_back(vertex.alpha);
        }

        glUseProgram(program_);
        const GLint matrixLocation = glGetUniformLocation(program_, "uViewProjection");
        if (matrixLocation >= 0) {
            glUniformMatrix4fv(matrixLocation, 1, GL_FALSE, viewProjection.data());
        }
        glBindVertexArray(vao_);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(vertices.size() * sizeof(float)),
                     vertices.data(), GL_STREAM_DRAW);
        glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(draw.vertices().size()));

        glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(oldArrayBuffer));
        glBindVertexArray(static_cast<GLuint>(oldVao));
        glUseProgram(static_cast<GLuint>(oldProgram));
        glDepthMask(depthMask);
        glBlendFunc(static_cast<GLenum>(blendSrc), static_cast<GLenum>(blendDst));
        if (blendWas) glEnable(GL_BLEND);
        else glDisable(GL_BLEND);
        if (depthWas) glEnable(GL_DEPTH_TEST);
        else glDisable(GL_DEPTH_TEST);
    }

    void DebugDrawRenderer::renderScreenSpace(const chrononmotion::preview::DebugDraw& draw,
                                              int width, int height, bool depthTest) {
        if (width <= 0 || height <= 0) return;
        const std::array<float, 16> identity{
                1.f, 0.f, 0.f, 0.f,
                0.f, 1.f, 0.f, 0.f,
                0.f, 0.f, 1.f, 0.f,
                0.f, 0.f, 0.f, 1.f};
        render(draw, identity, depthTest);
    }

}// namespace threepp
