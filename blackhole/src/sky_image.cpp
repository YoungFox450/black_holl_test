#include "sky_image.hpp"

#include "shader.hpp"   // glad

#include <stb_image.h>
#define TINYEXR_USE_MINIZ 0
#define TINYEXR_USE_STB_ZLIB 1
#include <tinyexr.h>

#include <algorithm>
#include <cstdint>
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

// Remplit img.rgb depuis l'image source (srcW x srcH) en la réduisant au
// passage d'un facteur entier (moyenne des blocs) si elle dépasse maxWidth :
// la lumière totale est conservée, les étoiles ne disparaissent pas, et on
// n'alloue jamais l'image pleine taille en flottants (une carte NASA en 16k
// ferait 1,6 Go de plus).
template <typename Get>
void readReduced(SkyImage& img, int srcW, int srcH, int maxWidth, Get get)
{
    int f = 1;
    while (srcW / f > maxWidth) f *= 2;
    const int w = srcW / f, h = srcH / f;
    img.width = w;
    img.height = h;
    img.rgb.assign(size_t(w) * h * 3, 0.0f);
    if (f == 1) {   // cas courant (carte 4096 px) : copie directe
        for (size_t i = 0; i < size_t(w) * h; ++i)
            for (int c = 0; c < 3; ++c) img.rgb[i * 3 + c] = get(i, c);
        return;
    }
    const float inv = 1.0f / float(f * f);
    for (int y = 0; y < h * f; ++y)
        for (int x = 0; x < w * f; ++x) {
            float* o = &img.rgb[(size_t(y / f) * w + x / f) * 3];
            for (int c = 0; c < 3; ++c) o[c] += get(size_t(y) * srcW + x, c) * inv;
        }
}

// Compacte l'image en RGB9_E5 (32 bits par texel, exposant commun) : c'est
// le format de la texture source sur le GPU, deux fois moins lourd que
// RGB16F (que les pilotes Intel stockent en RGBA16F, 8 octets par texel),
// et l'image gardée en mémoire passe de 12 à 4 octets par texel.
uint32_t packRgb9e5(const float* rgb)
{
    const float kMax = 65408.0f;   // (2^9 - 1) / 2^9 * 2^15
    float r = std::clamp(rgb[0], 0.0f, kMax), g = std::clamp(rgb[1], 0.0f, kMax), b = std::clamp(rgb[2], 0.0f, kMax);
    float m = std::max({r, g, b});
    if (m < 1.0e-20f) return 0;
    int ex = 0;
    std::frexp(m, &ex);                         // m = f * 2^ex, f dans [0,5 ; 1[
    int e = std::max(-16, ex - 1) + 1 + 15;     // exposant biaisé : floor(log2(m)) + 1 + 15
    float scale = std::ldexp(1.0f, 9 - (e - 15));
    if (int(std::floor(m * scale + 0.5f)) == 512) {
        ++e;
        scale *= 0.5f;
    }
    e = std::clamp(e, 0, 31);
    auto q = [scale](float v) { return std::min(uint32_t(std::floor(v * scale + 0.5f)), 511u); };
    return q(r) | (q(g) << 9) | (q(b) << 18) | (uint32_t(e) << 27);
}

void pack(SkyImage& img)
{
    img.packed.resize(size_t(img.width) * img.height);
    for (size_t i = 0; i < img.packed.size(); ++i) img.packed[i] = packRgb9e5(&img.rgb[i * 3]);
    std::vector<float>().swap(img.rgb);   // libère les flottants
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

bool loadPixels(const std::string& path, int maxWidth, SkyImage& img, std::string& error)
{
    if (endsWith(path, ".exr")) {
        float* rgba = nullptr;
        const char* err = nullptr;
        if (LoadEXR(&rgba, &img.width, &img.height, path.c_str(), &err) != TINYEXR_SUCCESS) {
            error = err ? err : "EXR illisible";
            if (err) FreeEXRErrorMessage(err);
            return false;
        }
        readReduced(img, img.width, img.height, maxWidth,
                    [rgba](size_t i, int c) { return std::max(rgba[i * 4 + c], 0.0f); });
        std::free(rgba);
        return true;
    }
    int n = 0;
    if (endsWith(path, ".rgbe.png")) {
        // RGBE dans un PNG : R, G, B mantisses, A = exposant + 128.
        stbi_uc* data = stbi_load(path.c_str(), &img.width, &img.height, &n, 4);
        if (!data) { error = stbi_failure_reason(); return false; }
        float scale[256];   // 2^(e - 136) pour chaque exposant (0 : noir)
        for (int e = 0; e < 256; ++e) scale[e] = e ? std::ldexp(1.0f, e - 136) : 0.0f;
        readReduced(img, img.width, img.height, maxWidth, [data, &scale](size_t i, int c) {
            const stbi_uc* p = data + i * 4;
            return (float(p[c]) + 0.5f) * scale[p[3]];
        });
        stbi_image_free(data);
        return true;
    }
    if (stbi_is_hdr(path.c_str())) {
        float* data = stbi_loadf(path.c_str(), &img.width, &img.height, &n, 3);
        if (!data) { error = stbi_failure_reason(); return false; }
        readReduced(img, img.width, img.height, maxWidth, [data](size_t i, int c) { return data[i * 3 + c]; });
        stbi_image_free(data);
        return true;
    }
    // Image ordinaire (8 bits, sRGB) : en lumière linéaire, et on redonne de
    // l'éclat aux points saturés (étoiles brillantes écrêtées à 255).
    stbi_uc* data = stbi_load(path.c_str(), &img.width, &img.height, &n, 3);
    if (!data) { error = stbi_failure_reason(); return false; }
    // Table : 256 valeurs possibles, inutile de refaire deux pow par octet.
    float lut[256];
    for (int k = 0; k < 256; ++k) {
        float v = std::pow(k / 255.0f, 2.2f);
        lut[k] = v * (1.0f + 8.0f * std::pow(v, 6.0f));
    }
    readReduced(img, img.width, img.height, maxWidth, [data, &lut](size_t i, int c) { return lut[data[i * 3 + c]]; });
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
    if (!loadPixels(path, maxWidth, img, error)) return false;
    normalize(img);
    pack(img);
    out = std::move(img);
    return true;
}

namespace {

// La texture source reste sur le GPU entre deux reconstructions : tourner le
// ciel au curseur (une reconstruction par image) ne renvoie plus l'image.
GLuint srcTex = 0;
std::string srcPath;
bool srcFloat = false;
// Verdict du pilote sur RGB9_E5, valable pour toute la session :
// 0 pas encore testé, 1 lu correctement, -1 lu en noir (on passe en RGB16F).
int rgb9e5State = 0;

void releaseSource()
{
    glDeleteTextures(1, &srcTex);
    srcTex = 0;
    srcPath.clear();
}

// Texture source : RGB9_E5 compacte, ou RGB16F décompressée sur le CPU.
GLuint uploadSource(bool asFloat)
{
    if (srcTex && srcPath == cache.path && srcFloat == asFloat) return srcTex;
    releaseSource();
    GLuint src = 0;
    glGenTextures(1, &src);
    glBindTexture(GL_TEXTURE_2D, src);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    if (asFloat) {
        std::vector<float> rgb(cache.packed.size() * 3);
        for (size_t i = 0; i < cache.packed.size(); ++i) {
            uint32_t v = cache.packed[i];
            float scale = std::ldexp(1.0f, int(v >> 27) - 15 - 9);
            rgb[3 * i + 0] = float(v & 511u) * scale;
            rgb[3 * i + 1] = float((v >> 9) & 511u) * scale;
            rgb[3 * i + 2] = float((v >> 18) & 511u) * scale;
        }
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, cache.width, cache.height, 0, GL_RGB, GL_FLOAT, rgb.data());
    } else {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB9_E5, cache.width, cache.height, 0, GL_RGB,
                     GL_UNSIGNED_INT_5_9_9_9_REV, cache.packed.data());
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    srcTex = src;
    srcPath = cache.path;
    srcFloat = asFloat;
    return src;
}

// Projette l'image sur les 6 faces du cube (la source est gardée).
GLuint renderSkyFaces(GLuint prog, GLuint vao, int faceSize, const SkySettings& s, GLuint src)
{
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

    glBindTexture(GL_TEXTURE_CUBE_MAP, tex);
    glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
    return tex;
}

// Lit un petit niveau de mipmap (8x8 par face) : tout à zéro = ciel noir.
bool isBlack(GLuint tex, int faceSize)
{
    int level = 0;
    while ((faceSize >> level) > 8) ++level;
    int n = std::max(faceSize >> level, 1);
    std::vector<float> px(size_t(n) * n * 3);
    float peak = 0.0f;
    glBindTexture(GL_TEXTURE_CUBE_MAP, tex);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    for (int face = 0; face < 6; ++face) {
        glGetTexImage(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, level, GL_RGB, GL_FLOAT, px.data());
        for (float v : px)
            if (std::isfinite(v)) peak = std::max(peak, v);
    }
    return peak < 1e-6f;
}

} // namespace

unsigned int bakeSkyFromImage(const std::string& shaderDir, unsigned int vao, int faceSize, SkySettings& s)
{
    s.rebuild = false;
    const std::string path = s.path.empty() ? defaultSkyImagePath() : s.path;
    if (cache.path != path) {
        std::string error;
        GLint maxTex = 0;
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTex);
        releaseSource();
        if (!loadSkyImage(path, std::min(4096, std::max(int(maxTex), 1024)), cache, error)) {
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

    // Certains pilotes (Intel sous Windows notamment) acceptent la texture
    // compacte RGB9_E5 mais la lisent en noir. On vérifie le résultat et on
    // recommence avec une texture flottante classique si besoin. Le test ne
    // tourne qu'à la première construction : ensuite on connaît le pilote.
    bool asFloat = rgb9e5State < 0;
    GLuint tex = renderSkyFaces(prog, vao, faceSize, s, uploadSource(asFloat));
    if (rgb9e5State == 0) {
        if (!isBlack(tex, faceSize)) {
            rgb9e5State = 1;
        } else {
            std::cerr << "Fond de ciel noir avec la texture RGB9_E5, nouvel essai en RGB16F\n";
            glDeleteTextures(1, &tex);
            asFloat = true;
            tex = renderSkyFaces(prog, vao, faceSize, s, uploadSource(true));
            if (isBlack(tex, faceSize)) {
                glDeleteTextures(1, &tex);
                glDeleteProgram(prog);
                releaseSource();
                s.status = "Image chargée mais rendue noire par le pilote graphique : ciel procédural utilisé";
                std::cerr << s.status << "\n";
                return 0;
            }
            rgb9e5State = -1;
        }
    }
    const char* mode = asFloat ? ", RGB16F" : "";
    glDeleteProgram(prog);
    char buf[64];
    std::snprintf(buf, sizeof(buf), " (%dx%d%s)", cache.width, cache.height, mode);
    s.status = path + buf;
    return tex;
}
