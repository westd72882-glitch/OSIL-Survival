#include "Primitives.h"
#include <cmath>

// ---- Простой куб с нормалями и UV — используется для интерактивного подбираемого предмета ----
std::vector<Vertex> buildCubeVerts(float half){
    std::vector<Vertex> v;
    auto face = [&](Vec3 n, Vec3 u, Vec3 vv){
        // 4 угла грани: смещаемся на half вдоль нормали (выносим грань на поверхность
        // куба), затем добавляем u/v направления для развёртки квадрата в этой плоскости.
        // Раньше здесь не было смещения вдоль n — все 6 граней проходили через центр
        // (0,0,0) и пересекались друг с другом, вместо того чтобы образовать закрытую
        // коробку. Именно это давало на экране "звезду из пересекающихся плоскостей"
        // вместо куба на ЛЮБОМ объекте-заглушке (интерактивный куб/дерево/монета/оружие).
        Vec3 base = v3scale(n, half);
        Vec3 c0 = v3add(base, v3add(v3scale(u,-half), v3scale(vv,-half)));
        Vec3 c1 = v3add(base, v3add(v3scale(u, half), v3scale(vv,-half)));
        Vec3 c2 = v3add(base, v3add(v3scale(u, half), v3scale(vv, half)));
        Vec3 c3 = v3add(base, v3add(v3scale(u,-half), v3scale(vv, half)));
        Vertex a{c0.x,c0.y,c0.z, 0,0, n.x,n.y,n.z};
        Vertex b{c1.x,c1.y,c1.z, 1,0, n.x,n.y,n.z};
        Vertex cc{c2.x,c2.y,c2.z, 1,1, n.x,n.y,n.z};
        Vertex d{c3.x,c3.y,c3.z, 0,1, n.x,n.y,n.z};
        v.push_back(a); v.push_back(b); v.push_back(cc);
        v.push_back(a); v.push_back(cc); v.push_back(d);
    };
    face({0,0, 1}, {1,0,0}, {0,1,0}); // +Z
    face({0,0,-1}, {-1,0,0}, {0,1,0}); // -Z
    face({1,0,0}, {0,0,-1}, {0,1,0}); // +X
    face({-1,0,0}, {0,0,1}, {0,1,0}); // -X
    face({0,1,0}, {1,0,0}, {0,0,-1}); // +Y
    face({0,-1,0}, {1,0,0}, {0,0,1}); // -Y
    return v;
}

// ---- Low-poly заглушка АК-47: несколько прямоугольных блоков вместо одного куба, чтобы
// силуэт читался как автомат, а не как случайный параллелепипед. Ось Z: дуло в -Z (вперёд
// от игрока), Y — вверх, X — вбок. Размеры подобраны в примерных пропорциях настоящего
// АК-47 (общая длина ~0.87, тут в игровых unit'ах близко к существующему handScale). ----
void appendBoxVerts(std::vector<Vertex>& out, Vec3 center, Vec3 halfSize){
    // Явные 8 углов бокса с независимыми half-extent по каждой оси — проще и понятнее,
    // чем переиспользовать face-лямбду buildCubeVerts (та рассчитана на равный half).
    float hx=halfSize.x, hy=halfSize.y, hz=halfSize.z;
    Vec3 c = center;
    Vec3 p[8] = {
        {c.x-hx,c.y-hy,c.z-hz}, {c.x+hx,c.y-hy,c.z-hz}, {c.x+hx,c.y+hy,c.z-hz}, {c.x-hx,c.y+hy,c.z-hz},
        {c.x-hx,c.y-hy,c.z+hz}, {c.x+hx,c.y-hy,c.z+hz}, {c.x+hx,c.y+hy,c.z+hz}, {c.x-hx,c.y+hy,c.z+hz},
    };
    auto quad = [&](int i0,int i1,int i2,int i3, Vec3 n){
        Vertex a{p[i0].x,p[i0].y,p[i0].z, 0,0, n.x,n.y,n.z};
        Vertex b{p[i1].x,p[i1].y,p[i1].z, 1,0, n.x,n.y,n.z};
        Vertex cc{p[i2].x,p[i2].y,p[i2].z, 1,1, n.x,n.y,n.z};
        Vertex d{p[i3].x,p[i3].y,p[i3].z, 0,1, n.x,n.y,n.z};
        out.push_back(a); out.push_back(b); out.push_back(cc);
        out.push_back(a); out.push_back(cc); out.push_back(d);
    };
    quad(0,1,2,3, {0,0,-1});  // -Z (задняя грань блока)
    quad(5,4,7,6, {0,0, 1});  // +Z (передняя грань блока)
    quad(4,0,3,7, {-1,0,0});  // -X
    quad(1,5,6,2, { 1,0,0});  // +X
    quad(3,2,6,7, {0, 1,0});  // +Y (верх)
    quad(4,5,1,0, {0,-1,0});  // -Y (низ)
}

// ---- Трансформированные примитивы (см. Render.h): бокс с произвольной матрицей и
// цилиндр вдоль локальной оси Z. Позиции гоняются через полную матрицу, нормали — через
// её 3x3-часть (модели строятся без неоднородного масштаба, поэтому обычной 3x3 хватает,
// инверсия-транспонирование не нужна). ----
static Vec3 xformPoint(const Mat4& m, Vec3 p){
    return {
        m.m[0]*p.x + m.m[4]*p.y + m.m[8]*p.z  + m.m[12],
        m.m[1]*p.x + m.m[5]*p.y + m.m[9]*p.z  + m.m[13],
        m.m[2]*p.x + m.m[6]*p.y + m.m[10]*p.z + m.m[14]
    };
}
static Vec3 xformDir(const Mat4& m, Vec3 d){
    return v3norm({
        m.m[0]*d.x + m.m[4]*d.y + m.m[8]*d.z,
        m.m[1]*d.x + m.m[5]*d.y + m.m[9]*d.z,
        m.m[2]*d.x + m.m[6]*d.y + m.m[10]*d.z
    });
}

void appendBoxXformVerts(std::vector<Vertex>& out, const Mat4& xform, Vec3 halfSize){
    float hx=halfSize.x, hy=halfSize.y, hz=halfSize.z;
    Vec3 lp[8] = {
        {-hx,-hy,-hz}, {hx,-hy,-hz}, {hx,hy,-hz}, {-hx,hy,-hz},
        {-hx,-hy, hz}, {hx,-hy, hz}, {hx,hy, hz}, {-hx,hy, hz},
    };
    Vec3 p[8];
    for(int i=0;i<8;i++) p[i] = xformPoint(xform, lp[i]);
    auto quad = [&](int i0,int i1,int i2,int i3, Vec3 localN){
        Vec3 n = xformDir(xform, localN);
        Vertex a{p[i0].x,p[i0].y,p[i0].z, 0,0, n.x,n.y,n.z};
        Vertex b{p[i1].x,p[i1].y,p[i1].z, 1,0, n.x,n.y,n.z};
        Vertex c{p[i2].x,p[i2].y,p[i2].z, 1,1, n.x,n.y,n.z};
        Vertex d{p[i3].x,p[i3].y,p[i3].z, 0,1, n.x,n.y,n.z};
        out.push_back(a); out.push_back(b); out.push_back(c);
        out.push_back(a); out.push_back(c); out.push_back(d);
    };
    quad(0,1,2,3, {0,0,-1});
    quad(5,4,7,6, {0,0, 1});
    quad(4,0,3,7, {-1,0,0});
    quad(1,5,6,2, { 1,0,0});
    quad(3,2,6,7, {0, 1,0});
    quad(4,5,1,0, {0,-1,0});
}

void appendCylinderVerts(std::vector<Vertex>& out, const Mat4& xform, float radius, float halfLen, int segments){
    if(segments < 3) segments = 3;
    for(int i=0;i<segments;i++){
        float a0 = (float)i / (float)segments * 2.0f * (float)M_PI;
        float a1 = (float)(i+1) / (float)segments * 2.0f * (float)M_PI;
        float c0 = cosf(a0), s0 = sinf(a0);
        float c1 = cosf(a1), s1 = sinf(a1);

        Vec3 n0 = xformDir(xform, {c0, s0, 0});
        Vec3 n1 = xformDir(xform, {c1, s1, 0});

        Vec3 p00 = xformPoint(xform, {c0*radius, s0*radius, -halfLen});
        Vec3 p01 = xformPoint(xform, {c0*radius, s0*radius,  halfLen});
        Vec3 p10 = xformPoint(xform, {c1*radius, s1*radius, -halfLen});
        Vec3 p11 = xformPoint(xform, {c1*radius, s1*radius,  halfLen});

        float u0 = (float)i/(float)segments, u1 = (float)(i+1)/(float)segments;
        // боковая поверхность
        out.push_back({p00.x,p00.y,p00.z, u0,0, n0.x,n0.y,n0.z});
        out.push_back({p10.x,p10.y,p10.z, u1,0, n1.x,n1.y,n1.z});
        out.push_back({p11.x,p11.y,p11.z, u1,1, n1.x,n1.y,n1.z});
        out.push_back({p00.x,p00.y,p00.z, u0,0, n0.x,n0.y,n0.z});
        out.push_back({p11.x,p11.y,p11.z, u1,1, n1.x,n1.y,n1.z});
        out.push_back({p01.x,p01.y,p01.z, u0,1, n0.x,n0.y,n0.z});

        // крышки (передняя +Z и задняя -Z)
        Vec3 capF = xformPoint(xform, {0,0, halfLen});
        Vec3 capB = xformPoint(xform, {0,0,-halfLen});
        Vec3 nF = xformDir(xform, {0,0, 1});
        Vec3 nB = xformDir(xform, {0,0,-1});
        out.push_back({capF.x,capF.y,capF.z, 0.5f,0.5f, nF.x,nF.y,nF.z});
        out.push_back({p01.x,p01.y,p01.z, u0,1, nF.x,nF.y,nF.z});
        out.push_back({p11.x,p11.y,p11.z, u1,1, nF.x,nF.y,nF.z});

        out.push_back({capB.x,capB.y,capB.z, 0.5f,0.5f, nB.x,nB.y,nB.z});
        out.push_back({p10.x,p10.y,p10.z, u1,0, nB.x,nB.y,nB.z});
        out.push_back({p00.x,p00.y,p00.z, u0,0, nB.x,nB.y,nB.z});
    }
}

std::vector<Vertex> buildAK47LowPolyVerts(){
    std::vector<Vertex> v;
    v.reserve(6 * 6 * 5); // 5 блоков * 6 граней * 6 вершин

    // Ствольная коробка (receiver) — основной блок, вокруг него всё центрируется (0,0,0
    // примерно соответствует хвату руки — совпадает с тем, что было у куба-заглушки).
    appendBoxVerts(v, {0.0f, 0.0f, 0.0f}, {0.045f, 0.045f, 0.22f});

    // Ствол — тонкий и длинный, уходит вперёд (-Z) от ствольной коробки.
    appendBoxVerts(v, {0.0f, 0.01f, -0.42f}, {0.018f, 0.018f, 0.20f});

    // Приклад — сзади (+Z), немного уже и ниже центра, с лёгким наклоном визуально не
    // делаем (упростили до прямого блока — силуэт всё равно читаем как приклад).
    appendBoxVerts(v, {0.0f, -0.02f, 0.34f}, {0.03f, 0.05f, 0.16f});

    // Пистолетная рукоятка — свисает вниз под ствольной коробкой, ближе к прикладу.
    appendBoxVerts(v, {0.0f, -0.10f, 0.16f}, {0.022f, 0.06f, 0.028f});

    // Магазин — характерный изогнутый рожок АК заменяем на прямой конусовидный блок,
    // расширяющийся книзу и уходящий немного вперёд (визуально узнаваемый силуэт рожка).
    appendBoxVerts(v, {0.0f, -0.14f, -0.06f}, {0.028f, 0.09f, 0.045f});

    return v;
}


// ---- Скайбокс-куб: рисуется изнутри, нормали не нужны, сэмплируется по позиции вершины ----
Mesh buildSkyCube(float half){
    // 36 вершин (6 граней * 2 треугольника * 3), порядок намотки такой, чтобы грани
    // были видны изнутри куба (камера всегда внутри скайбокса)
    float s = half;
    float raw[] = {
        // -X (left)
        -s,-s,-s,  -s,-s, s,  -s, s, s,   -s,-s,-s,  -s, s, s,  -s, s,-s,
        // +X (right)
         s,-s, s,   s,-s,-s,  s, s,-s,     s,-s, s,   s, s,-s,  s, s, s,
        // -Z (back, если считать -Z "лицом" вперёд по умолчанию)
        -s,-s, s,   s,-s, s,  s, s, s,    -s,-s, s,   s, s, s, -s, s, s,
        // +Z (front)
         s,-s,-s,  -s,-s,-s, -s, s,-s,     s,-s,-s,  -s, s,-s,  s, s,-s,
        // +Y (up)
        -s, s,-s,   s, s,-s,  s, s, s,    -s, s,-s,   s, s, s, -s, s, s,
        // -Y (down)
        -s,-s, s,   s,-s, s,  s,-s,-s,    -s,-s, s,   s,-s,-s,-s,-s,-s
    };
    Mesh mesh;
    mesh.vertexCount = 36;
    mesh.texture = 0;
    glGenVertexArrays(1, &mesh.vao);
    glGenBuffers(1, &mesh.vbo);
    glBindVertexArray(mesh.vao);
    glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(raw), raw, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3*sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);
    return mesh;
}


// ==================== ГЛАДКИЕ ФОРМЫ ====================
// Нормали строятся из ГЕОМЕТРИИ поверхности (радиальное направление, с поправкой на
// наклон образующей у конусов), а не из плоскостей граней — поэтому освещение течёт
// по поверхности непрерывно и объём выглядит органическим, а не гранёным.

void appendEllipsoidVerts(std::vector<Vertex>& out, const Mat4& xform, Vec3 radii, int segments, int rings){
    if(segments < 4) segments = 4;
    if(rings < 3) rings = 3;
    for(int r=0; r<rings; r++){
        float phi0 = (float)M_PI * (float)r / (float)rings;
        float phi1 = (float)M_PI * (float)(r+1) / (float)rings;
        for(int s=0; s<segments; s++){
            float th0 = 2.0f*(float)M_PI * (float)s / (float)segments;
            float th1 = 2.0f*(float)M_PI * (float)(s+1) / (float)segments;
            // Четыре угла кольцевого квада на поверхности
            auto mk = [&](float phi, float th, float u, float v)->Vertex{
                // Единичное направление на сфере
                Vec3 d = { sinf(phi)*cosf(th), cosf(phi), sinf(phi)*sinf(th) };
                Vec3 p = { d.x*radii.x, d.y*radii.y, d.z*radii.z };
                // Нормаль эллипсоида — градиент его уравнения: делим на квадраты радиусов
                Vec3 n = { d.x/(radii.x*radii.x), d.y/(radii.y*radii.y), d.z/(radii.z*radii.z) };
                Vec3 wp = xformPoint(xform, p);
                Vec3 wn = xformDir(xform, v3norm(n));
                return { wp.x, wp.y, wp.z, u, v, wn.x, wn.y, wn.z };
            };
            float u0 = (float)s/(float)segments, u1 = (float)(s+1)/(float)segments;
            float v0 = (float)r/(float)rings,    v1 = (float)(r+1)/(float)rings;
            Vertex a = mk(phi0, th0, u0, v0);
            Vertex b = mk(phi1, th0, u0, v1);
            Vertex c = mk(phi1, th1, u1, v1);
            Vertex d = mk(phi0, th1, u1, v0);
            out.push_back(a); out.push_back(b); out.push_back(c);
            out.push_back(a); out.push_back(c); out.push_back(d);
        }
    }
}

void appendSphereVerts(std::vector<Vertex>& out, const Mat4& xform, float radius, int segments, int rings){
    appendEllipsoidVerts(out, xform, {radius, radius, radius}, segments, rings);
}

void appendTaperedVerts(std::vector<Vertex>& out, const Mat4& xform,
                        float radiusBottom, float radiusTop, float halfLen, int segments, bool caps){
    if(segments < 4) segments = 4;
    // Наклон образующей: без этой поправки у сужающейся конечности освещение ломается
    // "ступенькой" на стыке с соседним сегментом.
    float slope = (radiusBottom - radiusTop) / (2.0f * halfLen);
    for(int s=0; s<segments; s++){
        float th0 = 2.0f*(float)M_PI * (float)s / (float)segments;
        float th1 = 2.0f*(float)M_PI * (float)(s+1) / (float)segments;
        float c0 = cosf(th0), n0 = sinf(th0);
        float c1 = cosf(th1), n1 = sinf(th1);
        Vec3 nrm0 = xformDir(xform, v3norm({c0, slope, n0}));
        Vec3 nrm1 = xformDir(xform, v3norm({c1, slope, n1}));

        Vec3 b0 = xformPoint(xform, {c0*radiusBottom, -halfLen, n0*radiusBottom});
        Vec3 t0 = xformPoint(xform, {c0*radiusTop,     halfLen, n0*radiusTop});
        Vec3 b1 = xformPoint(xform, {c1*radiusBottom, -halfLen, n1*radiusBottom});
        Vec3 t1 = xformPoint(xform, {c1*radiusTop,     halfLen, n1*radiusTop});
        float u0 = (float)s/(float)segments, u1 = (float)(s+1)/(float)segments;

        out.push_back({b0.x,b0.y,b0.z, u0,0, nrm0.x,nrm0.y,nrm0.z});
        out.push_back({b1.x,b1.y,b1.z, u1,0, nrm1.x,nrm1.y,nrm1.z});
        out.push_back({t1.x,t1.y,t1.z, u1,1, nrm1.x,nrm1.y,nrm1.z});
        out.push_back({b0.x,b0.y,b0.z, u0,0, nrm0.x,nrm0.y,nrm0.z});
        out.push_back({t1.x,t1.y,t1.z, u1,1, nrm1.x,nrm1.y,nrm1.z});
        out.push_back({t0.x,t0.y,t0.z, u0,1, nrm0.x,nrm0.y,nrm0.z});

        if(caps){
            Vec3 capT = xformPoint(xform, {0,  halfLen, 0});
            Vec3 capB = xformPoint(xform, {0, -halfLen, 0});
            Vec3 nT = xformDir(xform, {0, 1, 0});
            Vec3 nB = xformDir(xform, {0,-1, 0});
            out.push_back({capT.x,capT.y,capT.z, 0.5f,0.5f, nT.x,nT.y,nT.z});
            out.push_back({t0.x,t0.y,t0.z, u0,1, nT.x,nT.y,nT.z});
            out.push_back({t1.x,t1.y,t1.z, u1,1, nT.x,nT.y,nT.z});
            out.push_back({capB.x,capB.y,capB.z, 0.5f,0.5f, nB.x,nB.y,nB.z});
            out.push_back({b1.x,b1.y,b1.z, u1,0, nB.x,nB.y,nB.z});
            out.push_back({b0.x,b0.y,b0.z, u0,0, nB.x,nB.y,nB.z});
        }
    }
}

void appendCapsuleVerts(std::vector<Vertex>& out, const Mat4& xform, float radius, float halfLen, int segments, int rings){
    if(segments < 4) segments = 4;
    if(rings < 2) rings = 2;
    // Ствол капсулы
    appendTaperedVerts(out, xform, radius, radius, halfLen, segments, false);
    // Полусферы на торцах: строим половинки эллипсоида, сдвинутые к концам
    for(int half=0; half<2; half++){
        float sign = (half == 0) ? 1.0f : -1.0f;
        Mat4 capX = mat4Multiply(xform, mat4Translate({0, sign*halfLen, 0}));
        for(int r=0; r<rings; r++){
            float p0 = (float)M_PI*0.5f * (float)r / (float)rings;
            float p1 = (float)M_PI*0.5f * (float)(r+1) / (float)rings;
            for(int s=0; s<segments; s++){
                float t0 = 2.0f*(float)M_PI * (float)s / (float)segments;
                float t1 = 2.0f*(float)M_PI * (float)(s+1) / (float)segments;
                auto mk = [&](float phi, float th, float u, float v)->Vertex{
                    Vec3 d = { sinf(phi)*cosf(th), sign*cosf(phi), sinf(phi)*sinf(th) };
                    Vec3 p = v3scale(d, radius);
                    Vec3 wp = xformPoint(capX, p);
                    Vec3 wn = xformDir(capX, d);
                    return { wp.x, wp.y, wp.z, u, v, wn.x, wn.y, wn.z };
                };
                float u0=(float)s/(float)segments, u1=(float)(s+1)/(float)segments;
                float v0=(float)r/(float)rings,    v1=(float)(r+1)/(float)rings;
                Vertex a = mk(p1, t0, u0, v1);
                Vertex b = mk(p0, t0, u0, v0);
                Vertex c = mk(p0, t1, u1, v0);
                Vertex d2 = mk(p1, t1, u1, v1);
                // Порядок обхода зависит от полушария, иначе нижняя шапка выворачивается
                if(half == 0){
                    out.push_back(a); out.push_back(b); out.push_back(c);
                    out.push_back(a); out.push_back(c); out.push_back(d2);
                } else {
                    out.push_back(a); out.push_back(c); out.push_back(b);
                    out.push_back(a); out.push_back(d2); out.push_back(c);
                }
            }
        }
    }
}
