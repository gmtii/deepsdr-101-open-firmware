/*
 * Minimal compiler-abstraction shim for CMSIS-DSP on this project's
 * CMSIS-Core (pre-5.0, single-file core_cm4.h / core_cmInstr.h /
 * core_cm4_simd.h from the GD32 vendor package).
 *
 * The upstream CMSIS-DSP (5.x+) expects the modern split CMSIS-Core
 * headers (cmsis_compiler.h -> cmsis_gcc.h), which redefine the same
 * intrinsics (__CLZ, __SSAT, __QADD8, ...) this project's old
 * core_cmInstr.h / core_cm4_simd.h already provide for GCC - pulling
 * in the real cmsis_gcc.h causes "redefinition" errors when a file
 * includes both gd32f4xx.h and arm_math.h.
 *
 * Fix: build with ARM_MATH_DSP=1 (correct - the GD32F450 is a real
 * Cortex-M4F with the DSP extension) so CMSIS-DSP's dsp/none.h
 * software-emulation fallback is skipped entirely (it's only pulled
 * in for cores without the DSP extension, or MSVC/Apple/Python
 * hosts), and supply here ONLY the handful of compiler-abstraction
 * macros CMSIS-DSP needs that core_cm4.h doesn't already define.
 * Every macro below is guarded so this stays a no-op if a real
 * cmsis_compiler.h is ever swapped in later.
 */
#ifndef __CMSIS_COMPILER_H
#define __CMSIS_COMPILER_H

#include <stdint.h>

#if defined(__GNUC__)

#ifndef __ASM
#define __ASM  __asm
#endif

#ifndef __INLINE
#define __INLINE  inline
#endif

#ifndef __STATIC_FORCEINLINE
#define __STATIC_FORCEINLINE  __attribute__((always_inline)) static inline
#endif

#ifndef __STATIC_INLINE
#define __STATIC_INLINE  static inline
#endif

#ifndef __ALIGNED
#define __ALIGNED(x)  __attribute__((aligned(x)))
#endif

#ifndef __PACKED
#define __PACKED  __attribute__((packed, aligned(1)))
#endif

#ifndef __WEAK
#define __WEAK  __attribute__((weak))
#endif

#ifndef __RESTRICT
#define __RESTRICT  __restrict
#endif

#ifndef __UNALIGNED_UINT32
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpacked"
#pragma GCC diagnostic ignored "-Wattributes"
struct __attribute__((packed)) T_UINT32 { uint32_t v; };
#pragma GCC diagnostic pop
#define __UNALIGNED_UINT32(x) (((struct T_UINT32 *)(x))->v)
#endif

/* core_cmInstr.h guards its v7-M-only instructions (__CLZ, __SSAT,
 * __USAT, __RBIT, LDREX/STREX...) behind "#if (__CORTEX_M >= 0x03)".
 * That macro is normally set by core_cm4.h before core_cmInstr.h is
 * included; since we include core_cmInstr.h directly (without
 * core_cm4.h, to avoid needing a device header here), we have to
 * define it ourselves. Guarded so this is a no-op if core_cm4.h was
 * already included first in this translation unit. */
#ifndef __CORTEX_M
#define __CORTEX_M  (0x04)   /* mismo token que core_cm4.h: si difiere (0x04U), redefinir avisa */
#endif

/* core_cmInstr.h / core_cm4_simd.h (this project's vendor CMSIS-Core)
 * define the real hardware intrinsics (__CLZ, __SSAT, __QADD8, ...)
 * that CMSIS-DSP calls when ARM_MATH_DSP=1. They need __ASM/
 * __STATIC_INLINE (defined above) already visible, and they're
 * self-guarded (__CORE_CMINSTR_H / __CORE_CM4_SIMD_H), so including
 * them again here is a no-op if a translation unit already pulled
 * them in via gd32f4xx.h -> core_cm4.h. Do NOT include core_cm4.h
 * itself here: it needs a device header (IRQn_Type etc.) we don't
 * want to force on pure DSP translation units. */
#include "core_cmInstr.h"
#include "core_cm4_simd.h"

#else
#error "CMSIS/DSP/Include/cmsis_compiler.h shim only covers GCC; add your compiler's macros here."
#endif /* __GNUC__ */

#endif /* __CMSIS_COMPILER_H */
