#include "Console.h"
#include <SDL2/SDL.h>
#include <cstdio>

// ==================== ОТЛАДОЧНАЯ КОНСОЛЬ ====================
// Кольцевой буфер строк лога. Заполняется автоматически через кастомный
// SDL_LogOutputFunction — все существующие SDL_Log(...) во всём проекте
// начинают попадать сюда без каких-либо изменений в местах вызова.
static std::vector<std::string> g_consoleLines;
static const int CONSOLE_MAX_LINES = 200; // старые строки вытесняются новыми

static void consoleLogCallback(void* /*userdata*/, int /*category*/, SDL_LogPriority /*priority*/, const char* message){
    // Дублируем в стандартный вывод, чтобы поведение платформы (adb logcat / консоль
    // терминала) не изменилось — консоль в игре ДОПОЛНЯЕТ, а не заменяет обычный лог.
    fprintf(stderr, "%s\n", message);
    g_consoleLines.push_back(std::string(message));
    if((int)g_consoleLines.size() > CONSOLE_MAX_LINES){
        // Убираем самые старые строки пачкой, а не по одной, чтобы не дёргать erase()
        // на каждый лог-вызов при долгой игровой сессии.
        g_consoleLines.erase(g_consoleLines.begin(), g_consoleLines.begin() + (g_consoleLines.size() - CONSOLE_MAX_LINES));
    }
}

void consoleInit(){
    g_consoleLines.reserve(CONSOLE_MAX_LINES);
    SDL_LogSetOutputFunction(consoleLogCallback, nullptr);
    SDL_LogSetPriority(SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_VERBOSE);
    SDL_Log("Console initialized");
}

std::vector<std::string> consoleGetLines(){
    return g_consoleLines; // копия — безопасно для рендера в другом месте кадра
}

int consoleMaxLines(){
    return CONSOLE_MAX_LINES;
}

