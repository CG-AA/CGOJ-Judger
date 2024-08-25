#include <nlohmann/json.hpp>
#include <fstream>
#include <microhttpd.h>
#include <spdlog/spdlog.h>

nlohmann::json settings;
void loadSettings(){
    std::ifstream file("settings.json");
    file >> settings;
    file.close();
}


MHD_Result answer_to_connection(void *cls, struct MHD_Connection *connection,
                         const char *url, const char *method, const char *version,
                         const char *upload_data, size_t *upload_data_size, void **con_cls) {
    std::string response_str = settings.dump();

    struct MHD_Response *response;
    MHD_Result ret;

    response = MHD_create_response_from_buffer(response_str.size(), (void *)response_str.c_str(), MHD_RESPMEM_MUST_COPY);
    ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
    MHD_destroy_response(response);
    return ret;
}

int main(){
    loadSettings();
    
    struct MHD_Daemon *daemon;
    daemon = MHD_start_daemon(MHD_USE_SELECT_INTERNALLY, settings["port"].get<int>(), NULL, NULL, &answer_to_connection, NULL, MHD_OPTION_END);
    if (NULL == daemon) return 1;
}