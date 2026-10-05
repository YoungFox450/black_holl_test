#pragma once

#include <glad/gl.h>
#include <string>

// Petit utilitaire : compile un programme vertex + fragment depuis des fichiers.
// Retourne 0 en cas d'erreur (le message est affiché sur stderr).
GLuint loadShaderProgram(const std::string& vertexPath, const std::string& fragmentPath);
