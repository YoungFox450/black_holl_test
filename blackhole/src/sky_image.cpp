#include "sky_image.hpp"

#include "shader.hpp"   // glad

#include <stb_image.h>
#define TINYEXR_USE_MINIZ 0
#define TINYEXR_USE_STB_ZLIB 1
#include <tinyexr.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>

#ifndef BH_ASSET_DIR
#define BH_ASSET_DIR "assets"
#endif

namespace {

constexpr float kPi = 3.14159265358979f;

bool endsWith(std::string s, const std::string& suffix)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// Réduit l'image d'un facteur entier (moyenne des blocs) : la lumière totale
// est conservée, les étoiles ne disparaissent pas.
void shrink(SkyImage& img, int maxWidth)
{
    int f = 1;
    while (img.width / f > maxWidth) f *= 2;
    if (f == 1) return;
    int w = img.width / f, h = img.height / f;
    std::vector<float> out(size_t(w) * h * 3, 0.0f);
    for (int y = 0; y < h * f; ++y)
        for (int x = 0; x < w * f; ++x)
            for (int c = 0; c < 3; ++c)
                out[(size_t(y / f) * w + x / f) * 3 + c] += img.rgb[(size_t(y) * img.width + x) * 3 + c];
    for (float& v : out) v /= float(f * f);
    img.rgb = std::move(out);
    img.width = w;
    img.height = h;
}

// Ramène la luminance moyenne (pondérée par l'aire, cos(latitude)) à celle
// du ciel procédural : les images HDR n'ont pas d'unité commune.
void normalize(SkyImage& img)
{
    double sum = 0.0, area = 0.0;
    for (int y = 0; y < img.height; ++y) {
        double wy = std::cos((0.5 - (y + 0.5) / img.height) * kPi);
        for (int x = 0; x < img.width; ++x) {
            const float* p = &img.rgb[(size_t(y) * img.width + x) * 3];
            sum += wy * (0.2126 * p[0] + 0.7152 * p[1] + 0.0722 * p[2]);
            area += wy;
        }
    }
    double mean = sum / std::max(area, 1e-9);
    if (mean <= 0.0) return;
    const float scale = float(0.008 / mean);
    for (float& v : img.rgb) v *= scale;
}

bool loadPixels(const std::string& path, SkyImage& img, std::string& error)
{
    if (endsWith(path, ".exr")) {
        float* rgba = nullptr;
        const char* err = nullptr;
        if (LoadEXR(&rgba, &img.width, &img.height, path.c_str(), &err) != TINYEXR_SUCCESS) {
            error = err ? err : "EXR illisible";
            if (err) FreeEXRErrorMessage(err);
            return false;
        }
        img.rgb.resize(size_t(img.width) * img.height * 3);
        for (size_t i = 0; i < size_t(img.width) * img.height; ++i)
            for (int c = 0; c < 3; ++c) img.rgb[i * 3 + c] = std::max(rgba[i * 4 + c], 0.0f);
        std::free(rgba);
        return true;
    }
    int n = 0;
    if (endsWith(path, ".rgbe.png")) {
        // RGBE dans un PNG : R, G, B mantisses, A = exposant + 128.
        stbi_uc* data = stbi_load(path.c_str(), &img.width, &img.height, &n, 4);
        if (!data) { error = stbi_failure_reason(); return false; }
        img.rgb.resize(size_t(img.width) * img.height * 3);
        for (size_t i = 0; i < size_t(img.width) * img.height; ++i) {
            const stbi_uc* p = data + i * 4;
            float f = p[3] ? std::ldexp(1.0f, int(p[3]) - 136) : 0.0f;
            for (int c = 0; c < 3; ++c) img.rgb[i * 3 + c] = (float(p[c]) + 0.5f) * f * (p[3] ? 1.0f : 0.0f);
        }
        stbi_image_free(data);
        return true;
    }
    if (stbi_is_hdr(path.c_str())) {
        float* data = stbi_loadf(path.c_str(), &img.width, &img.height, &n, 3);
        if (!data) { error = stbi_failure_reason(); return false; }
        img.rgb.assign(data, data + size_t(img.width) * img.height * 3);
        stbi_image_free(data);
        return true;
    }
    // Image ordinaire (8 bits, sRGB) : en lumière linéaire, et on redonne de
    // l'éclat aux points saturés (étoiles brillantes écrêtées à 255).
    stbi_uc* data = stbi_load(path.c_str(), &img.width, &img.height, &n, 3);
    if (!data) { error = stbi_failure_reason(); return false; }
    img.rgb.resize(size_t(img.width) * img.height * 3);
    for (size_t i = 0; i < img.rgb.size(); ++i) {
        float v = std::pow(data[i] / 255.0f, 2.2f);
        img.rgb[i] = v * (1.0f + 8.0f * std::pow(v, 6.0f));
    }
    stbi_image_free(data);
    return true;
}

// Repère de l'image : axes x (centre, longitude 0), y (longitude 90°) et z
// (nord), exprimés dans la scène (y = axe du trou noir).
void sceneToImage(const SkySettings& s, float m[9])
{
    // Plan galactique : centre derrière le trou noir vu de la caméra de
    // départ (-z), incliné de "tilt" autour de cette direction, puis tourné
    // de "yaw" autour de l'axe du trou noir.
    const float t = s.tilt * kPi / 180.0f, a = s.yaw * kPi / 180.0f;
    float C[3] = {0.0f, 0.0f, -1.0f};
    float N[3] = {std::sin(t), std::cos(t), 0.0f};
    auto yawRot = [a](float* v) {
        float x = v[0] * std::cos(a) + v[2] * std::sin(a);
        float z = -v[0] * std::sin(a) + v[2] * std::cos(a);
        v[0] = x;
        v[2] = z;
    };
    yawRot(C);
    yawRot(N);
    float Y[3] = {N[1] * C[2] - N[2] * C[1], N[2] * C[0] - N[0] * C[2], N[0] * C[1] - N[1] * C[0]};
    float G[9] = {C[0], C[1], C[2], Y[0], Y[1], Y[2], N[0], N[1], N[2]};   // scène -> galactique
    if (s.galactic) {
        std::copy(G, G + 9, m);
        return;
    }
    // Galactique -> équatorial J2000 : transposée de la matrice classique.
    static const float T[9] = {-0.0548755604f, -0.8734370902f, -0.4838350155f,
                               0.4941094279f,  -0.4448296300f, 0.7469822445f,
                               -0.8676661490f, -0.1980763734f, 0.4559837762f};
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) {
            float v = 0.0f;
            for (int k = 0; k < 3; ++k) v += T[k * 3 + i] * G[k * 3 + j];
            m[i * 3 + j] = v;
        }
}

SkyImage cache;   // l'image reste en mémoire : changer l'orientation ne la relit pas

} // namespace

std::string defaultSkyImagePath()
{
    for (const std::string& dir : {std::string(BH_ASSET_DIR) + "/sky", std::string("assets/sky"),
                                   std::string("../assets/sky")}) {
        std::string p = dir + "/voie-lactee.rgbe.png";
        if (FILE* f = std::fopen(p.c_str(), "rb")) {
            std::fclose(f);
            return p;
        }
    }
    return std::string(BH_ASSET_DIR) + "/sky/voie-lactee.rgbe.png";
}

bool loadSkyImage(const std::string& path, int maxWidth, SkyImage& out, std::string& error)
{
    SkyImage img;
    img.path = path;
    if (!loadPixels(path, img, error)) return false;
    shrink(img, maxWidth);
    normalize(img);
    out = std::move(img);
    return true;
}

unsigned int bakeSkyFromImage(const std::string& shaderDir, unsigned int vao, int faceSize, SkySettings& s)
{
    s.rebuild = false;
    const std::string path = s.path.empty() ? defaultSkyImagePath() : s.path;
    if (cache.path != path) {
        std::string error;
        if (!loadSkyImage(path, 4096, cache, error)) {
            cache = SkyImage{};
            s.status = "Image illisible : " + path + " (" + error + ")";
            std::cerr << s.status << "\n";
            return 0;
        }
        // Les cartes NASA en coordonnées galactiques ont "_gal" dans leur nom.
        if (!s.path.empty()) s.galactic = path.find("_gal") != std::string::npos || path.find("rgbe") != std::string::npos;
    }

    GLuint prog = loadShaderProgram({shaderDir + "/fullscreen.vert"}, {shaderDir + "/sky_image.frag"});
    if (!prog) {
        s.status = "Shader sky_image.frag introuvable";
        return 0;
    }

    GLint maxTex = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTex);
    if (cache.width > maxTex) {
        shrink(cache, maxTex);
    }

    GLuint src = 0;
    glGenTextures(1, &src);
    glBindTexture(GL_TEXTURE_2D, src);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, cache.width, cache.height, 0, GL_RGB, GL_FLOAT, cache.rgb.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_CUBE_MAP, tex);
    for (int face = 0; face < 6; ++face)
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, GL_R11F_G11F_B10F, faceSize, faceSize, 0, GL_RGB,
                     GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

    float m[9];
    sceneToImage(s, m);
    GLuint fbo = 0;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, faceSize, faceSize);
    glUseProgram(prog);
    glUniform1f(glGetUniformLocation(prog, "uFaceSize"), float(faceSize));
    glUniformMatrix3fv(glGetUniformLocation(prog, "uToImage"), 1, GL_TRUE, m);
    glUniform1f(glGetUniformLocation(prog, "uMirror"), s.mirror ? 1.0f : 0.0f);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, src);
    glUniform1i(glGetUniformLocation(prog, "uImage"), 0);
    glBindVertexArray(vao);
    for (int face = 0; face < 6; ++face) {   // une face par appel, comme bakeSky (délai TDR de Windows)
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, tex, 0);
        glUniform1i(glGetUniformLocation(prog, "uFace"), face);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glFinish();
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &fbo);
    glDeleteTextures(1, &src);
    glDeleteProgram(prog);

    glBindTexture(GL_TEXTURE_CUBE_MAP, tex);
    glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
    char buf[64];
    std::snprintf(buf, sizeof(buf), " (%dx%d)", cache.width, cache.height);
    s.status = path + buf;
    return tex;
}
