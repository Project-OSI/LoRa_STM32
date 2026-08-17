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

static void require_before(const char *source, const char *first,
                           const char *second, const char *label)
{
    const char *first_pos = strstr(source, first);
    const char *second_pos = strstr(source, second);

    if (first_pos == 0 || second_pos == 0 || first_pos >= second_pos) {
        fprintf(stderr, "FAIL wrong order for %s\n", label);
        exit(1);
    }
}

static void require_function_without(const char *source, const char *function,
                                     const char *forbidden,
                                     const char *label)
{
    const char *start = strstr(source, function);
    const char *end;
    size_t length;

    if (start == 0) {
        fprintf(stderr, "FAIL missing function for %s\n", label);
        exit(1);
    }
    end = strstr(start + strlen(function), "\nvoid ");
    length = end == 0 ? strlen(start) : (size_t)(end - start);
    if (strstr(start, forbidden) != 0
            && (size_t)(strstr(start, forbidden) - start) < length) {
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
    char *hal_msp = read_source("stm32l0xx_hal_msp.c");
    char *hw = read_source("stm32l0xx_hw.c");
    char *chameleon_hw = read_source("chameleon_lsn50_hw.c");

    require_text(bsp, "#include \"chameleon_lsn50_hw.h\"", "lifecycle include");
    require_text(bsp, "chameleon_lsn50_acquire(&g_chameleon_last_sample",
                 "MOD3 lifecycle acquisition");
    require_text(bsp, "Chameleon result:%s attempts:%u flags:0x%02x",
                 "exact serial result and attempt diagnostics");
    require_text(bsp,
                 "#ifdef USE_CHAMELEON\n\tif((mode!=3)&&(power_time!=0))",
                 "MOD3-only vendor +5V pulse guard");
    require_text(at,
                 "ATEerror_t at_getsensorvaule_set(const char *param)\n{\n\tint stus;",
                 "GETSENSORVALUE percent-d destination type");
    forbid_text(bsp, "chameleon_i2c1_init_400khz", "Chameleon I2C1 init");
    forbid_text(bsp, "chameleon_board_i2c_write(", "board adapter in BSP");

    require_text(bsp, "Chameleon acquisition enabled [soft-i2c-5v]",
                 "soft-I2C boot identity");
    require_function_without(bsp, "void HAL_I2C_MspInit(", "I2C2",
                             "I2C2 MSP initialization branch");
    require_function_without(bsp, "void HAL_I2C_MspDeInit(", "I2C2",
                             "I2C2 MSP deinitialization branch");
    require_text(bsp, "GPIO_EXTI14_IoInit(inmode);",
                 "unconditional BSP EXTI14 initialization");
    forbid_text(main_source, "PB14_DIGITAL_READ", "PB14 replacement macro");
    forbid_text(main_source, "switch_status=0;", "Chameleon PB14 forced-low status");
    require_text(main_source,
                 "switch_status=HAL_GPIO_ReadPin(GPIO_EXTI14_PORT,GPIO_EXTI14_PIN);",
                 "direct MOD3 PB14 status read");
    require_text(main_source, "GPIO_EXTI14_IoInit(inmode);",
                 "downlink EXTI14 reinitialization");
    require_text(at, "GPIO_EXTI14_IoInit(inmode);",
                 "AT EXTI14 reinitialization");
    require_text(main_source,
                 "#ifdef USE_CHAMELEON\n\t\t\t\tif(AppData->Buff[1]==0x03)",
                 "downlink mode locked to MOD3");
    require_text(at,
                 "#ifdef USE_CHAMELEON\n\tif(workmode!=3)",
                 "dedicated image rejects non-MOD3 requests");
    require_text(command,
                 "#ifdef USE_CHAMELEON\n\t\t\t\t\t\t\tif(strcmp(cmd,AT_MOD)==0)\n\t\t\t\t\t\t\t{\n\t\t\t\t\t\t\t\tstore_config_status=0;\n\t\t\t\t\t\t\t}\n#endif",
                 "dedicated MOD3 command skips EEPROM storage");
    require_text(irq, "if(__HAL_GPIO_EXTI_GET_IT(GPIO_PIN_14) != RESET)",
                 "IRQ EXTI14 handling");
    require_text(irq, "__HAL_GPIO_EXTI_CLEAR_IT(GPIO_PIN_14);",
                 "IRQ EXTI14 clear");
    require_text(irq, "HAL_GPIO_EXTI_Callback(GPIO_PIN_14);",
                 "IRQ EXTI14 callback");
    require_text(lora,
                 "mode=(r_config[14]>>24)&0xFF;\n#ifdef USE_CHAMELEON\n\tmode=3;\n#endif",
                 "dedicated MOD3 override after persisted config read");
    require_text(hal_msp,
                 "if (rtc_timebase_ready)\n  {\n    return TimerGetCurrentTime();\n  }",
                 "RTC-backed HAL timeout clock");
    require_text(hal_msp, "return uwTick;",
                 "safe HAL clock before RTC initialization");
    require_text(hw,
                 "HW_RTC_Init( );\n    HAL_RTC_TimebaseReady( );",
                 "HAL timeout clock activation after RTC initialization");
    require_text(hw,
                 "TimerGetElapsedTime(adc_wait_started) >= HW_ADC_TIMEOUT_MS",
                 "finite VREFINT wait");
    forbid_text(hw, "HAL_ADC_PollForConversion( &hadc, HAL_MAX_DELAY )",
                "infinite ADC conversion wait");
    forbid_text(chameleon_hw, "return HAL_GetTick();",
                "stalled Chameleon timeout clock");
    require_text(chameleon_hw, "return TimerGetCurrentTime();",
                 "RTC-backed Chameleon timeout clock");
    require_text(main_source, "uint32_t chameleon_reset_flags = RCC->CSR;",
                 "single raw reset-flag snapshot");
    require_text(main_source, "Chameleon reset:%s flags:0x%08lx",
                 "reset-cause report");
    require_text(main_source, "__HAL_RCC_CLEAR_RESET_FLAGS();",
                 "reset-flag clear");
    require_before(main_source, "uint32_t chameleon_reset_flags = RCC->CSR;",
                   "__HAL_RCC_CLEAR_RESET_FLAGS();", "reset snapshot before clear");
    require_before(main_source, "Chameleon reset:%s flags:0x%08lx",
                   "__HAL_RCC_CLEAR_RESET_FLAGS();", "reset report before clear");
    require_before(main_source, "RCC_CSR_IWDGRSTF", "RCC_CSR_WWDGRSTF",
                   "reset classification watchdog priority");
    require_before(main_source, "RCC_CSR_WWDGRSTF", "RCC_CSR_SFTRSTF",
                   "reset classification software priority");
    require_before(main_source, "RCC_CSR_SFTRSTF", "RCC_CSR_LPWRRSTF",
                   "reset classification low-power priority");
    require_before(main_source, "RCC_CSR_LPWRRSTF", "RCC_CSR_PORRSTF",
                   "reset classification POR priority");
    require_before(main_source, "RCC_CSR_PORRSTF", "RCC_CSR_PINRSTF",
                   "reset classification pin priority");
    require_before(main_source, "RCC_CSR_PINRSTF", "RCC_CSR_OBLRSTF",
                   "reset classification option-byte priority");
    require_before(main_source, "RCC_CSR_OBLRSTF", "RCC_CSR_FWRSTF",
                   "reset classification firewall priority");

    forbid_text(bsp, "CHAMELEON_FIELD_DEBUG", "obsolete BSP debug switch");
    forbid_text(main_source, "CHAMELEON_FIELD_DEBUG", "obsolete main debug switch");
    forbid_text(at, "CHAMELEON_FIELD_DEBUG", "obsolete AT debug switch");
    forbid_text(command, "CHAMELEON_FIELD_DEBUG", "obsolete command debug switch");
    forbid_text(irq, "CHAMELEON_FIELD_DEBUG", "obsolete IRQ debug switch");
    forbid_text(chameleon_hw, "CHAMELEON_FIELD_DEBUG", "obsolete hardware debug switch");
    forbid_text(bsp, "[CHAM-DBG", "obsolete BSP debug output");
    forbid_text(main_source, "[CHAM-DBG", "obsolete main debug output");
    forbid_text(at, "[CHAM-DBG", "obsolete AT debug output");
    forbid_text(chameleon_hw, "I2C2", "obsolete I2C2 adapter");
    forbid_text(chameleon_hw, "PB14", "obsolete PB14 adapter");
    forbid_text(bsp, "[5v-reg", "obsolete 5V-regulator banner");
    forbid_text(bsp, "[vcc-pmos]", "obsolete PMOS banner");

    free(bsp);
    free(main_source);
    free(at);
    free(command);
    free(lora);
    free(irq);
    free(hal_msp);
    free(hw);
    free(chameleon_hw);
    puts("test_chameleon_integration_guards OK");
    return 0;
}
