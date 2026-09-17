#ifndef ARM_MATH_STUB_H
#define ARM_MATH_STUB_H
/*
 * Host-test-only stub. ft8_decimator.c includes "arm_math.h" purely
 * for the float32_t typedef (its FIR history/coefficients arrays) -
 * it never calls any real CMSIS-DSP function, so this one typedef is
 * the entire stub needed to compile it for the x86 test harness.
 */
typedef float float32_t;
#endif
