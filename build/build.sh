#!/bin/bash
set -e

TARGET_VARIANT="${1:-chameleon}"
case "$TARGET_VARIANT" in
  chameleon)
    TARGET_BASENAME="LSN50-chameleon"
    EXTRA_CFLAGS=(-UUSE_SHT -DUSE_CHAMELEON)
    ;;
  chameleon-dummy)
    TARGET_BASENAME="LSN50-chameleon-dummy"
    EXTRA_CFLAGS=(-UUSE_SHT -DUSE_CHAMELEON -DCHAMELEON_DUMMY)
    ;;
  *)
    echo "usage: $0 [chameleon|chameleon-dummy]" >&2
    exit 2
    ;;
esac

mkdir -p /home/phil/Repos/LoRa_STM32-claude/build/obj
OBJS=''

echo "AS startup"
arm-none-eabi-gcc -mcpu=cortex-m0plus -mthumb -x assembler-with-cpp -c '/home/phil/Repos/LoRa_STM32-claude/STM32CubeExpansion_LRWAN/Drivers/CMSIS/Device/ST/STM32L0xx/Source/Templates/gcc/startup_stm32l072xx.s' -o '/home/phil/Repos/LoRa_STM32-claude/build/obj/startup_stm32l072xx_e5a63952.o'
OBJS="$OBJS /home/phil/Repos/LoRa_STM32-claude/build/obj/startup_stm32l072xx_e5a63952.o"

echo "CC flash_eraseprogram.c"
arm-none-eabi-gcc @'/home/phil/Repos/LoRa_STM32-claude/build/cflags.rsp' "${EXTRA_CFLAGS[@]}" -c '/home/phil/Repos/LoRa_STM32-claude/STM32CubeExpansion_LRWAN/Drivers/BSP/Components/flash_eraseprogram/flash_eraseprogram.c' -o '/home/phil/Repos/LoRa_STM32-claude/build/obj/flash_eraseprogram_078ea408.o'
OBJS="$OBJS /home/phil/Repos/LoRa_STM32-claude/build/obj/flash_eraseprogram_078ea408.o"
echo "CC iwdg.c"
arm-none-eabi-gcc @'/home/phil/Repos/LoRa_STM32-claude/build/cflags.rsp' "${EXTRA_CFLAGS[@]}" -w -c '/home/phil/Repos/LoRa_STM32-claude/STM32CubeExpansion_LRWAN/Drivers/BSP/Components/iwdg/iwdg.c' -o '/home/phil/Repos/LoRa_STM32-claude/build/obj/iwdg_f9b75010.o'
OBJS="$OBJS /home/phil/Repos/LoRa_STM32-claude/build/obj/iwdg_f9b75010.o"
echo "CC sx1276.c"
arm-none-eabi-gcc @'/home/phil/Repos/LoRa_STM32-claude/build/cflags.rsp' "${EXTRA_CFLAGS[@]}" -c '/home/phil/Repos/LoRa_STM32-claude/STM32CubeExpansion_LRWAN/Drivers/BSP/Components/sx1276/sx1276.c' -o '/home/phil/Repos/LoRa_STM32-claude/build/obj/sx1276_174cd5fd.o'
OBJS="$OBJS /home/phil/Repos/LoRa_STM32-claude/build/obj/sx1276_174cd5fd.o"
echo "CC pwr_out.c"
arm-none-eabi-gcc @'/home/phil/Repos/LoRa_STM32-claude/build/cflags.rsp' "${EXTRA_CFLAGS[@]}" -c '/home/phil/Repos/LoRa_STM32-claude/STM32CubeExpansion_LRWAN/Drivers/BSP/Components/pwr_out/pwr_out.c' -o '/home/phil/Repos/LoRa_STM32-claude/build/obj/pwr_out_09116cf3.o'
OBJS="$OBJS /home/phil/Repos/LoRa_STM32-claude/build/obj/pwr_out_09116cf3.o"
echo "CC gpio_exti.c"
arm-none-eabi-gcc @'/home/phil/Repos/LoRa_STM32-claude/build/cflags.rsp' "${EXTRA_CFLAGS[@]}" -c '/home/phil/Repos/LoRa_STM32-claude/STM32CubeExpansion_LRWAN/Drivers/BSP/Components/gpio_exti/gpio_exti.c' -o '/home/phil/Repos/LoRa_STM32-claude/build/obj/gpio_exti_f80a3f3b.o'
OBJS="$OBJS /home/phil/Repos/LoRa_STM32-claude/build/obj/gpio_exti_f80a3f3b.o"
echo "CC oil_float.c"
arm-none-eabi-gcc @'/home/phil/Repos/LoRa_STM32-claude/build/cflags.rsp' "${EXTRA_CFLAGS[@]}" -c '/home/phil/Repos/LoRa_STM32-claude/STM32CubeExpansion_LRWAN/Drivers/BSP/Components/oil_float/oil_float.c' -o '/home/phil/Repos/LoRa_STM32-claude/build/obj/oil_float_a0c282d6.o'
OBJS="$OBJS /home/phil/Repos/LoRa_STM32-claude/build/obj/oil_float_a0c282d6.o"
echo "CC ds18b20.c"
arm-none-eabi-gcc @'/home/phil/Repos/LoRa_STM32-claude/build/cflags.rsp' "${EXTRA_CFLAGS[@]}" -c '/home/phil/Repos/LoRa_STM32-claude/STM32CubeExpansion_LRWAN/Drivers/BSP/Components/ds18b20/ds18b20.c' -o '/home/phil/Repos/LoRa_STM32-claude/build/obj/ds18b20_8a272139.o'
OBJS="$OBJS /home/phil/Repos/LoRa_STM32-claude/build/obj/ds18b20_8a272139.o"
echo "CC sht20.c"
arm-none-eabi-gcc @'/home/phil/Repos/LoRa_STM32-claude/build/cflags.rsp' "${EXTRA_CFLAGS[@]}" -c '/home/phil/Repos/LoRa_STM32-claude/STM32CubeExpansion_LRWAN/Drivers/BSP/Components/sht20/sht20.c' -o '/home/phil/Repos/LoRa_STM32-claude/build/obj/sht20_78cb122d.o'
OBJS="$OBJS /home/phil/Repos/LoRa_STM32-claude/build/obj/sht20_78cb122d.o"
echo "CC sht31.c"
arm-none-eabi-gcc @'/home/phil/Repos/LoRa_STM32-claude/build/cflags.rsp' "${EXTRA_CFLAGS[@]}" -c '/home/phil/Repos/LoRa_STM32-claude/STM32CubeExpansion_LRWAN/Drivers/BSP/Components/sht31/sht31.c' -o '/home/phil/Repos/LoRa_STM32-claude/build/obj/sht31_e71d2731.o'
OBJS="$OBJS /home/phil/Repos/LoRa_STM32-claude/build/obj/sht31_e71d2731.o"
echo "CC bh1750.c"
arm-none-eabi-gcc @'/home/phil/Repos/LoRa_STM32-claude/build/cflags.rsp' "${EXTRA_CFLAGS[@]}" -c '/home/phil/Repos/LoRa_STM32-claude/STM32CubeExpansion_LRWAN/Drivers/BSP/Components/bh1750/bh1750.c' -o '/home/phil/Repos/LoRa_STM32-claude/build/obj/bh1750_d36a6ef9.o'
OBJS="$OBJS /home/phil/Repos/LoRa_STM32-claude/build/obj/bh1750_d36a6ef9.o"
echo "CC lidar_lite_v3hp.c"
arm-none-eabi-gcc @'/home/phil/Repos/LoRa_STM32-claude/build/cflags.rsp' "${EXTRA_CFLAGS[@]}" -c '/home/phil/Repos/LoRa_STM32-claude/STM32CubeExpansion_LRWAN/Drivers/BSP/Components/lidar_lite_v3hp/lidar_lite_v3hp.c' -o '/home/phil/Repos/LoRa_STM32-claude/build/obj/lidar_lite_v3hp_29c3a030.o'
OBJS="$OBJS /home/phil/Repos/LoRa_STM32-claude/build/obj/lidar_lite_v3hp_29c3a030.o"
echo "CC tfsensor.c"
arm-none-eabi-gcc @'/home/phil/Repos/LoRa_STM32-claude/build/cflags.rsp' "${EXTRA_CFLAGS[@]}" -c '/home/phil/Repos/LoRa_STM32-claude/STM32CubeExpansion_LRWAN/Drivers/BSP/Components/tfsensor/tfsensor.c' -o '/home/phil/Repos/LoRa_STM32-claude/build/obj/tfsensor_f8521127.o'
OBJS="$OBJS /home/phil/Repos/LoRa_STM32-claude/build/obj/tfsensor_f8521127.o"
echo "CC ult.c"
arm-none-eabi-gcc @'/home/phil/Repos/LoRa_STM32-claude/build/cflags.rsp' "${EXTRA_CFLAGS[@]}" -c '/home/phil/Repos/LoRa_STM32-claude/STM32CubeExpansion_LRWAN/Drivers/BSP/Components/ult/ult.c' -o '/home/phil/Repos/LoRa_STM32-claude/build/obj/ult_3f609183.o'
OBJS="$OBJS /home/phil/Repos/LoRa_STM32-claude/build/obj/ult_3f609183.o"
echo "CC weight.c"
arm-none-eabi-gcc @'/home/phil/Repos/LoRa_STM32-claude/build/cflags.rsp' "${EXTRA_CFLAGS[@]}" -c '/home/phil/Repos/LoRa_STM32-claude/STM32CubeExpansion_LRWAN/Drivers/BSP/Components/weight/weight.c' -o '/home/phil/Repos/LoRa_STM32-claude/build/obj/weight_b45b8a79.o'
OBJS="$OBJS /home/phil/Repos/LoRa_STM32-claude/build/obj/weight_b45b8a79.o"
echo "CC stm32l0xx_nucleo.c"
arm-none-eabi-gcc @'/home/phil/Repos/LoRa_STM32-claude/build/cflags.rsp' "${EXTRA_CFLAGS[@]}" -c '/home/phil/Repos/LoRa_STM32-claude/STM32CubeExpansion_LRWAN/Drivers/BSP/STM32L0xx_Nucleo/stm32l0xx_nucleo.c' -o '/home/phil/Repos/LoRa_STM32-claude/build/obj/stm32l0xx_nucleo_36d0a9be.o'
OBJS="$OBJS /home/phil/Repos/LoRa_STM32-claude/build/obj/stm32l0xx_nucleo_36d0a9be.o"
echo "CC sx1276mb1las.c"
arm-none-eabi-gcc @'/home/phil/Repos/LoRa_STM32-claude/build/cflags.rsp' "${EXTRA_CFLAGS[@]}" -c '/home/phil/Repos/LoRa_STM32-claude/STM32CubeExpansion_LRWAN/Drivers/BSP/sx1276mb1las/sx1276mb1las.c' -o '/home/phil/Repos/LoRa_STM32-claude/build/obj/sx1276mb1las_a2dfe320.o'
OBJS="$OBJS /home/phil/Repos/LoRa_STM32-claude/build/obj/sx1276mb1las_a2dfe320.o"
echo "CC system_stm32l0xx.c"
arm-none-eabi-gcc @'/home/phil/Repos/LoRa_STM32-claude/build/cflags.rsp' "${EXTRA_CFLAGS[@]}" -c '/home/phil/Repos/LoRa_STM32-claude/STM32CubeExpansion_LRWAN/Drivers/CMSIS/Device/ST/STM32L0xx/Source/Templates/system_stm32l0xx.c' -o '/home/phil/Repos/LoRa_STM32-claude/build/obj/system_stm32l0xx_d1d9d34c.o'
OBJS="$OBJS /home/phil/Repos/LoRa_STM32-claude/build/obj/system_stm32l0xx_d1d9d34c.o"
echo "CC stm32l0xx_hal_spi.c"
arm-none-eabi-gcc @'/home/phil/Repos/LoRa_STM32-claude/build/cflags.rsp' "${EXTRA_CFLAGS[@]}" -c '/home/phil/Repos/LoRa_STM32-claude/STM32CubeExpansion_LRWAN/Drivers/STM32L0xx_HAL_Driver/Src/stm32l0xx_hal_spi.c' -o '/home/phil/Repos/LoRa_STM32-claude/build/obj/stm32l0xx_hal_spi_0d710a1d.o'
OBJS="$OBJS /home/phil/Repos/LoRa_STM32-claude/build/obj/stm32l0xx_hal_spi_0d710a1d.o"
echo "CC stm32l0xx_hal_uart.c"
arm-none-eabi-gcc @'/home/phil/Repos/LoRa_STM32-claude/build/cflags.rsp' "${EXTRA_CFLAGS[@]}" -c '/home/phil/Repos/LoRa_STM32-claude/STM32CubeExpansion_LRWAN/Drivers/STM32L0xx_HAL_Driver/Src/stm32l0xx_hal_uart.c' -o '/home/phil/Repos/LoRa_STM32-claude/build/obj/stm32l0xx_hal_uart_151166ad.o'
OBJS="$OBJS /home/phil/Repos/LoRa_STM32-claude/build/obj/stm32l0xx_hal_uart_151166ad.o"
echo "CC stm32l0xx_hal_adc.c"
arm-none-eabi-gcc @'/home/phil/Repos/LoRa_STM32-claude/build/cflags.rsp' "${EXTRA_CFLAGS[@]}" -c '/home/phil/Repos/LoRa_STM32-claude/STM32CubeExpansion_LRWAN/Drivers/STM32L0xx_HAL_Driver/Src/stm32l0xx_hal_adc.c' -o '/home/phil/Repos/LoRa_STM32-claude/build/obj/stm32l0xx_hal_adc_c7549bd9.o'
OBJS="$OBJS /home/phil/Repos/LoRa_STM32-claude/build/obj/stm32l0xx_hal_adc_c7549bd9.o"
echo "CC stm32l0xx_hal_rcc.c"
arm-none-eabi-gcc @'/home/phil/Repos/LoRa_STM32-claude/build/cflags.rsp' "${EXTRA_CFLAGS[@]}" -c '/home/phil/Repos/LoRa_STM32-claude/STM32CubeExpansion_LRWAN/Drivers/STM32L0xx_HAL_Driver/Src/stm32l0xx_hal_rcc.c' -o '/home/phil/Repos/LoRa_STM32-claude/build/obj/stm32l0xx_hal_rcc_5d6cbea2.o'
OBJS="$OBJS /home/phil/Repos/LoRa_STM32-claude/build/obj/stm32l0xx_hal_rcc_5d6cbea2.o"
echo "CC stm32l0xx_hal_rcc_ex.c"
arm-none-eabi-gcc @'/home/phil/Repos/LoRa_STM32-claude/build/cflags.rsp' "${EXTRA_CFLAGS[@]}" -c '/home/phil/Repos/LoRa_STM32-claude/STM32CubeExpansion_LRWAN/Drivers/STM32L0xx_HAL_Driver/Src/stm32l0xx_hal_rcc_ex.c' -o '/home/phil/Repos/LoRa_STM32-claude/build/obj/stm32l0xx_hal_rcc_ex_717f907f.o'
OBJS="$OBJS /home/phil/Repos/LoRa_STM32-claude/build/obj/stm32l0xx_hal_rcc_ex_717f907f.o"
echo "CC stm32l0xx_hal.c"
arm-none-eabi-gcc @'/home/phil/Repos/LoRa_STM32-claude/build/cflags.rsp' "${EXTRA_CFLAGS[@]}" -c '/home/phil/Repos/LoRa_STM32-claude/STM32CubeExpansion_LRWAN/Drivers/STM32L0xx_HAL_Driver/Src/stm32l0xx_hal.c' -o '/home/phil/Repos/LoRa_STM32-claude/build/obj/stm32l0xx_hal_48c60b3a.o'
OBJS="$OBJS /home/phil/Repos/LoRa_STM32-claude/build/obj/stm32l0xx_hal_48c60b3a.o"
echo "CC stm32l0xx_hal_cortex.c"
arm-none-eabi-gcc @'/home/phil/Repos/LoRa_STM32-claude/build/cflags.rsp' "${EXTRA_CFLAGS[@]}" -c '/home/phil/Repos/LoRa_STM32-claude/STM32CubeExpansion_LRWAN/Drivers/STM32L0xx_HAL_Driver/Src/stm32l0xx_hal_cortex.c' -o '/home/phil/Repos/LoRa_STM32-claude/build/obj/stm32l0xx_hal_cortex_96b83243.o'
OBJS="$OBJS /home/phil/Repos/LoRa_STM32-claude/build/obj/stm32l0xx_hal_cortex_96b83243.o"
echo "CC stm32l0xx_hal_gpio.c"
arm-none-eabi-gcc @'/home/phil/Repos/LoRa_STM32-claude/build/cflags.rsp' "${EXTRA_CFLAGS[@]}" -c '/home/phil/Repos/LoRa_STM32-claude/STM32CubeExpansion_LRWAN/Drivers/STM32L0xx_HAL_Driver/Src/stm32l0xx_hal_gpio.c' -o '/home/phil/Repos/LoRa_STM32-claude/build/obj/stm32l0xx_hal_gpio_6314eb15.o'
OBJS="$OBJS /home/phil/Repos/LoRa_STM32-claude/build/obj/stm32l0xx_hal_gpio_6314eb15.o"
echo "CC stm32l0xx_hal_dma.c"
arm-none-eabi-gcc @'/home/phil/Repos/LoRa_STM32-claude/build/cflags.rsp' "${EXTRA_CFLAGS[@]}" -c '/home/phil/Repos/LoRa_STM32-claude/STM32CubeExpansion_LRWAN/Drivers/STM32L0xx_HAL_Driver/Src/stm32l0xx_hal_dma.c' -o '/home/phil/Repos/LoRa_STM32-claude/build/obj/stm32l0xx_hal_dma_059f8871.o'
OBJS="$OBJS /home/phil/Repos/LoRa_STM32-claude/build/obj/stm32l0xx_hal_dma_059f8871.o"
echo "CC stm32l0xx_hal_pwr.c"
arm-none-eabi-gcc @'/home/phil/Repos/LoRa_STM32-claude/build/cflags.rsp' "${EXTRA_CFLAGS[@]}" -c '/home/phil/Repos/LoRa_STM32-claude/STM32CubeExpansion_LRWAN/Drivers/STM32L0xx_HAL_Driver/Src/stm32l0xx_hal_pwr.c' -o '/home/phil/Repos/LoRa_STM32-claude/build/obj/stm32l0xx_hal_pwr_cd42b393.o'
OBJS="$OBJS /home/phil/Repos/LoRa_STM32-claude/build/obj/stm32l0xx_hal_pwr_cd42b393.o"
echo "CC stm32l0xx_hal_pwr_ex.c"
arm-none-eabi-gcc @'/home/phil/Repos/LoRa_STM32-claude/build/cflags.rsp' "${EXTRA_CFLAGS[@]}" -c '/home/phil/Repos/LoRa_STM32-claude/STM32CubeExpansion_LRWAN/Drivers/STM32L0xx_HAL_Driver/Src/stm32l0xx_hal_pwr_ex.c' -o '/home/phil/Repos/LoRa_STM32-claude/build/obj/stm32l0xx_hal_pwr_ex_1298891d.o'
OBJS="$OBJS /home/phil/Repos/LoRa_STM32-claude/build/obj/stm32l0xx_hal_pwr_ex_1298891d.o"
echo "CC stm32l0xx_hal_rtc.c"
arm-none-eabi-gcc @'/home/phil/Repos/LoRa_STM32-claude/build/cflags.rsp' "${EXTRA_CFLAGS[@]}" -c '/home/phil/Repos/LoRa_STM32-claude/STM32CubeExpansion_LRWAN/Drivers/STM32L0xx_HAL_Driver/Src/stm32l0xx_hal_rtc.c' -o '/home/phil/Repos/LoRa_STM32-claude/build/obj/stm32l0xx_hal_rtc_760a24a3.o'
OBJS="$OBJS /home/phil/Repos/LoRa_STM32-claude/build/obj/stm32l0xx_hal_rtc_760a24a3.o"
echo "CC stm32l0xx_hal_rtc_ex.c"
arm-none-eabi-gcc @'/home/phil/Repos/LoRa_STM32-claude/build/cflags.rsp' "${EXTRA_CFLAGS[@]}" -c '/home/phil/Repos/LoRa_STM32-claude/STM32CubeExpansion_LRWAN/Drivers/STM32L0xx_HAL_Driver/Src/stm32l0xx_hal_rtc_ex.c' -o '/home/phil/Repos/LoRa_STM32-claude/build/obj/stm32l0xx_hal_rtc_ex_dcf355d0.o'
OBJS="$OBJS /home/phil/Repos/LoRa_STM32-claude/build/obj/stm32l0xx_hal_rtc_ex_dcf355d0.o"
echo "CC stm32l0xx_hal_tim.c"
arm-none-eabi-gcc @'/home/phil/Repos/LoRa_STM32-claude/build/cflags.rsp' "${EXTRA_CFLAGS[@]}" -c '/home/phil/Repos/LoRa_STM32-claude/STM32CubeExpansion_LRWAN/Drivers/STM32L0xx_HAL_Driver/Src/stm32l0xx_hal_tim.c' -o '/home/phil/Repos/LoRa_STM32-claude/build/obj/stm32l0xx_hal_tim_821fd5ef.o'
OBJS="$OBJS /home/phil/Repos/LoRa_STM32-claude/build/obj/stm32l0xx_hal_tim_821fd5ef.o"
echo "CC stm32l0xx_hal_tim_ex.c"
arm-none-eabi-gcc @'/home/phil/Repos/LoRa_STM32-claude/build/cflags.rsp' "${EXTRA_CFLAGS[@]}" -c '/home/phil/Repos/LoRa_STM32-claude/STM32CubeExpansion_LRWAN/Drivers/STM32L0xx_HAL_Driver/Src/stm32l0xx_hal_tim_ex.c' -o '/home/phil/Repos/LoRa_STM32-claude/build/obj/stm32l0xx_hal_tim_ex_b8a8c763.o'
OBJS="$OBJS /home/phil/Repos/LoRa_STM32-claude/build/obj/stm32l0xx_hal_tim_ex_b8a8c763.o"
echo "CC stm32l0xx_hal_adc_ex.c"
arm-none-eabi-gcc @'/home/phil/Repos/LoRa_STM32-claude/build/cflags.rsp' "${EXTRA_CFLAGS[@]}" -c '/home/phil/Repos/LoRa_STM32-claude/STM32CubeExpansion_LRWAN/Drivers/STM32L0xx_HAL_Driver/Src/stm32l0xx_hal_adc_ex.c' -o '/home/phil/Repos/LoRa_STM32-claude/build/obj/stm32l0xx_hal_adc_ex_38e6fee2.o'
OBJS="$OBJS /home/phil/Repos/LoRa_STM32-claude/build/obj/stm32l0xx_hal_adc_ex_38e6fee2.o"
echo "CC stm32l0xx_hal_uart_ex.c"
arm-none-eabi-gcc @'/home/phil/Repos/LoRa_STM32-claude/build/cflags.rsp' "${EXTRA_CFLAGS[@]}" -c '/home/phil/Repos/LoRa_STM32-claude/STM32CubeExpansion_LRWAN/Drivers/STM32L0xx_HAL_Driver/Src/stm32l0xx_hal_uart_ex.c' -o '/home/phil/Repos/LoRa_STM32-claude/build/obj/stm32l0xx_hal_uart_ex_5f0f995d.o'
OBJS="$OBJS /home/phil/Repos/LoRa_STM32-claude/build/obj/stm32l0xx_hal_uart_ex_5f0f995d.o"
echo "CC stm32l0xx_hal_i2c.c"
arm-none-eabi-gcc @'/home/phil/Repos/LoRa_STM32-claude/build/cflags.rsp' "${EXTRA_CFLAGS[@]}" -c '/home/phil/Repos/LoRa_STM32-claude/STM32CubeExpansion_LRWAN/Drivers/STM32L0xx_HAL_Driver/Src/stm32l0xx_hal_i2c.c' -o '/home/phil/Repos/LoRa_STM32-claude/build/obj/stm32l0xx_hal_i2c_d420655c.o'
OBJS="$OBJS /home/phil/Repos/LoRa_STM32-claude/build/obj/stm32l0xx_hal_i2c_d420655c.o"
echo "CC stm32l0xx_hal_i2c_ex.c"
arm-none-eabi-gcc @'/home/phil/Repos/LoRa_STM32-claude/build/cflags.rsp' "${EXTRA_CFLAGS[@]}" -c '/home/phil/Repos/LoRa_STM32-claude/STM32CubeExpansion_LRWAN/Drivers/STM32L0xx_HAL_Driver/Src/stm32l0xx_hal_i2c_ex.c' -o '/home/phil/Repos/LoRa_STM32-claude/build/obj/stm32l0xx_hal_i2c_ex_a1e58d1b.o'
OBJS="$OBJS /home/phil/Repos/LoRa_STM32-claude/build/obj/stm32l0xx_hal_i2c_ex_a1e58d1b.o"
echo "CC stm32l0xx_hal_flash_ex.c"
arm-none-eabi-gcc @'/home/phil/Repos/LoRa_STM32-claude/build/cflags.rsp' "${EXTRA_CFLAGS[@]}" -c '/home/phil/Repos/LoRa_STM32-claude/STM32CubeExpansion_LRWAN/Drivers/STM32L0xx_HAL_Driver/Src/stm32l0xx_hal_flash_ex.c' -o '/home/phil/Repos/LoRa_STM32-claude/build/obj/stm32l0xx_hal_flash_ex_2521ef03.o'
OBJS="$OBJS /home/phil/Repos/LoRa_STM32-claude/build/obj/stm32l0xx_hal_flash_ex_2521ef03.o"
echo "CC stm32l0xx_hal_flash.c"
arm-none-eabi-gcc @'/home/phil/Repos/LoRa_STM32-claude/build/cflags.rsp' "${EXTRA_CFLAGS[@]}" -c '/home/phil/Repos/LoRa_STM32-claude/STM32CubeExpansion_LRWAN/Drivers/STM32L0xx_HAL_Driver/Src/stm32l0xx_hal_flash.c' -o '/home/phil/Repos/LoRa_STM32-claude/build/obj/stm32l0xx_hal_flash_20ada424.o'
OBJS="$OBJS /home/phil/Repos/LoRa_STM32-claude/build/obj/stm32l0xx_hal_flash_20ada424.o"
echo "CC stm32l0xx_hal_iwdg.c"
arm-none-eabi-gcc @'/home/phil/Repos/LoRa_STM32-claude/build/cflags.rsp' "${EXTRA_CFLAGS[@]}" -c '/home/phil/Repos/LoRa_STM32-claude/STM32CubeExpansion_LRWAN/Drivers/STM32L0xx_HAL_Driver/Src/stm32l0xx_hal_iwdg.c' -o '/home/phil/Repos/LoRa_STM32-claude/build/obj/stm32l0xx_hal_iwdg_16129b01.o'
OBJS="$OBJS /home/phil/Repos/LoRa_STM32-claude/build/obj/stm32l0xx_hal_iwdg_16129b01.o"
echo "CC bsp.c"
arm-none-eabi-gcc @'/home/phil/Repos/LoRa_STM32-claude/build/cflags.rsp' "${EXTRA_CFLAGS[@]}" -c '/home/phil/Repos/LoRa_STM32-claude/STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN(AT)/src/bsp.c' -o '/home/phil/Repos/LoRa_STM32-claude/build/obj/bsp_fc5dfbea.o'
OBJS="$OBJS /home/phil/Repos/LoRa_STM32-claude/build/obj/bsp_fc5dfbea.o"
echo "CC debug.c"
arm-none-eabi-gcc @'/home/phil/Repos/LoRa_STM32-claude/build/cflags.rsp' "${EXTRA_CFLAGS[@]}" -c '/home/phil/Repos/LoRa_STM32-claude/STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN(AT)/src/debug.c' -o '/home/phil/Repos/LoRa_STM32-claude/build/obj/debug_f8cb5e76.o'
OBJS="$OBJS /home/phil/Repos/LoRa_STM32-claude/build/obj/debug_f8cb5e76.o"
echo "CC hw_gpio.c"
arm-none-eabi-gcc @'/home/phil/Repos/LoRa_STM32-claude/build/cflags.rsp' "${EXTRA_CFLAGS[@]}" -c '/home/phil/Repos/LoRa_STM32-claude/STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN(AT)/src/hw_gpio.c' -o '/home/phil/Repos/LoRa_STM32-claude/build/obj/hw_gpio_097d5577.o'
OBJS="$OBJS /home/phil/Repos/LoRa_STM32-claude/build/obj/hw_gpio_097d5577.o"
echo "CC hw_rtc.c"
arm-none-eabi-gcc @'/home/phil/Repos/LoRa_STM32-claude/build/cflags.rsp' "${EXTRA_CFLAGS[@]}" -c '/home/phil/Repos/LoRa_STM32-claude/STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN(AT)/src/hw_rtc.c' -o '/home/phil/Repos/LoRa_STM32-claude/build/obj/hw_rtc_40701610.o'
OBJS="$OBJS /home/phil/Repos/LoRa_STM32-claude/build/obj/hw_rtc_40701610.o"
echo "CC hw_spi.c"
arm-none-eabi-gcc @'/home/phil/Repos/LoRa_STM32-claude/build/cflags.rsp' "${EXTRA_CFLAGS[@]}" -c '/home/phil/Repos/LoRa_STM32-claude/STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN(AT)/src/hw_spi.c' -o '/home/phil/Repos/LoRa_STM32-claude/build/obj/hw_spi_1d9ffd93.o'
OBJS="$OBJS /home/phil/Repos/LoRa_STM32-claude/build/obj/hw_spi_1d9ffd93.o"
echo "CC main.c"
arm-none-eabi-gcc @'/home/phil/Repos/LoRa_STM32-claude/build/cflags.rsp' "${EXTRA_CFLAGS[@]}" -c '/home/phil/Repos/LoRa_STM32-claude/STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN(AT)/src/main.c' -o '/home/phil/Repos/LoRa_STM32-claude/build/obj/main_47d3d071.o'
OBJS="$OBJS /home/phil/Repos/LoRa_STM32-claude/build/obj/main_47d3d071.o"
echo "CC stm32l0xx_hal_msp.c"
arm-none-eabi-gcc @'/home/phil/Repos/LoRa_STM32-claude/build/cflags.rsp' "${EXTRA_CFLAGS[@]}" -c '/home/phil/Repos/LoRa_STM32-claude/STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN(AT)/src/stm32l0xx_hal_msp.c' -o '/home/phil/Repos/LoRa_STM32-claude/build/obj/stm32l0xx_hal_msp_59407185.o'
OBJS="$OBJS /home/phil/Repos/LoRa_STM32-claude/build/obj/stm32l0xx_hal_msp_59407185.o"
echo "CC stm32l0xx_hw.c"
arm-none-eabi-gcc @'/home/phil/Repos/LoRa_STM32-claude/build/cflags.rsp' "${EXTRA_CFLAGS[@]}" -c '/home/phil/Repos/LoRa_STM32-claude/STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN(AT)/src/stm32l0xx_hw.c' -o '/home/phil/Repos/LoRa_STM32-claude/build/obj/stm32l0xx_hw_492d4781.o'
OBJS="$OBJS /home/phil/Repos/LoRa_STM32-claude/build/obj/stm32l0xx_hw_492d4781.o"
echo "CC stm32l0xx_it.c"
arm-none-eabi-gcc @'/home/phil/Repos/LoRa_STM32-claude/build/cflags.rsp' "${EXTRA_CFLAGS[@]}" -c '/home/phil/Repos/LoRa_STM32-claude/STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN(AT)/src/stm32l0xx_it.c' -o '/home/phil/Repos/LoRa_STM32-claude/build/obj/stm32l0xx_it_b362cb1e.o'
OBJS="$OBJS /home/phil/Repos/LoRa_STM32-claude/build/obj/stm32l0xx_it_b362cb1e.o"
echo "CC vcom.c"
arm-none-eabi-gcc @'/home/phil/Repos/LoRa_STM32-claude/build/cflags.rsp' "${EXTRA_CFLAGS[@]}" -c '/home/phil/Repos/LoRa_STM32-claude/STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN(AT)/src/vcom.c' -o '/home/phil/Repos/LoRa_STM32-claude/build/obj/vcom_81525599.o'
OBJS="$OBJS /home/phil/Repos/LoRa_STM32-claude/build/obj/vcom_81525599.o"
echo "CC at.c"
arm-none-eabi-gcc @'/home/phil/Repos/LoRa_STM32-claude/build/cflags.rsp' "${EXTRA_CFLAGS[@]}" -c '/home/phil/Repos/LoRa_STM32-claude/STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN(AT)/src/at.c' -o '/home/phil/Repos/LoRa_STM32-claude/build/obj/at_82e457e9.o'
OBJS="$OBJS /home/phil/Repos/LoRa_STM32-claude/build/obj/at_82e457e9.o"
echo "CC command.c"
arm-none-eabi-gcc @'/home/phil/Repos/LoRa_STM32-claude/build/cflags.rsp' "${EXTRA_CFLAGS[@]}" -c '/home/phil/Repos/LoRa_STM32-claude/STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN(AT)/src/command.c' -o '/home/phil/Repos/LoRa_STM32-claude/build/obj/command_a2f38ca8.o'
OBJS="$OBJS /home/phil/Repos/LoRa_STM32-claude/build/obj/command_a2f38ca8.o"
echo "CC lora.c"
arm-none-eabi-gcc @'/home/phil/Repos/LoRa_STM32-claude/build/cflags.rsp' "${EXTRA_CFLAGS[@]}" -c '/home/phil/Repos/LoRa_STM32-claude/STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN(AT)/src/lora.c' -o '/home/phil/Repos/LoRa_STM32-claude/build/obj/lora_bb201500.o'
OBJS="$OBJS /home/phil/Repos/LoRa_STM32-claude/build/obj/lora_bb201500.o"
echo "CC tiny_sscanf.c"
arm-none-eabi-gcc @'/home/phil/Repos/LoRa_STM32-claude/build/cflags.rsp' "${EXTRA_CFLAGS[@]}" -c '/home/phil/Repos/LoRa_STM32-claude/STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN(AT)/src/tiny_sscanf.c' -o '/home/phil/Repos/LoRa_STM32-claude/build/obj/tiny_sscanf_7164f13a.o'
OBJS="$OBJS /home/phil/Repos/LoRa_STM32-claude/build/obj/tiny_sscanf_7164f13a.o"
echo "CC tiny_vsnprintf.c"
arm-none-eabi-gcc @'/home/phil/Repos/LoRa_STM32-claude/build/cflags.rsp' "${EXTRA_CFLAGS[@]}" -c '/home/phil/Repos/LoRa_STM32-claude/STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN(AT)/src/tiny_vsnprintf.c' -o '/home/phil/Repos/LoRa_STM32-claude/build/obj/tiny_vsnprintf_2c952753.o'
OBJS="$OBJS /home/phil/Repos/LoRa_STM32-claude/build/obj/tiny_vsnprintf_2c952753.o"
echo "CC Region.c"
arm-none-eabi-gcc @'/home/phil/Repos/LoRa_STM32-claude/build/cflags.rsp' "${EXTRA_CFLAGS[@]}" -c '/home/phil/Repos/LoRa_STM32-claude/STM32CubeExpansion_LRWAN/Middlewares/Third_Party/Lora/Mac/region/Region.c' -o '/home/phil/Repos/LoRa_STM32-claude/build/obj/Region_e7cf99a6.o'
OBJS="$OBJS /home/phil/Repos/LoRa_STM32-claude/build/obj/Region_e7cf99a6.o"
echo "CC RegionAS923.c"
arm-none-eabi-gcc @'/home/phil/Repos/LoRa_STM32-claude/build/cflags.rsp' "${EXTRA_CFLAGS[@]}" -c '/home/phil/Repos/LoRa_STM32-claude/STM32CubeExpansion_LRWAN/Middlewares/Third_Party/Lora/Mac/region/RegionAS923.c' -o '/home/phil/Repos/LoRa_STM32-claude/build/obj/RegionAS923_4ae8236a.o'
OBJS="$OBJS /home/phil/Repos/LoRa_STM32-claude/build/obj/RegionAS923_4ae8236a.o"
echo "CC RegionAU915.c"
arm-none-eabi-gcc @'/home/phil/Repos/LoRa_STM32-claude/build/cflags.rsp' "${EXTRA_CFLAGS[@]}" -c '/home/phil/Repos/LoRa_STM32-claude/STM32CubeExpansion_LRWAN/Middlewares/Third_Party/Lora/Mac/region/RegionAU915.c' -o '/home/phil/Repos/LoRa_STM32-claude/build/obj/RegionAU915_d811be32.o'
OBJS="$OBJS /home/phil/Repos/LoRa_STM32-claude/build/obj/RegionAU915_d811be32.o"
echo "CC RegionCN470.c"
arm-none-eabi-gcc @'/home/phil/Repos/LoRa_STM32-claude/build/cflags.rsp' "${EXTRA_CFLAGS[@]}" -c '/home/phil/Repos/LoRa_STM32-claude/STM32CubeExpansion_LRWAN/Middlewares/Third_Party/Lora/Mac/region/RegionCN470.c' -o '/home/phil/Repos/LoRa_STM32-claude/build/obj/RegionCN470_56439487.o'
OBJS="$OBJS /home/phil/Repos/LoRa_STM32-claude/build/obj/RegionCN470_56439487.o"
echo "CC RegionCN779.c"
arm-none-eabi-gcc @'/home/phil/Repos/LoRa_STM32-claude/build/cflags.rsp' "${EXTRA_CFLAGS[@]}" -c '/home/phil/Repos/LoRa_STM32-claude/STM32CubeExpansion_LRWAN/Middlewares/Third_Party/Lora/Mac/region/RegionCN779.c' -o '/home/phil/Repos/LoRa_STM32-claude/build/obj/RegionCN779_04586a81.o'
OBJS="$OBJS /home/phil/Repos/LoRa_STM32-claude/build/obj/RegionCN779_04586a81.o"
echo "CC RegionCommon.c"
arm-none-eabi-gcc @'/home/phil/Repos/LoRa_STM32-claude/build/cflags.rsp' "${EXTRA_CFLAGS[@]}" -c '/home/phil/Repos/LoRa_STM32-claude/STM32CubeExpansion_LRWAN/Middlewares/Third_Party/Lora/Mac/region/RegionCommon.c' -o '/home/phil/Repos/LoRa_STM32-claude/build/obj/RegionCommon_2f44c7de.o'
OBJS="$OBJS /home/phil/Repos/LoRa_STM32-claude/build/obj/RegionCommon_2f44c7de.o"
echo "CC RegionEU433.c"
arm-none-eabi-gcc @'/home/phil/Repos/LoRa_STM32-claude/build/cflags.rsp' "${EXTRA_CFLAGS[@]}" -c '/home/phil/Repos/LoRa_STM32-claude/STM32CubeExpansion_LRWAN/Middlewares/Third_Party/Lora/Mac/region/RegionEU433.c' -o '/home/phil/Repos/LoRa_STM32-claude/build/obj/RegionEU433_f02a5e49.o'
OBJS="$OBJS /home/phil/Repos/LoRa_STM32-claude/build/obj/RegionEU433_f02a5e49.o"
echo "CC RegionEU868.c"
arm-none-eabi-gcc @'/home/phil/Repos/LoRa_STM32-claude/build/cflags.rsp' "${EXTRA_CFLAGS[@]}" -c '/home/phil/Repos/LoRa_STM32-claude/STM32CubeExpansion_LRWAN/Middlewares/Third_Party/Lora/Mac/region/RegionEU868.c' -o '/home/phil/Repos/LoRa_STM32-claude/build/obj/RegionEU868_fd21cf6c.o'
OBJS="$OBJS /home/phil/Repos/LoRa_STM32-claude/build/obj/RegionEU868_fd21cf6c.o"
echo "CC RegionIN865.c"
arm-none-eabi-gcc @'/home/phil/Repos/LoRa_STM32-claude/build/cflags.rsp' "${EXTRA_CFLAGS[@]}" -c '/home/phil/Repos/LoRa_STM32-claude/STM32CubeExpansion_LRWAN/Middlewares/Third_Party/Lora/Mac/region/RegionIN865.c' -o '/home/phil/Repos/LoRa_STM32-claude/build/obj/RegionIN865_4b3f076a.o'
OBJS="$OBJS /home/phil/Repos/LoRa_STM32-claude/build/obj/RegionIN865_4b3f076a.o"
echo "CC RegionKR920.c"
arm-none-eabi-gcc @'/home/phil/Repos/LoRa_STM32-claude/build/cflags.rsp' "${EXTRA_CFLAGS[@]}" -c '/home/phil/Repos/LoRa_STM32-claude/STM32CubeExpansion_LRWAN/Middlewares/Third_Party/Lora/Mac/region/RegionKR920.c' -o '/home/phil/Repos/LoRa_STM32-claude/build/obj/RegionKR920_96d02b25.o'
OBJS="$OBJS /home/phil/Repos/LoRa_STM32-claude/build/obj/RegionKR920_96d02b25.o"
echo "CC RegionUS915.c"
arm-none-eabi-gcc @'/home/phil/Repos/LoRa_STM32-claude/build/cflags.rsp' "${EXTRA_CFLAGS[@]}" -c '/home/phil/Repos/LoRa_STM32-claude/STM32CubeExpansion_LRWAN/Middlewares/Third_Party/Lora/Mac/region/RegionUS915.c' -o '/home/phil/Repos/LoRa_STM32-claude/build/obj/RegionUS915_983effa7.o'
OBJS="$OBJS /home/phil/Repos/LoRa_STM32-claude/build/obj/RegionUS915_983effa7.o"
echo "CC RegionRU864.c"
arm-none-eabi-gcc @'/home/phil/Repos/LoRa_STM32-claude/build/cflags.rsp' "${EXTRA_CFLAGS[@]}" -c '/home/phil/Repos/LoRa_STM32-claude/STM32CubeExpansion_LRWAN/Middlewares/Third_Party/Lora/Mac/region/RegionRU864.c' -o '/home/phil/Repos/LoRa_STM32-claude/build/obj/RegionRU864_a0aa9aa7.o'
OBJS="$OBJS /home/phil/Repos/LoRa_STM32-claude/build/obj/RegionRU864_a0aa9aa7.o"
echo "CC RegionKZ865.c"
arm-none-eabi-gcc @'/home/phil/Repos/LoRa_STM32-claude/build/cflags.rsp' "${EXTRA_CFLAGS[@]}" -c '/home/phil/Repos/LoRa_STM32-claude/STM32CubeExpansion_LRWAN/Middlewares/Third_Party/Lora/Mac/region/RegionKZ865.c' -o '/home/phil/Repos/LoRa_STM32-claude/build/obj/RegionKZ865_12f67f7a.o'
OBJS="$OBJS /home/phil/Repos/LoRa_STM32-claude/build/obj/RegionKZ865_12f67f7a.o"
echo "CC RegionMA869.c"
arm-none-eabi-gcc @'/home/phil/Repos/LoRa_STM32-claude/build/cflags.rsp' "${EXTRA_CFLAGS[@]}" -c '/home/phil/Repos/LoRa_STM32-claude/STM32CubeExpansion_LRWAN/Middlewares/Third_Party/Lora/Mac/region/RegionMA869.c' -o '/home/phil/Repos/LoRa_STM32-claude/build/obj/RegionMA869_28b4a04d.o'
OBJS="$OBJS /home/phil/Repos/LoRa_STM32-claude/build/obj/RegionMA869_28b4a04d.o"
echo "CC LoRaMac.c"
arm-none-eabi-gcc @'/home/phil/Repos/LoRa_STM32-claude/build/cflags.rsp' "${EXTRA_CFLAGS[@]}" -c '/home/phil/Repos/LoRa_STM32-claude/STM32CubeExpansion_LRWAN/Middlewares/Third_Party/Lora/Mac/LoRaMac.c' -o '/home/phil/Repos/LoRa_STM32-claude/build/obj/LoRaMac_6eac7497.o'
OBJS="$OBJS /home/phil/Repos/LoRa_STM32-claude/build/obj/LoRaMac_6eac7497.o"
echo "CC LoRaMacCrypto.c"
arm-none-eabi-gcc @'/home/phil/Repos/LoRa_STM32-claude/build/cflags.rsp' "${EXTRA_CFLAGS[@]}" -c '/home/phil/Repos/LoRa_STM32-claude/STM32CubeExpansion_LRWAN/Middlewares/Third_Party/Lora/Mac/LoRaMacCrypto.c' -o '/home/phil/Repos/LoRa_STM32-claude/build/obj/LoRaMacCrypto_5c88798c.o'
OBJS="$OBJS /home/phil/Repos/LoRa_STM32-claude/build/obj/LoRaMacCrypto_5c88798c.o"
echo "CC lora-test.c"
arm-none-eabi-gcc @'/home/phil/Repos/LoRa_STM32-claude/build/cflags.rsp' "${EXTRA_CFLAGS[@]}" -c '/home/phil/Repos/LoRa_STM32-claude/STM32CubeExpansion_LRWAN/Middlewares/Third_Party/Lora/Core/lora-test.c' -o '/home/phil/Repos/LoRa_STM32-claude/build/obj/lora-test_9d43f367.o'
OBJS="$OBJS /home/phil/Repos/LoRa_STM32-claude/build/obj/lora-test_9d43f367.o"
echo "CC delay.c"
arm-none-eabi-gcc @'/home/phil/Repos/LoRa_STM32-claude/build/cflags.rsp' "${EXTRA_CFLAGS[@]}" -c '/home/phil/Repos/LoRa_STM32-claude/STM32CubeExpansion_LRWAN/Middlewares/Third_Party/Lora/Utilities/delay.c' -o '/home/phil/Repos/LoRa_STM32-claude/build/obj/delay_69b8651c.o'
OBJS="$OBJS /home/phil/Repos/LoRa_STM32-claude/build/obj/delay_69b8651c.o"
echo "CC low_power_manager.c"
arm-none-eabi-gcc @'/home/phil/Repos/LoRa_STM32-claude/build/cflags.rsp' "${EXTRA_CFLAGS[@]}" -c '/home/phil/Repos/LoRa_STM32-claude/STM32CubeExpansion_LRWAN/Middlewares/Third_Party/Lora/Utilities/low_power_manager.c' -o '/home/phil/Repos/LoRa_STM32-claude/build/obj/low_power_manager_062ac3e9.o'
OBJS="$OBJS /home/phil/Repos/LoRa_STM32-claude/build/obj/low_power_manager_062ac3e9.o"
echo "CC timeServer.c"
arm-none-eabi-gcc @'/home/phil/Repos/LoRa_STM32-claude/build/cflags.rsp' "${EXTRA_CFLAGS[@]}" -c '/home/phil/Repos/LoRa_STM32-claude/STM32CubeExpansion_LRWAN/Middlewares/Third_Party/Lora/Utilities/timeServer.c' -o '/home/phil/Repos/LoRa_STM32-claude/build/obj/timeServer_ff13a564.o'
OBJS="$OBJS /home/phil/Repos/LoRa_STM32-claude/build/obj/timeServer_ff13a564.o"
echo "CC utilities.c"
arm-none-eabi-gcc @'/home/phil/Repos/LoRa_STM32-claude/build/cflags.rsp' "${EXTRA_CFLAGS[@]}" -c '/home/phil/Repos/LoRa_STM32-claude/STM32CubeExpansion_LRWAN/Middlewares/Third_Party/Lora/Utilities/utilities.c' -o '/home/phil/Repos/LoRa_STM32-claude/build/obj/utilities_06cf1bad.o'
OBJS="$OBJS /home/phil/Repos/LoRa_STM32-claude/build/obj/utilities_06cf1bad.o"
echo "CC trace.c"
arm-none-eabi-gcc @'/home/phil/Repos/LoRa_STM32-claude/build/cflags.rsp' "${EXTRA_CFLAGS[@]}" -c '/home/phil/Repos/LoRa_STM32-claude/STM32CubeExpansion_LRWAN/Middlewares/Third_Party/Lora/Utilities/trace.c' -o '/home/phil/Repos/LoRa_STM32-claude/build/obj/trace_0f7c981b.o'
OBJS="$OBJS /home/phil/Repos/LoRa_STM32-claude/build/obj/trace_0f7c981b.o"
echo "CC queue.c"
arm-none-eabi-gcc @'/home/phil/Repos/LoRa_STM32-claude/build/cflags.rsp' "${EXTRA_CFLAGS[@]}" -c '/home/phil/Repos/LoRa_STM32-claude/STM32CubeExpansion_LRWAN/Middlewares/Third_Party/Lora/Utilities/queue.c' -o '/home/phil/Repos/LoRa_STM32-claude/build/obj/queue_53f702e2.o'
OBJS="$OBJS /home/phil/Repos/LoRa_STM32-claude/build/obj/queue_53f702e2.o"
echo "CC aes.c"
arm-none-eabi-gcc @'/home/phil/Repos/LoRa_STM32-claude/build/cflags.rsp' "${EXTRA_CFLAGS[@]}" -c '/home/phil/Repos/LoRa_STM32-claude/STM32CubeExpansion_LRWAN/Middlewares/Third_Party/Lora/Crypto/aes.c' -o '/home/phil/Repos/LoRa_STM32-claude/build/obj/aes_fc1b9773.o'
OBJS="$OBJS /home/phil/Repos/LoRa_STM32-claude/build/obj/aes_fc1b9773.o"
echo "CC cmac.c"
arm-none-eabi-gcc @'/home/phil/Repos/LoRa_STM32-claude/build/cflags.rsp' "${EXTRA_CFLAGS[@]}" -c '/home/phil/Repos/LoRa_STM32-claude/STM32CubeExpansion_LRWAN/Middlewares/Third_Party/Lora/Crypto/cmac.c' -o '/home/phil/Repos/LoRa_STM32-claude/build/obj/cmac_a4bbb0de.o'
OBJS="$OBJS /home/phil/Repos/LoRa_STM32-claude/build/obj/cmac_a4bbb0de.o"
echo "CC via_chameleon.c"
arm-none-eabi-gcc @'/home/phil/Repos/LoRa_STM32-claude/build/cflags.rsp' "${EXTRA_CFLAGS[@]}" -c '/home/phil/Repos/LoRa_STM32-claude/STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN(AT)/src/via_chameleon.c' -o '/home/phil/Repos/LoRa_STM32-claude/build/obj/via_chameleon.o'
OBJS="$OBJS /home/phil/Repos/LoRa_STM32-claude/build/obj/via_chameleon.o"
echo "CC chameleon_payload.c"
arm-none-eabi-gcc @'/home/phil/Repos/LoRa_STM32-claude/build/cflags.rsp' "${EXTRA_CFLAGS[@]}" -c '/home/phil/Repos/LoRa_STM32-claude/STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN(AT)/src/chameleon_payload.c' -o '/home/phil/Repos/LoRa_STM32-claude/build/obj/chameleon_payload.o'
OBJS="$OBJS /home/phil/Repos/LoRa_STM32-claude/build/obj/chameleon_payload.o"

echo "LINK"
arm-none-eabi-gcc -mcpu=cortex-m0plus -mthumb -T/home/phil/Repos/LoRa_STM32-claude/build/stm32l072cz.ld -Wl,--gc-sections -Wl,-Map="/home/phil/Repos/LoRa_STM32-claude/build/${TARGET_BASENAME}.map" --specs=nano.specs --specs=nosys.specs $OBJS -o "/home/phil/Repos/LoRa_STM32-claude/build/${TARGET_BASENAME}.elf"
arm-none-eabi-objcopy -O ihex "/home/phil/Repos/LoRa_STM32-claude/build/${TARGET_BASENAME}.elf" "/home/phil/Repos/LoRa_STM32-claude/build/${TARGET_BASENAME}.hex"
arm-none-eabi-objcopy -O binary "/home/phil/Repos/LoRa_STM32-claude/build/${TARGET_BASENAME}.elf" "/home/phil/Repos/LoRa_STM32-claude/build/${TARGET_BASENAME}.bin"
arm-none-eabi-size "/home/phil/Repos/LoRa_STM32-claude/build/${TARGET_BASENAME}.elf"
echo "BUILD OK"
