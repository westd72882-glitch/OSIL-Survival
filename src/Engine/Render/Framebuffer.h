#pragma once
// ==================== FBO ДЛЯ МАСШТАБА РЕНДЕРА (качество графики) ====================
// Сцена рисуется в offscreen-буфер пониженного разрешения и растягивается на экран,
// UI при этом остаётся в нативном разрешении.
#include "GL.h"

extern GLuint sceneFBO, sceneColorTex, sceneDepthRBO;
extern int sceneFboW, sceneFboH;

void destroySceneFBO();
void ensureSceneFBO(int w, int h);
