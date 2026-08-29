#pragma once
// ==================== 2D UI: примитивы и текстовый кэш ====================
#include "GL.h"
#include "Shaders.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <string>

extern GLuint uiVAO, uiVBO;
void initUIQuad();
void drawUIRect(float x, float y, float w, float h, GLuint tex, float r, float g, float b, float a, bool useTexture);

extern GLuint uiCircleVAO, uiCircleVBO;
extern const int UI_CIRCLE_SEGS;
void initUICircle();
void drawUICircle(float cx, float cy, float radius, float r, float g, float b, float a);
void drawUICircleOutline(float cx, float cy, float radius, float r, float g, float b, float a, float thickness);

// Текстура текста пересоздаётся только когда строка изменилась.
struct TextTexCache {
    GLuint tex=0; int w=0,h=0; std::string lastText;
};
void updateTextTexture(TextTexCache& cache, const std::string& text, SDL_Color color);

// Простая прямоугольная кнопка с подписью для меню/настроек.
struct MenuButton {
    float x, y, w, h;
    std::string label;
};
