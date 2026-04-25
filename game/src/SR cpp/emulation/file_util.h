#ifndef FILE_UTIL_H
#define FILE_UTIL_H

#include <sstream>
#include <cstdint>

#include <zlib.h>
#include "common.h"

std::stringstream decode_gzip(const char* filename);

int32_t read_7_bit_encoded_int(std::istream& stream);
std::string read_string(std::istream& stream);
int32_t read_int32(std::istream& stream);
float read_float(std::istream& stream);

#endif