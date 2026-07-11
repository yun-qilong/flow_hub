flowhub_add_ut(TestRouter
    SOURCES TestRouter.cpp ../Router.cpp
    EXTRA_LIBS GTest::gmock_main
)

flowhub_add_ut(TestAiChatBus
    SOURCES TestAiChatBus.cpp ../AiChatBus.cpp
    EXTRA_LIBS GTest::gmock_main commonLib utilsLib
)
