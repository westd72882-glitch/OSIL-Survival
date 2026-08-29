#pragma once
// ==================== ГЕОМЕТРИЧЕСКИЕ ПРИМИТИВЫ ====================
// Строительные блоки для всех процедурных моделей проекта (оружие, персонажи,
// строения). Внешних .obj у игры нет, поэтому качество картинки упирается именно
// в эти генераторы.
#include "Vertex.h"
#include "Mesh.h"
#include "../../Core/Math.h"
#include <vector>

// Простой куб с нормалями и UV.
std::vector<Vertex> buildCubeVerts(float half);

// Прямоугольный блок с независимым half-extent по осям — добавляется в общий массив.
void appendBoxVerts(std::vector<Vertex>& out, Vec3 center, Vec3 halfSize);

// То же, но с произвольной трансформацией: для наклонных/повёрнутых деталей,
// которые осевым боксом не выразить.
void appendBoxXformVerts(std::vector<Vertex>& out, const Mat4& xform, Vec3 halfSize);

// Цилиндр вдоль ЛОКАЛЬНОЙ оси Z с крышками — чтобы стволы и трубки не выглядели
// гранёными брусками. segments=10..14 уже читается как круглое.
void appendCylinderVerts(std::vector<Vertex>& out, const Mat4& xform, float radius, float halfLen, int segments);

// ==================== ГЛАДКИЕ ФОРМЫ (сглаженные нормали) ====================
// Ключевое отличие от боксов: нормали здесь смотрят НАРУЖУ ПО РАДИУСУ, а не по граням,
// поэтому освещение размазывается по поверхности непрерывно и силуэт читается как
// органический объём, а не как гранёный многогранник. Именно на этих примитивах
// строятся персонажи — тело из боксов неизбежно выглядит "майнкрафтом" при любом
// количестве деталей.

// Сфера вокруг локального начала координат.
void appendSphereVerts(std::vector<Vertex>& out, const Mat4& xform, float radius, int segments, int rings);

// Эллипсоид — сфера с разными радиусами по осям (голова, грудная клетка, плечи).
void appendEllipsoidVerts(std::vector<Vertex>& out, const Mat4& xform, Vec3 radii, int segments, int rings);

// Капсула вдоль локальной оси Y (от -halfLen до +halfLen) с полусферами на торцах —
// базовая форма для конечностей: у неё нет ни острых торцов, ни видимых стыков.
void appendCapsuleVerts(std::vector<Vertex>& out, const Mat4& xform, float radius, float halfLen, int segments, int rings);

// Конический сегмент вдоль локальной оси Y с РАЗНЫМИ радиусами на концах: бедро толще
// колена, предплечье сужается к кисти. Нормали учитывают наклон образующей, поэтому
// сужение не даёт "ступеньки" в освещении.
void appendTaperedVerts(std::vector<Vertex>& out, const Mat4& xform,
                        float radiusBottom, float radiusTop, float halfLen, int segments, bool caps);

// Скайбокс-куб: рисуется изнутри, сэмплируется по позиции вершины.
struct SkyVertex { float px, py, pz; };
Mesh buildSkyCube(float half);
