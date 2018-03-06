#define __STDC_FORMAT_MACROS
#define _POSIX_C_SOURCE 200112L
#define _FILE_OFFSET_BITS 64

#include "common.h"

#include "cryptopp/modes.h"

uint8_t hexNibbleToInt(char n) {
	if (n >= '0' && n <= '9') {
		return (uint8_t) (n - '0');
	}
	if (n >= 'a' && n <= 'f') {
		return (uint8_t) (n - 'a' + 10);
	}
    if (n >= 'A' && n <= 'F') {
        return (uint8_t) (n - 'A' + 10);
    }
    
    throw std::runtime_error("Bad hex digit");
}

char intToHexNibble(int i) {
	if (i < 10) {
		return (char) (i + '0');
	}
	return (char) ((i - 10) + 'A');
}

std::string hexStringToBin(std::string input) {
    if (input.length() % 2 != 0) {
        throw std::runtime_error("Hex string length must be a multiple of two");
    }
    
	int outputLength = input.length() / 2;
	char *buffer = new char[outputLength];
	int dest = 0;
	int source = 0;

	for (; source < input.length(); dest++, source += 2) {
		buffer[dest] = (hexNibbleToInt(input[source]) << 4) | hexNibbleToInt(input[source + 1]);
	}

	std::string result(buffer, outputLength);

	delete [] buffer;

	return result;
}

std::string binStringToHex(std::string input) {
	char *buffer = new char[input.length() * 2 + 1];
	int dest = 0;
	int source = 0;

	for (; source < input.length(); dest += 2, source++) {
		buffer[dest] = intToHexNibble(((uint8_t) input[source]) >> 4);
		buffer[dest + 1] = intToHexNibble(((uint8_t) input[source]) & 0x0F);
	}

	buffer[dest] = '\0';

	std::string result(buffer);

	delete [] buffer;

	return result;
}
