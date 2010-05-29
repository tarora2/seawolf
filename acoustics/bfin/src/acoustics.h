#ifndef __SEAWOLF3_ACOUSTICS_INCLUDE_H
#define __SEAWOLF3_ACOUSTICS_INCLUDE_H

#include <math_bf.h>
#include <complex_bf.h>

/* A sample from the FPGA/ADC is 16 bits */
typedef fract16 adcsample;

/* Asychronous banks */
#define BANK_0 0x20000000
#define BANK_1 0x20100000
#define BANK_2 0x20200000
#define BANK_3 0x20300000

/* Channels */
#define A 0
#define B 1
#define C 2
#define D 3

/* Data source locations */
#define DATA_ADDR  (*((adcsample**)BANK_2))
#define READY_FLAG (*(((adcsample*)BANK_2) + 1))
#define RESET_FLAG (*(((adcsample*)BANK_2) + 3))

/* Input data configuration */
#define CHANNELS 4
#define SAMPLES_PER_CHANNEL (8 * 1024)
#define SAMPLES_PER_BANK    (CHANNELS * SAMPLES_PER_CHANNEL)

/* Size of a circular buffer for a single channel. This is populated in
   increments of SAMPLES_PER_CHANNEL */
#define BUFFER_SIZE_CHANNEL (256 * 1024)

/* FIR filter coefficient count */
#define FIR_COEF_COUNT 613

/* Minimum value to trigger on */
#define TRIGGER_VALUE ((short)(-1100))

/* Circular buffer state */
#define READING   0x00
#define TRIGGERED 0x01
#define DONE      0x02

/* Extra number of read cyles to perform after trigger. This can be used to
   "pad" the other channels and ensure that the trigger is present in all
   channels */
#define EXTRA_READS 4

/* Profiling helpers */
#ifdef ACOUSTICS_PROFILE
# define TIME_PRE(t, text) do {                    \
        printf("%-30s", (text));                   \
        fflush(stdout);                            \
        Timer_reset(t);                            \
    } while(false)
# define TIME_POST(t) do {                      \
        printf("%5.3f\n", Timer_getDelta(t));   \
    } while(false)
#else
# define TIME_PRE(t, text) do { } while(false)
# define TIME_POST(t) do { } while(false)
#endif

/* Support routines */
void load_coefs(fract16* coefs, char* coef_file_name, int num_coefs);
int find_max_cmplx(complex_fract16* w, int size);
void multiply(complex_fract16* in1, complex_fract16* in2, complex_fract16* out, int size);
void conjugate(complex_fract16* w, int size);

#endif // #ifndef __SEAWOLF3_ACOUSTICS_INCLUDE_H
