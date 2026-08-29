#include "Noise.h"
#include "Random.h"

#include <cmath>

namespace {
// Кривая сглаживания Кена Перлина 6t^5-15t^4+10t^3: непрерывна по первой и второй
// производной, поэтому на границах решётки нет заметных «складок» рельефа.
inline float fade(float t){ return t*t*t*(t*(t*6.0f - 15.0f) + 10.0f); }
inline float lerp(float a, float b, float t){ return a + t*(b-a); }
} // namespace

void PerlinNoise::reseed(uint64_t seed){
    for(int i = 0; i < 256; ++i) perm_[i] = (uint8_t)i;

    Rng rng(seed ^ 0x50524c4eULL /* 'PRLN' */);
    // Фишер — Йетс: единственная перестановка, зависящая только от сида.
    for(int i = 255; i > 0; --i){
        int j = (int)rng.nextBelow((uint32_t)(i + 1));
        uint8_t tmp = perm_[i];
        perm_[i] = perm_[j];
        perm_[j] = tmp;
    }
    for(int i = 0; i < 256; ++i) perm_[256 + i] = perm_[i];
}

float PerlinNoise::gradDot_(int hash, float x, float y) const {
    // 8 направлений по кругу — классический вариант 2D-градиентов Перлина.
    switch(hash & 7){
        case 0: return  x + y;
        case 1: return  x - y;
        case 2: return -x + y;
        case 3: return -x - y;
        case 4: return  x;
        case 5: return -x;
        case 6: return  y;
        default: return -y;
    }
}

float PerlinNoise::noise2(float x, float y) const {
    float fx = floorf(x), fy = floorf(y);
    int xi = (int)((int64_t)fx & 255);
    int yi = (int)((int64_t)fy & 255);
    float xf = x - fx, yf = y - fy;

    float u = fade(xf), v = fade(yf);

    int aa = perm_[perm_[xi]     + yi];
    int ab = perm_[perm_[xi]     + yi + 1];
    int ba = perm_[perm_[xi + 1] + yi];
    int bb = perm_[perm_[xi + 1] + yi + 1];

    float x1 = lerp(gradDot_(aa, xf,        yf),        gradDot_(ba, xf - 1.0f, yf),        u);
    float x2 = lerp(gradDot_(ab, xf, yf - 1.0f),        gradDot_(bb, xf - 1.0f, yf - 1.0f), u);
    // Множитель 1.4 приводит характерный размах ~[-0.71,0.71] ближе к [-1,1].
    return lerp(x1, x2, v) * 1.4f;
}

float PerlinNoise::fbm(float x, float y, const NoiseParams& p) const {
    float sum = 0.0f, amp = 1.0f, freq = p.frequency, norm = 0.0f;
    for(int i = 0; i < p.octaves; ++i){
        sum  += noise2(x * freq, y * freq) * amp;
        norm += amp;
        amp  *= p.gain;
        freq *= p.lacunarity;
    }
    return norm > 0.0f ? sum / norm : 0.0f;
}

float PerlinNoise::ridged(float x, float y, const NoiseParams& p) const {
    float sum = 0.0f, amp = 1.0f, freq = p.frequency, norm = 0.0f;
    for(int i = 0; i < p.octaves; ++i){
        float n = 1.0f - fabsf(noise2(x * freq, y * freq));
        n *= n; // возведение в квадрат делает гребни уже, а склоны — круче
        sum  += n * amp;
        norm += amp;
        amp  *= p.gain;
        freq *= p.lacunarity;
    }
    return norm > 0.0f ? sum / norm : 0.0f;
}

float PerlinNoise::billow(float x, float y, const NoiseParams& p) const {
    float sum = 0.0f, amp = 1.0f, freq = p.frequency, norm = 0.0f;
    for(int i = 0; i < p.octaves; ++i){
        sum  += fabsf(noise2(x * freq, y * freq)) * amp;
        norm += amp;
        amp  *= p.gain;
        freq *= p.lacunarity;
    }
    return norm > 0.0f ? sum / norm : 0.0f;
}

void PerlinNoise::warp(float& x, float& y, float frequency, float strength) const {
    // Смещения берём в двух далеко разнесённых точках поля, чтобы dx и dy не
    // коррелировали: иначе искажение выродится в сдвиг вдоль диагонали.
    float dx = noise2(x * frequency,            y * frequency);
    float dy = noise2(x * frequency + 137.31f,  y * frequency - 91.77f);
    x += dx * strength;
    y += dy * strength;
}

float noiseTo01(float v){
    float t = (v + 1.0f) * 0.5f;
    return t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
}
