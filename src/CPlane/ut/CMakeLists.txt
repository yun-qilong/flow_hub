flowhub_add_ut(TestSessionMgr
    SOURCES TestSessionMgr.cpp ../SessionMgr.cpp
    EXTRA_LIBS GTest::gmock_main commonLib utilsLib
)

flowhub_add_ut(TestBusinessMgr
    SOURCES TestBusinessMgr.cpp ../BusinessMgr.cpp
    EXTRA_LIBS GTest::gmock_main
)
