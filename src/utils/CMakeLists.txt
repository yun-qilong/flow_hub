add_library(utilsLib STATIC
    JsonCoDec.cpp
    HttpClient.cpp
    SysLog.cpp
)
target_include_directories(utilsLib PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(utilsLib PUBLIC CURL::libcurl nlohmann_json::nlohmann_json)
