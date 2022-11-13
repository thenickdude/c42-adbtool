#include <fstream>
#include <iostream>
#include <cstdlib>
#include <ctype.h>
#include <thread>
#include <atomic>
#include <string>

#include "boost/algorithm/hex.hpp"
#include "boost/algorithm/string.hpp"
#include "boost/program_options.hpp"
#include "boost/filesystem/operations.hpp"
#include "boost/filesystem/string_file.hpp"

#include "common.h"
#include "adb.h"

#ifdef _WIN32
// For SHGetKnownFolderPath
#include <shlobj.h>
#include <objbase.h>
#include <Knownfolders.h>
#endif

enum ValueFormat {
    VF_RAW,
    VF_HEX
};

namespace po = boost::program_options;

std::istream& operator>>(std::istream& in, ValueFormat& format) {
    std::string token;

    in >> token;

    if (token == "raw") {
        format = ValueFormat::VF_RAW;
    } else if (token == "hex") {
        format = ValueFormat::VF_HEX;
    } else {
        in.setstate(std::ios_base::failbit);
    }

    return in;
}

std::ostream& operator<<(std::ostream& out, const ValueFormat& format) {
    switch (format) {
        case ValueFormat::VF_RAW:
            out << "raw";
            break;
        case ValueFormat::VF_HEX:
            out << "hex";
            break;
        default:
            out.setstate(std::ios_base::failbit);
    }
    
    return out;
}

std::vector<boost::filesystem::path> getPlatformDatabasePaths(const std::string &dirName) {
    std::vector<boost::filesystem::path> result;
    
#ifdef __APPLE__
    result.push_back(boost::filesystem::path(getenv("HOME")) / ("Library/Application Support/CrashPlan/conf/" + dirName));
    result.push_back(boost::filesystem::path("/Library/Application Support/CrashPlan/conf/" + dirName));
#else
    #ifdef _WIN32
        for (REFKNOWNFOLDERID folderID : {FOLDERID_LocalAppData, FOLDERID_ProgramData}) {
            PWSTR appData;
        
            if (SHGetKnownFolderPath(
                folderID,
                0,
                NULL,
                &appData    
            ) == S_OK) {
                result.push_back(boost::filesystem::path(appData) / ("CrashPlan/conf/" + dirName));
            }
            
            CoTaskMemFree(appData);
        }
    #else
        // Linux
        result.push_back(boost::filesystem::path("/usr/local/crashplan/conf/" + dirName));
    #endif
#endif
        
    return result;
}

std::string trimADBKeyPrefix(const std::string &key) {
    assert(key[0] == ADB_KEY_PREFIX[0]);

    return key.substr(1);
}

void commandListEntries(ADB *adb) {
    std::vector<std::pair<std::string, std::string>> values;

    adb->readAllEntries(values);

    for (auto pair : values) {
        bool printable = true;

        for (int i = 0; i < pair.second.length(); i++) {
            if (!isprint(pair.second[i]) && !isspace(pair.second[i])) {
                printable = false;
                break;
            }
        }

        if (printable) {
            std::cout << trimADBKeyPrefix(pair.first) << " = " << pair.second << std::endl;
        } else {
            std::cout << trimADBKeyPrefix(pair.first) << " (hex) = " << binStringToHex(pair.second) << std::endl;
        }
    }
}

void commandListKeys(ADB *adb) {
    std::vector<std::string> values;

    adb->readAllKeys(values);

    for (auto key : values) {
        std::cout << trimADBKeyPrefix(key) << std::endl;
    }
}

std::string commandReadKey(ADB *adb, const std::string &key, ValueFormat format) {
    std::string result(adb->readKey(ADB_KEY_PREFIX + key));

    switch (format) {
        case VF_RAW:
            return result;

        case VF_HEX:
            return binStringToHex(result);
    }
    
    throw std::runtime_error("Unsupported output format");
}

void commandWriteKey(ADB *adb, const std::string &key, const std::string &value, ValueFormat format) {
    std::string finalValue;

    if (format == VF_HEX) {
        finalValue = value;
        boost::trim(finalValue);
        finalValue = hexStringToBin(finalValue);
    } else {
        finalValue = value;
    }

    adb->writeKey(ADB_KEY_PREFIX + key, finalValue);
}

void commandDeleteKey(ADB *adb, const std::string &key) {
    adb->deleteKey(ADB_KEY_PREFIX + key);
}

int main(int argc, char **argv) {
    po::options_description mainOptions("Options");
    mainOptions.add_options()
        ("help", "shows this page")
        ( "adb", "operate on the adb database (default)")
        ( "udb", "operate on the udb database")
        ("path", po::value<std::string>(),
            "path to CrashPlan's 'adb' or 'udb' directory to operate on (omit to locate automatically)")
        ("mac-serial", po::value<std::string>(),
            "serial number of the Mac that matches the adb directory (for CrashPlan Small Business, optional)")
        ("linux-serial", po::value<std::string>(),
            "serial number of the Linux machine that matches the adb directory (for CrashPlan Small Business, optional)")
        ;

    po::options_description readWriteOptions("Read/write command options");
    readWriteOptions.add_options()
        ("key", po::value<std::string>(), "key to read/write from (required)")
        ("value", po::value<std::string>(), "value to write (optional, omit to read from stdin)")
        ("value-file", po::value<std::string>(), "file to read/write value from instead of supplying directly (optional)")
        ("format", po::value<ValueFormat>()->default_value(ValueFormat::VF_RAW), "encoding for read/write values ('raw', 'hex')")
        ;

    po::options_description hiddenOptions("Hidden options");
    hiddenOptions.add_options()
        ("command", po::value<std::string>(), "command to run");
    
    po::positional_options_description positionalOptions;
    positionalOptions.add("command", 1);

    po::options_description visibleOptions;
    visibleOptions.add(mainOptions).add(readWriteOptions);

    po::options_description allOptions;
    allOptions.add(mainOptions).add(readWriteOptions).add(hiddenOptions);

    po::variables_map vm;

    try {
        po::store(po::command_line_parser(argc, argv)
            .options(allOptions).positional(positionalOptions).run(), vm);
        po::notify(vm);
    } catch (std::exception &e) {
        std::cerr << "Error parsing options: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    if (vm.count("help") || !vm.count("command")) {
        std::cout << "Read and modify Code42/CrashPlan UDB and ADB databases" << std::endl << std::endl;
        std::cout << "Usage: c42-adbtool <command> [--options]" << std::endl;
        std::cout << visibleOptions << std::endl;
        std::cout << "Commands:" << std::endl;
        std::cout << "  read      - Read the value of a key" << std::endl;
        std::cout << "  write     - Write a value to a key" << std::endl;
        std::cout << "  delete    - Delete a key" << std::endl;
        std::cout << "  list      - List all keys and values in the database" << std::endl;
        std::cout << "  list-keys - List all keys in the database" << std::endl;
        return EXIT_FAILURE;
    }

    boost::filesystem::path adbPath;

    if (vm.count("path")) {
        adbPath = vm["path"].as<std::string>();
    }

    if (adbPath.empty()) {
        std::string dirName = vm.count("udb") ? "udb" : "adb";
        
        for (auto &path : getPlatformDatabasePaths(dirName)) {
            if (boost::filesystem::is_directory(path)) {
                adbPath = path;
                break;
            }
        }
    }

    if (adbPath.empty()) {
        std::cerr << "Couldn't find your ADB path automatically, supply a --path option instead" << std::endl;
        return EXIT_FAILURE;
    }
    
    ADB *adb;
    
    try {
        adb = new ADB(
            adbPath.string(), 
            vm.count("mac-serial") ? vm["mac-serial"].as<std::string>() : "",
            vm.count("linux-serial") ? vm["linux-serial"].as<std::string>() : ""
        );
    } catch (std::runtime_error &e) {
        std::cerr << "Failed to open ADB database (" + adbPath.string() + "):" << std::endl;
        std::cerr << e.what() << std::endl << std::endl;
#ifdef WIN32
        std::cerr << "You may need to run using PsExec to have enough permission to read that directory (see the Readme)." << std::endl << std::endl;
#else
        std::cerr << "You may need to run using sudo to have enough permission to read that directory." << std::endl << std::endl;
#endif
        std::cerr << "Also check that the CrashPlan service is not running (it holds a lock on ADB), try one of these:" << std::endl << std::endl;
        std::cerr << "  macOS   - sudo launchctl unload /Library/LaunchDaemons/com.code42.service.plist" << std::endl;
        std::cerr << "  Windows - net stop \"Code42 Service\"" << std::endl;
        std::cerr << "  Linux   - sudo /usr/local/crashplan/bin/service.sh stop" << std::endl;
        std::cerr << "  Other   - https://support.code42.com/Incydr/Agent/Troubleshooting/Stop_and_start_the_Code42_app_service" << std::endl;

        return EXIT_FAILURE;
    }

    if (vm["command"].as<std::string>() == "list") {
        commandListEntries(adb);

        delete adb;

        return EXIT_SUCCESS;
    }

    if (vm["command"].as<std::string>() == "list-keys") {
        commandListKeys(adb);

        delete adb;

        return EXIT_SUCCESS;
    }

    if (vm["command"].as<std::string>() == "read" && vm.count("key") > 0) {
        std::string value = commandReadKey(adb, vm["key"].as<std::string>(), vm["format"].as<ValueFormat>());

        if (vm.count("value-file") > 0) {
            boost::filesystem::save_string_file(vm["value-file"].as<std::string>(), value);
        } else {
            std::cout << value;
        }
        
        delete adb;

        return EXIT_SUCCESS;
    }

    if (vm["command"].as<std::string>() == "write" && vm.count("key") > 0) {
        std::string value;

        if (vm.count("value") > 0) {
            value = vm["value"].as<std::string>();
        } else if (vm.count("value-file") > 0) {
            boost::filesystem::load_string_file(vm["value-file"].as<std::string>(), value);
        } else {
            // Read from stdin
            std::ostringstream ss;
            ss << std::cin.rdbuf();

            value = ss.str();
        }

        commandWriteKey(adb, vm["key"].as<std::string>(), value, vm["format"].as<ValueFormat>());

        delete adb;

        return EXIT_SUCCESS;
    }

    if (vm["command"].as<std::string>() == "delete" && vm.count("key") > 0) {
        commandDeleteKey(adb, vm["key"].as<std::string>());

        delete adb;

        return EXIT_SUCCESS;
    }

    std::cerr << "Missing required arguments, use --help for syntax" << std::endl;

    delete adb;

    return EXIT_FAILURE;
}