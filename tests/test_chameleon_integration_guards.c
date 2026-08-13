#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *read_source(const char *name)
{
    static const char prefix[] =
        "../STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/"
        "DRAGINO-LRWAN(AT)/src/";
    char path[512];
    FILE *file;
    char *data;
    long size;

    snprintf(path, sizeof(path), "%s%s", prefix, name);
    file = fopen(path, "rb");
    if (!file || fseek(file, 0, SEEK_END) != 0 || (size = ftell(file)) < 0
            || fseek(file, 0, SEEK_SET) != 0) {
        perror(path);
        exit(1);
    }
    data = malloc((size_t)size + 1U);
    if (!data || fread(data, 1U, (size_t)size, file) != (size_t)size) {
        fprintf(stderr, "failed to read %s\n", path);
        exit(1);
    }
    data[size] = '\0';
    fclose(file);
    return data;
}

static void require_text(const char *source, const char *needle, const char *label)
{
    if (strstr(source, needle) == 0) {
        fprintf(stderr, "FAIL missing %s\n", label);
        exit(1);
    }
}

static void forbid_text(const char *source, const char *needle, const char *label)
{
    if (strstr(source, needle) != 0) {
        fprintf(stderr, "FAIL forbidden %s\n", label);
        exit(1);
    }
}

int main(void)
{
    char *bsp = read_source("bsp.c");
    char *main_source = read_source("main.c");
    char *at = read_source("at.c");
    char *command = read_source("command.c");
    char *lora = read_source("lora.c");
    char *irq = read_source("stm32l0xx_it.c");

    require_text(bsp, "#include \"chameleon_lsn50_hw.h\"", "lifecycle include");
    require_text(bsp, "chameleon_lsn50_acquire(&g_chameleon_last_sample",
                 "MOD3 lifecycle acquisition");
    require_text(bsp, "Chameleon result:%s attempts:%u flags:0x%02x",
                 "exact serial result and attempt diagnostics");
    forbid_text(bsp, "chameleon_i2c1_init_400khz", "Chameleon I2C1 init");
    forbid_text(bsp, "chameleon_board_i2c_write(", "board adapter in BSP");
    require_text(bsp,
                 "#ifndef USE_CHAMELEON\n\tGPIO_EXTI14_IoInit(inmode);\n#endif",
                 "BSP EXTI14 compile guard");

    require_text(main_source,
                 "#ifdef USE_CHAMELEON\n\t\tswitch_status=0;",
                 "MOD3 PB14 digital-read replacement");
    require_text(main_source,
                 "#ifndef USE_CHAMELEON\n\t\t\t\t\tGPIO_EXTI14_IoInit(inmode);\n#endif",
                 "downlink EXTI14 compile guard");
    require_text(main_source,
                 "#ifdef USE_CHAMELEON\n\t\t\t\tif(AppData->Buff[1]==0x03)",
                 "downlink mode locked to MOD3");
    require_text(at,
                 "#ifndef USE_CHAMELEON\n\tGPIO_EXTI14_IoInit(inmode);\n#endif",
                 "AT EXTI14 compile guard");
    require_text(at,
                 "#ifdef USE_CHAMELEON\n\tif(workmode!=3)",
                 "dedicated image rejects non-MOD3 requests");
    require_text(command,
                 "#ifdef USE_CHAMELEON\n\t\t\t\t\t\t\tif(strcmp(cmd,AT_MOD)==0)\n\t\t\t\t\t\t\t{\n\t\t\t\t\t\t\t\tstore_config_status=0;\n\t\t\t\t\t\t\t}\n#endif",
                 "dedicated MOD3 command skips EEPROM storage");
    require_text(irq,
                 "#ifndef USE_CHAMELEON\n if(__HAL_GPIO_EXTI_GET_IT(GPIO_PIN_14) != RESET)",
                 "IRQ EXTI14 compile guard");
    require_text(lora,
                 "mode=(r_config[14]>>24)&0xFF;\n#ifdef USE_CHAMELEON\n\tmode=3;\n#endif",
                 "dedicated MOD3 override after persisted config read");

    free(bsp);
    free(main_source);
    free(at);
    free(command);
    free(lora);
    free(irq);
    puts("test_chameleon_integration_guards OK");
    return 0;
}
