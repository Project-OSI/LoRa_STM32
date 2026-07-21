/*
 * gcc_compat.h
 *
 * Force-included (-include) ahead of every translation unit in this GCC
 * build so that any Keil/ARMCC-only construct a vendor .c/.h file relies on
 * can be papered over here instead of by editing vendor sources (see
 * gcc/README.md, "Rules" -- vendor sources are never modified).
 *
 * Empty until a real construct requires it: CMSIS (Drivers/CMSIS/Include/
 * core_cm0plus.h, cmsis_compiler dispatch) already auto-detects __GNUC__ and
 * routes to cmsis_gcc.h for intrinsics (__NOP, __WFI, __enable_irq, ...), so
 * nothing was needed there. If a future rebase against this branch adds a
 * vendor file using an ARMCC-only keyword (__weak, __packed, __irq, a bare
 * "#pragma anon_unions", etc.), add the GCC-equivalent macro/pragma here and
 * document it, do not touch the vendor file.
 */
#ifndef GCC_COMPAT_H
#define GCC_COMPAT_H

#endif /* GCC_COMPAT_H */
