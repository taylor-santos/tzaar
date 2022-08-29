include(CTest)
enable_testing()

set(TEST_NAME
        ${PROJECT_NAME}_test)

set(TEST_SRC
        test_tzaar.cpp
        ) # Add test_*.cpp sources here

add_executable(${TEST_NAME}
        test_main.cpp
        ${TEST_SRC})

target_link_libraries(${TEST_NAME}
        PRIVATE ${PROJECT_NAME}_lib
        PRIVATE doctest)

include(${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake)
doctest_discover_tests(${TEST_NAME}
        ADD_LABELS 1) # https://github.com/onqtam/doctest/pull/490
