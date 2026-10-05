#include "shader.hpp"

#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

static bool readFile(const std::string& path, std::string& out)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        std::cerr << "Impossible d'ouvrir " << path << "\n";
        return false;
    }
    std::stringstream ss;
    ss << file.rdbuf();
    out = ss.str();
    return true;
}

static GLuint compileStage(GLenum type, const std::string& source, const std::string& path)
{
    GLuint shader = glCreateShader(type);
    const char* src = source.c_str();
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);

    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GLint len = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &len);
        std::vector<char> log(len > 1 ? len : 1);
        glGetShaderInfoLog(shader, static_cast<GLsizei>(log.size()), nullptr, log.data());
        std::cerr << "Erreur de compilation dans " << path << " :\n" << log.data() << "\n";
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

GLuint loadShaderProgram(const std::string& vertexPath, const std::string& fragmentPath)
{
    std::string vsSrc, fsSrc;
    if (!readFile(vertexPath, vsSrc) || !readFile(fragmentPath, fsSrc))
        return 0;

    GLuint vs = compileStage(GL_VERTEX_SHADER, vsSrc, vertexPath);
    GLuint fs = compileStage(GL_FRAGMENT_SHADER, fsSrc, fragmentPath);
    if (!vs || !fs) {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        return 0;
    }

    GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint ok = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        GLint len = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &len);
        std::vector<char> log(len > 1 ? len : 1);
        glGetProgramInfoLog(program, static_cast<GLsizei>(log.size()), nullptr, log.data());
        std::cerr << "Erreur d'édition de liens :\n" << log.data() << "\n";
        glDeleteProgram(program);
        return 0;
    }
    return program;
}
