#include "binary_gfx.hpp"

#include "shader.hpp"

#include <cmath>
#include <vector>

namespace {

struct GasRenderer {
    GLuint program = 0, vao = 0, vbo = 0;
    bool failed = false;
    std::vector<float> data;
} gfx;

struct Basis {
    Vec3 pos, right, up, forward;
};

Basis cameraBasis(const OrbitCamera& cam)
{
    Basis b;
    b.pos = cam.position();
    b.forward = normalize(-b.pos);
    b.right = normalize(cross(b.forward, {0.0f, 1.0f, 0.0f}));
    b.up = cross(b.right, b.forward);
    return b;
}

} // namespace

void setCompanionUniforms(GLuint p, const App& app)
{
    auto loc = [p](const char* name) { return glGetUniformLocation(p, name); };
    const BinarySystem& bin = app.binary;
    if (!bin.settings.enabled || app.starMode) {
        glUniform4f(loc("uCompanion"), 0.0f, 0.0f, 0.0f, 0.0f);
        return;
    }
    const double t = app.simTime;
    Vec3d c = bin.companionPos(t), v = bin.companionVel(t);
    double s = bin.stretch();
    double perp = bin.radius() / std::cbrt(s);   // volume conservé : along * perp² = r³
    double a = bin.settings.separation;
    glUniform4f(loc("uCompanion"), float(c.x), float(c.y), float(c.z), float(perp));
    glUniform3f(loc("uCompAxis"), float(-c.x / a), float(-c.y / a), float(-c.z / a));
    glUniform1f(loc("uCompStretch"), float(s));
    glUniform3f(loc("uCompVel"), float(v.x), float(v.y), float(v.z));
    glUniform1f(loc("uCompTemp"), bin.settings.temperature);
    glUniform1f(loc("uCompBrightness"), 0.22f * bin.settings.brightness);
    // Les rayons X du bord intérieur du disque chauffent la face éclairée.
    float irr = app.showDisk ? 0.9f * app.disk.maxTemperature * std::sqrt(app.disk.brightness / 1.6f) : 0.0f;
    glUniform1f(loc("uCompIrradiation"), irr);
    glUniform1f(loc("uCompAngle"), float(bin.settings.phase + bin.omega() * t));
}

void drawGasStream(const std::string& shaderDir, GLuint fbo, int w, int h, const App& app)
{
    const BinarySystem& bin = app.binary;
    if (!bin.settings.enabled || app.starMode || bin.gas.empty() || gfx.failed) return;
    if (!gfx.program) {
        gfx.program = loadShaderProgram({shaderDir + "/gas.vert"}, {shaderDir + "/gas.frag"});
        if (!gfx.program) {
            gfx.failed = true;
            return;
        }
        glGenVertexArrays(1, &gfx.vao);
        glGenBuffers(1, &gfx.vbo);
        glBindVertexArray(gfx.vao);
        glBindBuffer(GL_ARRAY_BUFFER, gfx.vbo);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 7 * sizeof(float), nullptr);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 7 * sizeof(float),
                              reinterpret_cast<void*>(3 * sizeof(float)));
    }
    gfx.data.clear();
    for (const GasParcel& g : bin.gas)
        gfx.data.insert(gfx.data.end(), {float(g.pos[0]), float(g.pos[1]), float(g.pos[2]), float(g.vel[0]),
                                         float(g.vel[1]), float(g.vel[2]), g.heat});
    glBindVertexArray(gfx.vao);
    glBindBuffer(GL_ARRAY_BUFFER, gfx.vbo);
    glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(gfx.data.size() * sizeof(float)), gfx.data.data(), GL_STREAM_DRAW);

    const Basis cam = cameraBasis(app.camera);
    const GLuint p = gfx.program;
    auto loc = [p](const char* name) { return glGetUniformLocation(p, name); };
    const double t = app.simTime;
    Vec3d c = bin.companionPos(t);
    double perp = bin.radius() / std::cbrt(bin.stretch());

    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, w, h);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    glEnable(GL_PROGRAM_POINT_SIZE);
    glUseProgram(p);
    glUniform3f(loc("uCamPos"), cam.pos.x, cam.pos.y, cam.pos.z);
    glUniform3f(loc("uCamRight"), cam.right.x, cam.right.y, cam.right.z);
    glUniform3f(loc("uCamUp"), cam.up.x, cam.up.y, cam.up.z);
    glUniform3f(loc("uCamForward"), cam.forward.x, cam.forward.y, cam.forward.z);
    glUniform1f(loc("uTanHalf"), std::tan(app.camera.fovY * 0.5f));
    glUniform1f(loc("uAspect"), float(w) / float(h));
    glUniform1f(loc("uResY"), float(h));
    glUniform1i(loc("uDisk"), app.showDisk ? 1 : 0);
    glUniform1f(loc("uDiskIn"), app.disk.innerRadius);
    glUniform1f(loc("uDiskOut"), app.disk.outerRadius);
    glUniform1f(loc("uDoppler"), app.disk.doppler ? 1.0f : 0.0f);
    // Sphère inscrite dans l'étoile : la pointe vers L1 ne cache pas le gaz qui en sort.
    glUniform4f(loc("uCompanion"), float(c.x), float(c.y), float(c.z), float(perp));
    glUniform1f(loc("uGlow"), 0.5f * bin.settings.brightness);
    glDrawArrays(GL_POINTS, 0, GLsizei(bin.gas.size()));
    glDisable(GL_PROGRAM_POINT_SIZE);
    glDisable(GL_BLEND);
}

void destroyBinaryGfx()
{
    glDeleteProgram(gfx.program);
    glDeleteBuffers(1, &gfx.vbo);
    glDeleteVertexArrays(1, &gfx.vao);
    gfx = GasRenderer{};
}
