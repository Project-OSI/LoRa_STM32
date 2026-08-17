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

static const char *skip_space(const char *position)
{
    while (*position == ' ' || *position == '\t' || *position == '\n' ||
           *position == '\r') {
        ++position;
    }
    return position;
}

static const char *find_matching(const char *open, char opening, char closing)
{
    const char *position;
    unsigned int depth = 0U;

    for (position = open; *position != '\0'; ++position) {
        if (*position == opening) {
            ++depth;
        } else if (*position == closing) {
            --depth;
            if (depth == 0U) {
                return position;
            }
        }
    }
    return NULL;
}

static int range_contains(const char *begin, const char *end,
                          const char *expected)
{
    const char *position;
    size_t expected_length = strlen(expected);

    for (position = begin; position + expected_length <= end; ++position) {
        if (memcmp(position, expected, expected_length) == 0) {
            return 1;
        }
    }
    return 0;
}

static const char *find_in_range(const char *begin, const char *end,
                                 const char *expected)
{
    const char *position;
    size_t expected_length = strlen(expected);

    for (position = begin; position + expected_length <= end; ++position) {
        if (memcmp(position, expected, expected_length) == 0) {
            return position;
        }
    }
    return NULL;
}

static void require_capture_timeout_guard(const char *source)
{
    const char *search = source;
    const char *loop_body = NULL;
    const char *loop_end = NULL;

    while ((search = strstr(search, "while")) != NULL) {
        const char *condition_start;
        const char *condition_end;
        const char *body_start;
        const char *body_end;

        if ((search != source &&
             ((search[-1] >= 'a' && search[-1] <= 'z') ||
              (search[-1] >= 'A' && search[-1] <= 'Z') ||
              search[-1] == '_')) ||
            ((search[5] >= 'a' && search[5] <= 'z') ||
             (search[5] >= 'A' && search[5] <= 'Z') || search[5] == '_')) {
            search += 5;
            continue;
        }
        condition_start = skip_space(search + 5);
        if (*condition_start != '(') {
            search += 5;
            continue;
        }
        condition_end = find_matching(condition_start, '(', ')');
        if (condition_end == NULL ||
            !range_contains(condition_start, condition_end,
                            "uwCaptureNumber") ||
            !range_contains(condition_start, condition_end, "!=") ||
            !range_contains(condition_start, condition_end, "2U")) {
            search += 5;
            continue;
        }
        body_start = skip_space(condition_end + 1);
        if (*body_start != '{') {
            search += 5;
            continue;
        }
        body_end = find_matching(body_start, '{', '}');
        if (body_end == NULL) {
            fprintf(stderr, "FAIL capture wait: unmatched loop brace\n");
            exit(1);
        }
        loop_body = body_start + 1;
        loop_end = body_end;
        break;
    }

    if (loop_body == NULL) {
        fprintf(stderr, "FAIL capture wait: missing uwCaptureNumber != 2U loop\n");
        exit(1);
    }

    search = loop_body;
    while ((search = find_in_range(search, loop_end, "if")) != NULL) {
        const char *condition_start;
        const char *condition_end;
        const char *fallback_start;
        const char *fallback_end;
        const char *stop;
        const char *deinit;
        const char *reset;
        const char *fallback;

        if ((search != loop_body &&
             ((search[-1] >= 'a' && search[-1] <= 'z') ||
              (search[-1] >= 'A' && search[-1] <= 'Z') ||
              search[-1] == '_')) ||
            ((search[2] >= 'a' && search[2] <= 'z') ||
             (search[2] >= 'A' && search[2] <= 'Z') || search[2] == '_')) {
            search += 2;
            continue;
        }
        condition_start = skip_space(search + 2);
        if (*condition_start != '(') {
            search += 2;
            continue;
        }
        condition_end = find_matching(condition_start, '(', ')');
        if (condition_end == NULL ||
            !range_contains(condition_start, condition_end,
                            "TimerGetElapsedTime") ||
            !range_contains(condition_start, condition_end, "captureStart")) {
            search += 2;
            continue;
        }
        fallback_start = skip_space(condition_end + 1);
        if (*fallback_start != '{') {
            search += 2;
            continue;
        }
        fallback_end = find_matching(fallback_start, '{', '}');
        if (fallback_end == NULL || fallback_end > loop_end) {
            fprintf(stderr, "FAIL capture wait: malformed timeout block\n");
            exit(1);
        }
        stop = find_in_range(fallback_start + 1, fallback_end,
                             "HAL_TIM_IC_Stop_IT");
        deinit = find_in_range(fallback_start + 1, fallback_end,
                               "HAL_TIM_IC_DeInit");
        reset = find_in_range(fallback_start + 1, fallback_end,
                              "uwCaptureNumber = 0U;");
        fallback = find_in_range(fallback_start + 1, fallback_end,
                                 "return LSI_FALLBACK_HZ;");
        if (stop == NULL || deinit == NULL || reset == NULL || fallback == NULL ||
            !(stop < deinit && deinit < reset && reset < fallback)) {
            fprintf(stderr,
                    "FAIL capture wait: timeout cleanup is missing or unordered\n");
            exit(1);
        }
        return;
    }

    fprintf(stderr,
            "FAIL capture wait: loop has no elapsed-time fallback with cleanup\n");
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
    const char *source_override = getenv("IWDG_GUARD_SOURCE");
    char *iwdg_c = source_override == NULL ? read_iwdg_file("iwdg.c") :
                                             read_file(source_override);
    char *iwdg_h = read_iwdg_file("iwdg.h");

    assert_contains(iwdg_c, "#define LSI_FALLBACK_HZ          37000U",
                    "fallback LSI frequency");
    assert_contains(iwdg_c, "#define LSI_CAPTURE_TIMEOUT_MS   100U",
                    "capture timeout");
    assert_contains(iwdg_c, "TimerGetCurrentTime()",
                    "capture start timestamp");
    require_capture_timeout_guard(iwdg_c);
    assert_not_contains(iwdg_h, "static uint32_t GetLSIFrequency(void);",
                        "private LSI prototype in header");

    free(iwdg_h);
    free(iwdg_c);
    puts("test_iwdg_guard OK");
    return 0;
}
