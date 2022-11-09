#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void compress_dxt1 (uint8_t* out_buffer, const uint8_t* src_buffer, int32_t width, int32_t height, int32_t channels);
void compress_dxt5 (uint8_t* out_buffer, const uint8_t* src_buffer, int32_t width, int32_t height, int32_t channels);

// Level data consists of 6 int32s per level
void build_mipmaps(uint8_t* out_buffer, uint8_t* src_buffer, int32_t levelsCount, const int32_t* levels);

#ifdef __cplusplus
}
#endif