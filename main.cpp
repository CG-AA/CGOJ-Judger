#include <nlohmann/json.hpp>
#include <fstream>
#include <microhttpd.h>
#include <spdlog/spdlog.h>
#include <mutex>
#include <condition_variable>

std::mutex main_blocker;
std::condition_variable main_blocker_cv;
bool server_running = true;

nlohmann::json settings;
void loadSettings(){
    std::ifstream file("settings.json");
    file >> settings;
    file.close();
}


MHD_Result answer_to_connection(void *cls, struct MHD_Connection *connection,
                         const char *url, const char *method, const char *version,
                         const char *upload_data, size_t *upload_data_size, void **con_cls) {
    SPDLOG_INFO("Incoming request: URL: {}, Method: {}, Version: {}", url, method, version);
    if (upload_data && *upload_data_size > 0) {
        SPDLOG_INFO("Upload data: {}", std::string(upload_data, *upload_data_size));
    }
}

int main(){
    loadSettings();
    
    struct MHD_Daemon *daemon;
    daemon = MHD_start_daemon(MHD_USE_SELECT_INTERNALLY, settings["port"].get<int>(), NULL, NULL, &answer_to_connection, NULL, MHD_OPTION_END);
    if (NULL == daemon) return 1;
    SPDLOG_INFO("Server started on port {}", settings["port"].get<int>());


    // Wait for server to stop
    std::unique_lock<std::mutex> lock(main_blocker);
    main_blocker_cv.wait(lock, []{return !server_running;});

    MHD_stop_daemon(daemon);
    return 0;
}