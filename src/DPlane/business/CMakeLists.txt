add_library(businessLib STATIC
    Router.cpp
)
target_include_directories(businessLib PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(businessLib PUBLIC CAF::core)
