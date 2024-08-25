#include <nlohmann/json.hpp>
#include <fstream>

nlohmann::json settings;
void loadSettings(){
    std::ifstream file("settings.json");
    file >> settings;
    file.close();
}