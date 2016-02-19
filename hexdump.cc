#include "hexdump.h"

// TODO FIXME: can't find the idiom of string-copy to cursor in
// standard c
static char *append(char *s1, const char *s2) {
    strcpy(s1, s2);
    return s1 + strlen(s2);
}

char *hexdump::formatLine(char line[80], size_t *offset) const {
    char* cursor = &line[0];
    const int bytesPerChunk = 16;
    const int chunkSize = std::min(mLen - *offset, (size_t) bytesPerChunk);
    const uint8_t *base = &mData[*offset];

    cursor += sprintf(cursor, "%08x  ", (int32_t) *offset);

    int i;
    for(i = 0; i < chunkSize && i < (bytesPerChunk / 2); ++i){
        cursor += sprintf(cursor, "%.2x ", base[i]);
    }
    for(; i < chunkSize; ++i){
        cursor += sprintf(cursor, " %.2x", base[i]);
    }
    for (; i < bytesPerChunk; ++i) {
        cursor = append(cursor, "   ");
    }
    cursor = append(cursor, " |");
    for (i = 0; i < chunkSize; i++) {
        *cursor++ = isprint(base[i]) ? base[i] : '.';
    }
    cursor = append(cursor, "|");

    *offset += chunkSize;
    return &line[0];
}
