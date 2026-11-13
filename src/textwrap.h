#ifndef MUGEN_TEXTWRAP_H
#define MUGEN_TEXTWRAP_H

/*
 * Port addition: the measuring core of the TrueType line-break search, apart
 * from src/font.c so it can be tested without a font.
 *
 * Font_WrapLengthTTF finds the number of bytes of a line that fit a width by
 * measuring the text again for every UTF-8 character boundary:
 *
 *     for every boundary i:  if (width(text[0..i)) > max_width) stop
 *
 * Each of those measurements is an SDL_ttf call over a longer string than the
 * one before, so a line of n characters costs O(n) measurement calls and
 * O(n^2) bytes scanned inside SDL_ttf. On the layouts that use a TrueType font
 * for wrapped text (info boxes, storyboard text, the error dialog) that is the
 * dominant cost of drawing a line.
 *
 * The width of a prefix never decreases as the prefix grows, so the first
 * over-wide boundary can be found by binary search instead: O(log n) calls on
 * the same strings. Everything else the original does -- where the last break
 * opportunity was, the count one past the end when the whole line fits, the
 * byte read past the terminator that quirk relies on -- is pointer arithmetic
 * over the same bytes and is kept exactly.
 *
 * TextWrap_FitLength is that implementation. The caller supplies the measure
 * function, so this file has no font, no SDL_ttf and no engine dependency.
 */

/* Width of the NUL-terminated prefix `prefix`, in pixels. */
typedef int (*TextWrap_MeasureFn)(const char *prefix, void *ctx);

/*
 * Number of bytes of `text` to consume for one line of at most max_width
 * pixels, wrapping at a space or '-' when word_wrap is nonzero.
 *
 * Replicates Font_WrapLengthTTF (font.c, 0x0046DFD0) exactly, including its
 * quirks:
 *  - a line that fits whole counts one past its end: its '\n', or the
 *    terminator of the last line, so the caller steps over it (and the
 *    original reads the byte after it, which this does too);
 *  - when a break is impossible the position before the over-wide character
 *    is returned, which can be 0 for a single character that does not fit.
 */
int TextWrap_FitLength(const char *text, int max_width, int word_wrap,
                       TextWrap_MeasureFn measure, void *ctx);

#endif /* MUGEN_TEXTWRAP_H */
