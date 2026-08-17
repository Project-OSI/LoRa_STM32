#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *read_file(const char *path)
{
    FILE *file = fopen(path, "rb");
    char *data;
    long size;
    if (!file || fseek(file, 0, SEEK_END) != 0 || (size = ftell(file)) < 0
            || fseek(file, 0, SEEK_SET) != 0) {
        perror(path);
        exit(1);
    }
    data = malloc((size_t)size + 1U);
    if (!data || fread(data, 1U, (size_t)size, file) != (size_t)size) exit(1);
    data[size] = '\0';
    fclose(file);
    return data;
}

static char *read_from_candidates(const char *const *paths, size_t path_count)
{
    size_t index;

    for (index = 0U; index < path_count; ++index) {
        FILE *file = fopen(paths[index], "rb");

        if (file != NULL) {
            fclose(file);
            return read_file(paths[index]);
        }
    }

    fprintf(stderr, "FAIL could not locate build input\n");
    exit(1);
}

static void require(const char *data, const char *text)
{
    if (strstr(data, text) == 0) {
        fprintf(stderr, "FAIL build script missing: %s\n", text);
        exit(1);
    }
}

static void forbid(const char *data, const char *text)
{
    if (strstr(data, text) != 0) {
        fprintf(stderr, "FAIL build script contains forbidden text: %s\n", text);
        exit(1);
    }
}

int main(void)
{
    static const char *const script_paths[] = {
        "build/build.sh",
        "../build/build.sh"
    };
    static const char *const flag_paths[] = {
        "build/cflags.rsp",
        "../build/cflags.rsp"
    };
    static const char *const ignore_paths[] = {
        "build/.gitignore",
        "../build/.gitignore"
    };
    static const char *const hardware_paths[] = {
        "STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/"
        "DRAGINO-LRWAN(AT)/src/chameleon_lsn50_hw.c",
        "../STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/"
        "DRAGINO-LRWAN(AT)/src/chameleon_lsn50_hw.c"
    };
    char *script = read_from_candidates(script_paths,
        sizeof(script_paths) / sizeof(script_paths[0]));
    char *flags = read_from_candidates(flag_paths,
        sizeof(flag_paths) / sizeof(flag_paths[0]));
    char *ignore = read_from_candidates(ignore_paths,
        sizeof(ignore_paths) / sizeof(ignore_paths[0]));
    char *hardware = read_from_candidates(hardware_paths,
        sizeof(hardware_paths) / sizeof(hardware_paths[0]));

    if (strstr(script, "/home/") || strstr(flags, "/home/")) {
        fprintf(stderr, "FAIL build inputs contain absolute checkout paths\n");
        return 1;
    }
    require(script, "chameleon-soft-i2c-5v)");
    require(script, "LSN50-chameleon-soft-i2c-5v");
    require(script, "-DUSE_CHAMELEON");
    require(script, "CHAMELEON_POWER_LSN50_5V");
    require(script, "CHAMELEON_SOFT_I2C_PB12_PB13");
    require(script, "OBJDIR=\"./build/obj/${TARGET_VARIANT}\"");
    require(script, "src/chameleon_soft_i2c.c");
    require(script, "src/chameleon_lsn50_hw.c");
    forbid(script, "chameleon-i2c2-vcc-pmos)");
    forbid(script, "chameleon-i2c2-5v-reg)");
    forbid(script, "chameleon-i2c2-5v-reg-field-debug)");
    forbid(script, "CHAMELEON_POWER_EXTERNAL_PMOS");
    forbid(script, "CHAMELEON_FIELD_DEBUG");
    if (strstr(hardware, "GPIO_MODE_OUTPUT_PP") != 0) {
        fprintf(stderr, "FAIL PB5 must not use push-pull drive\n");
        return 1;
    }
    require(hardware, "gpio.Mode = GPIO_MODE_OUTPUT_OD;");
    require(hardware, "gpio.Pull = GPIO_PULLUP;");
    require(hardware, "#if defined(DEBUG) && defined(USE_CHAMELEON)");
    require(hardware, "#error \"DEBUG drives PB12/PB13 push-pull and is incompatible with Chameleon\"");
    require(ignore, "!LSN50-chameleon-soft-i2c-5v.bin");
    require(ignore, "!LSN50-chameleon-soft-i2c-5v.hex");

    free(script);
    free(flags);
    free(ignore);
    free(hardware);
    puts("test_chameleon_build_script OK");
    return 0;
}
