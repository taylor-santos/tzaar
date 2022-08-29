//
// Created by taylor-santos on 8/28/2022 at 18:30.
//

#include "doctest/doctest.h"

#include "tzaar.hpp"

TEST_SUITE_BEGIN("Tzaar");

TEST_CASE("window title") {
    CHECK(std::string(Tzaar::window_title()) == "TZAAR");
}
