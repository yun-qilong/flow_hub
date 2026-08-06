flowhub_add_ut(TestAccessGateway
    SOURCES TestAccessGateway.cpp ../AccessGateway.cpp
    EXTRA_LIBS GTest::gmock_main
)

flowhub_add_ut(TestCliAdapter
    SOURCES TestCliAdapter.cpp ../CliAdapter.cpp
    EXTRA_LIBS GTest::gmock_main nlohmann_json::nlohmann_json
)
