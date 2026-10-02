#ifndef ALPHABLIT_H
#define ALPHABLIT_H

#include <SDL3/SDL.h>

int pygame_AlphaBlit (SDL_Surface *src, SDL_Rect *srcrect,
                      SDL_Surface *dst, SDL_Rect *dstrect);

void pygame_stretch(SDL_Surface *src, SDL_Surface *dst);
#endif
