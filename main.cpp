#include <nlohmann/json.hpp>
#include <fstream>
#include <microhttpd.h>
#include <spdlog/spdlog.h>
#include <mutex>
#include <condition_variable>
#include <iostream>

std::mutex main_blocker;
std::condition_variable main_blocker_cv;
bool server_running = true;

nlohmann::json settings;

void loadSettings() {
    std::ifstream file("/usr/src/settings.json");
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

    return MHD_YES;
}

int main() {
    try {
        loadSettings();
    } catch (const std::exception &e) {
        std::cerr << "Error loading settings: " << e.what() << std::endl;
        return 1;
    }

    spdlog::info("Starting server...");
    spdlog::info("Settings: {}", settings.dump());

    struct MHD_Daemon *daemon;
    daemon = MHD_start_daemon(MHD_USE_SELECT_INTERNALLY, settings["port"].get<int>(), NULL, NULL, &answer_to_connection, NULL, MHD_OPTION_END);
    if (NULL == daemon) {
        spdlog::error("Failed to start server");
        return 1;
    }
    spdlog::info("Server started on port {}", settings["port"].get<int>());

    // Wait for server to stop
    std::unique_lock<std::mutex> lock(main_blocker);
    main_blocker_cv.wait(lock, []{return !server_running;});

    MHD_stop_daemon(daemon);
    return 0;
}