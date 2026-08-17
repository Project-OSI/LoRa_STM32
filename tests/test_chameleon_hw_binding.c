#include "chameleon_lsn50_hw.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *read_source(const char *prefix, const char *name)
{
    char path[768];
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

static char *read_project_source(const char *name)
{
    static const char root_src[] =
        "STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/"
        "DRAGINO-LRWAN(AT)/src/";
    static const char tests_src[] =
        "../STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/"
        "DRAGINO-LRWAN(AT)/src/";
    FILE *file;
    char path[768];

    snprintf(path, sizeof(path), "%s%s", root_src, name);
    file = fopen(path, "rb");
    if (file != 0) {
        fclose(file);
        return read_source(root_src, name);
    }
    return read_source(tests_src, name);
}

static char *read_pwr_source(const char *name)
{
    static const char root_src[] =
        "STM32CubeExpansion_LRWAN/Drivers/BSP/Components/pwr_out/";
    static const char tests_src[] =
        "../STM32CubeExpansion_LRWAN/Drivers/BSP/Components/pwr_out/";
    FILE *file;
    char path[768];

    snprintf(path, sizeof(path), "%s%s", root_src, name);
    file = fopen(path, "rb");
    if (file != 0) {
        fclose(file);
        return read_source(root_src, name);
    }
    return read_source(tests_src, name);
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
    const char *first_at = strstr(source, first);
    const char *second_at = strstr(source, second);

    if (first_at == 0 || second_at == 0 || first_at >= second_at) {
        fprintf(stderr, "FAIL ordering %s\n", label);
        exit(1);
    }
}

static void test_tim2_clock_extends_ffff_wrap(void)
{
    chameleon_tim2_clock_t clock;

    chameleon_tim2_clock_reset(&clock, 0xfff0U);
    assert(chameleon_tim2_clock_update(&clock, 0xfffeU) == 14U);
    assert(chameleon_tim2_clock_update(&clock, 0x0008U) == 24U);
}

static void test_source_contract(void)
{
    char *source = read_project_source("chameleon_lsn50_hw.c");
    char *hw = read_project_source("stm32l0xx_hw.c");
    char *pwr = read_pwr_source("pwr_out.c");

    require_text(source, "#define CHAMELEON_SCL_PIN GPIO_PIN_13", "PB13 SCL");
    require_text(source, "#define CHAMELEON_SDA_PIN GPIO_PIN_12", "PB12 SDA");
    require_text(source, "GPIO_MODE_OUTPUT_OD", "open drain");
    require_text(source, "GPIO_NOPULL", "no pull");
    require_text(source, "GPIO_MODE_ANALOG", "bus isolation");
    require_text(source, "TIM2->PSC", "TIM2 prescaler");
    require_text(source, "TIM2->CNT", "TIM2 counter");
    require_text(source, "TIM2->ARR = 0xffffU;", "16 bit TIM2 period");
    require_text(source, "chameleon_soft_i2c_write(", "soft write adapter");
    require_text(source, "chameleon_soft_i2c_write_read(", "soft read adapter");
    require_text(source, "#if defined(DEBUG) && defined(USE_CHAMELEON)",
                 "DEBUG compile guard");
    require_text(source, "IWDG_Refresh()", "watchdog binding");
    require_text(source, "chameleon_soft_i2c_bus_clear", "bus clear binding");
    require_text(source, "HAL_GPIO_WritePin(GPIOB, CHAMELEON_SCL_PIN | CHAMELEON_SDA_PIN, GPIO_PIN_SET)",
                 "bus latch before mode");
    require_text(source, "TIM2->CR1 &= ~TIM_CR1_CEN", "TIM2 stop");
    require_text(source, "__HAL_RCC_TIM2_CLK_DISABLE", "TIM2 clock disable");
    require_text(hw, "RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;",
                 "APB1 clock contract");
    require_text(pwr, "HAL_GPIO_WritePin(PWR_OUT_PORT,PWR_OUT_PIN,GPIO_PIN_SET);",
                 "PB5 latch before init");
    require_text(pwr, "GPIO_SPEED_FREQ_LOW", "PB5 low speed");
    require_before(pwr, "HAL_GPIO_WritePin(PWR_OUT_PORT,PWR_OUT_PIN,GPIO_PIN_SET);",
                   "HAL_GPIO_Init(PWR_OUT_PORT, &GPIO_InitStruct);",
                   "PB5 latch before output mode");
    require_text(source,
                 "HAL_GPIO_WritePin(GPIOB, CHAMELEON_SCL_PIN | CHAMELEON_SDA_PIN, GPIO_PIN_SET);\n"
                 "    memset(&gpio, 0, sizeof(gpio));\n"
                 "    gpio.Pin = CHAMELEON_SCL_PIN | CHAMELEON_SDA_PIN;\n"
                 "    gpio.Mode = GPIO_MODE_OUTPUT_OD;",
                 "bus latch before output mode");
    forbid_text(source, "I2C2", "I2C2 adapter");
    forbid_text(source, "HAL_I2C_", "HAL I2C adapter");
    forbid_text(source, "GPIO_AF5_I2C2", "I2C2 GPIO AF");
    forbid_text(source, "GPIO_PIN_14", "PB14 adapter");
    forbid_text(source, "TIM2->ARR = 0xffffffffU", "32 bit TIM2 period");
    forbid_text(source, "CHAMELEON_POWER_EXTERNAL_PMOS", "external PMOS backend");
    forbid_text(source, "GPIO_MODE_OUTPUT_PP", "push pull bus");
    free(source);
    free(hw);
    free(pwr);
}

int main(void)
{
    test_tim2_clock_extends_ffff_wrap();
    test_source_contract();
    puts("test_chameleon_hw_binding OK");
    return 0;
}
