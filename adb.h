#pragma once

#include <vector>
#include <utility>
#include <string>

#include "leveldb/db.h"

// Keys in CrashPlan ADB databases start with this byte, so be sure to include 
// it in any read or write operations or else CrashPlan won't see the values
#define ADB_KEY_PREFIX "\x01"

class ADB {
private:
    leveldb::DB *db;
    std::string obfuscationKey;
    
    std::string deobfuscate(const std::string &value);
    std::string obfuscate(const std::string &value);

    std::string pickObfuscationKey(const std::string &macOSSerial, const std::string &linuxSerial);

public:
    ADB(const std::string &adbPath, const std::string &macOSSerial, const std::string &linuxSerial);
    
    ~ADB();

    std::string readKey(const std::string &key);
    void writeKey(const std::string &key, const std::string &value);
    void deleteKey(const std::string &key);

    bool readAllKeys(std::vector<std::string> &result);
    bool readAllEntries(std::vector<std::pair<std::string, std::string>> &result);
};