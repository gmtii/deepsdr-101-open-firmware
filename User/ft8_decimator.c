#include "ft8_decimator.h"
#include "ft8_waterfall_adapter.h"
#include "arm_math.h"

#if defined(__arm__)
#include "gd32f4xx.h" /* __disable_irq()/__enable_irq() - see ft8_decimator_reset()'s critical section */
#else
/* Host test harness stubs - test/host/ft8_host_test.c is single-
 * threaded (no real ISR to race against), so these are genuine no-ops
 * there, not a simulation of the real critical section. */
#define __disable_irq() ((void)0)
#define __enable_irq()  ((void)0)
#endif

/*
 * Polyphase rational resampler coefficients: 480-tap Blackman-windowed
 * -sinc lowpass, cutoff 1600Hz referenced to the 48kHz virtual
 * (post-interpolation) rate, gain=L=4 (compensates the interpolation
 * zero-stuffing loss) - see ft8_decimator.h's comment for the
 * frequency response summary. Pre-split into L=4 polyphase phases
 * (120 taps each) at generation time (Python, windowed-sinc design),
 * NOT deinterleaved at runtime - phase r's coefficients are h[r],
 * h[r+4], h[r+8], ..., i.e. every 4th original tap starting at offset
 * r, which is exactly what a direct-form polyphase decomposition
 * needs. Generated offline - not hand-typed, not copied from any
 * other project's filter design.
 */
#define FT8_RESAMPLER_TAPS_PER_PHASE 120U
static const float32_t s_resampler_phase[FT8_RESAMPLER_L][FT8_RESAMPLER_TAPS_PER_PHASE] = {
{
    /* phase 0 */
    7.711798034e-21f, -1.084814782e-06f, -5.352920494e-06f, -6.295617728e-06f,
    7.088871390e-06f, 3.362889998e-05f, 4.983366585e-05f, 2.365648799e-05f,
    -5.163992772e-05f, -1.322862573e-04f, -1.399712912e-04f, -2.270712728e-05f,
    1.797295117e-04f, 3.278819889e-04f, 2.649130918e-04f, -4.950188372e-05f,
    -4.545458974e-04f, -6.473114721e-04f, -3.872259492e-04f, 2.784459909e-04f,
    9.527252062e-04f, 1.097391389e-03f, 4.256912039e-04f, -7.866833146e-04f,
    -1.751138692e-03f, -1.642276898e-03f, -2.398219103e-04f, 1.729901949e-03f,
    2.905016359e-03f, 2.178536883e-03f, -3.804902075e-04f, -3.285113929e-03f,
    -4.421689274e-03f, -2.511481456e-03f, 1.721798711e-03f, 5.637867228e-03f,
    6.236314034e-03f, 2.330826887e-03f, -4.163305937e-03f, -8.985162006e-03f,
    -8.195137404e-03f, -1.167501993e-03f, 8.242228285e-03f, 1.359216824e-02f,
    1.004573047e-02f, -1.735912350e-03f, -1.489252757e-02f, -2.001404094e-02f,
    -1.141299733e-02f, 7.906139454e-03f, 2.635872143e-02f, 2.996356232e-02f,
    1.164265242e-02f, -2.194324403e-02f, -5.096643955e-02f, -5.142342106e-02f,
    -8.441918558e-03f, 7.339480894e-02f, 1.690913211e-01f, 2.432057709e-01f,
    2.661725457e-01f, 2.285762065e-01f, 1.457731651e-01f, 5.037048049e-02f,
    -2.339129215e-02f, -5.507882147e-02f, -4.550238496e-02f, -1.303389039e-02f,
    1.817561525e-02f, 3.104368171e-02f, 2.263652821e-02f, 2.597799630e-03f,
    -1.485572071e-02f, -1.992133995e-02f, -1.200169618e-02f, 1.692384217e-03f,
    1.184627188e-02f, 1.297095558e-02f, 6.009795901e-03f, -3.368192834e-03f,
    -9.030659752e-03f, -8.188595697e-03f, -2.510421177e-03f, 3.678794056e-03f,
    6.511750747e-03f, 4.867467725e-03f, 5.675958453e-04f, -3.274152790e-03f,
    -4.401650078e-03f, -2.644353206e-03f, 3.701040762e-04f, 2.560571731e-03f,
    2.760596488e-03f, 1.254972446e-03f, -6.878150899e-04f, -1.797676994e-03f,
    -1.584057895e-03f, -4.704795200e-04f, 6.658599682e-04f, 1.134678810e-03f,
    8.138196519e-04f, 9.073501585e-05f, -4.985175818e-04f, -6.356407082e-04f,
    -3.604809845e-04f, 4.737238901e-05f, 3.058347823e-04f, 3.054517631e-04f,
    1.275244062e-04f, -6.351451720e-05f, -1.489009898e-04f, -1.157735450e-04f,
    -2.970538246e-05f, 3.531453756e-05f, 4.864500419e-05f, 2.669385367e-05f,
    2.094219199e-06f, -7.038615552e-06f, -4.168009843e-06f, -5.023455431e-07f,
},
{
    /* phase 1 */
    -2.554809599e-08f, -1.923261847e-06f, -6.361449877e-06f, -4.593557782e-06f,
    1.303256191e-05f, 3.997574821e-05f, 4.824496707e-05f, 8.652212259e-06f,
    -7.411158285e-05f, -1.441161630e-04f, -1.227441761e-04f, 2.397642027e-05f,
    2.286674379e-04f, 3.365164325e-04f, 2.071912714e-04f, -1.528423985e-04f,
    -5.350603327e-04f, -6.291603712e-04f, -2.486819775e-04f, 4.675243178e-04f,
    1.057252392e-03f, 1.006079763e-03f, 1.489142191e-04f, -1.087703462e-03f,
    -1.847978290e-03f, -1.400938203e-03f, 2.471564906e-04f, 2.153964417e-03f,
    2.924369566e-03f, 1.674299314e-03f, -1.156245927e-03f, -3.811109046e-03f,
    -4.240639613e-03f, -1.593192467e-03f, 2.858409288e-03f, 6.191452975e-03f,
    5.662779658e-03f, 8.082208384e-04f, -5.710383803e-03f, -9.413534167e-03f,
    -6.945690158e-03f, 1.196386233e-03f, 1.021293493e-02f, 1.362827311e-02f,
    7.697098706e-03f, -5.264535961e-03f, -1.726214485e-02f, -1.920307008e-02f,
    -7.254151722e-03f, 1.317432738e-02f, 2.911758124e-02f, 2.744080765e-02f,
    4.085353855e-03f, -3.059095690e-02f, -5.459851135e-02f, -4.495156692e-02f,
    9.043274377e-03f, 9.734089707e-02f, 1.910305042e-01f, 2.545334483e-01f,
    2.622578678e-01f, 2.110313228e-01f, 1.216597224e-01f, 2.877179758e-02f,
    -3.559947218e-02f, -5.606382902e-02f, -3.857870672e-02f, -4.241892100e-03f,
    2.348702480e-02f, 3.072820061e-02f, 1.816444258e-02f, -2.524350181e-03f,
    -1.747484194e-02f, -1.897592755e-02f, -8.739616072e-03f, 4.878309904e-03f,
    1.304799999e-02f, 1.181952704e-02f, 3.624437512e-03f, -5.318377187e-03f,
    -9.435807326e-03f, -7.075901711e-03f, -8.284666761e-04f, 4.802068104e-03f,
    6.491682840e-03f, 3.924460952e-03f, -5.530973588e-04f, -3.855906663e-03f,
    -4.191789279e-03f, -1.922814251e-03f, 1.064118853e-03f, 2.810398050e-03f,
    2.504416608e-03f, 7.528745903e-04f, -1.079471316e-03f, -1.865473774e-03f,
    -1.358397764e-03f, -1.539633985e-04f, 8.612159134e-04f, 1.119911556e-03f,
    6.490547677e-04f, -8.737970318e-05f, -5.796167215e-04f, -5.969514983e-04f,
    -2.581610400e-04f, 1.339513587e-04f, 3.295584537e-04f, 2.715077913e-04f,
    7.477530689e-05f, -9.713788811e-05f, -1.500118065e-04f, -9.592656802e-05f,
    -9.333971878e-06f, 4.348780400e-05f, 4.517357264e-05f, 1.968257613e-05f,
    -1.813025753e-06f, -6.993483109e-06f, -2.980071999e-06f, -1.660779086e-07f,
},
{
    /* phase 2 */
    -1.660779086e-07f, -2.980071999e-06f, -6.993483109e-06f, -1.813025753e-06f,
    1.968257613e-05f, 4.517357264e-05f, 4.348780400e-05f, -9.333971878e-06f,
    -9.592656802e-05f, -1.500118065e-04f, -9.713788811e-05f, 7.477530689e-05f,
    2.715077913e-04f, 3.295584537e-04f, 1.339513587e-04f, -2.581610400e-04f,
    -5.969514983e-04f, -5.796167215e-04f, -8.737970318e-05f, 6.490547677e-04f,
    1.119911556e-03f, 8.612159134e-04f, -1.539633985e-04f, -1.358397764e-03f,
    -1.865473774e-03f, -1.079471316e-03f, 7.528745903e-04f, 2.504416608e-03f,
    2.810398050e-03f, 1.064118853e-03f, -1.922814251e-03f, -4.191789279e-03f,
    -3.855906663e-03f, -5.530973588e-04f, 3.924460952e-03f, 6.491682840e-03f,
    4.802068104e-03f, -8.284666761e-04f, -7.075901711e-03f, -9.435807326e-03f,
    -5.318377187e-03f, 3.624437512e-03f, 1.181952704e-02f, 1.304799999e-02f,
    4.878309904e-03f, -8.739616072e-03f, -1.897592755e-02f, -1.747484194e-02f,
    -2.524350181e-03f, 1.816444258e-02f, 3.072820061e-02f, 2.348702480e-02f,
    -4.241892100e-03f, -3.857870672e-02f, -5.606382902e-02f, -3.559947218e-02f,
    2.877179758e-02f, 1.216597224e-01f, 2.110313228e-01f, 2.622578678e-01f,
    2.545334483e-01f, 1.910305042e-01f, 9.734089707e-02f, 9.043274377e-03f,
    -4.495156692e-02f, -5.459851135e-02f, -3.059095690e-02f, 4.085353855e-03f,
    2.744080765e-02f, 2.911758124e-02f, 1.317432738e-02f, -7.254151722e-03f,
    -1.920307008e-02f, -1.726214485e-02f, -5.264535961e-03f, 7.697098706e-03f,
    1.362827311e-02f, 1.021293493e-02f, 1.196386233e-03f, -6.945690158e-03f,
    -9.413534167e-03f, -5.710383803e-03f, 8.082208384e-04f, 5.662779658e-03f,
    6.191452975e-03f, 2.858409288e-03f, -1.593192467e-03f, -4.240639613e-03f,
    -3.811109046e-03f, -1.156245927e-03f, 1.674299314e-03f, 2.924369566e-03f,
    2.153964417e-03f, 2.471564906e-04f, -1.400938203e-03f, -1.847978290e-03f,
    -1.087703462e-03f, 1.489142191e-04f, 1.006079763e-03f, 1.057252392e-03f,
    4.675243178e-04f, -2.486819775e-04f, -6.291603712e-04f, -5.350603327e-04f,
    -1.528423985e-04f, 2.071912714e-04f, 3.365164325e-04f, 2.286674379e-04f,
    2.397642027e-05f, -1.227441761e-04f, -1.441161630e-04f, -7.411158285e-05f,
    8.652212259e-06f, 4.824496707e-05f, 3.997574821e-05f, 1.303256191e-05f,
    -4.593557782e-06f, -6.361449877e-06f, -1.923261847e-06f, -2.554809599e-08f,
},
{
    /* phase 3 */
    -5.023455431e-07f, -4.168009843e-06f, -7.038615552e-06f, 2.094219199e-06f,
    2.669385367e-05f, 4.864500419e-05f, 3.531453756e-05f, -2.970538246e-05f,
    -1.157735450e-04f, -1.489009898e-04f, -6.351451720e-05f, 1.275244062e-04f,
    3.054517631e-04f, 3.058347823e-04f, 4.737238901e-05f, -3.604809845e-04f,
    -6.356407082e-04f, -4.985175818e-04f, 9.073501585e-05f, 8.138196519e-04f,
    1.134678810e-03f, 6.658599682e-04f, -4.704795200e-04f, -1.584057895e-03f,
    -1.797676994e-03f, -6.878150899e-04f, 1.254972446e-03f, 2.760596488e-03f,
    2.560571731e-03f, 3.701040762e-04f, -2.644353206e-03f, -4.401650078e-03f,
    -3.274152790e-03f, 5.675958453e-04f, 4.867467725e-03f, 6.511750747e-03f,
    3.678794056e-03f, -2.510421177e-03f, -8.188595697e-03f, -9.030659752e-03f,
    -3.368192834e-03f, 6.009795901e-03f, 1.297095558e-02f, 1.184627188e-02f,
    1.692384217e-03f, -1.200169618e-02f, -1.992133995e-02f, -1.485572071e-02f,
    2.597799630e-03f, 2.263652821e-02f, 3.104368171e-02f, 1.817561525e-02f,
    -1.303389039e-02f, -4.550238496e-02f, -5.507882147e-02f, -2.339129215e-02f,
    5.037048049e-02f, 1.457731651e-01f, 2.285762065e-01f, 2.661725457e-01f,
    2.432057709e-01f, 1.690913211e-01f, 7.339480894e-02f, -8.441918558e-03f,
    -5.142342106e-02f, -5.096643955e-02f, -2.194324403e-02f, 1.164265242e-02f,
    2.996356232e-02f, 2.635872143e-02f, 7.906139454e-03f, -1.141299733e-02f,
    -2.001404094e-02f, -1.489252757e-02f, -1.735912350e-03f, 1.004573047e-02f,
    1.359216824e-02f, 8.242228285e-03f, -1.167501993e-03f, -8.195137404e-03f,
    -8.985162006e-03f, -4.163305937e-03f, 2.330826887e-03f, 6.236314034e-03f,
    5.637867228e-03f, 1.721798711e-03f, -2.511481456e-03f, -4.421689274e-03f,
    -3.285113929e-03f, -3.804902075e-04f, 2.178536883e-03f, 2.905016359e-03f,
    1.729901949e-03f, -2.398219103e-04f, -1.642276898e-03f, -1.751138692e-03f,
    -7.866833146e-04f, 4.256912039e-04f, 1.097391389e-03f, 9.527252062e-04f,
    2.784459909e-04f, -3.872259492e-04f, -6.473114721e-04f, -4.545458974e-04f,
    -4.950188372e-05f, 2.649130918e-04f, 3.278819889e-04f, 1.797295117e-04f,
    -2.270712728e-05f, -1.399712912e-04f, -1.322862573e-04f, -5.163992772e-05f,
    2.365648799e-05f, 4.983366585e-05f, 3.362889998e-05f, 7.088871390e-06f,
    -6.295617728e-06f, -5.352920494e-06f, -1.084814782e-06f, 7.711798034e-21f,
}
};

static float32_t s_history[FT8_RESAMPLER_TAPS_PER_PHASE]; /* shift register of the most recent input samples, oldest first (index 0), newest last (index TAPS_PER_PHASE-1) */
static uint32_t s_next_output_vpos; /* "virtual sample position" (in units of 1/48000s) of the next output due - see this file's own comment in ft8_decimator_feed_sample() for the polyphase bookkeeping */
static uint32_t s_input_count;      /* how many real 12kHz samples have been fed since reset */

/*
 * Own, independent AGC for the FT8 pipeline (09/2026, carried over
 * unchanged from the original 800Hz design) - our tap point
 * (s_ssb_dec in demod_am.c) sits BEFORE the main receiver's own AGC
 * gain multiply, so a signal that sounds loud through the speaker
 * (post-AGC) can be tiny in s_ssb_dec's raw, pre-AGC amplitude - see
 * this session's own real-hardware measurements (dB min=-127 max=-61
 * before this fix, vs -78/-11 with it) for why this exists. Same
 * architecture as demod_am.c's own AGC (instant attack, exponential
 * release, peak-based gain, fixed target) but completely separate
 * state.
 */
#define FT8_AGC_TARGET 8000.0f   /* comfortably under int16 full scale - leaves headroom for the release's attack-side overshoot before it catches up */
#define FT8_AGC_PEAK_MIN 50.0f   /* floor for the peak tracker - avoids a huge, noisy gain spike during near-silence between transmissions */
#define FT8_AGC_RELEASE 0.99995f /* slow exponential release (~a few hundred ms time constant at 12kHz) - fast enough to recover from one clipped transmission, slow enough not to pump within a single ~12.6s FT8 burst */
static float32_t s_agc_peak;

/*
 * DOUBLE-BUFFERED handoff to the main loop (09/2026) - CRITICAL FIX.
 * ft8_decimator_feed_sample() runs inside the audio DMA ISR (see
 * demod_am.c's own "EXECUTION CONTEXT" comment on that hook) - this
 * used to call ft8_waterfall_feed_subblock() DIRECTLY from there once
 * a subblock filled, which runs a full 1024-point FFT (ft8_fft1024.c)
 * plus a 16*256-cell cascade shift SYNCHRONOUSLY inside that same ISR
 * invocation, roughly every 160ms. That is real, non-trivial work
 * (5x the FFT cost of the original 256-point design) to be doing
 * inside an audio interrupt - per the project owner, this correlates
 * with the receiver freezing/hanging intermittently on entering FT8
 * mode. The ISR now only fills a buffer and flags it ready; the
 * actual FFT/encode/cascade work moves to ft8_decimator_poll()),
 * called from the MAIN LOOP instead, where taking a few extra
 * milliseconds is harmless. Two buffers so the ISR can keep filling
 * the NEXT subblock while the main loop is still processing the
 * previous one, without a data race.
 */
static int16_t s_out_block[2][FT8_ADAPTER_SUBBLOCK_SIZE];
static uint16_t s_out_block_fill;
/*
 * Raw input sample counter (09/2026) - completely independent of the
 * resampler's own L/M bookkeeping above, added specifically to answer
 * the project owner's slot-drift investigation: capms= (measured
 * wall-clock ms to reach 93 blocks) was reading ~13950-13970 instead
 * of the expected ~14880, a ~6% discrepancy far too large for any
 * plausible crystal-accuracy issue - every constant/formula in the
 * FT8 pipeline itself (resampler L=4/M=15, decimation factors,
 * FT8_SYMBOL_PERIOD, SysTick/HXTAL_VALUE calibration) checked out
 * exactly correct on inspection, so the next step is measuring the
 * RAW INPUT rate directly, with no resampler logic in between, to
 * settle whether the true audio delivery rate itself differs from
 * the assumed 12000Hz (see ft8_decimator_get_raw_sample_count()).
 */
static uint32_t s_raw_sample_count;
static volatile uint8_t s_out_block_writing; /* 0 or 1 - which of the two s_out_block[] the ISR is currently filling */
static volatile bool s_out_block_ready[2];   /* true = full, waiting for ft8_decimator_poll() to consume it */

void ft8_decimator_reset(void)
{
    uint16_t i;

    s_raw_sample_count = 0U;
    /*
     * CRITICAL SECTION (09/2026) - REAL BUG FIX, per the project
     * owner's report: the receiver would intermittently freeze on
     * entering FT8 mode. ft8_decimator_feed_sample() runs inside the
     * audio DMA ISR continuously for as long as
     * demod_am_ft8_capture_set_active(true) is set - which stays true
     * for the ENTIRE time FT8 mode is selected, across every 15-slot
     * boundary, NOT just once at mode entry. This reset function is
     * called from the MAIN LOOP both at mode entry AND at every
     * RTC-aligned slot boundary while FT8 mode stays active (see
     * main.c) - meaning the ISR could be mid-way through updating
     * s_history[]/s_out_block_writing/etc. at the exact moment this
     * function zeroes them, a genuine torn-write race. Depending on
     * timing, that can leave s_out_block_writing/s_out_block_ready[]
     * in a state ft8_decimator_feed_sample()'s own bookkeeping never
     * anticipated (e.g. resumed with a full flag on a buffer it's
     * about to write into again), which is a very plausible cause of
     * an intermittent hang - a data race by nature only bites
     * sometimes, matching "se congela muchas veces" (not always).
     */
    __disable_irq();

    for (i = 0; i < FT8_RESAMPLER_TAPS_PER_PHASE; i++)
    {
        s_history[i] = 0.0f;
    }
    s_next_output_vpos = 0U;
    s_input_count = 0U;
    s_agc_peak = FT8_AGC_TARGET; /* start at unity-ish gain, matches demod_am.c's own s_agc_peak init convention */

    for (i = 0; i < FT8_ADAPTER_SUBBLOCK_SIZE; i++)
    {
        s_out_block[0][i] = 0;
        s_out_block[1][i] = 0;
    }
    s_out_block_fill = 0U;
    s_out_block_writing = 0U;
    s_out_block_ready[0] = false;
    s_out_block_ready[1] = false;

    __enable_irq();
}

static void produce_output_sample(uint8_t phase)
{
    float32_t acc = 0.0f;
    uint16_t j;

    /* phase[r][j] pairs with history[TAPS_PER_PHASE-1-j] - j=0 wants
     * the NEWEST history sample (matching the polyphase derivation in
     * ft8_decimator.h: tap index t=r+4j corresponds to input sample
     * n=base_n-j, i.e. j samples back from the newest). */
    for (j = 0; j < FT8_RESAMPLER_TAPS_PER_PHASE; j++)
    {
        acc += s_resampler_phase[phase][j] * s_history[FT8_RESAMPLER_TAPS_PER_PHASE - 1U - j];
    }

    if (acc > 32767.0f) { acc = 32767.0f; }
    if (acc < -32768.0f) { acc = -32768.0f; }

    s_out_block[s_out_block_writing][s_out_block_fill] = (int16_t)acc;
    s_out_block_fill++;

    if (s_out_block_fill >= FT8_ADAPTER_SUBBLOCK_SIZE)
    {
        /* Flag this buffer ready for ft8_decimator_poll() (main loop)
         * to consume, and switch to filling the OTHER one - see this
         * file's header comment on why the FFT/encode work itself no
         * longer happens here, inside the ISR. If the main loop
         * hasn't drained the other buffer yet by the time we wrap
         * back around to it (a full ~160ms later), the still-ready
         * flag just gets silently overwritten/re-armed here - the
         * main loop is expected to keep up easily at that rate; this
         * is a safety fallback, not the intended steady state. */
        s_out_block_ready[s_out_block_writing] = true;
        s_out_block_writing = (uint8_t)(1U - s_out_block_writing);
        s_out_block_fill = 0U;
    }
}

void ft8_decimator_feed_sample(int16_t sample)
{
    uint16_t i;
    float32_t sample_f;
    float32_t mag;
    uint32_t base_n;

    s_raw_sample_count++; /* independent of everything else below - see ft8_decimator_get_raw_sample_count()'s comment */

    /* Own AGC - see s_agc_peak's declaration comment. Instant attack,
     * exponential release, independent from the main receiver's own. */
    sample_f = (float32_t)sample;
    mag = (sample_f < 0.0f) ? -sample_f : sample_f;
    s_agc_peak *= FT8_AGC_RELEASE;
    if (mag > s_agc_peak) { s_agc_peak = mag; }
    {
        float32_t pk = (s_agc_peak < FT8_AGC_PEAK_MIN) ? FT8_AGC_PEAK_MIN : s_agc_peak;
        float32_t gain = FT8_AGC_TARGET / pk;
        sample_f *= gain;
    }
    if (sample_f > 32767.0f) { sample_f = 32767.0f; }
    if (sample_f < -32768.0f) { sample_f = -32768.0f; }

    /* Shift the new (AGC'd) sample into history - same technique as
     * the original decimator's own sliding window, just a shorter
     * buffer now (120 taps/phase instead of the old single filter's
     * 257 full taps). */
    for (i = 0; i < FT8_RESAMPLER_TAPS_PER_PHASE - 1U; i++)
    {
        s_history[i] = s_history[i + 1U];
    }
    s_history[FT8_RESAMPLER_TAPS_PER_PHASE - 1U] = sample_f;

    s_input_count++;
    base_n = s_input_count - 1U; /* index of the sample just added */

    /*
     * Polyphase resampling bookkeeping: output k occurs at "virtual
     * position" P=15k (in units of 1/48000s, the conceptual L=4x-
     * interpolated rate we never actually compute). The newest real
     * input sample just added occupies virtual position 4*base_n.
     * That sample is USABLE for output k (as its "j=0" tap) once
     * base_n == floor(P/4) - i.e. once this is the newest real sample
     * whose virtual position is <= P. A `while` (not `if`) in case
     * bookkeeping ever needs to catch up by more than one output in
     * a single input sample - shouldn't happen given M=15 > L=4, but
     * safe either way.
     */
    while ((s_next_output_vpos / (uint32_t)FT8_RESAMPLER_L) == base_n)
    {
        uint8_t phase = (uint8_t)(s_next_output_vpos % (uint32_t)FT8_RESAMPLER_L);
        produce_output_sample(phase);
        s_next_output_vpos += (uint32_t)FT8_RESAMPLER_M;
    }
}

void ft8_decimator_feed_block(const int16_t *samples, uint16_t n)
{
    uint16_t i;
    for (i = 0; i < n; i++)
    {
        ft8_decimator_feed_sample(samples[i]);
    }
}

void ft8_decimator_poll(void)
{
    /* Drains whichever buffer(s) the ISR has marked ready - see this
     * file's header comment on why the FFT/encode work (inside
     * ft8_waterfall_feed_subblock()) happens HERE, in the main loop,
     * not inside ft8_decimator_feed_sample()'s ISR context. Checks
     * both slots (not just "the one not currently being written") so
     * a single main-loop iteration that's briefly fallen behind can
     * catch back up without waiting for the ISR to wrap around again. */
    uint8_t i;
    for (i = 0; i < 2U; i++)
    {
        if (s_out_block_ready[i])
        {
            s_out_block_ready[i] = false;
            ft8_waterfall_feed_subblock(s_out_block[i]);
        }
    }
}

uint32_t ft8_decimator_get_raw_sample_count(void)
{
    return s_raw_sample_count;
}
