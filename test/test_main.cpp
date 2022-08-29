//
// Created by taylor-santos on 4/16/2022 at 22:34.
//

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#ifdef __clang__
#    pragma message "clang++ " __clang_version__
#elif __GNUC__
#    pragma message "g++ " __VERSION__
#elif _MSC_VER
#    define STR(x)  #    x
#    define XSTR(x) STR(x)
#    pragma message("MSVC " XSTR(_MSC_FULL_VER))
#else
#    error Unknown compiler version
#endif
