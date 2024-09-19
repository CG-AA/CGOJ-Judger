#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>
#include <microhttpd.h>
#include <spdlog/spdlog.h>
#include <iostream>
#include <cstdlib>
#include <future>
#include <cryptopp/aes.h>
#include <cryptopp/filters.h>
#include <cryptopp/hex.h>
#include <cryptopp/modes.h>
#include <cryptopp/osrng.h>
#include <cryptopp/secblock.h>
#include <cryptopp/base64.h>

struct MHD_Daemon *server;
std::promise<void> promise;

nlohmann::json settings;

std::vector<bool> pendingPool;
int currentPool = 0;
std::mutex poolMutex;

void loadSettings() {
    std::ifstream file("./settings.json");
    if (!file.is_open()) {
        spdlog::error("Failed to open settings.json");
        throw std::runtime_error("Failed to open settings.json");
    }
    try {
        file >> settings;
    } catch (const std::exception &e) {
        spdlog::error("Failed to parse settings.json: {}", e.what());
        throw;
    }
    file.close();
}

std::string decrypt(const std::string& ciphertext, const std::string& key) {
    using namespace CryptoPP;

    std::string plaintext;
    SecByteBlock keyBlock(reinterpret_cast<const byte*>(key.data()), key.size());

    // Extract the IV from the ciphertext
    // stored the first block
    // used with sheared secret key to de/encrypt the rest of the message
    // to increase security(pervent the same message from having the same ciphertext)
    std::string ivStr = ciphertext.substr(0, AES::BLOCKSIZE);
    std::string encStr = ciphertext.substr(AES::BLOCKSIZE);

    // Decrypt
    try {
        CBC_Mode<AES>::Decryption decryption;
        decryption.SetKeyWithIV(keyBlock, keyBlock.size(), reinterpret_cast<const byte*>(ivStr.data()));

        StringSource(encStr, true,
            new StreamTransformationFilter(decryption,
                new StringSink(plaintext)
            ) // StreamTransformationFilter
        ); // StringSource
    } catch (const Exception& e) {
        spdlog::error("Decryption error: {}", e.what());
        throw;
    }

    return plaintext;
}

std::string encrypt(const std::string& plaintext, const std::string& key) {
    using namespace CryptoPP;

    std::string ciphertext;
    SecByteBlock keyBlock(reinterpret_cast<const byte*>(key.data()), key.size());

    // Generate a random IV
    AutoSeededRandomPool prng;
    byte iv[AES::BLOCKSIZE];
    prng.GenerateBlock(iv, sizeof(iv));

    // Encrypt
    try {
        CBC_Mode<AES>::Encryption encryption;
        encryption.SetKeyWithIV(keyBlock, keyBlock.size(), iv);

        StringSource(plaintext, true,
            new StreamTransformationFilter(encryption,
                new StringSink(ciphertext)
            ) // StreamTransformationFilter
        ); // StringSource
    } catch (const Exception& e) {
        spdlog::error("Encryption error: {}", e.what());
        throw;
    }

    // Prepend the IV to the ciphertext
    std::string result(reinterpret_cast<const char*>(iv), AES::BLOCKSIZE);
    result += ciphertext;

    return result;
}

std::string executeCommand(const std::string &command) {
    spdlog::debug("Executing command:{} ", command);
    std::array<char, 128> buffer;
    std::string result;
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(command.c_str(), "r"), pclose);
    if (!pipe) {
        spdlog::error("Failed to open pipe");
        throw std::runtime_error("Failed to open pipe");
    }
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result.append(buffer.data());
    }
    return result;
}

// @param program: the program itself
// compile the program, store binary in /dev/shm/CGJG/Cpp/pendingBin/ and return the path
std::string compileCpp(const std::string &program) {
    std::string pathTemplate = "/dev/shm/CGJG/Cpp/pendingBin/XXXXXX";
    char tempPath[pathTemplate.size() + 1];
    std::copy(pathTemplate.begin(), pathTemplate.end(), tempPath);
    tempPath[pathTemplate.size()] = '\0';

    int fd = mkstemp(tempPath);
    if (fd == -1) {
        throw std::runtime_error("Failed to create temporary file");
    }
    close(fd);

    std::string command = "g++ -o " + std::string(tempPath) + " " + program + "-Os -ffunction-sections -fdata-sections -lstdc++ -s -fno-ident -fno-asynchronous-unwind-tables -std=c++17";
    int result = system(command.c_str());
    if (result != 0) {
        throw std::runtime_error("Compilation failed");
    }

    return std::string(tempPath);
}

int selectPool() {
    std::lock_guard<std::mutex> lock(poolMutex);
    int boxId = currentPool;
    pendingPool[currentPool] = true;

    if (++currentPool == pendingPool.size()) {
        currentPool = 0;
    }
    if (pendingPool[currentPool]) {
        // Decide the next pool in a detached thread
        std::thread([]() {
            std::lock_guard<std::mutex> lock(poolMutex);
            do {
                if (++currentPool == pendingPool.size()) {
                    currentPool = 0;
                }
            } while (pendingPool[currentPool]);
        }).detach();
    }

    return boxId;
}

std::string findInMeta(const std::string &metaPath, const std::string &key) {
    std::ifstream metaFile(metaPath);
    if (!metaFile.is_open()) {
        spdlog::error("Failed to open meta file");
        throw std::runtime_error("Failed to open meta file");
    }

    std::string line;
    while (std::getline(metaFile, line)) {
        std::istringstream lineStream(line);
        std::string currentKey;
        if (std::getline(lineStream, currentKey, ':')) {
            std::string value;
            if (std::getline(lineStream, value)) {
                if (currentKey == key) {
                    return value;
                }
            }
        }
    }

    spdlog::error("Key not found in meta file: {}", key);
    throw std::runtime_error("Key not found in meta file");
}

bool isSpecialChar(char c) {
    return !std::isalnum(c) && !std::isspace(c);
}

std::string removeSpecialChars(const std::string &str) {
    std::string result = str;
    result.erase(std::remove_if(result.begin(), result.end(), isSpecialChar), result.end());
    return result;
}


nlohmann::json runTestCase(const std::string &binPath, const nlohmann::json &testCase, const bool noSC) {
    typedef std::string sstr;
    sstr cuttedPath;
    size_t pos = binPath.find_last_of('/');
    if (pos != sstr::npos) {
        cuttedPath = binPath.substr(0, pos);
    } else {
        cuttedPath = ".";
    }

    // create in/out/err/meta files
    sstr inPath = cuttedPath + "/in.txt";
    sstr outPath = cuttedPath + "/out.txt";
    sstr errPath = cuttedPath + "/err.txt";
    sstr metaPath = cuttedPath + "/meta.txt";

    try {
        std::ofstream inFile(inPath);
        std::ofstream outFile(outPath);
        std::ofstream errFile(errPath);
        std::ofstream metaFile(metaPath);

        if (!inFile.is_open() || !outFile.is_open() || !errFile.is_open() || !metaFile.is_open()) {
            throw std::runtime_error("Failed to create in/out/err/meta files");
        }

        inFile << testCase["in"].get<sstr>();
        inFile.close();
        outFile.close();
        errFile.close();
    } catch (const std::exception &e) {
        spdlog::error("Error creating in/out/err/meta files: {}", e.what());
        throw;
    }

    sstr boxId = std::to_string(selectPool());

    // init Isolate
    executeCommand("isolate --init --box-id=" + boxId);

    // run
    sstr iso_result;
    try {
        sstr _stdin = " --stdin=\"" + inPath + "\"";
        sstr _stdout = " --stdout=\"" + outPath + "\"";
        sstr _stderr = " --stderr=\"" + errPath + "\"";
        sstr meta = " --meta=\"" + metaPath + "\"";
        std::string timeLIM = " --time=" + std::to_string(testCase["ti"].get<float>() / 1000.0);
        float extraTime = testCase["et"].get<float>() / 1000.0;
        float totalTime = testCase["ti"].get<float>() / 1000.0 + extraTime;
        sstr totalTimeLIM = " --extra-time=" + std::to_string(extraTime);
        sstr memLIM = " --mem=" + std::to_string(testCase["me"].get<int>());
        iso_result = executeCommand("isolate --box-id=" + boxId + _stdin + _stdout + _stderr + meta + timeLIM + totalTimeLIM + memLIM + " --run -- " + binPath);
    } catch (const std::exception &e) {
        spdlog::error("Error running isolate command: {}", e.what());
        throw;
    }

    nlohmann::json result;
    result["id"] = testCase["id"];

    // score the case
    if (iso_result.find("OK") != 0) {
        result["ti"] = -1;
        result["me"] = -1;
        result["sc"] = 0;
        try {
            result["st"] = findInMeta(metaPath, "status");
            result["msg"] = findInMeta(metaPath, "message");
        } catch (const std::exception &e) {
            spdlog::error("Error reading meta file: {}", e.what());
            throw;
        }
        return result;
    }

    // get time and memory usage
    try {
        result["ti"] = std::stof(findInMeta(metaPath, "time"));
        result["me"] = std::stoi(findInMeta(metaPath, "max-rss"));
    } catch (const std::exception &e) {
        spdlog::error("Error reading meta file: {}", e.what());
        throw;
    }

    // check output correctness
    try {
        std::ifstream outFile(outPath);
        if (!outFile.is_open()) {
            throw std::runtime_error("Failed to open out file");
        }
        std::stringstream outBuffer;
        outBuffer << outFile.rdbuf();
        sstr outContent = removeSpecialChars(outBuffer.str());
        sstr expectedOut = removeSpecialChars(testCase["ou"].get<sstr>());
        if (outContent == expectedOut) {
            result["sc"] = testCase["sc"].get<int>();
            result["st"] = "AC";
        } else {
            result["sc"] = 0;
            result["st"] = "WA";
        }
    } catch (const std::exception &e) {
        spdlog::error("Error reading output file: {}", e.what());
        throw;
    }

    executeCommand("isolate --cleanup --box-id=" + boxId);

    return result;
}

nlohmann::json judgeProgram(const std::string &binPath, const nlohmann::json &cases, const bool noSC) {
    nlohmann::json result;
    std::vector<std::thread> threads;;

    for (const auto &testCase : cases) {
        threads.emplace_back([binPath, testCase, &result, noSC]() {
            nlohmann::json caseResult;
            try {
                caseResult = runTestCase(binPath, testCase, noSC);
            } catch (const std::exception &e) {
                spdlog::error("Error running test case: {}", e.what());
                caseResult["id"] = testCase["id"];
                caseResult["ti"] = -1;
                caseResult["me"] = -1;
                caseResult["sc"] = 0;
                caseResult["st"] = "IE";
                caseResult["msg"] = e.what();
            }
            result.push_back(caseResult);
        });
    }

    // Join all threads
    for (auto &thread : threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }

    return result;
}

MHD_Result answer_to_connection(void *cls, struct MHD_Connection *connection,
                                const char *url, const char *method, const char *version,
                                const char *upload_data, size_t *upload_data_size, void **con_cls) {
    if (*con_cls == nullptr) {
        // First call for this connection
        spdlog::debug("Incoming request: URL: {}, Method: {}, Version: {}", url, method, version);
        *con_cls = (void*)1; // Mark this connection as processed
        return MHD_YES;
    }
    if (upload_data && *upload_data_size > 0) {
        std::string data(upload_data, *upload_data_size);
        spdlog::debug("Upload data: {}", data);
        *upload_data_size = 0; // Indicate that the upload data has been processed

        // Decrypt the data and prase it
        std::string key = settings["token"].get<std::string>();
        // fill the key with 0s if it is less than 16 bytes
        if (key.size() < 16) {
            key.append(16 - key.size(), '0');
        }
        std::string decryptedData = decrypt(data, key);
        nlohmann::json jsonData = nlohmann::json::parse(decryptedData);
        // check code, lan, cases, linkLibs
        try {
            if(jsonData.find("code") == jsonData.end() || jsonData.find("lan") == jsonData.end() || jsonData.find("cases") == jsonData.end() || jsonData.find("linkLibs") == jsonData.end()) {
                spdlog::error("Missing required fields in backend request");
                throw std::runtime_error("Missing required fields");
            }
        } catch (const std::exception &e) {
            spdlog::error("Error parsing request: {}", e.what());
            throw;
        }
        // Compile the code
        std::string binPath;
        if(jsonData["lan"] == "C++") {
            try {
                binPath = compileCpp(jsonData["code"]);
            } catch (const std::exception &e) {
                spdlog::error("Error compiling code: {}", e.what());
                throw;
            }
        } else {
            spdlog::error("Unsupported language: {}", jsonData["lan"].get<std::string>());
            throw std::runtime_error("Unsupported language");
        }
        // Judge the program
        nlohmann::json casesResult;
        try {
            casesResult = judgeProgram(binPath, jsonData["cases"], jsonData["noSC"].get<bool>());
        } catch (const std::exception &e) {
            spdlog::error("Error judging program: {}", e.what());
            throw;
        }
        // Get maxmimum time and memory usage
        double maxTime = 0;
        int maxMemory = 0;
        for (const auto &caseResult : casesResult) {
            if (caseResult["ti"] > maxTime) {
                maxTime = caseResult["ti"];
            }
            if (caseResult["me"] > maxMemory) {
                maxMemory = caseResult["me"];
            }
        }
        // Add up scores
        int totalScore = 0;
        for (const auto &caseResult : casesResult) {
            totalScore += caseResult["sc"].get<int>();
        }
        // Response
        nlohmann::json response;
        response["cases"] = casesResult;
        response["maxTime"] = maxTime;
        response["maxMemory"] = maxMemory;
        response["totalScore"] = totalScore;
        std::string responseStr = response.dump();
        // Encrypt the response
        std::string key = jsonData["key"].get<std::string>();
        // fill the key with 0s if it is less than 16 bytes
        if (key.size() < 16) {
            key.append(16 - key.size(), '0');
        }
        std::string encryptedResponse = encrypt(responseStr, key);
        struct MHD_Response *mhd_response = MHD_create_response_from_buffer(encryptedResponse.size(), (void *) encryptedResponse.c_str(), MHD_RESPMEM_PERSISTENT);
        MHD_add_response_header(mhd_response, "Content-Type", "application/json");
        MHD_Result ret = MHD_queue_response(connection, MHD_HTTP_OK, mhd_response);
        MHD_destroy_response(mhd_response);
        return ret;
    }

    //response
    const char *response = "Heard you loud and clear!";
    struct MHD_Response *mhd_response = MHD_create_response_from_buffer(strlen(response), (void *) response, MHD_RESPMEM_PERSISTENT);
    MHD_Result ret = MHD_queue_response(connection, MHD_HTTP_OK, mhd_response);
    MHD_destroy_response(mhd_response);

    return MHD_YES;
}

void startServer() {
    spdlog::info("Starting server...");
    spdlog::info("Settings: {}", settings.dump());

    server = MHD_start_daemon(MHD_USE_SELECT_INTERNALLY, settings["port"].get<int>(), nullptr, nullptr,
                              &answer_to_connection, nullptr, MHD_OPTION_END);
    if (server == NULL) {
        spdlog::error("Failed to start server");
        throw std::runtime_error("Failed to start server");
    }
    spdlog::info("Server started on port {}", settings["port"].get<int>());
}

int main() {
    spdlog::set_level(spdlog::level::debug);
    try {
        loadSettings();
    } catch (const std::exception &e) {
        std::cerr << "Error loading settings: " << e.what() << std::endl;
        return 1;
    }
    pendingPool.resize(settings["pending_pool_size"].get<int>());
    try {
        startServer();
    } catch (const std::exception &e) {
        std::cerr << "Error starting server: " << e.what() << std::endl;
        return 1;
    }
    // Wait for server to stop
    spdlog::debug("Waiting for server to stop");
    std::future<void> future = promise.get_future();
    future.wait();
    MHD_stop_daemon(server);
    spdlog::info("Server stopped");
    return 0;
}