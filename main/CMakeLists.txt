if (MSVC)
    add_compile_options(/W4 /WX)
else ()
    add_compile_options(-Wall -Wextra -pedantic -Werror)
endif ()

add_executable(${PROJECT_NAME}
        main.cpp)

target_link_libraries(${PROJECT_NAME}
        PRIVATE ${PROJECT_NAME}_lib
        SDL2-static SDL2main
        )

if (MSVC)
    target_compile_options(${PROJECT_NAME}
            PRIVATE /W4 /WX)
    target_compile_definitions(${PROJECT_NAME}
            PRIVATE _CRT_SECURE_NO_WARNINGS)
else ()
    target_compile_options(${PROJECT_NAME}
            PRIVATE -Wall -Wextra -pedantic -Werror -Wno-missing-field-initializers)
endif ()
