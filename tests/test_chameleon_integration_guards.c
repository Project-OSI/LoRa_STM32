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
    char *hal_msp = read_source("stm32l0xx_hal_msp.c");
    char *hw = read_source("stm32l0xx_hw.c");
    char *chameleon_hw = read_source("chameleon_lsn50_hw.c");

    require_text(bsp, "#include \"chameleon_lsn50_hw.h\"", "lifecycle include");
    require_text(bsp, "chameleon_lsn50_acquire(&g_chameleon_last_sample",
                 "MOD3 lifecycle acquisition");
    require_text(bsp, "Chameleon result:%s attempts:%u flags:0x%02x",
                 "exact serial result and attempt diagnostics");
    require_text(bsp,
                 "Chameleon I2C2 acquisition enabled [5v-reg field-debug-7 100khz]",
                 "5V-regulator field-build identity");
    require_text(bsp,
                 "Chameleon I2C2 acquisition enabled [vcc-pmos]",
                 "VCC-PMOS field-build identity");
    require_text(at, "[CHAM-DBG1] command-enter",
                 "field diagnostic command boundary");
    require_text(bsp, "[CHAM-DBG1] sensor-enter",
                 "field diagnostic sensor boundary");
    require_text(bsp, "[CHAM-DBG1] battery=%u",
                 "field diagnostic battery boundary");
    require_text(bsp, "[CHAM-DBG1] battery-print-ok",
                 "field diagnostic floating-point print boundary");
    require_text(bsp, "[CHAM-DBG1] adc-ok",
                 "field diagnostic ADC boundary");
    require_text(bsp, "[CHAM-DBG1] acquire=%u",
                 "field diagnostic acquisition boundary");
    require_text(bsp,
                 "[CHAM-DBG4] probes=%lu hal=%lu err=0x%08lx state=0x%08lx isr=0x%08lx lines=0x%02lx",
                 "field diagnostic I2C2 probe report");
    require_text(main_source, "[CHAM-DBG1] reset=%s",
                 "field diagnostic reset cause");
    require_text(main_source, "[CHAM-DBG2] last-stage=%lu",
                 "retained field diagnostic stage report");
    require_text(main_source, "[CHAM-DBG3] raw-stage=0x%08lx",
                 "raw retained field diagnostic report");
    require_text(command, "chameleon_field_debug_set_stage(90U);",
                 "retained command-dispatch stage");
    require_text(at, "chameleon_field_debug_set_stage(77U);",
                 "retained marker self-test stage");
    require_text(at, "chameleon_field_debug_set_stage(1U);",
                 "retained command-entry stage");
    require_text(at,
                 "ATEerror_t at_getsensorvaule_set(const char *param)\n{\n\tint stus;",
                 "GETSENSORVALUE percent-d destination type");
    require_text(bsp, "chameleon_field_debug_set_stage(3U);",
                 "retained pre-battery stage");
    require_text(bsp, "chameleon_field_debug_set_stage(4U);",
                 "retained post-battery stage");
    require_text(bsp, "chameleon_field_debug_set_stage(9U);",
                 "retained pre-acquisition stage");
    require_text(bsp, "chameleon_field_debug_set_stage(10U);",
                 "retained post-acquisition stage");
    require_text(chameleon_hw, "RTC->BKP4R = CHAMELEON_FIELD_DEBUG_MAGIC | stage;",
                 "RTC backup stage persistence");
    require_text(chameleon_hw, "HAL_I2C_GetError(&chameleon_i2c2)",
                 "I2C2 HAL error capture");
    require_text(chameleon_hw, "I2C2->ISR",
                 "I2C2 peripheral status capture");
    require_text(chameleon_hw, "GPIOB->IDR",
                 "I2C2 live line-state capture");
    require_text(chameleon_hw,
                 "#define CHAMELEON_I2C_TIMING_100KHZ 0x10A13E56U",
                 "field diagnostic 100 kHz timing");
    require_text(chameleon_hw,
                 "#define CHAMELEON_I2C_TIMING_400KHZ 0x00B1112EU",
                 "production 400 kHz timing");
    require_text(chameleon_hw,
                 "#ifdef CHAMELEON_FIELD_DEBUG\n#define CHAMELEON_I2C_TIMING CHAMELEON_I2C_TIMING_100KHZ\n#else\n#define CHAMELEON_I2C_TIMING CHAMELEON_I2C_TIMING_400KHZ\n#endif",
                 "debug-only 100 kHz timing selection");
    require_text(irq, "chameleon_field_debug_set_stage(91U);",
                 "retained HardFault stage");
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
    require_text(chameleon_hw, "#define CHAMELEON_I2C_TXN_MS       1000U",
                 "working-firmware HAL transaction timeout");

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
