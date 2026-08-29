#include <SDL2/SDL_mixer.h>   // настоящие типы и функции - только здесь, см. Audio.h
#include "Audio.h"
#include "Settings.h"
#include <SDL2/SDL.h>

// ==================== АУДИО ====================
Mix_Music* bgMusic = nullptr;
Mix_Chunk* stepSound = nullptr;
Mix_Chunk* damageSound = nullptr;
Mix_Chunk* notifySound = nullptr;

bool audioInit(){
    if(Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 1024) < 0){
        SDL_Log("Mix_OpenAudio failed: %s", Mix_GetError());
        return false;
    }
    Mix_AllocateChannels(8);

    // Музыка: первый найденный трек music.ogg / music.mp3
    const char* musicCandidates[] = { "music.ogg", "music.mp3" };
    for(const char* path : musicCandidates){
        bgMusic = Mix_LoadMUS(path);
        if(bgMusic) break;
    }
    if(!bgMusic) SDL_Log("No background music file found (music.ogg/.mp3)");

    // Звук шагов
    const char* stepCandidates[] = { "step.ogg", "step.wav", "footstep.ogg", "footstep.wav" };
    for(const char* path : stepCandidates){
        stepSound = Mix_LoadWAV(path);
        if(stepSound) break;
    }
    if(!stepSound) SDL_Log("No footstep sound file found (step.ogg/.wav)");

    // Звук ранения. Файл не обязателен: нет — значит урон будет только визуальным,
    // игра всё равно обязана запускаться без ассетов.
    const char* hurtCandidates[] = { "damageplayer.mp3", "damageplayer.ogg", "damageplayer.wav" };
    for(const char* path : hurtCandidates){
        damageSound = Mix_LoadWAV(path);
        if(damageSound) break;
    }
    if(!damageSound) SDL_Log("Звук ранения damageplayer.mp3 не найден - урон останется только визуальным");

    // Сигнал начала игры. Тоже необязателен — без него вступление просто идёт молча.
    const char* notifyCandidates[] = { "notification.mp3", "notification.ogg", "notification.wav" };
    for(const char* path : notifyCandidates){
        notifySound = Mix_LoadWAV(path);
        if(notifySound) break;
    }
    if(!notifySound) SDL_Log("Сигнал notification.mp3 не найден - вступление пойдёт без звука");

    return true;
}

void audioApplySettings(){
    // Музыка
    if(settings.musicOn){
        if(bgMusic && Mix_PlayingMusic() == 0){
            Mix_VolumeMusic(MIX_MAX_VOLUME * 0.55f);
            Mix_PlayMusic(bgMusic, -1); // зациклена
        } else if(bgMusic && Mix_PausedMusic()){
            Mix_ResumeMusic();
        }
    } else {
        if(Mix_PlayingMusic()) Mix_PauseMusic();
    }
}

void audioPlayStep(){
    if(!settings.sfxOn || !stepSound) return;
    // Проигрываем только если канал шагов сейчас свободен, чтобы не спамить звук
    if(Mix_Playing(0) == 0){
        Mix_VolumeChunk(stepSound, MIX_MAX_VOLUME * 0.6f);
        Mix_PlayChannel(0, stepSound, 0);
    }
}

void audioPlayDamage(){
    if(!settings.sfxOn || !damageSound) return;
    // Отдельный канал от шагов: ранение не должно глохнуть под топот.
    Mix_VolumeChunk(damageSound, MIX_MAX_VOLUME * 0.85f);
    Mix_PlayChannel(3, damageSound, 0);
}

void audioPlayNotification(){
    if(!settings.sfxOn || !notifySound) return;
    Mix_VolumeChunk(notifySound, MIX_MAX_VOLUME);
    Mix_PlayChannel(4, notifySound, 0);
}

float audioNotificationLength(){
    if(!notifySound) return 0.0f;
    // Длина куска в секундах: байт всего / (частота * каналы * байт на сэмпл).
    // Микшер открыт как 44100 Гц, стерео, 16 бит (см. audioInit) — 4 байта на кадр.
    int freq = 44100, channels = 2; Uint16 fmt = MIX_DEFAULT_FORMAT;
    Mix_QuerySpec(&freq, &fmt, &channels);
    int bytesPerSample = (int)((SDL_AUDIO_BITSIZE(fmt)) / 8);
    if(bytesPerSample <= 0) bytesPerSample = 2;
    float frameBytes = (float)(bytesPerSample * channels);
    if(frameBytes <= 0.0f || freq <= 0) return 0.0f;
    return (float)notifySound->alen / (frameBytes * (float)freq);
}

void audioShutdown(){
    if(bgMusic){ Mix_FreeMusic(bgMusic); bgMusic = nullptr; }
    if(stepSound){ Mix_FreeChunk(stepSound); stepSound = nullptr; }
    if(damageSound){ Mix_FreeChunk(damageSound); damageSound = nullptr; }
    if(notifySound){ Mix_FreeChunk(notifySound); notifySound = nullptr; }
    Mix_CloseAudio();
}

