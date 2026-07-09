flowhub_add_ut(TestAccessGateway
    SOURCES TestAccessGateway.cpp ../AccessGateway.cpp
    EXTRA_LIBS GTest::gmock_main
)
