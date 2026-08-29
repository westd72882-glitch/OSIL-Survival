#include "Random.h"

#include <cstdlib>
#include <cstring>

uint64_t splitMix64(uint64_t x){
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

Rng::Rng(uint64_t seed, uint64_t sequence){
    state = 0;
    inc = (sequence << 1u) | 1u; // поток обязан быть нечётным
    nextU32();
    state += splitMix64(seed);
    nextU32();
}

uint32_t Rng::nextU32(){
    uint64_t old = state;
    state = old * 6364136223846793005ULL + inc;
    uint32_t xorshifted = (uint32_t)(((old >> 18u) ^ old) >> 27u);
    uint32_t rot = (uint32_t)(old >> 59u);
    return (xorshifted >> rot) | (xorshifted << ((32u - rot) & 31u));
}

uint64_t Rng::nextU64(){
    uint64_t hi = nextU32();
    return (hi << 32) | nextU32();
}

float Rng::nextFloat(){
    // 24 бита мантиссы float: берём старшие биты, младшие всё равно потерялись бы.
    return (float)(nextU32() >> 8) * (1.0f / 16777216.0f);
}

float Rng::nextRange(float lo, float hi){
    return lo + (hi - lo) * nextFloat();
}

uint32_t Rng::nextBelow(uint32_t bound){
    if(bound == 0) return 0;
    // Отбрасываем значения из неполного «хвоста», иначе младшие остатки выпадают чаще.
    uint32_t threshold = (uint32_t)(-(int32_t)bound) % bound;
    for(;;){
        uint32_t r = nextU32();
        if(r >= threshold) return r % bound;
    }
}

int Rng::nextInt(int lo, int hi){
    if(hi <= lo) return lo;
    return lo + (int)nextBelow((uint32_t)(hi - lo + 1));
}

bool Rng::chance(float p){
    if(p <= 0.0f) return false;
    if(p >= 1.0f) return true;
    return nextFloat() < p;
}

uint64_t hashCoords(int64_t x, int64_t z, uint64_t seed, uint64_t salt){
    uint64_t h = seed ^ (salt * 0x9e3779b97f4a7c15ULL);
    h = splitMix64(h ^ (uint64_t)(x * 0x2545f4914f6cdd1dULL));
    h = splitMix64(h ^ (uint64_t)(z * 0x9e3779b97f4a7c15ULL));
    return h;
}

Rng rngForCell(int64_t x, int64_t z, uint64_t seed, uint64_t salt){
    uint64_t h = hashCoords(x, z, seed, salt);
    return Rng(h, h | 1ULL);
}

uint64_t seedFromString(const char* text){
    if(!text || !*text) return 0;
    // Чисто числовой сид оставляем как есть — так его удобно передавать между
    // сервером и утилитой предпросмотра карты.
    char* end = nullptr;
    unsigned long long numeric = strtoull(text, &end, 10);
    if(end && *end == '\0') return (uint64_t)numeric;

    uint64_t h = 0xcbf29ce484222325ULL; // FNV-1a по байтам, затем перемешивание
    for(const char* p = text; *p; ++p){
        h ^= (uint64_t)(unsigned char)*p;
        h *= 0x100000001b3ULL;
    }
    return splitMix64(h);
}
