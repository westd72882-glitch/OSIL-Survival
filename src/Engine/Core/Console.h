#pragma once
// ==================== ОТЛАДОЧНАЯ КОНСОЛЬ ====================
// Перехватывает ВСЕ SDL_Log(...) во всём проекте (через SDL_LogSetOutputFunction) и
// складывает их в кольцевой буфер строк для отрисовки оверлеем поверх игры.
// Существующие SDL_Log(...) менять не нужно — они попадают сюда автоматически.
#include <string>
#include <vector>

void consoleInit();
std::vector<std::string> consoleGetLines(); // от старых к новым
int consoleMaxLines();
