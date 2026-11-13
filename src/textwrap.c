/*
 * Port addition: the measuring core of the TrueType line-break search
 * (textwrap.h). Not part of the original binary; there is no address to name.
 *
 * Font_WrapLengthTTF in font.c keeps its name, its prototype and its behaviour
 * and delegates the search here, so the original's quirks stay in one place and
 * this file stays testable without SDL_ttf: the caller passes the measurement
 * in.
 */
#include <stdlib.h>
#include <string.h>

#include "textwrap.h"

/* Is c the first byte of a UTF-8 character rather than a continuation byte? */
static int TextWrap_IsBoundary(unsigned char c)
{
    return (c & 0xC0) != 0x80;
}

/* The character a word wrap may break after (Font_IsSpace, plus '-'). */
static int TextWrap_IsBreakChar(unsigned char c)
{
    if (c >= 1 && c <= 0x7E &&
        (c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r')) {
        return 1;
    }
    return c == '-';
}

/* Width of buf[0..boundary), measured with the boundary temporarily cut. */
static int TextWrap_MeasurePrefix(char *buf, int boundary, TextWrap_MeasureFn measure, void *ctx)
{
    char saved = buf[boundary];
    int width;

    buf[boundary] = '\0';
    width = measure(buf, ctx);
    buf[boundary] = saved;
    return width;
}

/*
 * The last break opportunity before `limit`, as the original computes it while
 * scanning upward: a break at boundary i when the character before i is a
 * space or '-', or when i is the end of the line. Pure pointer work -- it
 * measures nothing.
 */
static int TextWrap_LastBreak(const char *buf, int limit, int len)
{
    int last_break = 0;
    int prev = 0;
    int i;

    for (i = 1; i < limit; i++) {
        if (!TextWrap_IsBoundary((unsigned char)buf[i])) {
            continue;
        }
        if (TextWrap_IsBreakChar((unsigned char)buf[prev]) || i == len) {
            last_break = i;
        }
        prev = i;
    }
    return last_break;
}

int TextWrap_FitLength(const char *text, int max_width, int word_wrap,
                       TextWrap_MeasureFn measure, void *ctx)
{
    const char *nl;
    char *buf;
    int *bounds;
    int len;
    int count;
    int first_fail;
    int before = 0;
    int last_break;
    int i_out;
    int pos;
    int lo, hi;

    if (text == NULL || measure == NULL || *text == '\0') {
        return 0;
    }
    nl = strchr(text, '\n');
    len = (nl != NULL) ? (int)(nl - text) : (int)strlen(text);
    buf = malloc((size_t)len + 1);
    if (buf == NULL) {
        return 0;
    }
    memcpy(buf, text, (size_t)len);
    buf[len] = '\0';
    bounds = malloc(((size_t)len + 1) * sizeof(*bounds));
    if (bounds == NULL) {
        free(buf);
        return 0;
    }

    /*
     * The candidate positions: character starts in 1..len. Index 0 is not a
     * candidate -- the original starts at 1 -- which is why an over-wide first
     * character leaves `before` at 0 and the line consumes nothing. The
     * terminator at `len` is always one (buf[len] is '\0'), which is what makes
     * "the whole line fits" land on len.
     *
     * Collecting them first keeps the search over a dense, sorted array, so
     * every probe is a real candidate and the loop always makes progress. An
     * earlier version searched byte positions and snapped each probe forward to
     * the next boundary; with a multi-byte character right after the first one
     * the snapped position never moved below the window and the search looped
     * forever (found by tools/test_textwrap.sh on "\xC3\xA9..." with width 0).
     */
    count = 0;
    for (pos = 1; pos <= len; pos++) {
        if (TextWrap_IsBoundary((unsigned char)buf[pos])) {
            bounds[count++] = pos;
        }
    }

    /*
     * Binary search for the first candidate whose prefix is too wide. Width
     * never decreases as a prefix grows, so "too wide" is monotone and the
     * first true candidate is found in O(log n) measures instead of the
     * original's one measure per character.
     */
    lo = 0;
    hi = count - 1;
    first_fail = -1;
    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;

        if (TextWrap_MeasurePrefix(buf, bounds[mid], measure, ctx) > max_width) {
            first_fail = mid;
            hi = mid - 1;
        } else {
            lo = mid + 1;
        }
    }

    if (first_fail < 0) {
        /* Everything fit: the original ends its loop with i == len + 1 and
         * last_break == len, because a boundary at len counts as the end. */
        i_out = len + 1;
        last_break = len;
    } else {
        int fail_at = bounds[first_fail];

        before = (first_fail > 0) ? bounds[first_fail - 1] : 0;
        last_break = TextWrap_LastBreak(buf, fail_at, len);
        if (last_break == 0) {
            last_break = before;
        }
        i_out = before;
    }

    free(bounds);
    free(buf);
    if (word_wrap) {
        i_out = last_break;
    }
    /* Quirk kept: the original reads text[i] here, one past the terminator when
     * the whole line fits. */
    if (text[i_out] == '\n') {
        i_out++;
    }
    return i_out;
}
