#pragma once
// ==================== ЕДИНЫЙ СТИЛЬ ИНТЕРФЕЙСА В ДУХЕ S.T.A.L.K.E.R.: ТЧ ====================
// Интерфейс оригинала 2007 года устроен принципиально не так, как современный «чистый» UI,
// и разница не в одном лишь цвете:
//   * фон — почти чёрный графит, панели полупрозрачные, а не залитые сплошняком;
//   * рамки ТОНКИЕ И ДВОЙНЫЕ: линия в один пиксель, зазор, вторая линия — этот приём
//     даёт то самое ощущение старого технического интерфейса лучше любой текстуры;
//   * контраст низкий, ярких цветов нет вообще: текст серо-зелёный, активное приглушённо
//     зелёное, деньги и важные числа жёлто-золотые, опасное — тускло-красное;
//   * углы прямые, градиентов и бликов нет, отступы маленькие, всё плотно;
//   * поверх панелей идёт еле заметная горизонтальная штриховка — «потёртость» экрана.
// Всё это собрано здесь, чтобы инвентарь, торговля, HUD и меню выглядели одним целым, а
// не тремя разными интерфейсами, и чтобы палитра правилась в одном месте.
#include "GL.h"

// ---- Палитра ----
struct UIColor { float r, g, b; };

extern const UIColor UI_BG_DEEP;      // графит: фон полноэкранных панелей
extern const UIColor UI_BG_PANEL;     // чуть светлее: поле секции
extern const UIColor UI_BG_SLOT;      // дно ячейки
extern const UIColor UI_LINE;         // основная линия рамки, серо-оливковая
extern const UIColor UI_LINE_DIM;     // вторая (внутренняя) линия двойной рамки
extern const UIColor UI_TEXT;         // основной текст — серо-белый с зеленцой
extern const UIColor UI_TEXT_DIM;     // подписи, неактивное
extern const UIColor UI_ACCENT;       // активное, выбранное — приглушённый зелёный
extern const UIColor UI_GOLD;         // деньги и важные значения
extern const UIColor UI_DANGER;       // враг, критическое состояние

// Те же цвета для updateTextTexture (SDL_Color) — без включения SDL в каждый файл.
void uiTextColor(const UIColor& c, unsigned char out[4], float alpha = 1.0f);

// ---- Примитивы ----
// Двойная рамка: внешняя линия, зазор в 2 px, внутренняя потусклее. Основной элемент
// стиля — им обведено всё, от ячейки инвентаря до окна торговли.
void uiDoubleFrame(float x, float y, float w, float h, float alpha);
// Одинарная тонкая рамка — для мелких ячеек, где двойная превратилась бы в кашу.
void uiThinFrame(float x, float y, float w, float h, const UIColor& c, float alpha);
// Панель: полупрозрачная заливка + двойная рамка + штриховка «старого экрана».
void uiPanel(float x, float y, float w, float h, float alpha, float fill = 0.82f);
// Заголовок секции: тёмная полоса поверх верхнего края панели (текст пишет вызывающий).
void uiSectionBar(float x, float y, float w, float alpha);
// Ячейка предмета: дно, тонкая рамка, при выделении — приглушённая оливковая подсветка.
void uiSlot(float x, float y, float w, float h, float alpha, bool selected);
// Горизонтальная шкала (вес, состояние): дно, тонкая рамка, заполнение долей frac.
void uiBar(float x, float y, float w, float h, float frac, const UIColor& c, float alpha);
// Плоская «старая» кнопка: заливка, двойная рамка, без скруглений и теней.
void uiButton(float x, float y, float w, float h, float alpha, bool pressed);
// Сегментированный индикатор состояния предмета — короткие зелёные штрихи, как в
// оригинале под слотами экипировки.
void uiCondition(float x, float y, float w, float frac, float alpha);
// Еле заметные горизонтальные линии поверх области — «потёртость» старого экрана.
void uiScanlines(float x, float y, float w, float h, float alpha);
