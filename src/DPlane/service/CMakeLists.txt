add_library(serviceLib STATIC
    AiApiAdapter.cpp
    ServiceGateway.cpp
)
target_include_directories(serviceLib PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(serviceLib PUBLIC CAF::core utilsLib)
