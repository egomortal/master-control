/* Console notification texts: every language takes the same printf
 * arguments as English (a mismatch would crash the payload), codes, and the
 * saved choice. */
#include "../src/i18n.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

/* The conversions of a format, e.g. "sd" for "%s ... %d"; '%%' is skipped. */
static void conversions(const char *f, char *out, size_t max)
{
    size_t n = 0;

    for (; *f; f++) {
        if (*f != '%') continue;
        f++;
        if (*f == '%') continue;
        while (*f && strchr("-+ #0123456789.", *f)) f++;
        if (*f && n + 1 < max) out[n++] = *f;
        if (!*f) break;
    }
    out[n] = '\0';
}

int main(void)
{
    const char *path = "build/test_language";
    char want[16], got[16];
    int l, m;
    FILE *f;

    printf("every translation has the English arguments, and is not empty\n");
    for (m = 0; m < MSG_COUNT; m++) {
        conversions(tr_in(0, m), want, sizeof want);
        for (l = 1; l < LANG_COUNT; l++) {
            conversions(tr_in(l, m), got, sizeof got);
            if (strcmp(want, got) != 0)
                printf("  %s message %d: '%s' vs English '%s'\n", i18n_code(l), m, got, want);
            CHECK(strcmp(want, got) == 0);
            CHECK(tr_in(l, m)[0] != '\0');
        }
    }

    printf("every console notification says Master Control, never PadBridge (all languages, and none left untranslated)\n");
    for (l = 0; l < LANG_COUNT; l++)
        for (m = 0; m < MSG_COUNT; m++) {
            if (m == MSG_CONTROLLER) continue;          /* a word, not a notification */
            CHECK(strncmp(tr_in(l, m), "Master Control", 14) == 0);
            CHECK(strstr(tr_in(l, m), "PadBridge") == NULL);
        }
    f = fopen("src/ps5_main.c", "r");
    CHECK(f != NULL);
    if (f) {
        char line[512];
        while (fgets(line, sizeof line, f)) {
            if (strstr(line, "notify(\"PadBridge")) printf("  untranslated: %s", line);
            CHECK(strstr(line, "notify(\"PadBridge") == NULL);
        }
        fclose(f);
    }

    printf("codes\n");
    CHECK(i18n_index("en") == 0 && i18n_index("ar") == 1 && i18n_index("it") == 9 && i18n_index("he") == 10);
    CHECK(i18n_index("xx") == -1 && i18n_index("") == -1 && i18n_index("EN") == -1 && i18n_index(NULL) == -1);
    CHECK(strcmp(i18n_code(8), "zh") == 0 && strcmp(i18n_code(99), "en") == 0);

    printf("English by default, then the saved choice\n");
    unlink(path);
    CHECK(i18n_get() == 0 && strstr(tr(MSG_CONNECTED), "connected"));
    CHECK(i18n_load(path) == 0);
    i18n_set(i18n_index("ja"));
    CHECK(i18n_save(path));
    i18n_set(0);
    CHECK(i18n_load(path) == i18n_index("ja") && strstr(tr(MSG_STOPPED), "停止"));

    printf("a damaged file falls back to English\n");
    f = fopen(path, "w");
    if (f) { fputs("xx\n", f); fclose(f); }
    CHECK(i18n_load(path) == 0);
    unlink(path);

    printf(fails ? "i18n: %d FAILED\n" : "i18n: all ok\n", fails);
    return fails != 0;
}
