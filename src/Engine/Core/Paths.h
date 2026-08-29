#pragma once
// ==================== ПИСАБЕЛЬНЫЕ ПУТИ ====================
// Голое относительное имя файла на Android чаще всего указывает в непишущуюся
// директорию — запись тихо проваливается. SDL_GetPrefPath() даёт гарантированно
// писабельный путь внутри данных приложения на любой платформе.
#include <string>

extern std::string g_writableDir;
extern std::string SETTINGS_PATH_STR, SAVE_PATH_STR;

void initWritablePaths();
