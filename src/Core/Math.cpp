#include "Math.h"
#include <cmath>

// ==================== МАТЕМАТИКА ====================
Vec3 v3sub(Vec3 a, Vec3 b){ return {a.x-b.x, a.y-b.y, a.z-b.z}; }
Vec3 v3add(Vec3 a, Vec3 b){ return {a.x+b.x, a.y+b.y, a.z+b.z}; }
Vec3 v3scale(Vec3 a, float s){ return {a.x*s, a.y*s, a.z*s}; }
Vec3 v3cross(Vec3 a, Vec3 b){
    return { a.y*b.z-a.z*b.y, a.z*b.x-a.x*b.z, a.x*b.y-a.y*b.x };
}
float v3len(Vec3 a){ return sqrtf(a.x*a.x+a.y*a.y+a.z*a.z); }
Vec3 v3norm(Vec3 a){
    float l=v3len(a); if(l<0.00001f) return {0,0,0};
    return {a.x/l, a.y/l, a.z/l};
}

Mat4 mat4Identity(){
    Mat4 r{}; r.m[0]=r.m[5]=r.m[10]=r.m[15]=1.f; return r;
}
Mat4 mat4Multiply(const Mat4& a, const Mat4& b){
    Mat4 r{};
    for(int c=0;c<4;c++) for(int row=0;row<4;row++){
        float sum=0;
        for(int k=0;k<4;k++) sum += a.m[k*4+row]*b.m[c*4+k];
        r.m[c*4+row]=sum;
    }
    return r;
}
Mat4 mat4Perspective(float fovYRad, float aspect, float nearZ, float farZ){
    Mat4 r{};
    float f = 1.0f/tanf(fovYRad/2.0f);
    r.m[0]=f/aspect; r.m[5]=f;
    r.m[10]=(farZ+nearZ)/(nearZ-farZ); r.m[11]=-1.0f;
    r.m[14]=(2*farZ*nearZ)/(nearZ-farZ);
    return r;
}
Mat4 mat4LookAt(Vec3 eye, Vec3 center, Vec3 up){
    Vec3 f = v3norm(v3sub(center, eye));
    Vec3 s = v3norm(v3cross(f, up));
    Vec3 u = v3cross(s, f);
    Mat4 r = mat4Identity();
    r.m[0]=s.x; r.m[4]=s.y; r.m[8]=s.z;
    r.m[1]=u.x; r.m[5]=u.y; r.m[9]=u.z;
    r.m[2]=-f.x; r.m[6]=-f.y; r.m[10]=-f.z;
    r.m[12]=-(s.x*eye.x+s.y*eye.y+s.z*eye.z);
    r.m[13]=-(u.x*eye.x+u.y*eye.y+u.z*eye.z);
    r.m[14]=(f.x*eye.x+f.y*eye.y+f.z*eye.z);
    return r;
}
Mat4 mat4Translate(Vec3 t){
    Mat4 r = mat4Identity();
    r.m[12]=t.x; r.m[13]=t.y; r.m[14]=t.z;
    return r;
}
Mat4 mat4Scale(float s){
    Mat4 r = mat4Identity();
    r.m[0]=s; r.m[5]=s; r.m[10]=s;
    return r;
}
Mat4 mat4ScaleXYZ(float sx, float sy, float sz){
    Mat4 r = mat4Identity();
    r.m[0]=sx; r.m[5]=sy; r.m[10]=sz;
    return r;
}
Mat4 mat4RotateY(float rad){
    Mat4 r = mat4Identity();
    float c=cosf(rad), s=sinf(rad);
    r.m[0]=c; r.m[2]=-s; r.m[8]=s; r.m[10]=c;
    return r;
}
Mat4 mat4RotateX(float rad){
    Mat4 r = mat4Identity();
    float c=cosf(rad), s=sinf(rad);
    r.m[5]=c; r.m[6]=s; r.m[9]=-s; r.m[10]=c;
    return r;
}
Mat4 mat4RotateZ(float rad){
    Mat4 r = mat4Identity();
    float c=cosf(rad), s=sinf(rad);
    r.m[0]=c; r.m[1]=s; r.m[4]=-s; r.m[5]=c;
    return r;
}
Mat4 mat4Ortho(float l,float rr,float b,float t,float n,float f){
    Mat4 r{};
    r.m[0]=2.0f/(rr-l);
    r.m[5]=2.0f/(t-b);
    r.m[10]=-2.0f/(f-n);
    r.m[12]=-(rr+l)/(rr-l);
    r.m[13]=-(t+b)/(t-b);
    r.m[14]=-(f+n)/(f-n);
    r.m[15]=1.0f;
    return r;
}
// Верхние 3x3 матрицы поворота (для normal matrix при однородном масштабе — модель без масштаба)
void mat4ToMat3(const Mat4& m, float out[9]){
    out[0]=m.m[0]; out[1]=m.m[1]; out[2]=m.m[2];
    out[3]=m.m[4]; out[4]=m.m[5]; out[5]=m.m[6];
    out[6]=m.m[8]; out[7]=m.m[9]; out[8]=m.m[10];
}


// ==================== КВАТЕРНИОНЫ ====================
Quat quatIdentity(){ return Quat{0,0,0,1}; }

Quat quatNormalize(Quat q){
    float len = sqrtf(q.x*q.x + q.y*q.y + q.z*q.z + q.w*q.w);
    if(len < 1e-8f) return quatIdentity();
    float inv = 1.0f/len;
    return Quat{ q.x*inv, q.y*inv, q.z*inv, q.w*inv };
}

Quat quatMultiply(Quat a, Quat b){
    return Quat{
        a.w*b.x + a.x*b.w + a.y*b.z - a.z*b.y,
        a.w*b.y - a.x*b.z + a.y*b.w + a.z*b.x,
        a.w*b.z + a.x*b.y - a.y*b.x + a.z*b.w,
        a.w*b.w - a.x*b.x - a.y*b.y - a.z*b.z
    };
}

Quat quatSlerp(Quat a, Quat b, float t){
    float dot = a.x*b.x + a.y*b.y + a.z*b.z + a.w*b.w;
    // Кватернионы q и -q задают один поворот. Если скалярное произведение отрицательно,
    // переворачиваем второй — иначе интерполяция пойдёт "длинной дорогой" через 360°,
    // и рука/нога на кадре сделает полный оборот вместо плавного доворота.
    if(dot < 0.0f){
        b.x = -b.x; b.y = -b.y; b.z = -b.z; b.w = -b.w;
        dot = -dot;
    }
    // Почти сонаправленные — линейная интерполяция, чтобы не делить на sin(~0).
    if(dot > 0.9995f){
        Quat r{ a.x + (b.x-a.x)*t, a.y + (b.y-a.y)*t, a.z + (b.z-a.z)*t, a.w + (b.w-a.w)*t };
        return quatNormalize(r);
    }
    float theta0 = acosf(dot);
    float theta = theta0 * t;
    float sinTheta0 = sinf(theta0);
    float s0 = cosf(theta) - dot * sinf(theta) / sinTheta0;
    float s1 = sinf(theta) / sinTheta0;
    return Quat{ a.x*s0 + b.x*s1, a.y*s0 + b.y*s1, a.z*s0 + b.z*s1, a.w*s0 + b.w*s1 };
}

Mat4 quatToMat4(Quat q){
    q = quatNormalize(q);
    float x=q.x, y=q.y, z=q.z, w=q.w;
    Mat4 m = mat4Identity();
    m.m[0] = 1.0f - 2.0f*(y*y + z*z);
    m.m[1] = 2.0f*(x*y + z*w);
    m.m[2] = 2.0f*(x*z - y*w);
    m.m[4] = 2.0f*(x*y - z*w);
    m.m[5] = 1.0f - 2.0f*(x*x + z*z);
    m.m[6] = 2.0f*(y*z + x*w);
    m.m[8] = 2.0f*(x*z + y*w);
    m.m[9] = 2.0f*(y*z - x*w);
    m.m[10] = 1.0f - 2.0f*(x*x + y*y);
    return m;
}

Mat4 mat4Compose(Vec3 translation, Quat rotation, Vec3 scale){
    Mat4 r = quatToMat4(rotation);
    // Масштаб применяется первым (столбцы поворота домножаются покомпонентно)
    r.m[0]*=scale.x; r.m[1]*=scale.x; r.m[2]*=scale.x;
    r.m[4]*=scale.y; r.m[5]*=scale.y; r.m[6]*=scale.y;
    r.m[8]*=scale.z; r.m[9]*=scale.z; r.m[10]*=scale.z;
    r.m[12]=translation.x; r.m[13]=translation.y; r.m[14]=translation.z;
    return r;
}

Mat4 mat4InverseAffine(const Mat4& m){
    // Обращаем 3x3-часть по правилу Крамера, затем переносим начало координат.
    float a=m.m[0], b=m.m[4], c=m.m[8];
    float d=m.m[1], e=m.m[5], f=m.m[9];
    float g=m.m[2], h=m.m[6], i=m.m[10];
    float det = a*(e*i - f*h) - b*(d*i - f*g) + c*(d*h - e*g);
    Mat4 r = mat4Identity();
    if(fabsf(det) < 1e-12f) return r; // вырожденная — возвращаем единичную, чтобы не плодить NaN
    float inv = 1.0f/det;
    r.m[0] =  (e*i - f*h)*inv;  r.m[4] = -(b*i - c*h)*inv;  r.m[8]  =  (b*f - c*e)*inv;
    r.m[1] = -(d*i - f*g)*inv;  r.m[5] =  (a*i - c*g)*inv;  r.m[9]  = -(a*f - c*d)*inv;
    r.m[2] =  (d*h - e*g)*inv;  r.m[6] = -(a*h - b*g)*inv;  r.m[10] =  (a*e - b*d)*inv;
    float tx=m.m[12], ty=m.m[13], tz=m.m[14];
    r.m[12] = -(r.m[0]*tx + r.m[4]*ty + r.m[8]*tz);
    r.m[13] = -(r.m[1]*tx + r.m[5]*ty + r.m[9]*tz);
    r.m[14] = -(r.m[2]*tx + r.m[6]*ty + r.m[10]*tz);
    return r;
}

// ==================== ДОБАВЛЕНО В OSIL-SURVIVAL ====================
Vec2 v2add(Vec2 a, Vec2 b){ return { a.x+b.x, a.y+b.y }; }
Vec2 v2sub(Vec2 a, Vec2 b){ return { a.x-b.x, a.y-b.y }; }
Vec2 v2scale(Vec2 a, float s){ return { a.x*s, a.y*s }; }
float v2len(Vec2 a){ return sqrtf(a.x*a.x + a.y*a.y); }
float v2dist(Vec2 a, Vec2 b){ return v2len(v2sub(a,b)); }
Vec2 v2norm(Vec2 a){
    float l = v2len(a);
    if(l < 1e-6f) return {0,0};
    return { a.x/l, a.y/l };
}

float clampf(float v, float lo, float hi){ return v < lo ? lo : (v > hi ? hi : v); }
int   clampi(int v, int lo, int hi){ return v < lo ? lo : (v > hi ? hi : v); }
float lerpf(float a, float b, float t){ return a + (b-a)*t; }

float invLerpf(float a, float b, float v){
    if(fabsf(b-a) < 1e-8f) return 0.0f;
    return clampf((v-a)/(b-a), 0.0f, 1.0f);
}

float smoothstepf(float edge0, float edge1, float v){
    float t = invLerpf(edge0, edge1, v);
    return t*t*(3.0f - 2.0f*t);
}

float horizontalDist(Vec3 a, Vec3 b){
    float dx = a.x-b.x, dz = a.z-b.z;
    return sqrtf(dx*dx + dz*dz);
}
