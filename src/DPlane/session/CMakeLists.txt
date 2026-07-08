add_library(sessionLib STATIC
    SessionMgr.cpp
    SessionData.cpp
)
target_include_directories(sessionLib PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(sessionLib PUBLIC CAF::core commonLib)

if(FLOWHUB_BUILD_TESTS)
    add_subdirectory(ut)
endif()
