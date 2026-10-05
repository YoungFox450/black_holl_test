// Chargeur OpenGL minimal : déclare uniquement les fonctions et constantes
// dont le moteur a besoin, et les charge à l'exécution via GLFW.
// Aucune dépendance externe (pas de glad/glew à générer).
#pragma once

#include <cstddef>
#include <cstdint>

#if defined(_WIN32)
#define GL_APIENTRY __stdcall
#else
#define GL_APIENTRY
#endif

using GLenum     = unsigned int;
using GLboolean  = unsigned char;
using GLbitfield = unsigned int;
using GLint      = int;
using GLuint     = unsigned int;
using GLsizei    = int;
using GLfloat    = float;
using GLchar     = char;

namespace gl {

// Constantes utilisées par le moteur
constexpr GLenum COLOR_BUFFER_BIT = 0x00004000;
constexpr GLenum TRIANGLES        = 0x0004;
constexpr GLenum RGB              = 0x1907;
constexpr GLenum RGBA             = 0x1908;
constexpr GLenum UNSIGNED_BYTE    = 0x1401;
constexpr GLenum PACK_ALIGNMENT   = 0x0D05;
constexpr GLenum VERSION          = 0x1F02;
constexpr GLenum RENDERER         = 0x1F01;
constexpr GLenum FRAGMENT_SHADER  = 0x8B30;
constexpr GLenum VERTEX_SHADER    = 0x8B31;
constexpr GLenum COMPILE_STATUS   = 0x8B81;
constexpr GLenum LINK_STATUS      = 0x8B82;
constexpr GLenum INFO_LOG_LENGTH  = 0x8B84;
constexpr GLenum FRONT            = 0x0404;
constexpr GLenum BACK             = 0x0405;

// Pointeurs de fonctions (remplis par load()).
#define GL_FUNCTIONS(X)                                                              \
    X(void,          Viewport,          GLint, GLint, GLsizei, GLsizei)              \
    X(void,          ClearColor,        GLfloat, GLfloat, GLfloat, GLfloat)          \
    X(void,          Clear,             GLbitfield)                                  \
    X(const unsigned char*, GetString,  GLenum)                                      \
    X(void,          DrawArrays,        GLenum, GLint, GLsizei)                      \
    X(void,          PixelStorei,       GLenum, GLint)                               \
    X(void,          ReadBuffer,        GLenum)                                      \
    X(void,          ReadPixels,        GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*) \
    X(void,          Finish,            void)                                        \
    X(GLuint,        CreateShader,      GLenum)                                      \
    X(void,          ShaderSource,      GLuint, GLsizei, const GLchar* const*, const GLint*) \
    X(void,          CompileShader,     GLuint)                                      \
    X(void,          GetShaderiv,       GLuint, GLenum, GLint*)                      \
    X(void,          GetShaderInfoLog,  GLuint, GLsizei, GLsizei*, GLchar*)          \
    X(void,          DeleteShader,      GLuint)                                      \
    X(GLuint,        CreateProgram,     void)                                        \
    X(void,          AttachShader,      GLuint, GLuint)                              \
    X(void,          LinkProgram,       GLuint)                                      \
    X(void,          GetProgramiv,      GLuint, GLenum, GLint*)                      \
    X(void,          GetProgramInfoLog, GLuint, GLsizei, GLsizei*, GLchar*)          \
    X(void,          DeleteProgram,     GLuint)                                      \
    X(void,          UseProgram,        GLuint)                                      \
    X(GLint,         GetUniformLocation, GLuint, const GLchar*)                      \
    X(void,          Uniform1f,         GLint, GLfloat)                              \
    X(void,          Uniform1i,         GLint, GLint)                                \
    X(void,          Uniform2f,         GLint, GLfloat, GLfloat)                     \
    X(void,          Uniform3f,         GLint, GLfloat, GLfloat, GLfloat)            \
    X(void,          UniformMatrix3fv,  GLint, GLsizei, GLboolean, const GLfloat*)   \
    X(void,          GenVertexArrays,   GLsizei, GLuint*)                            \
    X(void,          BindVertexArray,   GLuint)                                      \
    X(void,          DeleteVertexArrays, GLsizei, const GLuint*)

#define GL_DECLARE(ret, name, ...) \
    using PFN_##name = ret(GL_APIENTRY*)(__VA_ARGS__); \
    extern PFN_##name name;
GL_FUNCTIONS(GL_DECLARE)
#undef GL_DECLARE

// Charge toutes les fonctions. À appeler après glfwMakeContextCurrent().
// Renvoie false et écrit le nom manquant si une fonction est introuvable.
bool load();

} // namespace gl
