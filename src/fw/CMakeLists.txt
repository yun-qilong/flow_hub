add_library(fwLib INTERFACE)
target_include_directories(fwLib INTERFACE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(fwLib INTERFACE CAF::core)

message(STATUS "fwLib target created!")