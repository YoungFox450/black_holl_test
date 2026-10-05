#pragma once

#include "gl_loader.hpp"

#include <string>

// Programme GLSL (vertex + fragment) chargé depuis des fichiers.
class Shader {
public:
    Shader() = default;
    ~Shader();
    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;

    // Compile et lie le programme. En cas d'erreur, renvoie false et garde
    // l'ancien programme (pratique pour le rechargement à chaud).
    bool load(const std::string& vertexPath, const std::string& fragmentPath);

    void use() const { gl::UseProgram(program_); }
    GLint uniform(const char* name) const { return gl::GetUniformLocation(program_, name); }
    bool valid() const { return program_ != 0; }

private:
    GLuint program_ = 0;
};
