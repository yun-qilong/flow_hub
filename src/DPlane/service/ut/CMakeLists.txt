flowhub_add_ut(TestServiceGateway
    SOURCES TestServiceGateway.cpp ../ServiceGateway.cpp
    EXTRA_LIBS GTest::gmock_main
)

flowhub_add_ut(TestServiceMgr
    SOURCES TestServiceMgr.cpp ../ServiceMgr.cpp
    EXTRA_LIBS GTest::gmock_main
)
