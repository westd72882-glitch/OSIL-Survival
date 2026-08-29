#include "UIStyle.h"
#include "UIDraw.h"
#include <math.h>

// Палитра снята с интерфейса оригинала: почти чёрный графит, серо-оливковые линии,
// текст без белизны. Ни одного насыщенного цвета — самый «яркий» здесь золотой, и тот
// приглушён до 0.83.
const UIColor UI_BG_DEEP   = { 0.035f, 0.040f, 0.036f };
const UIColor UI_BG_PANEL  = { 0.070f, 0.078f, 0.068f };
const UIColor UI_BG_SLOT   = { 0.105f, 0.113f, 0.098f };
const UIColor UI_LINE      = { 0.380f, 0.400f, 0.320f };
const UIColor UI_LINE_DIM  = { 0.215f, 0.230f, 0.185f };
const UIColor UI_TEXT      = { 0.780f, 0.790f, 0.720f };
const UIColor UI_TEXT_DIM  = { 0.480f, 0.500f, 0.440f };
const UIColor UI_ACCENT    = { 0.560f, 0.680f, 0.380f };
const UIColor UI_GOLD      = { 0.830f, 0.740f, 0.360f };
const UIColor UI_DANGER    = { 0.720f, 0.270f, 0.220f };

void uiTextColor(const UIColor& c, unsigned char out[4], float alpha){
    auto b = [](float v)->unsigned char{
        int i = (int)(v * 255.0f + 0.5f);
        if(i < 0) i = 0; if(i > 255) i = 255;
        return (unsigned char)i;
    };
    out[0] = b(c.r); out[1] = b(c.g); out[2] = b(c.b); out[3] = b(alpha);
}

// Толщина линии — ровно один «пиксель интерфейса». Двойная рамка держится на том, что
// линии тонкие: сделай их толще, и вместо старого технического окна получится бордюр.
static const float T = 1.0f;

void uiThinFrame(float x, float y, float w, float h, const UIColor& c, float alpha){
    drawUIRect(x,         y,         w, T, 0, c.r, c.g, c.b, alpha, false);
    drawUIRect(x,         y + h - T, w, T, 0, c.r, c.g, c.b, alpha, false);
    drawUIRect(x,         y,         T, h, 0, c.r, c.g, c.b, alpha, false);
    drawUIRect(x + w - T, y,         T, h, 0, c.r, c.g, c.b, alpha, false);
}

void uiDoubleFrame(float x, float y, float w, float h, float alpha){
    uiThinFrame(x, y, w, h, UI_LINE, 0.85f * alpha);
    const float g = 3.0f; // зазор между линиями
    if(w > g*2.0f + 2.0f && h > g*2.0f + 2.0f)
        uiThinFrame(x + g, y + g, w - g*2.0f, h - g*2.0f, UI_LINE_DIM, 0.70f * alpha);
}

void uiScanlines(float x, float y, float w, float h, float alpha){
    // Через каждые 3 пикселя — линия в 1 px с очень малой непрозрачностью. Заметить её
    // отдельно нельзя, но панель перестаёт выглядеть «пластиковой заливкой».
    for(float yy = y + 1.0f; yy < y + h; yy += 3.0f){
        drawUIRect(x, yy, w, 1.0f, 0, 0.0f, 0.0f, 0.0f, 0.10f * alpha, false);
    }
}

void uiPanel(float x, float y, float w, float h, float alpha, float fill){
    drawUIRect(x, y, w, h, 0, UI_BG_PANEL.r, UI_BG_PANEL.g, UI_BG_PANEL.b, fill * alpha, false);
    uiScanlines(x, y, w, h, alpha);
    uiDoubleFrame(x, y, w, h, alpha);
}

void uiSectionBar(float x, float y, float w, float alpha){
    drawUIRect(x + 1.0f, y + 1.0f, w - 2.0f, 20.0f, 0,
               UI_BG_DEEP.r, UI_BG_DEEP.g, UI_BG_DEEP.b, 0.92f * alpha, false);
    drawUIRect(x + 1.0f, y + 21.0f, w - 2.0f, T, 0,
               UI_LINE_DIM.r, UI_LINE_DIM.g, UI_LINE_DIM.b, 0.75f * alpha, false);
}

void uiSlot(float x, float y, float w, float h, float alpha, bool selected){
    drawUIRect(x, y, w, h, 0, UI_BG_SLOT.r, UI_BG_SLOT.g, UI_BG_SLOT.b, 0.72f * alpha, false);
    if(selected){
        // Выделение — не яркая подсветка, а чуть более светлое оливковое поле.
        drawUIRect(x, y, w, h, 0, 0.30f, 0.34f, 0.24f, 0.55f * alpha, false);
        uiThinFrame(x, y, w, h, UI_ACCENT, 0.85f * alpha);
    } else {
        uiThinFrame(x, y, w, h, UI_LINE_DIM, 0.75f * alpha);
    }
}

void uiBar(float x, float y, float w, float h, float frac, const UIColor& c, float alpha){
    if(frac < 0.0f) frac = 0.0f;
    if(frac > 1.0f) frac = 1.0f;
    drawUIRect(x, y, w, h, 0, 0.02f, 0.03f, 0.02f, 0.85f * alpha, false);
    drawUIRect(x + 1.0f, y + 1.0f, (w - 2.0f) * frac, h - 2.0f, 0, c.r, c.g, c.b, 0.85f * alpha, false);
    uiThinFrame(x, y, w, h, UI_LINE_DIM, 0.8f * alpha);
}

void uiButton(float x, float y, float w, float h, float alpha, bool pressed){
    float f = pressed ? 0.28f : 0.16f;
    drawUIRect(x, y, w, h, 0, 0.20f, 0.23f, 0.16f, f * alpha, false);
    uiScanlines(x, y, w, h, alpha * 0.7f);
    uiDoubleFrame(x, y, w, h, alpha);
}

void uiCondition(float x, float y, float w, float frac, float alpha){
    if(frac < 0.0f) frac = 0.0f;
    if(frac > 1.0f) frac = 1.0f;
    const int SEG = 12;
    float sw = (w - 2.0f) / (float)SEG;
    drawUIRect(x, y, w, 5.0f, 0, 0.02f, 0.03f, 0.02f, 0.8f * alpha, false);
    for(int i=0;i<SEG;i++){
        if((float)i / (float)SEG >= frac) break;
        drawUIRect(x + 1.0f + sw*(float)i, y + 1.0f, sw - 1.0f, 3.0f, 0,
                   UI_ACCENT.r, UI_ACCENT.g, UI_ACCENT.b, 0.9f * alpha, false);
    }
}
