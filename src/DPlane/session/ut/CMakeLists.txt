flowhub_add_ut(TestSessionData
    SOURCES TestSessionData.cpp ../SessionData.cpp
    EXTRA_LIBS GTest::gmock_main
)

flowhub_add_ut(TestSessionMgr
    SOURCES TestSessionMgr.cpp ../SessionMgr.cpp
    EXTRA_LIBS GTest::gmock_main commonLib
)
