#pragma once

#if FLOWHUB_TEST_BUILD
#define TESTABLE_MOCK(PATH) PATH
#define USE_ORIG(CLASS) defined(CLASS##_orig)
#define USE_EMPTY(CLASS) defined(CLASS##_empty)
#else
#define TESTABLE_MOCK(PATH) "common/testable/TestableEmpty.hpp"
#define USE_ORIG(CLASS) 1
#define USE_EMPTY(CLASS) 0
#endif
