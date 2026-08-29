#include "AssetLoader.h"
#include "../Core/Settings.h"
#include "../../Core/Math.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <sstream>
#include <string>
#include <cmath>
#include <cstdlib>
#include <cerrno>

// ==================== OBJ ЗАГРУЗЧИК / РЕСУРСЫ ====================
// Простой парсер Wavefront OBJ: v, vt, vn, f (треугольники и квады, триангулируем веером)
bool loadOBJ(const char* path, std::vector<Vertex>& outVerts){
    SDL_RWops* rw = SDL_RWFromFile(path, "rb");
    if(!rw){
        SDL_Log("Failed to open OBJ: %s (%s)", path, SDL_GetError());
        return false;
    }
    Sint64 rwSize = SDL_RWsize(rw);
    std::string fileData(rwSize > 0 ? (size_t)rwSize : 0, '\0');
    if(rwSize > 0) SDL_RWread(rw, &fileData[0], 1, (size_t)rwSize);
    SDL_RWclose(rw);
    std::istringstream file(fileData);
    std::vector<Vec3> positions;
    std::vector<Vec3> normals;
    struct UV{ float u,v; };
    std::vector<UV> uvs;

    struct FaceIdx { int p, t, n; };
    std::vector<std::vector<FaceIdx>> faces;

    std::string line;
    while(std::getline(file, line)){
        if(line.empty() || line[0]=='#') continue;
        std::istringstream iss(line);
        std::string tag; iss >> tag;
        if(tag=="v"){
            Vec3 p; iss >> p.x >> p.y >> p.z; positions.push_back(p);
        } else if(tag=="vt"){
            UV t; iss >> t.u >> t.v; uvs.push_back(t);
        } else if(tag=="vn"){
            Vec3 n; iss >> n.x >> n.y >> n.z; normals.push_back(n);
        } else if(tag=="f"){
            std::vector<FaceIdx> face;
            std::string tok;
            // ВАЖНО: разбор индексов через безопасную обёртку, а не через голый std::stoi.
            // Раньше здесь был именно он — и любой файл с нечисловым токеном в строке "f"
            // (битая загрузка, экзотический экспортёр, обрезанный файл) бросал
            // std::invalid_argument, которое никто не ловил => std::terminate и падение
            // приложения на старте вместо мягкого отката на процедурную модель.
            auto parseIndex = [](const std::string& text, int& outValue)->bool{
                if(text.empty()) return false;
                errno = 0;
                char* endPtr = nullptr;
                long parsed = strtol(text.c_str(), &endPtr, 10);
                if(endPtr == text.c_str() || errno == ERANGE) return false;
                if(parsed > 2147483647L || parsed < -2147483648L) return false;
                outValue = (int)parsed;
                return true;
            };
            bool faceOk = true;
            while(iss >> tok){
                FaceIdx fi{0,0,0};
                size_t firstSlash = tok.find('/');
                if(firstSlash==std::string::npos){
                    if(!parseIndex(tok, fi.p)){ faceOk = false; break; }
                } else {
                    if(!parseIndex(tok.substr(0, firstSlash), fi.p)){ faceOk = false; break; }
                    size_t secondSlash = tok.find('/', firstSlash+1);
                    if(secondSlash==std::string::npos){
                        parseIndex(tok.substr(firstSlash+1), fi.t); // UV необязателен
                    } else {
                        std::string tPart = tok.substr(firstSlash+1, secondSlash-firstSlash-1);
                        if(!tPart.empty()) parseIndex(tPart, fi.t);
                        std::string nPart = tok.substr(secondSlash+1);
                        if(!nPart.empty()) parseIndex(nPart, fi.n);
                    }
                }
                face.push_back(fi);
            }
            // Грань с нечитаемым индексом позиции пропускаем целиком: лучше потерять один
            // треугольник, чем всю модель или всё приложение.
            if(faceOk && face.size() >= 3) faces.push_back(face);
        }
    }

    auto resolveIdx = [](int idx, int count)->int{
        if(idx > 0) return idx - 1;
        if(idx < 0) return count + idx;
        return -1;
    };

    for(auto& face : faces){
        for(size_t i=1; i+1<face.size(); i++){
            FaceIdx tri[3] = { face[0], face[i], face[i+1] };
            for(int k=0;k<3;k++){
                Vertex vert{};
                int pi = resolveIdx(tri[k].p, (int)positions.size());
                if(pi>=0 && pi<(int)positions.size()){
                    vert.px=positions[pi].x; vert.py=positions[pi].y; vert.pz=positions[pi].z;
                }
                if(tri[k].t != 0){
                    int ti = resolveIdx(tri[k].t, (int)uvs.size());
                    if(ti>=0 && ti<(int)uvs.size()){
                        vert.u=uvs[ti].u; vert.v=1.0f - uvs[ti].v;
                    }
                }
                if(tri[k].n != 0){
                    int ni = resolveIdx(tri[k].n, (int)normals.size());
                    if(ni>=0 && ni<(int)normals.size()){
                        vert.nx=normals[ni].x; vert.ny=normals[ni].y; vert.nz=normals[ni].z;
                    }
                }
                outVerts.push_back(vert);
            }
        }
    }

    // Считаем нормаль треугольника для ЛЮБОЙ вершины, у которой она осталась нулевой —
    // раньше это делалось только если во всём файле не было НИ ОДНОЙ "vn" строки, из-за
    // чего .obj с частично заданными нормалями (одни грани с vn, другие без) оставлял
    // часть вершин с нормалью (0,0,0). При освещении dot(normal, lightDir) для такой
    // вершины даёт мусорный/чёрный результат — визуально это и есть "странные фигуры
    // с артефактами рендера" на месте моделей оружия/объектов без честного .obj.
    const float ZERO_EPS = 1e-8f;
    for(size_t i=0;i+2<outVerts.size(); i+=3){
        Vertex& v0 = outVerts[i]; Vertex& v1 = outVerts[i+1]; Vertex& v2 = outVerts[i+2];
        bool n0Zero = (fabsf(v0.nx)<ZERO_EPS && fabsf(v0.ny)<ZERO_EPS && fabsf(v0.nz)<ZERO_EPS);
        bool n1Zero = (fabsf(v1.nx)<ZERO_EPS && fabsf(v1.ny)<ZERO_EPS && fabsf(v1.nz)<ZERO_EPS);
        bool n2Zero = (fabsf(v2.nx)<ZERO_EPS && fabsf(v2.ny)<ZERO_EPS && fabsf(v2.nz)<ZERO_EPS);
        if(!n0Zero && !n1Zero && !n2Zero) continue; // у всех трёх уже есть валидная нормаль из vn
        Vec3 a{v0.px,v0.py,v0.pz};
        Vec3 b{v1.px,v1.py,v1.pz};
        Vec3 c{v2.px,v2.py,v2.pz};
        Vec3 n = v3norm(v3cross(v3sub(b,a), v3sub(c,a)));
        if(n0Zero){ v0.nx=n.x; v0.ny=n.y; v0.nz=n.z; }
        if(n1Zero){ v1.nx=n.x; v1.ny=n.y; v1.nz=n.z; }
        if(n2Zero){ v2.nx=n.x; v2.ny=n.y; v2.nz=n.z; }
    }

    SDL_Log("OBJ loaded: %s (%d verts)", path, (int)outVerts.size());
    return !outVerts.empty();
}

GLuint loadTextureFromFile(const char* path, int* outW, int* outH){
    if(outW) *outW = 0;
    if(outH) *outH = 0;
    SDL_Surface* surf = IMG_Load(path);
    if(!surf){
        SDL_Log("Failed to load texture: %s (%s)", path, IMG_GetError());
        return 0;
    }
    SDL_Surface* conv = SDL_ConvertSurfaceFormat(surf, SDL_PIXELFORMAT_ABGR8888, 0);
    SDL_FreeSurface(surf);
    if(!conv) return 0;

    // «Супер оптимизация» (и низкое качество) ужимают текстуры ПРИ ЗАГРУЗКЕ. Основной
    // расход видеопамяти и пропускной способности на телефоне — именно текстуры, и
    // ужатая вдвое сторона это вчетверо меньше данных на каждую выборку. Ужимаем
    // степенями двойки: так остаются целыми мип-уровни и не плывёт фильтрация.
    int limit = qualityTextureLimit();
    if(limit > 0 && (conv->w > limit || conv->h > limit)){
        int nw = conv->w, nh = conv->h;
        while(nw > limit || nh > limit){
            if(nw > 1) nw /= 2;
            if(nh > 1) nh /= 2;
        }
        SDL_Surface* small = SDL_CreateRGBSurfaceWithFormat(0, nw, nh, 32, SDL_PIXELFORMAT_ABGR8888);
        if(small && SDL_BlitScaled(conv, nullptr, small, nullptr) == 0){
            SDL_FreeSurface(conv);
            conv = small;
        } else if(small){
            SDL_FreeSurface(small); // не получилось — грузим как есть, это не повод падать
        }
    }

    if(outW) *outW = conv->w;
    if(outH) *outH = conv->h;

    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, conv->w, conv->h, 0, GL_RGBA, GL_UNSIGNED_BYTE, conv->pixels);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    SDL_FreeSurface(conv);
    return tex;
}

