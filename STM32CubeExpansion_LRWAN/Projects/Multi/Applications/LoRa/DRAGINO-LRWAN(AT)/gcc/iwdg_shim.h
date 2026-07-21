/*
 * iwdg_shim.h
 *
 * Force-included ONLY when compiling
 * Drivers/BSP/Components/iwdg/iwdg.c (see the gcc/Makefile rule for
 * build/iwdg.o), ahead of that file's own `#include "iwdg.h"` line.
 *
 * Vendor bug, not touched (see gcc/README.md "vendor warnings that look
 * like real bugs"): Drivers/BSP/Components/iwdg/iwdg.h declares
 * `uint32_t GetLSIFrequency(void);` with external linkage, but iwdg.c
 * immediately defines it `static` (internal linkage) and is the only
 * caller anywhere in this source tree (checked with grep). That is a
 * genuine declaration/definition linkage mismatch -- undefined behavior
 * per C99 6.2.2p7 -- predating the ML3 branch. GCC (unlike, evidently,
 * ARMCC, since the vendor's Keil build exists) treats it as an
 * unconditional hard error with no controlling -W flag: checked
 * empirically that -w, -std=gnu99, and -fpermissive (a C++-only flag with
 * no effect in C mode) all still error.
 *
 * Fix without editing iwdg.c or iwdg.h: pre-arm iwdg.h's own include
 * guard (__IWDG_H__) so its body -- including the conflicting extern
 * prototype -- is skipped for this one translation unit, and supply the
 * same public declarations here instead, with GetLSIFrequency declared
 * `static` to match what iwdg.c actually does. iwdg.c's logic is
 * unchanged; only which prototype it sees changes, and only for this one
 * file. Every other translation unit that includes iwdg.h (bsp.c, main.c,
 * stm32l0xx_it.c, LoRaMac.c) still sees the real, unmodified vendor
 * header -- none of them call GetLSIFrequency, so its declared linkage
 * there is moot.
 */
#ifndef __IWDG_H__
#define __IWDG_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void iwdg_init(void);
static uint32_t GetLSIFrequency(void);
void TIMER_IRQHandler(void);
void IWDG_Refresh(void);

#ifdef __cplusplus
}
#endif

#endif /* __IWDG_H__ */
