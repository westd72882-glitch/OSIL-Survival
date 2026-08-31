#include "Paths.h"
#include <SDL2/SDL.h>
#include <fstream>
#include <cstdio>

// Путь к файлам настроек/сохранения. Раньше использовалось голое относительное имя
// файла ("settings.cfg"), что на Android чаще всего указывает в непишущуюся директорию —
// запись тихо проваливалась (отсюда "настройки/сохранение не работают"). SDL_GetPrefPath()
// даёт гарантированно писабельный путь внутри данных приложения на любой платформе.
std::string g_writableDir = "./";
std::string SETTINGS_PATH_STR, SAVE_PATH_STR;

void initWritablePaths(){
    char* pref = SDL_GetPrefPath("osil", "OSILSurvival");
    if(pref){
        g_writableDir = pref;
        SDL_free(pref);
    } else {
        SDL_Log("SDL_GetPrefPath failed (%s), falling back to './'", SDL_GetError());
    }
    SETTINGS_PATH_STR = g_writableDir + "settings.cfg";
    SAVE_PATH_STR = g_writableDir + "savegame.dat";
    SDL_Log("Writable dir resolved to: %s", g_writableDir.c_str());

    // Проверяем, что путь реально доступен для записи прямо сейчас — если по какой-то
    // причине (права доступа, отсутствие директории) запись невозможна, лучше узнать об
    // этом сразу в логах при старте, а не потом молча терять прогресс игрока.
    std::ofstream probe(g_writableDir + ".write_test", std::ios::trunc);
    if(!probe.is_open()){
        SDL_Log("WARNING: writable dir '%s' is NOT writable - settings/save will fail!", g_writableDir.c_str());
    } else {
        probe.close();
        std::remove((g_writableDir + ".write_test").c_str());
    }
}


std::string assetPath(const char* name){
    if(!name || !*name) return std::string();
    const char* prefixes[] = { "", "assets/", "../assets/", "../../assets/" };
    for(const char* prefix : prefixes){
        std::string candidate = std::string(prefix) + name;
        SDL_RWops* rw = SDL_RWFromFile(candidate.c_str(), "rb");
        if(rw){
            SDL_RWclose(rw);
            return candidate;
        }
    }
    return std::string(name);   // пусть вызывающий сам сообщит, что файла нет
}
