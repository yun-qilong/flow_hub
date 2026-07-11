add_library(serviceLib STATIC
    AiApiAdapter.cpp
    ServiceGateway.cpp
    ServiceMgr.cpp
)
target_include_directories(serviceLib PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(serviceLib PUBLIC CAF::core utilsLib)

if(FLOWHUB_BUILD_TESTS)
    add_subdirectory(ut)
endif()
