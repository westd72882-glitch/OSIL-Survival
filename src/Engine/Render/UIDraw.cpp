#include "UIDraw.h"
#include "../Core/Window.h"
#include "../../Core/Math.h"
#include <cmath>
#include <vector>

// ==================== UI КВАД ====================
GLuint uiVAO=0, uiVBO=0;
void initUIQuad(){
    glGenVertexArrays(1, &uiVAO);
    glGenBuffers(1, &uiVBO);
    glBindVertexArray(uiVAO);
    glBindBuffer(GL_ARRAY_BUFFER, uiVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float)*6*4, nullptr, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), (void*)(2*sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
}
void drawUIRectUV(float x, float y, float w, float h, GLuint tex,
                  float u0, float v0, float u1, float v1, float a){
    float verts[6][4] = {
        {x,   y,   u0,v0},
        {x+w, y,   u1,v0},
        {x+w, y+h, u1,v1},
        {x,   y,   u0,v0},
        {x+w, y+h, u1,v1},
        {x,   y+h, u0,v1},
    };
    glBindVertexArray(uiVAO);
    glBindBuffer(GL_ARRAY_BUFFER, uiVBO);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
    glUseProgram(uiProg);
    glUniform4f(uiColorLoc, 1,1,1,a);
    glUniform1i(uiUseTextureLoc, 1);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    glUniform1i(uiTexLoc, 0);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
}

void drawUIRectRotated(float cx, float cy, float w, float h, GLuint tex, float angle, float a){
    float ca = cosf(angle), sa = sinf(angle);
    float hw = w * 0.5f, hh = h * 0.5f;
    // Углы в своей системе, затем поворот вокруг центра.
    const float lx[4] = { -hw,  hw,  hw, -hw };
    const float ly[4] = { -hh, -hh,  hh,  hh };
    const float lu[4] = { 0.0f, 1.0f, 1.0f, 0.0f };
    const float lv[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
    float px[4], py[4];
    for(int i = 0; i < 4; ++i){
        px[i] = cx + lx[i] * ca - ly[i] * sa;
        py[i] = cy + lx[i] * sa + ly[i] * ca;
    }
    const int idx[6] = { 0, 1, 2, 0, 2, 3 };
    float verts[6][4];
    for(int i = 0; i < 6; ++i){
        int k = idx[i];
        verts[i][0] = px[k]; verts[i][1] = py[k];
        verts[i][2] = lu[k]; verts[i][3] = lv[k];
    }
    glBindVertexArray(uiVAO);
    glBindBuffer(GL_ARRAY_BUFFER, uiVBO);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
    glUseProgram(uiProg);
    glUniform4f(uiColorLoc, 1,1,1,a);
    glUniform1i(uiUseTextureLoc, 1);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    glUniform1i(uiTexLoc, 0);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
}

void drawUIRect(float x, float y, float w, float h, GLuint tex, float r, float g, float b, float a, bool useTexture){
    float verts[6][4] = {
        {x,   y,   0,0},
        {x+w, y,   1,0},
        {x+w, y+h, 1,1},
        {x,   y,   0,0},
        {x+w, y+h, 1,1},
        {x,   y+h, 0,1},
    };
    glBindVertexArray(uiVAO);
    glBindBuffer(GL_ARRAY_BUFFER, uiVBO);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
    glUseProgram(uiProg);
    glUniform4f(uiColorLoc, r,g,b,a);
    glUniform1i(uiUseTextureLoc, useTexture ? 1 : 0);
    if(useTexture){
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, tex);
        glUniform1i(uiTexLoc, 0);
    }
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
}

// ==================== UI КРУГ ====================
GLuint uiCircleVAO=0, uiCircleVBO=0;
const int UI_CIRCLE_SEGS = 32;
void initUICircle(){
    glGenVertexArrays(1, &uiCircleVAO);
    glGenBuffers(1, &uiCircleVBO);
    glBindVertexArray(uiCircleVAO);
    glBindBuffer(GL_ARRAY_BUFFER, uiCircleVBO);
    // Буфер общий для drawUICircle (fan: SEGS+2 вершин) и drawUICircleOutline (SEGS*6 вершин) —
    // выделяем под большую из двух схем использования
    glBufferData(GL_ARRAY_BUFFER, sizeof(float)*4*(UI_CIRCLE_SEGS*6), nullptr, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), (void*)(2*sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
}
// Рисует закрашенный круг (triangle fan) без текстуры — используется для круглого джойстика
void drawUICircle(float cx, float cy, float radius, float r, float g, float b, float a){
    float verts[UI_CIRCLE_SEGS+2][4];
    verts[0][0] = cx; verts[0][1] = cy; verts[0][2] = 0.5f; verts[0][3] = 0.5f;
    for(int i=0; i<=UI_CIRCLE_SEGS; i++){
        float t = (float)i / (float)UI_CIRCLE_SEGS * 2.0f * (float)M_PI;
        verts[i+1][0] = cx + cosf(t)*radius;
        verts[i+1][1] = cy + sinf(t)*radius;
        verts[i+1][2] = 0.0f; verts[i+1][3] = 0.0f;
    }
    glBindVertexArray(uiCircleVAO);
    glBindBuffer(GL_ARRAY_BUFFER, uiCircleVBO);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
    glUseProgram(uiProg);
    glUniform4f(uiColorLoc, r,g,b,a);
    glUniform1i(uiUseTextureLoc, 0);
    glDrawArrays(GL_TRIANGLE_FAN, 0, UI_CIRCLE_SEGS+2);
    glBindVertexArray(0);
}
// Рисует контур круга (кольцо линиями) — используется для обводки базы джойстика
void drawUICircleOutline(float cx, float cy, float radius, float r, float g, float b, float a, float thickness){
    // Реализовано как закрашенное кольцо: внешний круг цветом, внутренний — "вырезаем"
    // через второй проход поверх с прозрачным цветом фона недоступен без stencil,
    // поэтому рисуем набор коротких радиальных отрезков через тонкие треугольники.
    float verts[UI_CIRCLE_SEGS*6][4];
    int vi = 0;
    for(int i=0; i<UI_CIRCLE_SEGS; i++){
        float t0 = (float)i / (float)UI_CIRCLE_SEGS * 2.0f * (float)M_PI;
        float t1 = (float)(i+1) / (float)UI_CIRCLE_SEGS * 2.0f * (float)M_PI;
        float ox0 = cx + cosf(t0)*(radius), oy0 = cy + sinf(t0)*(radius);
        float ox1 = cx + cosf(t1)*(radius), oy1 = cy + sinf(t1)*(radius);
        float ix0 = cx + cosf(t0)*(radius-thickness), iy0 = cy + sinf(t0)*(radius-thickness);
        float ix1 = cx + cosf(t1)*(radius-thickness), iy1 = cy + sinf(t1)*(radius-thickness);
        verts[vi][0]=ox0; verts[vi][1]=oy0; verts[vi][2]=0; verts[vi][3]=0; vi++;
        verts[vi][0]=ox1; verts[vi][1]=oy1; verts[vi][2]=0; verts[vi][3]=0; vi++;
        verts[vi][0]=ix1; verts[vi][1]=iy1; verts[vi][2]=0; verts[vi][3]=0; vi++;
        verts[vi][0]=ox0; verts[vi][1]=oy0; verts[vi][2]=0; verts[vi][3]=0; vi++;
        verts[vi][0]=ix1; verts[vi][1]=iy1; verts[vi][2]=0; verts[vi][3]=0; vi++;
        verts[vi][0]=ix0; verts[vi][1]=iy0; verts[vi][2]=0; verts[vi][3]=0; vi++;
    }
    glBindVertexArray(uiCircleVAO);
    glBindBuffer(GL_ARRAY_BUFFER, uiCircleVBO);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
    glUseProgram(uiProg);
    glUniform4f(uiColorLoc, r,g,b,a);
    glUniform1i(uiUseTextureLoc, 0);
    glDrawArrays(GL_TRIANGLES, 0, vi);
    glBindVertexArray(0);
}

// ==================== ТЕКСТОВЫЙ КЭШ ====================
void updateTextTexture(TextTexCache& cache, const std::string& text, SDL_Color color){
    if(text == cache.lastText && cache.tex != 0) return;
    if(cache.tex){ glDeleteTextures(1, &cache.tex); cache.tex=0; }
    if(!uiFont || text.empty()) { cache.lastText = text; return; }
    SDL_Surface* surf = TTF_RenderUTF8_Blended(uiFont, text.c_str(), color);
    if(!surf) return;
    SDL_Surface* conv = SDL_ConvertSurfaceFormat(surf, SDL_PIXELFORMAT_ABGR8888, 0);
    SDL_FreeSurface(surf);
    if(!conv) return;
    glGenTextures(1, &cache.tex);
    glBindTexture(GL_TEXTURE_2D, cache.tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, conv->w, conv->h, 0, GL_RGBA, GL_UNSIGNED_BYTE, conv->pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    cache.w = conv->w; cache.h = conv->h;
    SDL_FreeSurface(conv);
    cache.lastText = text;
}
