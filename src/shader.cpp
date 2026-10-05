#include "shader.hpp"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <vector>

namespace {

bool readFile(const std::string& path, std::string& out)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        std::fprintf(stderr, "[shader] impossible d'ouvrir %s\n", path.c_str());
        return false;
    }
    std::stringstream ss;
    ss << file.rdbuf();
    out = ss.str();
    return true;
}

GLuint compile(GLenum type, const std::string& source, const std::string& path)
{
    GLuint shader = gl::CreateShader(type);
    const char* src = source.c_str();
    gl::ShaderSource(shader, 1, &src, nullptr);
    gl::CompileShader(shader);

    GLint status = 0;
    gl::GetShaderiv(shader, gl::COMPILE_STATUS, &status);
    if (!status) {
        GLint len = 0;
        gl::GetShaderiv(shader, gl::INFO_LOG_LENGTH, &len);
        std::vector<char> log(len > 1 ? len : 1);
        gl::GetShaderInfoLog(shader, len, nullptr, log.data());
        std::fprintf(stderr, "[shader] erreur de compilation dans %s :\n%s\n", path.c_str(), log.data());
        gl::DeleteShader(shader);
        return 0;
    }
    return shader;
}

} // namespace

Shader::~Shader()
{
    if (program_)
        gl::DeleteProgram(program_);
}

bool Shader::load(const std::string& vertexPath, const std::string& fragmentPath)
{
    std::string vsSrc, fsSrc;
    if (!readFile(vertexPath, vsSrc) || !readFile(fragmentPath, fsSrc))
        return false;

    GLuint vs = compile(gl::VERTEX_SHADER, vsSrc, vertexPath);
    GLuint fs = compile(gl::FRAGMENT_SHADER, fsSrc, fragmentPath);
    if (!vs || !fs) {
        if (vs) gl::DeleteShader(vs);
        if (fs) gl::DeleteShader(fs);
        return false;
    }

    GLuint program = gl::CreateProgram();
    gl::AttachShader(program, vs);
    gl::AttachShader(program, fs);
    gl::LinkProgram(program);
    gl::DeleteShader(vs);
    gl::DeleteShader(fs);

    GLint status = 0;
    gl::GetProgramiv(program, gl::LINK_STATUS, &status);
    if (!status) {
        GLint len = 0;
        gl::GetProgramiv(program, gl::INFO_LOG_LENGTH, &len);
        std::vector<char> log(len > 1 ? len : 1);
        gl::GetProgramInfoLog(program, len, nullptr, log.data());
        std::fprintf(stderr, "[shader] erreur d'édition de liens :\n%s\n", log.data());
        gl::DeleteProgram(program);
        return false;
    }

    if (program_)
        gl::DeleteProgram(program_);
    program_ = program;
    return true;
}
