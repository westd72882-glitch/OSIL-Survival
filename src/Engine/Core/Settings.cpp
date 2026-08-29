#include "Settings.h"
#include "Paths.h"

#include <SDL2/SDL.h>
#include <fstream>
#include <sstream>

Settings settings;

float qualityRenderScale(){
    switch(settings.quality){
        case Quality::TURBO:  return 0.42f;  // площадь кадра меньше в 5.7 раза
        case Quality::LOW:    return 0.55f;
        case Quality::MEDIUM: return 0.75f;
        default:              return 1.0f;
    }
}

int qualityTextureLimit(){
    // Основной расход памяти и пропускной способности мобильного GPU — текстуры.
    switch(settings.quality){
        case Quality::TURBO:  return 256;
        case Quality::LOW:    return 512;
        default:              return 0;
    }
}

float qualityViewDistanceScale(){
    switch(settings.quality){
        case Quality::TURBO:  return 0.55f;
        case Quality::LOW:    return 0.75f;
        case Quality::MEDIUM: return 0.90f;
        default:              return 1.0f;
    }
}

const char* qualityLabel(){
    switch(settings.quality){
        case Quality::TURBO:  return "Турбо";
        case Quality::LOW:    return "Низкое";
        case Quality::MEDIUM: return "Среднее";
        default:              return "Высокое";
    }
}

void loadSettings(){
    std::ifstream f(SETTINGS_PATH_STR);
    if(!f.is_open()) return;
    std::string line;
    while(std::getline(f, line)){
        std::istringstream iss(line);
        std::string key;
        iss >> key;
        if(key == "music"){ int v; iss >> v; settings.musicOn = (v != 0); }
        else if(key == "sfx"){ int v; iss >> v; settings.sfxOn = (v != 0); }
        else if(key == "fps_limit"){ iss >> settings.fpsLimit; }
        else if(key == "quality"){ int v; iss >> v; if(v < 0) v = 0; if(v > 3) v = 3; settings.quality = (Quality)v; }
        else if(key == "debug_info"){ int v; iss >> v; settings.showDebugInfo = (v != 0); }
        else if(key == "look_sens"){ iss >> settings.lookSensitivity; }
        else if(key == "layout_version"){ iss >> settings.layoutVersion; }
        else if(key == "stick"){ iss >> settings.stickNormX >> settings.stickNormY; }
        else if(key == "jump"){ iss >> settings.jumpNormX >> settings.jumpNormY; }
        else if(key == "sprint"){ iss >> settings.sprintNormX >> settings.sprintNormY; }
        else if(key == "crouch"){ iss >> settings.crouchNormX >> settings.crouchNormY; }
        else if(key == "action"){ iss >> settings.actionNormX >> settings.actionNormY; }
        else if(key == "attack"){ iss >> settings.attackNormX >> settings.attackNormY; }
        else if(key == "inventory"){ iss >> settings.invNormX >> settings.invNormY; }
        else if(key == "craft"){ iss >> settings.craftNormX >> settings.craftNormY; }
        else if(key == "map"){ iss >> settings.mapNormX >> settings.mapNormY; }
    }

    // Сохранённая раскладка от прошлой версии интерфейса сбрасывается один раз —
    // иначе новая стандартная раскладка не включилась бы до захода в редактор управления.
    // Личные настройки (звук, качество, чувствительность) при этом не трогаются.
    if(settings.layoutVersion != CONTROL_LAYOUT_VERSION){
        settings.stickNormY = settings.jumpNormY = settings.sprintNormY = -1.0f;
        settings.crouchNormY = settings.actionNormY = settings.attackNormY = -1.0f;
        settings.invNormY = settings.craftNormY = settings.mapNormY = -1.0f;
        settings.layoutVersion = CONTROL_LAYOUT_VERSION;
    }
}

void saveSettings(){
    if(SETTINGS_PATH_STR.empty()){
        SDL_Log("saveSettings: пути не инициализированы (initWritablePaths не вызван)");
        return;
    }
    std::ofstream f(SETTINGS_PATH_STR, std::ios::trunc);
    if(!f.is_open()){ SDL_Log("Не удалось записать настройки в %s", SETTINGS_PATH_STR.c_str()); return; }
    f << "music " << (settings.musicOn ? 1 : 0) << "\n";
    f << "sfx " << (settings.sfxOn ? 1 : 0) << "\n";
    f << "fps_limit " << settings.fpsLimit << "\n";
    f << "quality " << (int)settings.quality << "\n";
    f << "debug_info " << (settings.showDebugInfo ? 1 : 0) << "\n";
    f << "look_sens " << settings.lookSensitivity << "\n";
    f << "layout_version " << CONTROL_LAYOUT_VERSION << "\n";
    f << "stick "     << settings.stickNormX  << " " << settings.stickNormY  << "\n";
    f << "jump "      << settings.jumpNormX   << " " << settings.jumpNormY   << "\n";
    f << "sprint "    << settings.sprintNormX << " " << settings.sprintNormY << "\n";
    f << "crouch "    << settings.crouchNormX << " " << settings.crouchNormY << "\n";
    f << "action "    << settings.actionNormX << " " << settings.actionNormY << "\n";
    f << "attack "    << settings.attackNormX << " " << settings.attackNormY << "\n";
    f << "inventory " << settings.invNormX    << " " << settings.invNormY    << "\n";
    f << "craft "     << settings.craftNormX  << " " << settings.craftNormY  << "\n";
    f << "map "       << settings.mapNormX    << " " << settings.mapNormY    << "\n";
    f.close();
}
