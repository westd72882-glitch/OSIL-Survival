// ==================== ТЕСТЫ НАБОРА КАРТИНОК ====================
// Игра запускается и без ассетов (тогда блоки рисуются цветом, а кнопки — кругами с
// подписью), но если файл из набора пропал или битый, узнать об этом надо на сборке, а
// не на телефоне: APK кладёт ту же папку assets/, что и настольная сборка.
#include "TestHarness.h"

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace {

std::string assetFile(const char* name){
    return std::string(OSIL_REPO_DIR) + "/assets/" + name;
}

// Читаем заголовок PNG вручную: подключать SDL_image ради проверки файла ни к чему.
bool readPngSize(const std::string& path, int& w, int& h){
    std::ifstream f(path, std::ios::binary);
    if(!f) return false;
    unsigned char head[33] = {0};
    f.read((char*)head, 33);
    if(f.gcount() < 33) return false;
    static const unsigned char sig[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
    for(int i = 0; i < 8; ++i) if(head[i] != sig[i]) return false;
    // 8..15 — длина и тип первого чанка, он обязан быть IHDR
    if(head[12] != 'I' || head[13] != 'H' || head[14] != 'D' || head[15] != 'R') return false;
    w = (head[16] << 24) | (head[17] << 16) | (head[18] << 8) | head[19];
    h = (head[20] << 24) | (head[21] << 16) | (head[22] << 8) | head[23];
    return w > 0 && h > 0;
}

// Слои снега, заснеженной листвы и воды строятся на месте, своих файлов у них нет —
// см. BlockTextures.cpp.
const char* BLOCK_TEXTURES[] = {
    "block_ground.png", "block_stone.png", "block_wood.png", "block_planks.png",
    "block_leaves.png", "block_dirt.png", "block_sulfur.png", "block_metal.png",
    "block_road.png"
};

const char* UI_TEXTURES[] = {
    "ui_dig.png", "ui_place.png", "ui_interact.png", "ui_inventory.png",
    "ui_craft.png", "ui_map.png", "ui_settings.png", "ui_close.png",
    "ui_jump.png", "ui_run.png", "ui_crouch.png",
    "item_torch.png", "item_axe.png", "item_scrap.png", "item_wood.png",
    "item_stone.png", "item_iron.png",
    "ui_joystick_base.png", "ui_joystick_stick.png", "ui_player_marker.png",
    "menu_bg.png"
};

} // namespace

TEST(текстуры_блоков_на_месте_и_квадратные){
    for(const char* name : BLOCK_TEXTURES){
        int w = 0, h = 0;
        bool ok = readPngSize(assetFile(name), w, h);
        if(!ok) printf("      нет файла или он не PNG: assets/%s\n", name);
        CHECK(ok);
        // Слои массива текстур обязаны совпадать по размеру и быть степенью двойки:
        // иначе не собрать GL_TEXTURE_2D_ARRAY и не построить мип-уровни.
        CHECK(w == h);
        CHECK((w & (w - 1)) == 0);
    }
}

TEST(иконки_интерфейса_на_месте){
    for(const char* name : UI_TEXTURES){
        int w = 0, h = 0;
        bool ok = readPngSize(assetFile(name), w, h);
        if(!ok) printf("      нет файла или он не PNG: assets/%s\n", name);
        CHECK(ok);
    }
}

TEST(картинки_не_раздули_репозиторий){
    // Ассеты едут внутрь APK целиком, поэтому за общим весом следим тестом: телефонная
    // сборка не должна незаметно вырасти на десятки мегабайт.
    size_t total = 0;
    for(const char* name : BLOCK_TEXTURES){
        std::ifstream f(assetFile(name), std::ios::binary | std::ios::ate);
        if(f) total += (size_t)f.tellg();
    }
    for(const char* name : UI_TEXTURES){
        std::ifstream f(assetFile(name), std::ios::binary | std::ios::ate);
        if(f) total += (size_t)f.tellg();
    }
    CHECK(total > 0);
    CHECK(total < 8u * 1024u * 1024u);
}
