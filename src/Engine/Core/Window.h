#pragma once
// ==================== ОКНО / ЭКРАН ====================
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

extern int SCR_W, SCR_H;
extern SDL_Window* win;
extern SDL_GLContext glCtx;
extern TTF_Font* uiFont;
