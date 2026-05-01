#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *read_file(const char *path, long *size)
{
    FILE *file = fopen(path, "rb");
    char *data;
    long len;

    if (!file) {
        perror(path);
        return NULL;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }
    len = ftell(file);
    if (len < 0) {
        fclose(file);
        return NULL;
    }
    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }

    data = malloc((size_t)len + 1U);
    if (!data) {
        fclose(file);
        return NULL;
    }
    if (fread(data, 1, (size_t)len, file) != (size_t)len) {
        free(data);
        fclose(file);
        return NULL;
    }
    data[len] = '\0';
    fclose(file);
    *size = len;
    return data;
}

static const char *matching_function_end(const char *open_brace)
{
    int depth = 0;

    for (const char *p = open_brace; *p != '\0'; ++p) {
        if (*p == '{') {
            ++depth;
        } else if (*p == '}') {
            --depth;
            if (depth == 0) {
                return p;
            }
        }
    }
    return NULL;
}

int main(void)
{
    static const char path[] =
        "../STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/"
        "DRAGINO-LRWAN(AT)/src/stm32l0xx_hw.c";
    long size = 0;
    char *source = read_file(path, &size);
    const char *start;
    const char *open_brace;
    const char *end;
    size_t len;
    char *body;

    (void)size;
    if (!source) {
        fprintf(stderr, "failed to read stm32l0xx_hw.c\n");
        return 1;
    }

    start = strstr(source, "uint8_t HW_GetBatteryLevel( void )");
    if (!start) {
        fprintf(stderr, "HW_GetBatteryLevel function not found\n");
        free(source);
        return 1;
    }
    open_brace = strchr(start, '{');
    if (!open_brace) {
        fprintf(stderr, "HW_GetBatteryLevel opening brace not found\n");
        free(source);
        return 1;
    }
    end = matching_function_end(open_brace);
    if (!end) {
        fprintf(stderr, "HW_GetBatteryLevel closing brace not found\n");
        free(source);
        return 1;
    }

    len = (size_t)(end - start);
    body = malloc(len + 1U);
    if (!body) {
        free(source);
        return 1;
    }
    memcpy(body, start, len);
    body[len] = '\0';

    if (strstr(body, "batteryLevelmV")) {
        fprintf(stderr, "HW_GetBatteryLevel still references batteryLevelmV typo\n");
        free(body);
        free(source);
        return 1;
    }
    if (!strstr(body, "batteryLevel_mV")) {
        fprintf(stderr, "HW_GetBatteryLevel no longer updates global batteryLevel_mV\n");
        free(body);
        free(source);
        return 1;
    }

    free(body);
    free(source);
    puts("test_battery_level_typo OK");
    return 0;
}
