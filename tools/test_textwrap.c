/*
 * Differential test for the extracted TrueType line-break search.
 *
 * src/textwrap.c replaced the loop in Font_WrapLengthTTF (font.c, 0x0046DFD0)
 * with a binary search over the same prefix measurements. That is a rewrite of
 * text-layout logic in a project whose whole point is matching the original, so
 * it is checked against the original loop itself: TextWrap_Reference below is
 * that loop, transcribed with the measuring call swapped for the shared fake
 * one. Both are run over the same inputs and must agree on every case.
 *
 * The fake measure is deterministic and monotone (a prefix can never be
 * narrower than its parent), carries wide and narrow characters, and has a
 * cost counter, so the test also asserts that the new search performs far
 * fewer measurements -- a rewrite that agreed but still measured per character
 * would pass the equivalence half and fail the point of the change.
 *
 * Runs on the host, no SDL_ttf and no font needed. Usage:
 *   sh tools/test_textwrap.sh
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "textwrap.h"

static int failures;
static int measures;

static void check(int cond, const char *what, const char *detail)
{
    if (!cond) {
        if (detail != NULL) {
            printf("FAIL %s [%s]\n", what, detail);
        } else {
            printf("FAIL %s\n", what);
        }
        failures++;
    }
}

/*
 * A synthetic font: every character advances 4..9 px, except a set of wide
 * characters that advance 40. Monotone, and enough variety that wrapping
 * decisions differ between cases.
 */
static int fake_measure(const char *prefix, void *ctx)
{
    int width = 0;
    const unsigned char *p = (const unsigned char *)prefix;

    (void)ctx;
    measures++;
    while (*p != '\0') {
        unsigned char c = *p;

        if (c >= 0xC0) {
            /* A wide character (the first byte of a multi-byte sequence). */
            width += 40;
            p++;
            while ((*p & 0xC0) == 0x80) {
                p++;
            }
            continue;
        }
        width += 4 + (c % 6);
        p++;
    }
    return width;
}

/*
 * The original Font_WrapLengthTTF loop, verbatim apart from the measuring call
 * (Font_TextWidth(buf, font) becomes fake_measure(buf, NULL)).
 */
static int wrap_reference(const char *text, int max_width, int word_wrap)
{
    const char *nl;
    char *buf = NULL;
    int len = 0;
    int last_break = 0;
    int prev = 0;
    int before = 0;
    int i;

    if (*text == '\0') {
        return 0;
    }
    nl = strchr(text, '\n');
    if (nl != NULL) {
        len = (int)(nl - text);
        buf = malloc(len + 1);
    }
    if (buf == NULL) {
        len = (int)strlen(text);
        buf = malloc(len + 1);
        if (buf == NULL) {
            return 0;
        }
    }
    strncpy(buf, text, len);
    buf[len] = '\0';
    i = 1;
    if (len >= 1) {
        do {
            char ch = buf[i];

            if ((ch & 0xC0) != 0x80) {
                char c;

                before = prev;
                prev = i;
                buf[i] = '\0';
                if (fake_measure(buf, NULL) > max_width) {
                    i = before;
                    break;
                }
                buf[i] = ch;
                c = buf[before];
                if ((c >= 1 && c <= 0x7E && strchr(" \t\n\v\f\r", c) != NULL) || c == '-' || i == len) {
                    last_break = i;
                }
            }
            i++;
        } while (i <= len);
    }
    if (last_break == 0) {
        last_break = before;
    }
    if (last_break < 0) {
        last_break = 0;
    }
    free(buf);
    if (word_wrap) {
        i = last_break;
    }
    if (text[i] == '\n') {
        i++;
    }
    return i;
}

/* Position one past the last byte a case needs, so text[i] reads are safe. */
static char text_buf[512];

static void compare_one(const char *text, int max_width, int word_wrap)
{
    int got;
    int want;
    int ref_measures;
    int new_measures;

    /* Both implementations read text[i] with i possibly one past the end when
     * the whole line fits, so give them a NUL-padded buffer. */
    memset(text_buf, 0, sizeof(text_buf));
    strncpy(text_buf, text, sizeof(text_buf) - 2);

    measures = 0;
    want = wrap_reference(text_buf, max_width, word_wrap);
    ref_measures = measures;

    measures = 0;
    got = TextWrap_FitLength(text_buf, max_width, word_wrap, fake_measure, NULL);
    new_measures = measures;

    if (got != want) {
        char detail[256];

        snprintf(detail, sizeof(detail), "text=\"%s\" max=%d word=%d got=%d want=%d",
                 text, max_width, word_wrap, got, want);
        check(0, "extracted search matches the original loop", detail);
    }
    /*
     * No per-case cost assertion: the loop stops as soon as the text is too
     * wide, so for a small width it measures two prefixes while a binary search
     * probes about log n of them. The cost property that matters -- a long line
     * not costing one measurement per character -- is test_cost below.
     */
    (void)ref_measures;
    (void)new_measures;
}

static void test_equivalence(void)
{
    static const char *const texts[] = {
        "",
        "a",
        "ab",
        "abc",
        "Hello, world",
        "Hello, world\nsecond line",
        "one two three four five six seven eight",
        "aaa-bbb-ccc-ddd-eee-fff",
        "a  b   c    d",
        "supercalifragilisticexpialidocious and then some more words",
        "no-break-possible-at-all-here",
        "trailing spaces   ",
        "  leading spaces",
        "\ttabbed\ttext\there",
        /* UTF-8: 2- and 3-byte characters, including wide ones. */
        "\xC3\xA9\xC3\xA9\xC3\xA9 \xC3\xA9\xC3\xA9\xC3\xA9 caf\xC3\xA9",
        "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E \xE3\x83\x86\xE3\x82\xB9\xE3\x83\x88",
        "\xE6\x97\xA5 \xE6\x9C\xAC \xE8\xAA\x9E \xE3\x83\x86 \xE3\x82\xB9",
        "mixed ascii and \xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E in one line",
        NULL
    };
    int t;

    for (t = 0; texts[t] != NULL; t++) {
        int max_width;

        for (max_width = 0; max_width <= 140; max_width += 4) {
            compare_one(texts[t], max_width, 0);
            compare_one(texts[t], max_width, 1);
        }
        /* A few extreme widths. */
        compare_one(texts[t], -1, 0);
        compare_one(texts[t], -1, 1);
        compare_one(texts[t], 100000, 0);
        compare_one(texts[t], 100000, 1);
    }
}

/* Every width from 1..60 on a mid-length line, char wrap and word wrap. */
static void test_sweep(void)
{
    const char *text = "the quick brown fox jumps over the lazy dog";
    int w;

    for (w = 1; w <= 60; w++) {
        compare_one(text, w, 0);
        compare_one(text, w, 1);
    }
}

/* The single-character-that-does-not-fit case returns 0. */
static void test_edges(void)
{
    char one[3];

    one[0] = 'W';
    one[1] = '\0';
    one[2] = '\0';
    check(TextWrap_FitLength(one, 0, 0, fake_measure, NULL) == 0,
          "a character that does not fit consumes nothing", NULL);

    check(TextWrap_FitLength("", 100, 0, fake_measure, NULL) == 0,
          "empty text consumes nothing", NULL);

    /* A NULL measure is refused rather than dereferenced. */
    check(TextWrap_FitLength("abc", 100, 0, NULL, NULL) == 0,
          "a NULL measure is refused", NULL);
}

/* Cost: a long line must not cost one measurement per character. */
static void test_cost(void)
{
    char line[400];
    int i;
    int new_measures;
    int ref_measures;

    for (i = 0; i < (int)sizeof(line) - 1; i++) {
        line[i] = (i % 7 == 6) ? ' ' : 'x';
    }
    line[sizeof(line) - 1] = '\0';

    memset(text_buf, 0, sizeof(text_buf));
    strncpy(text_buf, line, sizeof(text_buf) - 2);

    measures = 0;
    (void)wrap_reference(text_buf, 300, 1);
    ref_measures = measures;

    measures = 0;
    (void)TextWrap_FitLength(text_buf, 300, 1, fake_measure, NULL);
    new_measures = measures;

    printf("cost: %d-byte line, original %d measurement(s), binary search %d\n",
           (int)strlen(line), ref_measures, new_measures);
    check(new_measures <= 24, "a 400-byte line takes O(log n) measurements, not O(n)", NULL);
    check(new_measures < ref_measures,
          "the binary search measures less than the original loop", NULL);
}

int main(void)
{
    test_equivalence();
    test_sweep();
    test_edges();
    test_cost();

    if (failures != 0) {
        printf("test_textwrap: %d failure(s)\n", failures);
        return 1;
    }
    printf("test_textwrap: OK\n");
    return 0;
}
