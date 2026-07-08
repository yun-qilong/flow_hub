#pragma once

#if FLOWHUB_TEST_BUILD
#define TESTABLE_MOCK(PATH) PATH
#define USE_EMPTY_CLASS(CLASS) defined(CLASS##_empty)
#define USE_MOCK(CLASS) defined(CLASS##_mock)
#define USE_ORIG(CLASS) (!USE_EMPTY_CLASS(CLASS) && !USE_MOCK(CLASS))
#else
#define TESTABLE_MOCK(PATH) "common/testable/TestableEmpty.hpp"
#define USE_EMPTY_CLASS(CLASS) 0
#define USE_MOCK(CLASS) 0
#define USE_ORIG(CLASS) 1
#endif
