flowhub_add_ut(TestSessionDispatcher
    SOURCES TestSessionDispatcher.cpp ../SessionDispatcher.cpp
    EXTRA_LIBS GTest::gmock_main
)
flowhub_add_ut(TestAiAgora
    SOURCES TestAiAgora.cpp ../AiAgora.cpp
    EXTRA_LIBS GTest::gmock_main
)
