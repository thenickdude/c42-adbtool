#include "crypto.h"

#include "cryptopp/aes.h"
#include "cryptopp/modes.h"
#include "cryptopp/sha.h"
#include "cryptopp/filters.h"
#include "cryptopp/pwdbased.h"
#include "cryptopp/osrng.h"

/**
 * Decrypt a value using AES-256 CBC, where the first block is the message IV, and verify the PKCS#5 message padding 
 * is correct.
 * 
 * @throws BadPaddingException if padding is bad or input is the wrong size
 */
std::string Code42AES256RandomIV::decrypt(const std::string & cipherText, const std::string &key) const {
    // We expect the encrypted value to start with an IV and be padded to a full block size (padding)
    if (cipherText.length() < CryptoPP::AES::BLOCKSIZE * 2 || cipherText.length() % CryptoPP::AES::BLOCKSIZE != 0) {
        throw BadPaddingException();
    }

    // The first block of the input is the random IV:
    const CryptoPP::byte *iv = (const CryptoPP::byte *) cipherText.data();
    // And the remainder is ciphertext:
    const CryptoPP::byte *encrypted = (const CryptoPP::byte *) cipherText.data() + CryptoPP::AES::BLOCKSIZE;
    int encryptedSize = cipherText.length() - CryptoPP::AES::BLOCKSIZE;

    uint8_t *buffer = new uint8_t[encryptedSize];

    CryptoPP::CBC_Mode<CryptoPP::AES>::Decryption decryptor((const CryptoPP::byte *)key.data(), 256 / 8, iv);

    decryptor.ProcessData(buffer, encrypted, encryptedSize);

    // Verify padding is correct after decryption:
    uint8_t padByte = buffer[encryptedSize - 1];

    if (padByte <= 0 || padByte > CryptoPP::AES::BLOCKSIZE) {
        throw BadPaddingException();
    }

    for (int i = 1; i < padByte; i++) {
        if (buffer[encryptedSize - 1 - i] != padByte) {
            throw BadPaddingException();
        }
    }

    int unpaddedLength = encryptedSize - padByte;

    std::string result((const char *) buffer, unpaddedLength);

    delete[] buffer;

    return result;
}

std::string Code42AES256RandomIV::encrypt(const std::string & plainText, const std::string & key) const {
    CryptoPP::AutoSeededRandomPool prng;
    CryptoPP::byte iv[CryptoPP::AES::BLOCKSIZE];
    
    prng.GenerateBlock(iv, sizeof(iv));

    CryptoPP::CBC_Mode<CryptoPP::AES>::Encryption e((const CryptoPP::byte *)key.data(), 256 / 8, iv);

    // First block of output is the IV:
    std::string result((const char *) iv, sizeof(iv));
    
    // Followed by the ciphertext:
    CryptoPP::StringSource ss( 
        plainText, 
        true,
        new CryptoPP::StreamTransformationFilter( 
            e, 
            new CryptoPP::StringSink(result), 
            CryptoPP::BlockPaddingSchemeDef::PKCS_PADDING
        )      
    ); 
    
    return result;
}

std::string generateSmallBusinessKeyV2(const std::string &passphrase, const std::string &salt) {
    CryptoPP::PKCS5_PBKDF2_HMAC<CryptoPP::SHA512> generator;
    CryptoPP::byte derived[32];
    
    generator.DeriveKey(
        derived, sizeof(derived), 0, 
        (const CryptoPP::byte*) passphrase.data(), passphrase.length(),
        (const CryptoPP::byte*) salt.data(), salt.length(), 
        10000
    );
    
    return std::string((const char *)derived, sizeof(derived));
}