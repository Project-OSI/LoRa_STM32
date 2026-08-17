#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *read_file(const char *path)
{
    FILE *file = fopen(path, "rb");
    long size;
    char *contents;

    if (file == NULL) {
        fprintf(stderr, "failed to open %s\n", path);
        exit(1);
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        exit(1);
    }
    size = ftell(file);
    if (size < 0) {
        exit(1);
    }
    rewind(file);
    contents = (char *)calloc((size_t)size + 1U, 1U);
    if (contents == NULL) {
        exit(1);
    }
    if (fread(contents, 1U, (size_t)size, file) != (size_t)size) {
        exit(1);
    }
    fclose(file);
    return contents;
}

static char *read_iwdg_file(const char *name)
{
    static const char *const prefixes[] = {
        "../STM32CubeExpansion_LRWAN/Drivers/BSP/Components/iwdg/",
        "STM32CubeExpansion_LRWAN/Drivers/BSP/Components/iwdg/"
    };
    size_t index;
    char path[128];

    for (index = 0U; index < sizeof(prefixes) / sizeof(prefixes[0]); ++index) {
        FILE *file;

        (void)snprintf(path, sizeof(path), "%s%s", prefixes[index], name);
        file = fopen(path, "rb");
        if (file != NULL) {
            fclose(file);
            return read_file(path);
        }
    }

    fprintf(stderr, "failed to locate %s\n", name);
    exit(1);
}

static void assert_contains(const char *contents, const char *expected,
                            const char *label)
{
    if (strstr(contents, expected) == NULL) {
        fprintf(stderr, "FAIL %s: missing [%s]\n", label, expected);
        exit(1);
    }
}

static void assert_not_contains(const char *contents, const char *unexpected,
                                const char *label)
{
    if (strstr(contents, unexpected) != NULL) {
        fprintf(stderr, "FAIL %s: still contains [%s]\n", label, unexpected);
        exit(1);
    }
}

int main(void)
{
    char *iwdg_c = read_iwdg_file("iwdg.c");
    char *iwdg_h = read_iwdg_file("iwdg.h");

    assert_contains(iwdg_c, "#define LSI_FALLBACK_HZ          37000U",
                    "fallback LSI frequency");
    assert_contains(iwdg_c, "#define LSI_CAPTURE_TIMEOUT_MS   100U",
                    "capture timeout");
    assert_contains(iwdg_c, "TimerGetCurrentTime()",
                    "capture start timestamp");
    assert_contains(iwdg_c, "TimerGetElapsedTime(captureStart)",
                    "elapsed capture timeout check");
    assert_contains(iwdg_c, "return LSI_FALLBACK_HZ;",
                    "fallback return path");
    assert_not_contains(iwdg_c, "while(uwCaptureNumber != 2)\n  {\n  }",
                        "unbounded capture wait");
    assert_not_contains(iwdg_h, "static uint32_t GetLSIFrequency(void);",
                        "private LSI prototype in header");

    free(iwdg_h);
    free(iwdg_c);
    puts("test_iwdg_guard OK");
    return 0;
}
