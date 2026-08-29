#include "Text.h"

size_t utf8Length(const std::string& s){
    size_t count = 0;
    for(unsigned char c : s){
        // Продолжения многобайтовой последовательности имеют вид 10xxxxxx — их не считаем.
        if((c & 0xC0) != 0x80) ++count;
    }
    return count;
}

std::string padRightUtf8(const std::string& s, size_t width){
    size_t len = utf8Length(s);
    if(len >= width) return s;
    return s + std::string(width - len, ' ');
}
