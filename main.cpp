#include <nlohmann/json.hpp>
#include <fstream>
#include <microhttpd.h>
#include <spdlog/spdlog.h>
#include <mutex>
#include <condition_variable>
#include <iostream>
#include <cstdlib>

std::mutex main_blocker;
std::condition_variable main_blocker_cv;
bool server_running = true;

nlohmann::json settings;

struct MHD_Daemon *daemon;

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

void startServer() {
    spdlog::info("Starting server...");
    spdlog::info("Settings: {}", settings.dump());

    daemon = MHD_start_daemon(MHD_USE_SELECT_INTERNALLY, settings["port"].get<int>(), NULL, NULL, &answer_to_connection, NULL, MHD_OPTION_END);
    if (NULL == daemon) {
        spdlog::error("Failed to start server");
        throw std::runtime_error("Failed to start server");
    }
    spdlog::info("Server started on port {}", settings["port"].get<int>());
}

MHD_Result answer_to_connection(void *cls, struct MHD_Connection *connection,
                                const char *url, const char *method, const char *version,
                                const char *upload_data, size_t *upload_data_size, void **con_cls) {
    if (*con_cls == nullptr) {
        // First call for this connection
        spdlog::info("Incoming request: URL: {}, Method: {}, Version: {}", url, method, version);
        *con_cls = (void*)1; // Mark this connection as processed
        return MHD_YES;
    }
    if (upload_data && *upload_data_size > 0) {
        spdlog::info("Upload data: {}", std::string(upload_data, *upload_data_size));
        *upload_data_size = 0; // Indicate that the upload data has been processed
    }

    //response
    const char *response = "Heard you loud and clear!";
    struct MHD_Response *mhd_response = MHD_create_response_from_buffer(strlen(response), (void *) response, MHD_RESPMEM_PERSISTENT);
    MHD_Result ret = MHD_queue_response(connection, MHD_HTTP_OK, mhd_response);
    MHD_destroy_response(mhd_response);

    return MHD_YES;
}

void waitForServer(struct MHD_Daemon *daemon) {
    std::unique_lock<std::mutex> lock(main_blocker);
    main_blocker_cv.wait(lock, []{return !server_running;});
    MHD_stop_daemon(daemon);
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



int main() {
    try {
        loadSettings();
    } catch (const std::exception &e) {
        std::cerr << "Error loading settings: " << e.what() << std::endl;
        return 1;
    }
    try {
        startServer();
    } catch (const std::exception &e) {
        std::cerr << "Error starting server: " << e.what() << std::endl;
        return 1;
    }
    // Wait for server to stop
    waitForServer(daemon);
    return 0;
}