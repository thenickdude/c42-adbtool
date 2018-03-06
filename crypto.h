#pragma once

#include <string>
#include <stdexcept>

class BadPaddingException : public std::runtime_error {
public:
	BadPaddingException() : std::runtime_error("Bad padding") {
	}
};

class Code42Cipher {
public:
	virtual std::string decrypt(const std::string & cipherText, const std::string & key) const = 0;
    virtual std::string encrypt(const std::string & plainText, const std::string & key) const = 0;
};


class Code42AES256RandomIV : public Code42Cipher {
public:
	std::string decrypt(const std::string & cipherText, const std::string & key) const override;
    std::string encrypt(const std::string & plainText, const std::string & key) const override;
};

std::string generateSmallBusinessKeyV2(const std::string &passphrase, const std::string &salt);