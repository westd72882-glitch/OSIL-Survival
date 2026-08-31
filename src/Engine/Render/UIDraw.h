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
// То же, но с явным куском текстуры. Нужен карте с приближением: она показывает не всю
// картинку мира, а её часть, и растягивать этот кусок надо самой выборкой, а не
// геометрией — иначе при зуме карта уезжает за края своей рамки.
void drawUIRectUV(float x, float y, float w, float h, GLuint tex,
                  float u0, float v0, float u1, float v1, float a);

// Повёрнутая картинка вокруг своего центра. Нужна метке игрока на карте: она уже
// нарисована «носом вверх», и показать курс — значит просто повернуть её.
void drawUIRectRotated(float cx, float cy, float w, float h, GLuint tex, float angle, float a);

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
