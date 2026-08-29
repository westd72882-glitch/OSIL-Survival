#pragma once
// ==================== АУДИО ====================
// Заголовок SDL_mixer здесь НЕ подключается, хотя типы из него в объявлениях есть.
// Причина: Audio.h входит в зонтичный Engine.h, а тот подключают все подряд - и
// получалось, что для сборки чего угодно, даже редактора карт из mapper/, где звука нет
// вовсе, требовался SDL_mixer. Хватает предварительного объявления типов: разыменовывать
// их в заголовке некому, а Audio.cpp подключает настоящий SDL_mixer.h сам.
typedef struct _Mix_Music Mix_Music;
typedef struct Mix_Chunk Mix_Chunk;

extern Mix_Music* bgMusic;
extern Mix_Chunk* stepSound;
// Звук получения урона игроком (damageplayer.mp3 в корне проекта).
extern Mix_Chunk* damageSound;
// Сигнал начала игры — с него стартует вступительная катсцена (notification.mp3).
extern Mix_Chunk* notifySound;

bool audioInit();
void audioApplySettings();
void audioPlayStep();
// Проигрывает звук ранения. Зовётся, когда у игрока убавилось здоровье.
void audioPlayDamage();
// Сигнал-уведомление (начало катсцены, важное событие сюжета).
void audioPlayNotification();
// Длительность notification.mp3 в секундах (0, если файла нет) — вступление держится
// на экране не меньше, чем звучит сигнал.
float audioNotificationLength();
void audioShutdown();
