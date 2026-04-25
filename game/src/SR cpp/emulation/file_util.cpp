#include <iterator>
#include <string>
#include <array>
#include <bit>

#include "file_util.h"

std::stringstream decode_gzip(const char* filename)
{
	gzFile in_file = gzopen(filename, "rb");

	if (in_file == NULL)
		throw std::invalid_argument{ "File not found!" };

	std::array<char, 8192> unzip_buffer{};
	int unzipped_bytes;
	std::stringstream unzipped_data;
	while (true)
	{
		unzipped_bytes = gzread(in_file, unzip_buffer.data(), (unsigned int)unzip_buffer.size());
		if (unzipped_bytes > 0)
			unzipped_data.write(unzip_buffer.data(), unzipped_bytes);
		else
			break;
	}
	gzclose(in_file);

	unzipped_data.seekg(0, std::ios::beg);
	return unzipped_data;
}

int32_t read_7_bit_encoded_int(std::istream& stream)
{
	static_assert(CHAR_BIT == 8);
	
	int32_t num = 0;
	int32_t shift = 0;
	uint8_t b = 0;
	do
	{
		b = static_cast<uint8_t>(stream.get());
		num |= (b & 0x7F) << shift;
		shift += 7;
	} while ((b & 0x80) != 0);
	return num;
}

std::string read_string(std::istream& stream)
{
	int length = read_7_bit_encoded_int(stream);
	std::istreambuf_iterator<char> it{ stream };
	std::string string(length, '\0');
	if (length > 0)
	{
		std::copy_n(it, length, string.begin());
		stream.ignore();
	}
	return string;
}

int32_t read_int32(std::istream& stream)
{
	static_assert(CHAR_BIT == 8);

	int32_t b1 = static_cast<int32_t>(stream.get());
	int32_t b2 = static_cast<int32_t>(stream.get());
	int32_t b3 = static_cast<int32_t>(stream.get());
	int32_t b4 = static_cast<int32_t>(stream.get());
	
	return b1 | (b2 << 8) | (b3 << 16) | (b4 << 24);
}

float read_float(std::istream& stream)
{
	return std::bit_cast<float>(read_int32(stream));
}