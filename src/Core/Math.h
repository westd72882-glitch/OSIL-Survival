#pragma once
// ==================== МАТЕМАТИКА ДВИЖКА ====================
// Источник: движок A.N.O.D.E (src/Engine/Core/Math.h), перенесён в OSIL-Survival как есть.
// Расширения для выживания — в конце файла, после отбивки «ДОБАВЛЕНО В OSIL-SURVIVAL».
// Векторы, матрицы 4x4 и всё, что нужно для камеры/трансформов. Ничего не знает
// ни про GL, ни про игру — чистая математика.

struct Vec3 { float x=0, y=0, z=0; };
struct Mat4 { float m[16]; };

Vec3 v3sub(Vec3 a, Vec3 b);
Vec3 v3add(Vec3 a, Vec3 b);
Vec3 v3scale(Vec3 a, float s);
Vec3 v3cross(Vec3 a, Vec3 b);
float v3len(Vec3 a);
Vec3 v3norm(Vec3 a);

Mat4 mat4Identity();
Mat4 mat4Multiply(const Mat4& a, const Mat4& b);
Mat4 mat4Perspective(float fovYRad, float aspect, float nearZ, float farZ);
Mat4 mat4LookAt(Vec3 eye, Vec3 center, Vec3 up);
Mat4 mat4Translate(Vec3 t);
Mat4 mat4Scale(float s);
Mat4 mat4ScaleXYZ(float sx, float sy, float sz);
Mat4 mat4RotateY(float rad);
Mat4 mat4RotateX(float rad);
// Поворот вокруг Z — нужен скелетной анимации персонажей (разведение конечностей
// в стороны и крен корпуса задаются вокруг продольной оси).
Mat4 mat4RotateZ(float rad);
Mat4 mat4Ortho(float l,float rr,float b,float t,float n,float f);
void mat4ToMat3(const Mat4& m, float out[9]);

// ==================== КВАТЕРНИОНЫ ====================
// Нужны для скелетной анимации из glTF: там повороты костей хранятся именно
// кватернионами, и интерполировать их между ключевыми кадрами надо сферически (slerp),
// иначе на больших углах конечности "проседают" и дёргаются.
struct Quat { float x=0, y=0, z=0, w=1; };

Quat quatIdentity();
Quat quatNormalize(Quat q);
Quat quatMultiply(Quat a, Quat b);
// Сферическая интерполяция: t=0 -> a, t=1 -> b. Автоматически выбирает кратчайшую дугу.
Quat quatSlerp(Quat a, Quat b, float t);
Mat4 quatToMat4(Quat q);

// Собирает матрицу из «перенос * поворот * масштаб» — именно в таком порядке узлы
// задаются в glTF.
Mat4 mat4Compose(Vec3 translation, Quat rotation, Vec3 scale);
// Обращение матрицы, содержащей только поворот/масштаб/перенос (без проекции).
// Нужно для обратных матриц привязки (inverse bind) при скиннинге.
Mat4 mat4InverseAffine(const Mat4& m);

// ==================== ДОБАВЛЕНО В OSIL-SURVIVAL ====================
// Всё выше перенесено из движка A.N.O.D.E (src/Engine/Core/Math.*) без изменений.
// Ниже — то, что нужно именно серверу выживания: 2D-математика для работы с картой
// (высота/биом — функции от XZ), а также зажимы и интерполяции, которыми пользуются
// генератор мира и симуляция.

struct Vec2 { float x = 0, y = 0; };

Vec2 v2add(Vec2 a, Vec2 b);
Vec2 v2sub(Vec2 a, Vec2 b);
Vec2 v2scale(Vec2 a, float s);
float v2len(Vec2 a);
float v2dist(Vec2 a, Vec2 b);
Vec2 v2norm(Vec2 a);

float clampf(float v, float lo, float hi);
int   clampi(int v, int lo, int hi);
float lerpf(float a, float b, float t);
// Обратная линейная интерполяция: где значение v лежит между a и b (0..1, с зажимом).
float invLerpf(float a, float b, float v);
// Плавная ступенька Хермита — сглаживает резкие границы биомов и переходы погоды.
float smoothstepf(float edge0, float edge1, float v);
// Расстояние между точками мира по горизонтали (высота игнорируется): почти все
// игровые проверки радиуса (привилегия стройки, слышимость, агро NPC) — плоские.
float horizontalDist(Vec3 a, Vec3 b);
