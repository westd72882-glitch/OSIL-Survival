#include "PngWriter.h"

#include <cstdio>
#include <cstring>

namespace {

uint32_t crcTable[256];
bool crcTableReady = false;

void makeCrcTable(){
    for(uint32_t n = 0; n < 256; ++n){
        uint32_t c = n;
        for(int k = 0; k < 8; ++k) c = (c & 1) ? (0xedb88320u ^ (c >> 1)) : (c >> 1);
        crcTable[n] = c;
    }
    crcTableReady = true;
}

uint32_t crc32Of(const uint8_t* data, size_t len, uint32_t start = 0xffffffffu){
    if(!crcTableReady) makeCrcTable();
    uint32_t c = start;
    for(size_t i = 0; i < len; ++i) c = crcTable[(c ^ data[i]) & 0xff] ^ (c >> 8);
    return c;
}

uint32_t adler32Of(const uint8_t* data, size_t len){
    uint32_t a = 1, b = 0;
    for(size_t i = 0; i < len; ++i){
        a = (a + data[i]) % 65521;
        b = (b + a) % 65521;
    }
    return (b << 16) | a;
}

void pushBE32(std::vector<uint8_t>& out, uint32_t v){
    out.push_back((uint8_t)(v >> 24)); out.push_back((uint8_t)(v >> 16));
    out.push_back((uint8_t)(v >> 8));  out.push_back((uint8_t)v);
}

void writeChunk(std::vector<uint8_t>& out, const char type[4], const std::vector<uint8_t>& data){
    pushBE32(out, (uint32_t)data.size());
    size_t crcStart = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), data.begin(), data.end());
    uint32_t crc = crc32Of(&out[crcStart], out.size() - crcStart) ^ 0xffffffffu;
    pushBE32(out, crc);
}

} // namespace

bool writePng(const std::string& path, int width, int height, const std::vector<uint8_t>& rgb){
    if(width <= 0 || height <= 0) return false;
    if(rgb.size() != (size_t)width * height * 3) return false;

    // ---- Сырые данные изображения: каждая строка предваряется байтом фильтра (0 — без фильтра).
    std::vector<uint8_t> raw;
    raw.reserve((size_t)height * (1 + (size_t)width * 3));
    for(int y = 0; y < height; ++y){
        raw.push_back(0);
        const uint8_t* row = &rgb[(size_t)y * width * 3];
        raw.insert(raw.end(), row, row + (size_t)width * 3);
    }

    // ---- zlib-поток из несжатых deflate-блоков (не более 65535 байт каждый).
    std::vector<uint8_t> z;
    z.push_back(0x78); z.push_back(0x01); // CMF/FLG: deflate, окно 32К, без словаря
    size_t pos = 0;
    while(pos < raw.size()){
        size_t chunk = raw.size() - pos;
        if(chunk > 65535) chunk = 65535;
        bool last = (pos + chunk) >= raw.size();
        z.push_back(last ? 1 : 0);
        z.push_back((uint8_t)(chunk & 0xff));
        z.push_back((uint8_t)((chunk >> 8) & 0xff));
        z.push_back((uint8_t)((~chunk) & 0xff));        // LEN и его дополнение — требование формата
        z.push_back((uint8_t)(((~chunk) >> 8) & 0xff));
        z.insert(z.end(), raw.begin() + pos, raw.begin() + pos + chunk);
        pos += chunk;
    }
    pushBE32(z, adler32Of(raw.data(), raw.size()));

    // ---- Сборка файла.
    std::vector<uint8_t> png = { 0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a };

    std::vector<uint8_t> ihdr;
    pushBE32(ihdr, (uint32_t)width);
    pushBE32(ihdr, (uint32_t)height);
    ihdr.push_back(8);   // 8 бит на канал
    ihdr.push_back(2);   // цветовой тип 2 — truecolor RGB
    ihdr.push_back(0);   // сжатие: deflate
    ihdr.push_back(0);   // фильтрация: стандартная
    ihdr.push_back(0);   // без чересстрочности
    writeChunk(png, "IHDR", ihdr);
    writeChunk(png, "IDAT", z);
    writeChunk(png, "IEND", {});

    FILE* f = fopen(path.c_str(), "wb");
    if(!f) return false;
    size_t written = fwrite(png.data(), 1, png.size(), f);
    fclose(f);
    return written == png.size();
}
