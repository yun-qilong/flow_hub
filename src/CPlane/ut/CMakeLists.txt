flowhub_add_ut(TestSessionMgr
    SOURCES TestSessionMgr.cpp ../SessionMgr.cpp
    EXTRA_LIBS GTest::gmock_main commonLib
)
