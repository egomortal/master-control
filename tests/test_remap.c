/* The remap layer: maps, their files, and what reaches the virtual pad. */
#include "../src/remap.h"

#include <sys/stat.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int g_fail;
#define CHECK(c) do { if (!(c)) { printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); g_fail++; } } while (0)

#define DIR_ "build/test_remap.d"
#define VID 0x045E
#define PID 0x0B13

static pad_state state(uint32_t buttons, uint8_t extra)
{
    pad_state s;

    pad_state_reset(&s);
    s.buttons = buttons;
    s.extra = extra;
    s.lx = 10; s.ly = 20; s.rx = 200; s.ry = 220;
    return s;
}

static int set(const char *text)
{
    const char *err = "";
    int line = 0;
    return remap_set(VID, PID, text, &err, &line);
}

static void clean_dir(void)
{
    unlink(DIR_ "/045E_0B13.map");
    unlink(DIR_ "/045E_0B13.map.tmp");
    unlink(DIR_ "/1234_ABCD.map");
    unlink(DIR_ "/BAD.map");
    rmdir(DIR_);
}

int main(void)
{
    pad_state s, before;

    mkdir("build", 0755);
    clean_dir();

    printf("a missing directory is created, and empty is fine\n");
    remap_init(DIR_);
    {
        struct stat sb;
        CHECK(stat(DIR_, &sb) == 0 && S_ISDIR(sb.st_mode));
    }
    printf("a directory that cannot exist is fine too\n");
    remap_init("build/no/such/parent/remap");
    s = state(PAD_CROSS, PAD_X_P1);
    remap_apply(VID, PID, &s);
    CHECK(s.buttons == PAD_CROSS);
    remap_init(DIR_);

    printf("identity by default; extra buttons do nothing\n");
    s = state(PAD_CROSS | PAD_L2 | PAD_UP, PAD_X_P1 | PAD_X_M6);
    s.l2 = 200;
    before = s;
    remap_apply(VID, PID, &s);
    CHECK(memcmp(&s, &before, sizeof s) == 0);
    remap_note_device(VID, PID, "Xbox Wireless Controller");
    remap_apply(VID, PID, &s);
    CHECK(memcmp(&s, &before, sizeof s) == 0);

    printf("an identity map does nothing either\n");
    CHECK(set("# nothing\n\ncross = cross\n"));
    s = before;
    remap_apply(VID, PID, &s);
    CHECK(s.buttons == before.buttons && s.extra == before.extra && s.l2 == 200);

    printf("button swap\n");
    CHECK(set("cross = circle\ncircle = cross\n"));
    s = state(PAD_CROSS | PAD_SQUARE, 0);
    remap_apply(VID, PID, &s);
    CHECK(s.buttons == (PAD_CIRCLE | PAD_SQUARE));
    s = state(PAD_CIRCLE, 0);
    remap_apply(VID, PID, &s);
    CHECK(s.buttons == PAD_CROSS);
    printf("another model is not touched\n");
    s = state(PAD_CROSS, 0);
    remap_apply(0x054C, 0x05C4, &s);
    CHECK(s.buttons == PAD_CROSS);

    printf("extra buttons to DualSense buttons, many to one, none\n");
    CHECK(set("p1 = cross\nm2 = triangle\nm6 = cross\nsquare = cross\nps = none\n"));
    s = state(0, PAD_X_P1);
    remap_apply(VID, PID, &s);
    CHECK(s.buttons == PAD_CROSS);
    s = state(0, PAD_X_P2);                       /* m2 is p2 */
    remap_apply(VID, PID, &s);
    CHECK(s.buttons == PAD_TRIANGLE);
    s = state(PAD_SQUARE | PAD_CROSS, PAD_X_M6 | PAD_X_P1);
    remap_apply(VID, PID, &s);
    CHECK(s.buttons == PAD_CROSS);
    s = state(PAD_PS | PAD_OPTIONS, PAD_X_P3);    /* p3 still does nothing */
    remap_apply(VID, PID, &s);
    CHECK(s.buttons == PAD_OPTIONS);
    printf("bits outside the table pass through\n");
    s = state(0x00400000u | PAD_SQUARE, 0);
    remap_apply(VID, PID, &s);
    CHECK(s.buttons == (0x00400000u | PAD_CROSS));

    printf("stick swap and Y inversion\n");
    CHECK(set("swap_sticks = 1\ninvert_ly = yes\n"));
    s = state(0, 0);
    remap_apply(VID, PID, &s);
    CHECK(s.lx == 200 && s.ly == 256 - 220 && s.rx == 10 && s.ry == 20);
    CHECK(set("invert_ry = on\n"));
    s = state(0, 0);
    s.ry = 128;
    remap_apply(VID, PID, &s);
    CHECK(s.ry == 128 && s.lx == 10 && s.ly == 20 && s.rx == 200);
    s.ry = 0;
    remap_apply(VID, PID, &s);
    CHECK(s.ry == 255);
    s.ry = 255;
    remap_apply(VID, PID, &s);
    CHECK(s.ry == 1);

    printf("trigger swap carries the analog values\n");
    CHECK(set("swap_triggers = 1\n"));
    s = state(PAD_L2, 0);
    s.l2 = 200; s.r2 = 5;
    remap_apply(VID, PID, &s);
    CHECK(s.buttons == PAD_R2 && s.r2 == 200 && s.l2 == 5);
    printf("... and so does swapping them in the table\n");
    CHECK(set("l2 = r2\nr2 = l2\n"));
    s = state(PAD_L2, 0);
    s.l2 = 200; s.r2 = 5;
    remap_apply(VID, PID, &s);
    CHECK(s.buttons == PAD_R2 && s.r2 == 200 && s.l2 == 5);
    printf("a trigger on a face button: no analog left; a button on a trigger: full\n");
    CHECK(set("l2 = cross\nr1 = r2\n"));
    s = state(PAD_L2, 0);
    s.l2 = 200;
    remap_apply(VID, PID, &s);
    CHECK(s.buttons == PAD_CROSS && s.l2 == 0);
    s = state(PAD_R1, 0);
    remap_apply(VID, PID, &s);
    CHECK(s.buttons == PAD_R2 && s.r2 == 255);
    CHECK(set("p2 = l2\n"));
    s = state(0, PAD_X_P2);
    remap_apply(VID, PID, &s);
    CHECK(s.buttons == PAD_L2 && s.l2 == 255);
    printf("both triggers on one: the larger value\n");
    CHECK(set("r2 = l2\n"));
    s = state(PAD_L2 | PAD_R2, 0);
    s.l2 = 100; s.r2 = 180;
    remap_apply(VID, PID, &s);
    CHECK(s.buttons == PAD_L2 && s.l2 == 180 && s.r2 == 0);

    printf("hair trigger\n");
    CHECK(set("hair_r2 = 1\n"));
    s = state(0, 0);
    s.r2 = REMAP_HAIR + 1; s.l2 = REMAP_HAIR + 1;
    remap_apply(VID, PID, &s);
    CHECK(s.r2 == 255 && (s.buttons & PAD_R2) && s.l2 == REMAP_HAIR + 1 && !(s.buttons & PAD_L2));
    s = state(0, 0);
    s.r2 = REMAP_HAIR;
    remap_apply(VID, PID, &s);
    CHECK(s.r2 == 0 && !(s.buttons & PAD_R2));
    s = state(PAD_R2, 0);                         /* a digital-only trigger */
    remap_apply(VID, PID, &s);
    CHECK(s.r2 == 255 && (s.buttons & PAD_R2));
    CHECK(set("hair_l2 = 1\nswap_triggers = 1\n"));    /* follows the swap */
    s = state(0, 0);
    s.r2 = 40;
    remap_apply(VID, PID, &s);
    CHECK(s.l2 == 255 && (s.buttons & PAD_L2) && s.r2 == 0);

    printf("parse and format round-trip\n");
    {
        remap_map m, m2;
        char text[REMAP_TEXT_MAX], name[REMAP_NAME_MAX] = "";
        int bad = -1, n, i;
        remap_default(&m);
        for (i = 0; i < REMAP_SRC_N; i++) m.target[i] = (uint8_t)((i * 7 + 3) % REMAP_BTN_N);
        m.target[5] = REMAP_NONE;
        m.swap_sticks = m.invert_ry = m.hair_l2 = 1;
        n = remap_format(&m, "Pad \"X\" \\ 1", text, sizeof text);
        CHECK(n > 0 && strstr(text, "name = Pad X  1\n") == text);
        remap_default(&m2);
        CHECK(remap_parse(text, &m2, name, sizeof name, &bad) && bad == 0);
        CHECK(memcmp(&m, &m2, sizeof m) == 0);
        CHECK(strcmp(name, "Pad X  1") == 0);
        remap_default(&m);
        CHECK(remap_format(&m, "", text, sizeof text) == 0 && text[0] == '\0');
        CHECK(remap_format(&m2, "", text, 10) == -1);
        printf("aliases and case\n");
        remap_default(&m);
        CHECK(remap_parse("Share = PS\r\nM1 = Cross\nSWAP_STICKS = true\n", &m, NULL, 0, &bad));
        CHECK(m.target[11] == 13 && m.target[18] == 0 && m.swap_sticks == 1);
    }

    printf("garbage is refused, with its line, and changes nothing\n");
    {
        static const char *const bad_texts[] = {
            "cross = circle\njump = cross\n", "cross = circle\ncross\n", "cross = p1\n",
            "swap_sticks = maybe\n", "= cross\n", "cross = \n", "p7 = cross\n", "cross == circle\n",
        };
        const char *err = "";
        char longline[400];
        int line = 0;
        size_t i;
        CHECK(set("cross = circle\n"));
        for (i = 0; i < sizeof bad_texts / sizeof bad_texts[0]; i++) {
            CHECK(!remap_set(VID, PID, bad_texts[i], &err, &line));
            CHECK(strcmp(err, "syntax") == 0 && line >= 1);
        }
        CHECK(!remap_set(VID, PID, "cross = circle\njump = cross\n", &err, &line) && line == 2);
        memset(longline, 'a', sizeof longline - 1);
        longline[sizeof longline - 1] = '\0';
        CHECK(!remap_set(VID, PID, longline, &err, &line));
        s = state(PAD_CROSS, 0);
        remap_apply(VID, PID, &s);
        CHECK(s.buttons == PAD_CIRCLE);
    }

    printf("saved to a file, loaded again after a restart\n");
    {
        FILE *f;
        char buf[512];
        size_t n;
        CHECK(set("p1 = cross\ninvert_ly = 1\n"));
        f = fopen(DIR_ "/045E_0B13.map", "r");
        CHECK(f != NULL);
        if (f) {
            n = fread(buf, 1, sizeof buf - 1, f);
            buf[n] = '\0';
            fclose(f);
            CHECK(strcmp(buf, "name = Xbox Wireless Controller\np1 = cross\ninvert_ly = 1\n") == 0);
        }
        CHECK(access(DIR_ "/045E_0B13.map.tmp", F_OK) != 0);
        f = fopen(DIR_ "/BAD.map", "w");               /* not a model's name: ignored */
        if (f) { fputs("cross = circle\n", f); fclose(f); }
        f = fopen(DIR_ "/1234_ABCD.map", "w");         /* a bad line: not used */
        if (f) { fputs("cross = circle\nwhat\n", f); fclose(f); }
        remap_init(DIR_);
        s = state(0, PAD_X_P1);
        remap_apply(VID, PID, &s);
        CHECK(s.buttons == PAD_CROSS && s.ly == 256 - 20);
        s = state(PAD_CROSS, 0);
        remap_apply(0x1234, 0xABCD, &s);
        CHECK(s.buttons == PAD_CROSS);
    }

    printf("the list for the menu\n");
    {
        remap_info r;
        CHECK(remap_get(0, &r));
        CHECK(r.vid == VID && r.pid == PID && r.saved && !r.seen);
        CHECK(strcmp(r.name, "Xbox Wireless Controller") == 0);
        CHECK(r.map.target[18] == 0 && r.map.invert_ly == 1);
        CHECK(!remap_get(1, &r));
        remap_note_device(0x054C, 0x05C4, "DualShock 4");
        remap_note_device(VID, PID, NULL);
        CHECK(remap_get(1, &r) && r.vid == 0x054C && r.seen && !r.saved && r.map.target[0] == 0);
        CHECK(remap_get(0, &r) && r.seen && strcmp(r.name, "Xbox Wireless Controller") == 0);
    }

    printf("reset: the file and the live map go\n");
    CHECK(remap_reset(VID, PID));
    CHECK(access(DIR_ "/045E_0B13.map", F_OK) != 0);
    s = state(0, PAD_X_P1);
    remap_apply(VID, PID, &s);
    CHECK(s.buttons == 0 && s.ly == 20);
    CHECK(!remap_reset(VID, PID));

    printf("a full table refuses new models, keeps the rest\n");
    {
        const char *err = "";
        int i, line;
        remap_init(NULL);
        for (i = 0; i < REMAP_MODELS; i++) remap_note_device(0x1000, (uint16_t)i, "x");
        remap_note_device(0x2000, 1, "y");
        CHECK(!remap_set(0x2000, 1, "cross = circle\n", &err, &line) && strcmp(err, "full") == 0);
        CHECK(remap_set(0x1000, 3, "cross = circle\n", &err, &line));
        s = state(PAD_CROSS, 0);
        remap_apply(0x1000, 3, &s);
        CHECK(s.buttons == PAD_CIRCLE);
        printf("many saves in a row: the latest wins\n");
        for (i = 0; i < 10; i++) CHECK(remap_set(0x1000, 3, i & 1 ? "cross = square\n" : "cross = triangle\n", &err, &line));
        s = state(PAD_CROSS, 0);
        remap_apply(0x1000, 3, &s);
        CHECK(s.buttons == PAD_SQUARE);
    }

    clean_dir();
    unlink(DIR_ "/1234_ABCD.map");
    if (g_fail) printf("test_remap: %d FAILED\n", g_fail);
    else printf("test_remap: all passed\n");
    return g_fail != 0;
}
