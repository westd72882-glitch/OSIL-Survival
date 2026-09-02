#pragma once
// ==================== ЭТАЛОННАЯ РАЗМЕТКА ИНТЕРФЕЙСА ====================
// Весь интерфейс размечен в одном эталонном разрешении 1536x689 и масштабируется
// ЦЕЛИКОМ одним множителем scale = ширина экрана / 1536. Никакой «резиновой» вёрстки:
// позиции, размеры, шрифты, толщины линий и отступы едут одним коэффициентом, поэтому
// на любом экране интерфейс выглядит одной и той же картинкой, а не пересобирается.
//
// Единственная поправка — привязка к краю. Эталон снят с телефона (2.23:1), а на другом
// соотношении сторон высота в масштабе по ширине не сходится с высотой экрана. Поэтому
// элементы, стоящие у нижнего или правого края, отсчитываются от СВОЕГО края: иначе
// пояс и кнопки уезжали бы в середину экрана. Внутри группы взаимное расположение при
// этом сохраняется точно.
#include "../Core/Math.h"

struct UiRef {
    static constexpr float REF_W = 1536.0f;
    static constexpr float REF_H = 689.0f;

    float scale = 1.0f;
    float screenW = REF_W, screenH = REF_H;

    UiRef(int w, int h){
        screenW = (float)w;
        screenH = (float)h;
        scale = screenW / REF_W;
    }

    // Размер (длина, толщина, кегль) из эталона в пиксели экрана.
    float s(float refSize) const { return refSize * scale; }
    // Координата от левого и от верхнего края.
    float x(float refX) const { return refX * scale; }
    float y(float refY) const { return refY * scale; }
    // Координата с привязкой к правому и к нижнему краю эталона.
    float xr(float refX) const { return screenW - (REF_W - refX) * scale; }
    float yb(float refY) const { return screenH - (REF_H - refY) * scale; }
    // Центр экрана по горизонтали — прицел и компас стоят по центру, а не по эталону:
    // при другом соотношении сторон «центр эталона» центром экрана быть перестаёт.
    float cx() const { return screenW * 0.5f; }
    float cy() const { return screenH * 0.5f; }
};

// ---- Цвета из эталона. Держим их здесь, а не в общей палитре интерфейса: это
// конкретные цвета конкретного экрана, и подмешивать их в стиль движка незачем.
namespace UiRefColors {
// Полосы состояния
constexpr float HP[3]       = { 0.718f, 0.216f, 0.196f };   // #B73732
constexpr float HUNGER[3]   = { 0.604f, 0.412f, 0.118f };   // #9A691E
constexpr float THIRST[3]   = { 0.176f, 0.427f, 0.639f };   // #2D6DA3
constexpr float STRENGTH[3] = { 0.224f, 0.486f, 0.208f };   // #397C35
constexpr float FPS[3]      = { 0.553f, 0.839f, 0.247f };   // #8DD63F
constexpr float SELECTED[3] = { 0.655f, 0.851f, 0.357f };   // #A7D95B
} // namespace UiRefColors
