#pragma once

// Rendu du système double (src/binary.*) : uniforms de l'étoile compagne pour
// le ray tracer, et jet de gaz dessiné en points (shaders/gas.*).

#include "app.hpp"

#include <glad/gl.h>

#include <string>

// Envoie l'étoile compagne au programme de ray tracing (déjà actif).
void setCompanionUniforms(GLuint traceProgram, const App& app);
// Dessine le jet de gaz dans le framebuffer fbo (w x h), en lumière HDR.
void drawGasStream(const std::string& shaderDir, GLuint fbo, int w, int h, const App& app);
void destroyBinaryGfx();
