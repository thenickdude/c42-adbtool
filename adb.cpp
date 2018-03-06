#include <vector>
#include <iostream>

#include "adb.h"
#include "crypto.h"
#include "comparator.h"

#include "boost/filesystem/operations.hpp"
#include "boost/filesystem/string_file.hpp"

#ifdef _WIN32
#include <windows.h>
#include <wincrypt.h>
#include <dpapi.h>
#endif

#ifdef __APPLE__
#include <CoreFoundation/CFString.h>
#include <IOKit/IOKitLib.h>
#endif

// CrashPlan Home:
static const std::string STATIC_OBFUSCATION_KEY = "HWANToDk3L6hcXryaU95X6fasmufN8Ok";

static Code42AES256RandomIV aes256;

std::pair<std::string, std::string> makeMacPlatformIDFromSerial(const std::string &serial) {
    std::string output = serial + serial + serial + serial + "\n";

    return std::make_pair(output, output.substr(0, 32));
}

#ifdef __APPLE__

static std::pair<std::string, std::string> getMacPlatformID() {
    char buffer[64];

    io_registry_entry_t ioRegistryRoot = IORegistryEntryFromPath(kIOMasterPortDefault, "IOService:/");
    CFStringRef uuidCf = (CFStringRef) IORegistryEntryCreateCFProperty(ioRegistryRoot, CFSTR(kIOPlatformSerialNumberKey), kCFAllocatorDefault, 0);
    IOObjectRelease(ioRegistryRoot);
    
    if (!CFStringGetCString(uuidCf, buffer, sizeof(buffer), kCFStringEncodingMacRoman)) {
        throw std::runtime_error("Serial number too long for buffer");
    }
    
    CFRelease(uuidCf);
    
    return makeMacPlatformIDFromSerial(std::string(buffer));
}

#endif

std::pair<std::string, std::string> makeLinuxPlatformIDFromSerial(const std::string &serial) {
    return std::make_pair(serial, serial.substr(0, 32));
}

#ifndef _WIN32
#ifndef __APPLE__

static std::pair<std::string, std::string> getLinuxPlatformID() {
    std::string id1, id2;

    try {
        boost::filesystem::load_string_file(boost::filesystem::path("/var/lib/dbus/machine-id"), id1);
    } catch (...) {
    }

    try {
        boost::filesystem::load_string_file(boost::filesystem::path("/etc/machine-id"), id2);
    } catch (...) {
    }

    std::string serial = id1 + id2;
    
    return makeLinuxPlatformIDFromSerial(serial);
}

#endif
#endif

/**
 * Attempt decryption using DPAPI and return true if successful
 */
static bool deobfuscateWin32(const std::string &input, std::string &output) {
#ifdef _WIN32
    DATA_BLOB in, out;
    
    in.cbData = input.length();
    in.pbData = (BYTE*) input.data();
    
    if (CryptUnprotectData(&in, NULL, NULL, NULL, NULL, 0, &out)) {
        std::string result((const char *)out.pbData, out.cbData);
         
        LocalFree(out.pbData);
         
        output = result;
        
        return true;
    }
#endif

    return false;
}

/**
 * Attempt encryption using DPAPI and return true if successful
 */
static bool obfuscateWin32(const std::string &input, std::string &output) {
#ifdef _WIN32
    DATA_BLOB in, out;
    
    in.cbData = input.length();
    in.pbData = (BYTE*) input.data();
    
    if (CryptProtectData(&in, NULL, NULL, NULL, NULL, 0, &out)) {
        std::string result((const char *)out.pbData, out.cbData);
         
        LocalFree(out.pbData);
         
        output = result;
        
        return true;
    }
#endif

    return false;
}

/**
 * Attempts to choose an obfuscation key which matches the loaded database, either using the provided serials, by
 * reading them from the host, or using the fallback static key. 
 * 
 * @param macOSSerial - Optional, override generation of the key using this serial 
 * @param linuxSerial - Optional, override generation of the key using this serial
 */
std::string ADB::pickObfuscationKey(const std::string &macOSSerial, const std::string &linuxSerial) {
    // Fetch the machine ID to generate a machine-specific key: (new in version 1499922000650L)
    std::pair<std::string, std::string> platformID;

    if (macOSSerial.length() > 0) {
        platformID = makeMacPlatformIDFromSerial(macOSSerial);
    } else if (linuxSerial.length() > 0) {
        platformID = makeLinuxPlatformIDFromSerial(linuxSerial);
    } else {
#ifdef __APPLE__
        platformID = getMacPlatformID();
#else
    // Windows uses DPAPI instead
#ifndef _WIN32
        platformID = getLinuxPlatformID();
#endif
#endif
    }
    
    std::vector<std::string> candidates;
    
    if (platformID.first.length() >= 32) {
        candidates.push_back(generateSmallBusinessKeyV2(platformID.first, platformID.second));
    }
    
    candidates.push_back(STATIC_OBFUSCATION_KEY);
    
    // Figure out which key if any is correct
    for (std::string &candidate : candidates) {
        // First see if the dedicated sentinel value is present in the database and readable using that key:
        std::string valueEncrypted;
        leveldb::Status status = db->Get(leveldb::ReadOptions(), ADB_KEY_PREFIX "ACCESSIBLE_KEY", &valueEncrypted);

        if (status.ok()) {
            std::string accessibleValue;
            
            if (deobfuscateWin32(valueEncrypted, accessibleValue)) {
                // Win32 using DPAPI instead of an encryption key
                return "";
            }

            try {
                accessibleValue = aes256.decrypt(valueEncrypted, candidate);
            } catch (BadPaddingException &e) {
                continue;
            }
            
            if (accessibleValue == std::string(16, '\0')) {
                // Key must be correct since we decrypted to the correct sentinel value
                return candidate;
            }
        }
    }
    
    // Failing that, see if there is a key that can decrypt all values in the database
    //
    // (Identification is only probabilistic, since we only check for correct padding and at least 1/256 of these 
    // succeed with random keys)
    
    // We'll ignore the case where the database is empty since this should not happen in practice
    for (std::string &candidate : candidates) {
        bool success = true;
        leveldb::Iterator *it = db->NewIterator(leveldb::ReadOptions());

        for (it->SeekToFirst(); it->Valid(); it->Next()) {
            std::string valueEncrypted = it->value().ToString();
            std::string valueDecrypted;

            if (deobfuscateWin32(valueEncrypted, valueDecrypted)) {
                // Win32 using DPAPI instead of an encryption key
                return "";
            }

            try {
                aes256.decrypt(valueEncrypted, candidate);
            } catch (BadPaddingException &e) {
                success = false;
                break;
            }
        }

        success = success && it->status().ok();

        delete it;

        if (success) {
            return candidate;
        }
    }
    
    throw std::runtime_error("Failed to determine a working obfuscation key for the database, make sure your serial numbers are correct");
}

std::string ADB::deobfuscate(const std::string &value) {
    std::string result;
    
    if (deobfuscateWin32(value, result)) {
        return result;
    }
    
    try {
        return aes256.decrypt(value, obfuscationKey);
    } catch (BadPaddingException &e) {
    }

    throw std::runtime_error("Failed to deobfuscate values from ADB, bad serial number? "
         "Please see the readme for instructions.");
}

std::string ADB::obfuscate(const std::string &value) {
    if (obfuscationKey.empty()) {
        // Only permitted on Windows, where DPAPI will encrypt the value for us
        std::string output;
        
        if (obfuscateWin32(value, output)) {
            return output;
        }
        
        throw std::runtime_error("No obfuscation key available!");
    }
    
    return aes256.encrypt(value, obfuscationKey);
}

ADB::ADB(const std::string &adbPath, const std::string &macOSSerial, const std::string &linuxSerial) {
	leveldb::Options options;
	auto *comp = new Code42Comparator();

	options.create_if_missing = false;
	options.compression = leveldb::CompressionType::kNoCompression;
	options.comparator = comp;

	leveldb::Status status = leveldb::DB::Open(options, adbPath, &db);

	if (!status.ok()) {
		throw std::runtime_error(status.ToString());
	}

    obfuscationKey = pickObfuscationKey(macOSSerial, linuxSerial);
}

ADB::~ADB() {
    // Important that this gets called because LevelDB could have pending writes it needs to flush 
    delete db;
}

std::string ADB::readKey(const std::string &key) {
	std::string value;
	leveldb::Status status = db->Get(leveldb::ReadOptions(), key, &value);

	if (status.ok()) {
		return deobfuscate(value);
	} else {
		throw std::runtime_error("Failed to fetch " + key + ": " + status.ToString());
	}
}

void ADB::deleteKey(const std::string &key) {
    if (!db->Delete(leveldb::WriteOptions(), key).ok()) {
        throw std::runtime_error("Failed to delete " + key);
    }
}

void ADB::writeKey(const std::string &key, const std::string &value) {
    leveldb::Status status = db->Put(leveldb::WriteOptions(), key, obfuscate(value));

    if (!status.ok()) {
        throw std::runtime_error("Failed to write to " + key + ": " + status.ToString());
    }
}

bool ADB::readAllKeys(std::vector<std::string> &result) {
	leveldb::Iterator *it = db->NewIterator(leveldb::ReadOptions());

	for (it->SeekToFirst(); it->Valid(); it->Next()) {
		result.push_back(it->key().ToString());
	}

	bool success = it->status().ok();

	delete it;

	return success;
}

bool ADB::readAllEntries(std::vector<std::pair<std::string, std::string>> &result) {
    leveldb::Iterator *it = db->NewIterator(leveldb::ReadOptions());

    for (it->SeekToFirst(); it->Valid(); it->Next()) {
        /*
         * We avoid calling decryptAndPrintValueForKey here, because if our comparator is wrong then we expect iteration
         * to find keys that ->Get() can't see.
         */
        result.push_back(std::pair<std::string,std::string>(it->key().ToString(), deobfuscate(it->value().ToString())));
    }

    bool success = it->status().ok();

    delete it;

    return success;
}