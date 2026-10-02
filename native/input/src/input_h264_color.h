#pragma once
#include <cstddef>
#include <cstdint>
namespace hvinput {
// Values matrix/range use the input contract; remaining values retain H.264 VUI domain.
struct H264Color { uint32_t matrix=0, range=0, primaries=0, transfer=0, matrix_code=0; bool present=false; };
bool ParseH264Color(const uint8_t* data,size_t size,H264Color& result) noexcept;
}
