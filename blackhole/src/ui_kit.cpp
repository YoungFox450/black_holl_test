// Kit d'interface : thème sombre "espace", polices Inter, widgets maison
// (curseurs fins, interrupteurs, sélecteurs segmentés, cartes) et registre
// des sections du panneau. Voir ui_kit.hpp pour l'utilisation.

#include "ui_kit.hpp"

#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#ifndef BH_ASSET_DIR
#define BH_ASSET_DIR "assets"
#endif

namespace ui {

namespace {

ImFont* gBold = nullptr;

std::vector<Section>& sectionList()
{
    static std::vector<Section> list;   // construit au premier appel
    return list;
}

std::vector<Scene>& sceneList()
{
    static std::vector<Scene> list;
    return list;
}

ImU32 withAlpha(ImU32 c, float a)
{
    return (c & ~IM_COL32_A_MASK) | (ImU32(std::clamp(a, 0.0f, 1.0f) * 255.0f) << IM_COL32_A_SHIFT);
}

// Fin du texte visible d'une étiquette ("Masse##ms" -> "Masse").
const char* labelEnd(const char* label)
{
    const char* end = std::strstr(label, "##");
    return end ? end : label + std::strlen(label);
}

void tooltip(const char* text)
{
    if (!text) return;
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 24.0f);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}

// Petit rond "?" après une étiquette : signale qu'une explication existe.
void helpBadge(ImDrawList* dl, ImVec2 center, float radius, bool hovered)
{
    const ImU32 c = hovered ? color::accent : withAlpha(color::muted, 0.8f);
    dl->AddCircle(center, radius, c, 16, 1.2f);
    const ImVec2 q = ImGui::CalcTextSize("?");
    const float s = 0.75f;
    dl->AddText(ImGui::GetFont(), ImGui::GetFontSize() * s,
                ImVec2(center.x - q.x * s * 0.5f, center.y - q.y * s * 0.5f), c, "?");
}

// Ligne "étiquette (?) ......... valeur" au-dessus d'un curseur.
void labelRow(const char* label, const char* valueText, const char* help)
{
    const float w = ImGui::GetContentRegionAvail().x;
    const float h = ImGui::GetTextLineHeight();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(w, h));
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    const char* end = labelEnd(label);
    dl->AddText(p, ImGui::GetColorU32(ImGuiCol_Text), label, end);
    if (help) {
        const float lw = ImGui::CalcTextSize(label, end).x;
        helpBadge(dl, ImVec2(p.x + lw + h * 0.6f, p.y + h * 0.5f), h * 0.36f, hovered);
        if (hovered) tooltip(help);
    }
    if (valueText) {
        const float vw = ImGui::CalcTextSize(valueText).x;
        dl->AddText(ImVec2(p.x + w - vw, p.y), withAlpha(color::accent, 0.95f), valueText);
    }
}

// Curseur fin : piste, partie remplie couleur accent, bouton rond.
// La logique (glisser, Ctrl+clic pour saisir) reste celle d'ImGui.
bool sliderImpl(const char* label, ImGuiDataType type, void* v, const void* lo, const void* hi,
                const char* fmt, const char* help, bool log, double t)
{
    char valueText[64];
    if (type == ImGuiDataType_Float) std::snprintf(valueText, sizeof(valueText), fmt, double(*(float*)v));
    else if (type == ImGuiDataType_Double) std::snprintf(valueText, sizeof(valueText), fmt, *(double*)v);
    else std::snprintf(valueText, sizeof(valueText), fmt, *(int*)v);

    ImGui::PushID(label);
    labelRow(label, valueText, help);

    const ImGuiID id = ImGui::GetID("##s");
    const bool typing = ImGui::TempInputIsActive(id);
    const ImU32 clear = IM_COL32(0, 0, 0, 0);
    int pushed = 0;
    if (!typing) {
        for (ImGuiCol c : {ImGuiCol_FrameBg, ImGuiCol_FrameBgHovered, ImGuiCol_FrameBgActive,
                           ImGuiCol_SliderGrab, ImGuiCol_SliderGrabActive, ImGuiCol_Text}) {
            ImGui::PushStyleColor(c, clear);
            ++pushed;
        }
    }
    ImGui::SetNextItemWidth(-FLT_MIN);
    const bool changed = ImGui::SliderScalar("##s", type, v, lo, hi, fmt,
                                             log ? ImGuiSliderFlags_Logarithmic : ImGuiSliderFlags_None);
    ImGui::PopStyleColor(pushed);
    ImGui::SetItemTooltip("Ctrl + clic : saisir une valeur");

    if (!typing && !ImGui::TempInputIsActive(id)) {
        const ImVec2 a = ImGui::GetItemRectMin();
        const ImVec2 b = ImGui::GetItemRectMax();
        const bool hot = ImGui::IsItemHovered() || ImGui::IsItemActive();
        // Recalcule la position après l'éventuel changement de valeur.
        if (changed) {
            if (type == ImGuiDataType_Float) {
                const float x = *(float*)v, l = *(const float*)lo, h = *(const float*)hi;
                t = log ? std::log(x / l) / std::log(h / l) : (x - l) / (h - l);
            } else if (type == ImGuiDataType_Double) {
                const double x = *(double*)v, l = *(const double*)lo, h = *(const double*)hi;
                t = log ? std::log(x / l) / std::log(h / l) : (x - l) / (h - l);
            } else {
                const int x = *(int*)v, l = *(const int*)lo, h = *(const int*)hi;
                t = h > l ? double(x - l) / double(h - l) : 0.0;
            }
        }
        t = std::clamp(std::isfinite(t) ? t : 0.0, 0.0, 1.0);

        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImGuiStyle& st = ImGui::GetStyle();
        const float knob = st.GrabMinSize;
        const float x0 = a.x + 2.0f + knob * 0.5f;
        const float x1 = b.x - 2.0f - knob * 0.5f;
        const float cy = (a.y + b.y) * 0.5f;
        const float th = std::max(3.0f, knob * 0.32f);
        const float kx = x0 + float(t) * (x1 - x0);
        dl->AddRectFilled(ImVec2(a.x + 2.0f, cy - th * 0.5f), ImVec2(b.x - 2.0f, cy + th * 0.5f),
                          ImGui::GetColorU32(ImGuiCol_FrameBg), th);
        dl->AddRectFilled(ImVec2(a.x + 2.0f, cy - th * 0.5f), ImVec2(kx, cy + th * 0.5f),
                          withAlpha(color::accent, hot ? 1.0f : 0.85f), th);
        const float r = knob * (hot ? 0.58f : 0.5f);
        if (hot) dl->AddCircleFilled(ImVec2(kx, cy), r * 1.7f, withAlpha(color::accent, 0.18f));
        dl->AddCircleFilled(ImVec2(kx, cy), r, IM_COL32(245, 247, 250, 255));
        dl->AddCircle(ImVec2(kx, cy), r, withAlpha(color::accent, 0.9f), 0, 1.5f);
    }
    ImGui::PopID();
    return changed;
}

template <typename T>
double ratio(T v, T lo, T hi, bool log)
{
    if (log && lo > 0 && v > 0) return std::log(double(v) / lo) / std::log(double(hi) / lo);
    return hi > lo ? double(v - lo) / double(hi - lo) : 0.0;
}

// Majuscules pour les titres de cartes, accents compris (é -> É).
std::string upper(const char* s)
{
    std::string out(s, labelEnd(s));
    for (size_t i = 0; i < out.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(out[i]);
        if (c >= 'a' && c <= 'z') out[i] = char(c - 32);
        else if (c == 0xC3 && i + 1 < out.size()) {
            unsigned char n = static_cast<unsigned char>(out[i + 1]);
            if (n >= 0xA0 && n <= 0xBE && n != 0xB7) out[i + 1] = char(n - 0x20);
            ++i;
        }
    }
    return out;
}

bool fileExists(const std::string& path)
{
    if (FILE* f = std::fopen(path.c_str(), "rb")) {
        std::fclose(f);
        return true;
    }
    return false;
}

std::string findFont(const char* name)
{
    for (const std::string& dir : {std::string(BH_ASSET_DIR) + "/fonts", std::string("assets/fonts"),
                                  std::string("../assets/fonts")}) {
        const std::string path = dir + "/" + name;
        if (fileExists(path)) return path;
    }
    return {};
}

} // namespace

bool registerSection(Tab tab, int order, const char* title, DrawFn draw, VisibleFn visible)
{
    auto& list = sectionList();
    list.push_back(Section{tab, order, title, std::move(draw), std::move(visible)});
    std::stable_sort(list.begin(), list.end(), [](const Section& a, const Section& b) {
        return a.tab != b.tab ? a.tab < b.tab : a.order < b.order;
    });
    return true;
}

bool registerScene(const char* name, VisibleFn isActive, SelectFn select)
{
    sceneList().push_back(Scene{name, std::move(isActive), std::move(select)});
    return true;
}

const std::vector<Section>& sections() { return sectionList(); }
const std::vector<Scene>& scenes() { return sceneList(); }

ImFont* boldFont() { return gBold ? gBold : ImGui::GetFont(); }

void loadFonts(float scale)
{
    ImGuiIO& io = ImGui::GetIO();
    const std::string regular = findFont("Inter-Regular.otf");
    const std::string bold = findFont("Inter-SemiBold.otf");
    ImFontConfig cfg;
    cfg.OversampleH = 2;
    cfg.OversampleV = 1;
    cfg.PixelSnapH = false;
    ImFont* base = nullptr;
    if (!regular.empty())
        base = io.Fonts->AddFontFromFileTTF(regular.c_str(), 15.0f * scale, &cfg);
    if (!base) {
        // Police intégrée d'ImGui si les fichiers manquent.
        ImFontConfig def;
        def.SizePixels = 13.0f * scale;
        io.Fonts->AddFontDefault(&def);
        gBold = nullptr;
        return;
    }
    gBold = bold.empty() ? nullptr : io.Fonts->AddFontFromFileTTF(bold.c_str(), 15.0f * scale, &cfg);
}

void applyTheme(float scale)
{
    ImGuiStyle& s = ImGui::GetStyle();
    s = ImGuiStyle();
    s.WindowPadding = ImVec2(16, 14);
    s.FramePadding = ImVec2(10, 6);
    s.CellPadding = ImVec2(6, 5);
    s.ItemSpacing = ImVec2(8, 8);
    s.ItemInnerSpacing = ImVec2(6, 6);
    s.IndentSpacing = 14;
    s.ScrollbarSize = 8;
    s.GrabMinSize = 14;
    s.WindowBorderSize = 0;
    s.ChildBorderSize = 1;
    s.PopupBorderSize = 1;
    s.FrameBorderSize = 0;
    s.WindowRounding = 0;
    s.ChildRounding = 10;
    s.FrameRounding = 7;
    s.PopupRounding = 8;
    s.ScrollbarRounding = 8;
    s.GrabRounding = 7;
    s.TabRounding = 7;
    s.SeparatorTextBorderSize = 1;

    auto rgb = [](int r, int g, int b, float a = 1.0f) {
        return ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, a);
    };
    const ImVec4 accent = rgb(255, 159, 67);
    ImVec4* c = s.Colors;
    c[ImGuiCol_Text] = rgb(230, 233, 239);
    c[ImGuiCol_TextDisabled] = rgb(140, 148, 165);
    c[ImGuiCol_WindowBg] = rgb(13, 16, 23, 0.94f);
    c[ImGuiCol_ChildBg] = rgb(22, 27, 38, 0.95f);
    c[ImGuiCol_PopupBg] = rgb(22, 27, 38, 0.98f);
    c[ImGuiCol_Border] = rgb(42, 49, 64, 0.8f);
    c[ImGuiCol_BorderShadow] = rgb(0, 0, 0, 0);
    c[ImGuiCol_FrameBg] = rgb(38, 45, 60);
    c[ImGuiCol_FrameBgHovered] = rgb(46, 54, 72);
    c[ImGuiCol_FrameBgActive] = rgb(54, 63, 84);
    c[ImGuiCol_TitleBg] = c[ImGuiCol_WindowBg];
    c[ImGuiCol_TitleBgActive] = c[ImGuiCol_WindowBg];
    c[ImGuiCol_TitleBgCollapsed] = c[ImGuiCol_WindowBg];
    c[ImGuiCol_MenuBarBg] = c[ImGuiCol_ChildBg];
    c[ImGuiCol_ScrollbarBg] = rgb(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = rgb(60, 68, 88);
    c[ImGuiCol_ScrollbarGrabHovered] = rgb(80, 90, 115);
    c[ImGuiCol_ScrollbarGrabActive] = accent;
    c[ImGuiCol_CheckMark] = accent;
    c[ImGuiCol_SliderGrab] = accent;
    c[ImGuiCol_SliderGrabActive] = rgb(255, 184, 110);
    c[ImGuiCol_Button] = rgb(38, 45, 60);
    c[ImGuiCol_ButtonHovered] = rgb(50, 59, 79);
    c[ImGuiCol_ButtonActive] = rgb(62, 72, 96);
    c[ImGuiCol_Header] = rgb(38, 45, 60);
    c[ImGuiCol_HeaderHovered] = rgb(50, 59, 79);
    c[ImGuiCol_HeaderActive] = rgb(62, 72, 96);
    c[ImGuiCol_Separator] = rgb(42, 49, 64);
    c[ImGuiCol_SeparatorHovered] = accent;
    c[ImGuiCol_SeparatorActive] = accent;
    c[ImGuiCol_ResizeGrip] = rgb(0, 0, 0, 0);
    c[ImGuiCol_ResizeGripHovered] = accent;
    c[ImGuiCol_ResizeGripActive] = accent;
    c[ImGuiCol_Tab] = c[ImGuiCol_Button];
    c[ImGuiCol_TabHovered] = c[ImGuiCol_ButtonHovered];
    c[ImGuiCol_TabSelected] = c[ImGuiCol_ButtonActive];
    c[ImGuiCol_TabSelectedOverline] = accent;
    c[ImGuiCol_TextSelectedBg] = rgb(255, 159, 67, 0.35f);
    c[ImGuiCol_NavCursor] = accent;
    c[ImGuiCol_ModalWindowDimBg] = rgb(0, 0, 0, 0.5f);

    s.ScaleAllSizes(scale);
}

bool slider(const char* label, float* v, float lo, float hi, const char* fmt, const char* help, bool log)
{
    return sliderImpl(label, ImGuiDataType_Float, v, &lo, &hi, fmt, help, log, ratio(*v, lo, hi, log));
}

bool slider(const char* label, double* v, double lo, double hi, const char* fmt, const char* help, bool log)
{
    return sliderImpl(label, ImGuiDataType_Double, v, &lo, &hi, fmt, help, log, ratio(*v, lo, hi, log));
}

bool slider(const char* label, int* v, int lo, int hi, const char* fmt, const char* help)
{
    return sliderImpl(label, ImGuiDataType_S32, v, &lo, &hi, fmt, help, false, ratio(*v, lo, hi, false));
}

bool toggle(const char* label, bool* v, const char* help)
{
    ImGui::PushID(label);
    const float w = ImGui::GetContentRegionAvail().x;
    const float rowH = ImGui::GetFrameHeight();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton("##t", ImVec2(w, rowH));
    if (pressed) *v = !*v;
    const bool hovered = ImGui::IsItemHovered();

    // Petite animation du bouton (0 = éteint, 1 = allumé).
    ImGuiStorage* store = ImGui::GetStateStorage();
    const ImGuiID key = ImGui::GetItemID();
    float t = store->GetFloat(key, *v ? 1.0f : 0.0f);
    const float target = *v ? 1.0f : 0.0f;
    const float step = ImGui::GetIO().DeltaTime * 10.0f;
    t = t < target ? std::min(target, t + step) : std::max(target, t - step);
    store->SetFloat(key, t);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float th = ImGui::GetTextLineHeight();
    const float ty = p.y + (rowH - th) * 0.5f;
    const char* end = labelEnd(label);
    dl->AddText(ImVec2(p.x, ty), ImGui::GetColorU32(ImGuiCol_Text), label, end);
    if (help) {
        const float lw = ImGui::CalcTextSize(label, end).x;
        helpBadge(dl, ImVec2(p.x + lw + th * 0.6f, ty + th * 0.5f), th * 0.36f, hovered);
    }

    const float sh = rowH * 0.72f, sw = sh * 1.8f;
    const ImVec2 a(p.x + w - sw, p.y + (rowH - sh) * 0.5f);
    const ImVec2 b(a.x + sw, a.y + sh);
    const ImVec4 off = ImGui::GetStyleColorVec4(ImGuiCol_FrameBg);
    const ImVec4 on = ImGui::ColorConvertU32ToFloat4(color::accent);
    ImVec4 bg(off.x + (on.x - off.x) * t, off.y + (on.y - off.y) * t, off.z + (on.z - off.z) * t, 1.0f);
    if (hovered) bg = ImVec4(std::min(1.0f, bg.x * 1.1f + 0.03f), std::min(1.0f, bg.y * 1.1f + 0.03f),
                             std::min(1.0f, bg.z * 1.1f + 0.03f), 1.0f);
    dl->AddRectFilled(a, b, ImGui::ColorConvertFloat4ToU32(bg), sh * 0.5f);
    const float r = sh * 0.5f - 2.5f;
    const float kx = a.x + sh * 0.5f + t * (sw - sh);
    dl->AddCircleFilled(ImVec2(kx, a.y + sh * 0.5f), r, IM_COL32(245, 247, 250, 255));

    if (hovered && help) tooltip(help);
    ImGui::PopID();
    return pressed;
}

bool button(const char* label, bool primary)
{
    if (primary) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(withAlpha(color::accent, 0.9f)));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::ColorConvertU32ToFloat4(color::accent));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.0f, 0.72f, 0.43f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.08f, 0.06f, 0.04f, 1.0f));
    }
    const bool pressed = ImGui::Button(label, ImVec2(-FLT_MIN, 0.0f));
    if (primary) ImGui::PopStyleColor(4);
    return pressed;
}

bool segmented(const char* id, int* current, const char* const* items, int count)
{
    if (count <= 0) return false;
    ImGui::PushID(id);
    const float w = ImGui::GetContentRegionAvail().x;
    const float h = ImGui::GetFrameHeight() + 4.0f;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), ImGui::GetColorU32(ImGuiCol_FrameBg),
                      ImGui::GetStyle().FrameRounding + 2.0f);

    bool changed = false;
    const float itemW = w / float(count);
    for (int i = 0; i < count; ++i) {
        const ImVec2 a(p.x + itemW * i, p.y);
        ImGui::SetCursorScreenPos(a);
        ImGui::PushID(i);
        if (ImGui::InvisibleButton("##seg", ImVec2(itemW, h)) && *current != i) {
            *current = i;
            changed = true;
        }
        const bool hovered = ImGui::IsItemHovered();
        ImGui::PopID();
        const bool selected = *current == i;
        const ImVec2 ia(a.x + 3.0f, a.y + 3.0f), ib(a.x + itemW - 3.0f, a.y + h - 3.0f);
        if (selected)
            dl->AddRectFilled(ia, ib, withAlpha(color::accent, 0.95f), ImGui::GetStyle().FrameRounding);
        else if (hovered)
            dl->AddRectFilled(ia, ib, ImGui::GetColorU32(ImGuiCol_FrameBgHovered), ImGui::GetStyle().FrameRounding);
        if (selected) ImGui::PushFont(boldFont());
        const char* end = labelEnd(items[i]);
        const ImVec2 ts = ImGui::CalcTextSize(items[i], end);
        const ImU32 tc = selected ? IM_COL32(24, 16, 8, 255)
                                  : (hovered ? ImGui::GetColorU32(ImGuiCol_Text) : color::muted);
        dl->AddText(ImVec2(a.x + (itemW - ts.x) * 0.5f, a.y + (h - ts.y) * 0.5f), tc, items[i], end);
        if (selected) ImGui::PopFont();
    }
    ImGui::SetCursorScreenPos(p);
    ImGui::Dummy(ImVec2(w, h));
    ImGui::PopID();
    return changed;
}

void value(const char* label, const char* fmt, ...)
{
    char buf[256];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    const float w = ImGui::GetContentRegionAvail().x;
    const float vw = ImGui::CalcTextSize(buf).x;
    const float lw = ImGui::CalcTextSize(label).x;
    if (lw + vw + 12.0f > w) {
        // Trop long pour une ligne : valeur sous l'étiquette.
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(color::muted), "%s", label);
        ImGui::Indent();
        ImGui::TextWrapped("%s", buf);
        ImGui::Unindent();
        return;
    }
    const float x = ImGui::GetCursorPosX();
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(color::muted), "%s", label);
    ImGui::SameLine(x + w - vw);
    ImGui::TextUnformatted(buf);
}

void subheading(const char* text)
{
    ImGui::Dummy(ImVec2(0.0f, 2.0f));
    ImGui::PushFont(boldFont());
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(color::muted), "%s", text);
    ImGui::PopFont();
}

void note(const char* text)
{
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(color::muted));
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}

ImVec4 blackbodyColor(double temperature)
{
    // Même formule que blackbody() des shaders (approximation de Tanner Helland).
    const double t = std::clamp(temperature, 1000.0, 40000.0) / 100.0;
    auto c01 = [](double v) { return float(std::clamp(v, 0.0, 1.0)); };
    const float r = t <= 66.0 ? 1.0f : c01(1.29293618 * std::pow(t - 60.0, -0.1332047592));
    const float g = t <= 66.0 ? c01(0.39008157 * std::log(t) - 0.63184144)
                              : c01(1.12989086 * std::pow(t - 60.0, -0.0755148492));
    const float b = t >= 66.0 ? 1.0f : (t <= 19.0 ? 0.0f : c01(0.54320678 * std::log(t - 10.0) - 1.19625408));
    return ImVec4(r, g, b, 1.0f);
}

void beginCard(const char* title)
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ImGui::GetFontSize() * 0.9f, ImGui::GetFontSize() * 0.75f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, ImGui::GetFontSize() * 0.4f));
    ImGui::BeginChild(title, ImVec2(0.0f, 0.0f),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PushFont(boldFont());
    const std::string t = upper(title);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float h = ImGui::GetTextLineHeight();
    // Petite barre de couleur avant le titre.
    ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(p.x, p.y + h * 0.15f), ImVec2(p.x + 3.0f, p.y + h * 0.85f),
                                              color::accent, 1.5f);
    ImGui::SetCursorScreenPos(ImVec2(p.x + 10.0f, p.y));
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(color::accent), "%s", t.c_str());
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0.0f, 1.0f));
}

void endCard()
{
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::Dummy(ImVec2(0.0f, 2.0f));
}

} // namespace ui
