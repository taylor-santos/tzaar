#include "tzaar.hpp"

#include <iostream>
#include <SDL.h>

const int SCREEN_WIDTH  = 720;
const int SCREEN_HEIGHT = 720;

void
SDL_failure_exit(const char *message) {
    std::cerr << message << ", SDL_Error: " << SDL_GetError() << std::endl;
    exit(EXIT_FAILURE);
}

int
main(int, char **) {
    if (SDL_Init(SDL_INIT_EVERYTHING) < 0) {
        SDL_failure_exit("SDL could not initialize");
    }

    auto window = SDL_CreateWindow(
        Tzaar::window_title(),
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        SCREEN_WIDTH,
        SCREEN_HEIGHT,
        SDL_WINDOW_RESIZABLE);
    if (!window) SDL_failure_exit("SDL could not create window");

    auto renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer) SDL_failure_exit("SDL could not create rendering context");

    auto done_looping = SDL_FALSE;

    while (!done_looping) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_QUIT: done_looping = SDL_TRUE; break;
            }
        }

        SDL_RenderClear(renderer);
        SDL_RenderPresent(renderer);
    }

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);

    SDL_Quit();

    return 0;
}
