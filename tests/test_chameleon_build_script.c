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

static void require(const char *data, const char *text)
{
    if (strstr(data, text) == 0) {
        fprintf(stderr, "FAIL build script missing: %s\n", text);
        exit(1);
    }
}

int main(void)
{
    char *script = read_file("../build/build.sh");
    char *flags = read_file("../build/cflags.rsp");

    if (strstr(script, "/home/") || strstr(flags, "/home/")) {
        fprintf(stderr, "FAIL build inputs contain absolute checkout paths\n");
        return 1;
    }
    require(script, "chameleon-i2c2-vcc-pmos)");
    require(script, "CHAMELEON_POWER_EXTERNAL_PMOS");
    require(script, "LSN50-chameleon-i2c2-vcc-pmos");
    require(script, "chameleon-i2c2-5v-reg)");
    require(script, "CHAMELEON_POWER_LSN50_5V");
    require(script, "LSN50-chameleon-i2c2-5v-reg");
    require(script, "OBJDIR=\"./build/obj/${TARGET_VARIANT}\"");
    require(script, "src/chameleon_lsn50_hw.c");

    free(script);
    free(flags);
    puts("test_chameleon_build_script OK");
    return 0;
}
