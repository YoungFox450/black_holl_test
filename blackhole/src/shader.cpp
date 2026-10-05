#include "shader.hpp"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
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

static GLuint compileStage(GLenum type, const std::vector<std::string>& paths)
{
    // "#version" puis les fichiers, chacun précédé de "#line 1 N" : dans les
    // messages d'erreur du pilote, "N:ligne" ou "N(ligne)" désigne alors la
    // ligne du fichier N (voir la liste affichée avec l'erreur).
    std::vector<std::string> sources{"#version 330 core\n"};
    for (size_t i = 0; i < paths.size(); ++i) {
        const std::string& path = paths[i];
        std::string text;
        if (!readFile(path, text))
            return 0;
        // GLSL n'accepte que l'ASCII, même dans les commentaires : certains
        // pilotes Intel refusent de compiler à cause d'un "é". On remplace
        // les octets non ASCII (accents des commentaires) par des espaces.
        for (char& ch : text)
            if (static_cast<unsigned char>(ch) > 127)
                ch = ' ';
        sources.push_back("#line 1 " + std::to_string(i + 1) + "\n" + text);
    }
    std::vector<const char*> ptrs;
    for (const std::string& src : sources)
        ptrs.push_back(src.c_str());

    GLuint shader = glCreateShader(type);
    glShaderSource(shader, static_cast<GLsizei>(ptrs.size()), ptrs.data(), nullptr);
    glCompileShader(shader);

    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GLint len = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &len);
        std::vector<char> log(len > 1 ? len : 1);
        glGetShaderInfoLog(shader, static_cast<GLsizei>(log.size()), nullptr, log.data());
        std::cerr << "Erreur de compilation du shader :\n";
        for (size_t i = 0; i < paths.size(); ++i)
            std::cerr << "  chaîne " << i + 1 << " = " << paths[i] << "\n";
        std::cerr << log.data() << "\n";
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

GLuint loadShaderProgram(const std::vector<std::string>& vertexPaths,
                         const std::vector<std::string>& fragmentPaths)
{
    GLuint vs = compileStage(GL_VERTEX_SHADER, vertexPaths);
    GLuint fs = compileStage(GL_FRAGMENT_SHADER, fragmentPaths);
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
