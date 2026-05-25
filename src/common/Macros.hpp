// src/common/Macros.hpp

#pragma once
#define FH_LIKELY(expr) __builtin_expect(!!(expr), 1)

// PARAMETER — field + const/non-const accessors.  That's all.
#define PARAMETER(Type, name, ...)                                                                 \
    Type name##_{__VA_ARGS__};                                                                     \
    const Type &name() const                                                                       \
    {                                                                                              \
        return name##_;                                                                            \
    }                                                                                              \
    Type &name()                                                                                   \
    {                                                                                              \
        return name##_;                                                                            \
    }
