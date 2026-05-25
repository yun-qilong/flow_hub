add_subdirectory(session)
add_subdirectory(business)
add_subdirectory(service)

add_library(dplaneLib INTERFACE)
target_link_libraries(dplaneLib INTERFACE
    sessionLib
    businessLib
    serviceLib
)
target_include_directories(dplaneLib INTERFACE ${CMAKE_SOURCE_DIR}/src)
