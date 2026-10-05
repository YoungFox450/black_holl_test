#pragma once

#include <glad/gl.h>
#include <string>
#include <vector>

// Petit utilitaire : compile un programme vertex + fragment depuis des fichiers.
// Chaque étape peut être faite de plusieurs fichiers, collés dans l'ordre
// (GLSL 3.30 n'a pas de #include) ; la ligne "#version 330 core" est ajoutée
// devant, les fichiers n'en contiennent donc pas.
// Retourne 0 en cas d'erreur (le message est affiché sur stderr).
GLuint loadShaderProgram(const std::vector<std::string>& vertexPaths,
                         const std::vector<std::string>& fragmentPaths);
