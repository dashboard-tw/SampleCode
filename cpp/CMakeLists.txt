cmake_minimum_required(VERSION 3.16)
project(dashboard_tw_sample CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(CURL REQUIRED)
find_package(nlohmann_json 3.2.0 REQUIRED)

add_executable(dashboard_tw_sample main.cpp)
target_link_libraries(dashboard_tw_sample PRIVATE CURL::libcurl nlohmann_json::nlohmann_json)
