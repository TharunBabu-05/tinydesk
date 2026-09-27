/*
 * test_render.c - the diff renderer sends only what changed.
 */
#include "td_test.h"
#include "tinydesk/td_screen.h"

static char s_out[65536];
static int s_len;
static int s_accept = 1 << 30;   /* bytes the fake link accepts */

static int fake_write(void *ctx, const uint8_t *buf, int len)
{
    (void)ctx;
    if (len > s_accept) len = s_accept;
    s_accept -= len;
    if (s_len + len < (int)sizeof(s_out)) memcpy(s_out + s_len, buf, (size_t)len);
    s_len += len;
    return len;
}
static int fake_read(void *ctx) { (void)ctx; return -1; }
static uint32_t fake_millis(void *ctx) { (void)ctx; return 0; }
static void fake_sleep(void *ctx, uint32_t ms) { (void)ctx; (void)ms; }

static const td_hal_t s_hal = { fake_read, fake_write, fake_millis, fake_sleep, NULL };
static td_buffer_t s_front, s_back;
static td_renderer_t s_r;

static void reset_output(void)
{
    s_len = 0;
    s_out[0] = '\0';
}

static bool output_is(const char *expect)
{
    s_out[s_len] = '\0';
    if (strcmp(s_out, expect) == 0) return true;
    printf("  output: ");
    for (int i = 0; i < s_len; i++) {
        unsigned char c = (unsigned char)s_out[i];
        if (c == 0x1b) printf("\\e");
        else if (c < 0x20 || c >= 0x7f) printf("\\x%02x", c);
        else putchar(c);
    }
    printf("\n");
    return false;
}

static void test_full_then_incremental(void)
{
    td_render_init(&s_r, &s_hal);
    td_buffer_init(&s_back, 80, 25);
    td_buffer_init(&s_front, 80, 25);
    td_buffer_invalidate(&s_front);

    /* First frame sends every cell. */
    reset_output();
    CHECK(td_render_diff(&s_r, &s_front, &s_back));
    CHECK(s_len >= 80 * 25);

    /* Nothing changed: nothing sent. */
    reset_output();
    CHECK(td_render_diff(&s_r, &s_front, &s_back));
    CHECK_EQ(s_len, 0);

    /* One cell changed: a cursor move and the character only. */
    td_draw_target(&s_back);
    td_putc(10, 5, 'X', 7, 0, 0);
    reset_output();
    td_render_diff(&s_r, &s_front, &s_back);
    CHECK(output_is("\x1b[6;11HX"));
    CHECK(s_len <= 12);

    /* Neighbouring cell: the cursor is already there, no move needed. */
    td_putc(11, 5, 'Y', 7, 0, 0);
    reset_output();
    td_render_diff(&s_r, &s_front, &s_back);
    CHECK(output_is("Y"));

    /* Colour change: only the changed SGR part is sent. */
    td_putc(12, 5, 'Z', 1, 0, 0);
    reset_output();
    td_render_diff(&s_r, &s_front, &s_back);
    CHECK(output_is("\x1b[31mZ"));

    /* 256-colour background plus bold (attribute changes reset first). */
    td_putc(40, 0, 'B', 15, 200, TD_BOLD);
    reset_output();
    td_render_diff(&s_r, &s_front, &s_back);
    CHECK(output_is("\x1b[1;41H\x1b[0;1;97;48;5;200mB"));
}

static void test_utf8_and_ascii_mode(void)
{
    td_render_init(&s_r, &s_hal);
    td_buffer_init(&s_back, 40, 12);
    td_buffer_init(&s_front, 40, 12);
    td_draw_target(&s_back);
    td_putc(0, 0, 0x2500, 7, 0, 0);
    td_render_reset_state(&s_r);

    reset_output();
    td_render_diff(&s_r, &s_front, &s_back);
    CHECK(output_is("\x1b[1;1H\x1b[0;37;40m\xE2\x94\x80"));

    td_set_ascii_mode(true);
    td_buffer_invalidate(&s_front);
    td_buffer_init(&s_back, 40, 12);
    td_draw_target(&s_back);
    td_putc(0, 0, 0x2500, 7, 0, 0);
    reset_output();
    td_render_diff(&s_r, &s_front, &s_back);
    s_out[s_len] = '\0';
    CHECK(strstr(s_out, "\xE2\x94\x80") == NULL);
    CHECK(strchr(s_out, '-') != NULL);
    td_set_ascii_mode(false);
}

static void test_last_column(void)
{
    /* After writing the last column the cursor position is unknown, so the
     * next cell always gets an explicit move. */
    td_render_init(&s_r, &s_hal);
    td_buffer_init(&s_back, 40, 12);
    td_buffer_init(&s_front, 40, 12);
    td_draw_target(&s_back);
    td_putc(39, 0, 'E', 7, 0, 0);
    td_putc(0, 1, 'N', 7, 0, 0);
    td_render_reset_state(&s_r);
    s_r.cur_fg = 7;
    s_r.cur_bg = 0;
    s_r.cur_attr = 0;
    reset_output();
    td_render_diff(&s_r, &s_front, &s_back);
    CHECK(output_is("\x1b[1;40HE\x1b[2;1HN"));
}

static void test_dropped_frame(void)
{
    td_render_init(&s_r, &s_hal);
    td_buffer_init(&s_back, 80, 25);
    td_buffer_init(&s_front, 80, 25);
    td_buffer_invalidate(&s_front);
    s_accept = 100;       /* the link stalls after 100 bytes */
    reset_output();
    CHECK(!td_render_diff(&s_r, &s_front, &s_back));
    CHECK(s_len <= 100);
    s_accept = 1 << 30;
}

int main(void)
{
    test_full_then_incremental();
    test_utf8_and_ascii_mode();
    test_last_column();
    test_dropped_frame();
    return TD_TEST_RESULT();
}
