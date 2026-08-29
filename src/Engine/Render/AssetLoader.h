#pragma once
// ==================== ЗАГРУЗКА РЕСУРСОВ (OBJ / текстуры) ====================
#include "GL.h"
#include "Vertex.h"
#include <vector>

// Простой парсер Wavefront OBJ: v, vt, vn, f (треугольники и квады, триангуляция веером).
bool loadOBJ(const char* path, std::vector<Vertex>& outVerts);
// outW/outH — размер картинки в пикселях. Нужен там, где текстуру нельзя растягивать по
// месту: иконка оружия в инвентаре вписывается в ячейку С СОХРАНЕНИЕМ ПРОПОРЦИЙ, иначе
// длинный автомат раздувается в квадрат и выглядит игрушечным.
GLuint loadTextureFromFile(const char* path, int* outW = nullptr, int* outH = nullptr);
