#ifndef MUGEN_VIDEO_H
#define MUGEN_VIDEO_H

/*
 * SDL video mode and the window (video.c), and the coordinate values that the
 * small helpers at the start of that module work on.
 */

/* Coord.type of the values made by Coord_Make. */
#define COORD_TYPE_2D 2

/*
 * A position (or size) in a coordinate space whose unit is `unit` (e.g. the
 * localcoord scale of a character). Passed and returned by value.
 */
typedef struct Coord {
    double x;               /* 0x00 */
    double y;               /* 0x08 */
    double z;               /* 0x10 */
    double unit;            /* 0x18 Coord_Rescale scales x, y, z by old unit / new unit */
    int type;               /* 0x20 COORD_TYPE_* */
    int pad_24;             /* 0x24 copied along, never set */
} Coord;
MUGEN_ASSERT_OFFSET(Coord, unit, 0x18);
MUGEN_ASSERT_SIZE(Coord, 0x28);

/* Engine display request bits inherited from the original config contract.
 * SDL2 backend explicitly translates them to SDL_WINDOW_*; never pass these
 * to SDL_CreateWindow or store them in an SDL_Surface's native flags. */
#define DISPLAY_SOFTWARE  0x00000000u
#define DISPLAY_HARDWARE  0x00000001u
#define DISPLAY_OPENGL    0x00000002u
#define DISPLAY_RESIZABLE 0x00000010u
#define DISPLAY_BORDERLESS 0x00000020u
#define DISPLAY_ACCEL     0x00000100u
#define DISPLAY_DOUBLEBUF 0x40000000u
#define DISPLAY_FULLSCREEN 0x80000000u


void Video_SwapBuffers(void);
void Video_SetSwapInterval(int interval);

#endif /* MUGEN_VIDEO_H */
