/*
 * test_utf8.c - UTF-8 encoder, string decoder and streaming decoder.
 */
#include "td_test.h"
#include "tinydesk/td_screen.h"

static void test_encode(void)
{
    uint8_t b[4];
    CHECK_EQ(td_utf8_encode('A', b), 1);
    CHECK_EQ(b[0], 'A');

    CHECK_EQ(td_utf8_encode(0xE9, b), 2);                 /* é */
    CHECK(b[0] == 0xC3 && b[1] == 0xA9);

    CHECK_EQ(td_utf8_encode(0x2500, b), 3);               /* ─ */
    CHECK(b[0] == 0xE2 && b[1] == 0x94 && b[2] == 0x80);

    CHECK_EQ(td_utf8_encode(0x1F600, b), 4);              /* emoji */
    CHECK(b[0] == 0xF0 && b[1] == 0x9F && b[2] == 0x98 && b[3] == 0x80);

    CHECK_EQ(td_utf8_encode(0xD800, b), 3);               /* surrogate -> U+FFFD */
    CHECK(b[0] == 0xEF && b[1] == 0xBF && b[2] == 0xBD);
    CHECK_EQ(td_utf8_encode(0x110000, b), 3);
}

static void test_decode_string(void)
{
    const char *s = "a\xC3\xA9\xE2\x94\x80\xF0\x9F\x98\x80";
    CHECK_EQ(td_utf8_next(&s), 'a');
    CHECK_EQ(td_utf8_next(&s), 0xE9);
    CHECK_EQ(td_utf8_next(&s), 0x2500);
    CHECK_EQ(td_utf8_next(&s), 0x1F600);
    CHECK_EQ(td_utf8_next(&s), 0);

    /* Malformed: stray continuation, overlong, truncated. */
    const char *bad = "\x80"
                      "\xC0\x80"
                      "\xE2\x94"
                      "z";
    CHECK_EQ(td_utf8_next(&bad), 0xFFFD);
    CHECK_EQ(td_utf8_next(&bad), 0xFFFD);   /* C0 is never valid */
    CHECK_EQ(td_utf8_next(&bad), 0xFFFD);   /* its 80 */
    CHECK_EQ(td_utf8_next(&bad), 0xFFFD);   /* E2 94 cut short by 'z' */
    CHECK_EQ(td_utf8_next(&bad), 0xFFFD);   /* the lone 94 */
    CHECK_EQ(td_utf8_next(&bad), 'z');

    CHECK_EQ(td_utf8_len("h\xC3\xA9llo"), 5);
}

static void test_stream(void)
{
    td_utf8_decoder_t d = {0};
    uint32_t out[2];
    const uint8_t seq[] = {0xE2, 0x94, 0x80};
    CHECK_EQ(td_utf8_feed(&d, seq[0], out), 0);
    CHECK_EQ(td_utf8_feed(&d, seq[1], out), 0);
    CHECK_EQ(td_utf8_feed(&d, seq[2], out), 1);
    CHECK_EQ(out[0], 0x2500);

    CHECK_EQ(td_utf8_feed(&d, 'x', out), 1);
    CHECK_EQ(out[0], 'x');

    /* A sequence interrupted by ASCII yields U+FFFD then the character. */
    CHECK_EQ(td_utf8_feed(&d, 0xC3, out), 0);
    CHECK_EQ(td_utf8_feed(&d, 'y', out), 2);
    CHECK_EQ(out[0], 0xFFFD);
    CHECK_EQ(out[1], 'y');

    /* Overlong three-byte form of '/' is rejected. */
    CHECK_EQ(td_utf8_feed(&d, 0xE0, out), 0);
    CHECK_EQ(td_utf8_feed(&d, 0x80, out), 0);
    CHECK_EQ(td_utf8_feed(&d, 0xAF, out), 1);
    CHECK_EQ(out[0], 0xFFFD);
}

int main(void)
{
    test_encode();
    test_decode_string();
    test_stream();
    return TD_TEST_RESULT();
}
