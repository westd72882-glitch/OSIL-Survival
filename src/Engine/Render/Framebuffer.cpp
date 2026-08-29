#include "Framebuffer.h"
#include <SDL2/SDL.h>

// ==================== FBO ДЛЯ МАСШТАБА РЕНДЕРА (Качество графики) ====================
// 3D-сцена рисуется в этот буфер (размер = разрешение экрана * qualityRenderScale()),
// затем растягивается на весь экран одним текстурированным квадом. UI рисуется поверх
// уже в полном разрешении экрана, так что текст/кнопки остаются чёткими на любом качестве.
GLuint sceneFBO = 0, sceneColorTex = 0, sceneDepthRBO = 0;
int sceneFboW = 0, sceneFboH = 0;

void destroySceneFBO(){
    if(sceneFBO){ glDeleteFramebuffers(1, &sceneFBO); sceneFBO = 0; }
    if(sceneColorTex){ glDeleteTextures(1, &sceneColorTex); sceneColorTex = 0; }
    if(sceneDepthRBO){ glDeleteRenderbuffers(1, &sceneDepthRBO); sceneDepthRBO = 0; }
}
void ensureSceneFBO(int w, int h){
    if(sceneFBO != 0 && sceneFboW == w && sceneFboH == h) return; // уже подходящего размера
    destroySceneFBO();
    sceneFboW = w; sceneFboH = h;
    glGenTextures(1, &sceneColorTex);
    glBindTexture(GL_TEXTURE_2D, sceneColorTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenRenderbuffers(1, &sceneDepthRBO);
    glBindRenderbuffer(GL_RENDERBUFFER, sceneDepthRBO);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);

    glGenFramebuffers(1, &sceneFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, sceneFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, sceneColorTex, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, sceneDepthRBO);
    if(glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE){
        SDL_Log("Scene FBO incomplete, falling back to full quality");
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

