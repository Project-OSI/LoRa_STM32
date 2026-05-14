#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "failed to open %s\n", path);
        exit(1);
    }
    if (fseek(f, 0, SEEK_END) != 0) exit(1);
    long n = ftell(f);
    if (n < 0) exit(1);
    rewind(f);
    char *buf = (char *)calloc((size_t)n + 1U, 1U);
    if (!buf) exit(1);
    if (fread(buf, 1U, (size_t)n, f) != (size_t)n) exit(1);
    fclose(f);
    return buf;
}

static void assert_contains(const char *haystack, const char *needle, const char *label) {
    if (!strstr(haystack, needle)) {
        fprintf(stderr, "FAIL %s: missing [%s]\n", label, needle);
        exit(1);
    }
}

static char *copy_between(const char *source, const char *start_marker, const char *end_marker, const char *label) {
    const char *start = strstr(source, start_marker);
    const char *end;
    size_t len;
    char *section;

    if (!start) {
        fprintf(stderr, "FAIL %s: missing start marker [%s]\n", label, start_marker);
        exit(1);
    }
    end = strstr(start, end_marker);
    if (!end) {
        fprintf(stderr, "FAIL %s: missing end marker [%s]\n", label, end_marker);
        exit(1);
    }
    len = (size_t)(end - start);
    section = (char *)calloc(len + 1U, 1U);
    if (!section) exit(1);
    memcpy(section, start, len);
    return section;
}

int main(void) {
    char *lora = read_file("../STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN(AT)/src/lora.c");
    char *at = read_file("../STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN(AT)/src/at.c");
    char *main_source = read_file("../STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN(AT)/src/main.c");
    char *at_mod_set = copy_between(at, "ATEerror_t at_MOD_set", "ATEerror_t at_MOD_get", "at_MOD_set");
    char *downlink_mod_case = copy_between(main_source, "case 0x0A:", "case 0x20:", "downlink MOD case");

    assert_contains(lora, "#ifdef USE_CHAMELEON\nuint8_t mode=3;\n#else\nuint8_t mode;\n#endif", "chameleon mode initializer");
    assert_contains(lora, "#ifdef USE_CHAMELEON\n\tmode=3;\n#else\n\tmode=1;\n#endif", "fdr chameleon mode");
    assert_contains(lora, "Chameleon firmware is MOD3-only", "eeprom force comment");
    assert_contains(lora, "#ifdef USE_CHAMELEON\n\tmode=3;\n#endif", "eeprom read forces mode 3");
    assert_contains(at_mod_set, "#ifdef USE_CHAMELEON\n\tif (workmode != 3)\n\t{\n\t\tPPRINTF(\"Chameleon firmware supports MOD=3 only\\r\\n\");\n\t\treturn AT_PARAM_ERROR;\n\t}\n#endif", "at mod rejects non-3 chameleon mode");
    assert_contains(at_mod_set, "Chameleon firmware supports MOD=3 only", "at mod chameleon message");
    assert_contains(downlink_mod_case, "#ifdef USE_CHAMELEON\n\t\t\t\t\tmode=0x03;\n#else\n\t\t\t\t\tmode=AppData->Buff[1];\n#endif", "downlink mod clamps chameleon mode");

    free(downlink_mod_case);
    free(at_mod_set);
    free(main_source);
    free(at);
    free(lora);
    puts("test_chameleon_fdr_defaults OK");
    return 0;
}
