#include <math.h>
#include "hfdl_symsync.h"

/* symsync_crcf_create_kaiser(k=3,m=3,beta=0.9,M=16) real de liquid-dsp,
 * ya descompuesto en M=16 sub-filtros de 18 taps (h_len=289, 1 coef.
 * descartado por division entera, igual que en firpfb_create()). */
const float32_t hfdl_symsync_mf_pfb[HFDL_SYMSYNC_M][HFDL_SYMSYNC_SUBFILTER_LEN] = {
  { 1.0516506777e-08f, -5.0599079579e-02f, -1.8917689637e-09f, 1.1966356635e-01f, 3.1178842619e-08f, -2.7094095945e-01f, -3.8891286636e-08f, 9.3831491470e-01f, 1.5000000000e+00f, 9.3831491470e-01f, -3.8891286636e-08f, -2.7094095945e-01f, 3.1178842619e-08f, 1.1966356635e-01f, -1.8917689637e-09f, -5.0599079579e-02f, 1.0516506777e-08f, 1.5699625015e-02f },
  { 2.8436598368e-03f, -4.8865661025e-02f, -7.5699691661e-03f, 1.1613944173e-01f, 1.7045322806e-02f, -2.6230517030e-01f, -4.2091596872e-02f, 8.7687587738e-01f, 1.4974889755e+00f, 9.9817371368e-01f, 4.5209005475e-02f, -2.7724918723e-01f, -1.7924206331e-02f, 1.2210410833e-01f, 7.9839210957e-03f, -5.1876664162e-02f, -3.0541424640e-03f, 1.6367148608e-02f },
  { 5.4573873058e-03f, -4.6720810235e-02f, -1.4667805284e-02f, 1.1161934584e-01f, 3.3087387681e-02f, -2.5154641271e-01f, -8.0934062600e-02f, 8.1421506405e-01f, 1.4899728298e+00f, 1.0560983419e+00f, 9.3377754092e-02f, -2.8103587031e-01f, -3.6588016897e-02f, 1.2338047475e-01f, 1.6316065565e-02f, -5.2656874061e-02f, -6.2953969464e-03f, 1.6881110147e-02f },
  { 7.8256223351e-03f, -4.4211216271e-02f, -2.1243384108e-02f, 1.0619721562e-01f, 4.8017293215e-02f, -2.3887741566e-01f, -1.1642171443e-01f, 7.5069391727e-01f, 1.4774976969e+00f, 1.1117438078e+00f, 1.4432343841e-01f, -2.8211861849e-01f, -5.5838216096e-02f, 1.2342047691e-01f, 2.4923741817e-02f, -5.2901715040e-02f, -9.6965255216e-03f, 1.7221435905e-02f },
  { 9.9366987124e-03f, -4.1385136545e-02f, -2.7254387736e-02f, 9.9971622229e-02f, 6.1741568148e-02f, -2.2451703250e-01f, -1.4847549796e-01f, 6.8667340279e-01f, 1.4601439238e+00f, 1.1647757292e+00f, 1.9783896208e-01f, -2.8032970428e-01f, -7.5508035719e-02f, 1.2216158211e-01f, 3.3727299422e-02f, -5.2577346563e-02f, -1.3226712123e-02f, 1.7369149253e-02f },
  { 1.1782868765e-02f, -3.8291797042e-02f, -3.2666638494e-02f, 9.3044765294e-02f, 7.4183039367e-02f, -2.0868852735e-01f, -1.7704275250e-01f, 6.2251204252e-01f, 1.4380204678e+00f, 1.2148709297e+00f, 2.5369411707e-01f, -2.7551698685e-01f, -9.5418646932e-02f, 1.1955164373e-01f, 4.2641047388e-02f, -5.1654711366e-02f, -1.6851529479e-02f, 1.7306609079e-02f },
  { 1.3360030949e-02f, -3.4980837256e-02f, -3.7454035133e-02f, 8.5521310568e-02f, 8.5280202329e-02f, -1.9161719084e-01f, -2.0209702849e-01f, 5.5856347084e-01f, 1.4112675190e+00f, 1.2617243528e+00f, 3.1163656712e-01f, -2.6754602790e-01f, -1.1538000405e-01f, 1.1555039138e-01f, 5.1573678851e-02f, -5.0110094249e-02f, -2.0533336326e-02f, 1.7017915845e-02f },
  { 1.4667686075e-02f, -3.1501628458e-02f, -4.1598591954e-02f, 7.7507086098e-02f, 9.4987720251e-02f, -1.7352859676e-01f, -2.2363781929e-01f, 4.9517288804e-01f, 1.3800526857e+00f, 1.3050473928e+00f, 3.7139415741e-01f, -2.5630176067e-01f, -1.3519215584e-01f, 1.1012989283e-01f, 6.0429371893e-02f, -4.7925658524e-02f, -2.4231471121e-02f, 1.6489125788e-02f },
  { 1.5708697960e-02f, -2.7902737260e-02f, -4.5090042055e-02f, 6.9108165801e-02f, 1.0327576101e-01f, -1.5464679897e-01f, -2.4168974161e-01f, 4.3267545104e-01f, 1.3445719481e+00f, 1.3445719481e+00f, 4.3267545104e-01f, -2.4168974161e-01f, -1.5464679897e-01f, 1.0327576101e-01f, 6.9108165801e-02f, -4.5090042055e-02f, -2.7902737260e-02f, 1.5708697960e-02f },
  { 1.6489125788e-02f, -2.4231471121e-02f, -4.7925658524e-02f, 6.0429371893e-02f, 1.1012989283e-01f, -1.3519215584e-01f, -2.5630176067e-01f, 3.7139415741e-01f, 1.3050473928e+00f, 1.3800526857e+00f, 4.9517288804e-01f, -2.2363781929e-01f, -1.7352859676e-01f, 9.4987720251e-02f, 7.7507086098e-02f, -4.1598591954e-02f, -3.1501628458e-02f, 1.4667686075e-02f },
  { 1.7017915845e-02f, -2.0533336326e-02f, -5.0110094249e-02f, 5.1573678851e-02f, 1.1555039138e-01f, -1.1538000405e-01f, -2.6754602790e-01f, 3.1163656712e-01f, 1.2617243528e+00f, 1.4112675190e+00f, 5.5856347084e-01f, -2.0209702849e-01f, -1.9161719084e-01f, 8.5280202329e-02f, 8.5521310568e-02f, -3.7454035133e-02f, -3.4980837256e-02f, 1.3360030949e-02f },
  { 1.7306609079e-02f, -1.6851529479e-02f, -5.1654711366e-02f, 4.2641047388e-02f, 1.1955164373e-01f, -9.5418646932e-02f, -2.7551698685e-01f, 2.5369411707e-01f, 1.2148709297e+00f, 1.4380204678e+00f, 6.2251204252e-01f, -1.7704275250e-01f, -2.0868852735e-01f, 7.4183039367e-02f, 9.3044765294e-02f, -3.2666638494e-02f, -3.8291797042e-02f, 1.1782868765e-02f },
  { 1.7369149253e-02f, -1.3226712123e-02f, -5.2577346563e-02f, 3.3727299422e-02f, 1.2216158211e-01f, -7.5508035719e-02f, -2.8032970428e-01f, 1.9783896208e-01f, 1.1647757292e+00f, 1.4601439238e+00f, 6.8667340279e-01f, -1.4847549796e-01f, -2.2451703250e-01f, 6.1741568148e-02f, 9.9971622229e-02f, -2.7254387736e-02f, -4.1385136545e-02f, 9.9366987124e-03f },
  { 1.7221435905e-02f, -9.6965255216e-03f, -5.2901715040e-02f, 2.4923741817e-02f, 1.2342047691e-01f, -5.5838216096e-02f, -2.8211861849e-01f, 1.4432343841e-01f, 1.1117438078e+00f, 1.4774976969e+00f, 7.5069391727e-01f, -1.1642171443e-01f, -2.3887741566e-01f, 4.8017293215e-02f, 1.0619721562e-01f, -2.1243384108e-02f, -4.4211216271e-02f, 7.8256223351e-03f },
  { 1.6881110147e-02f, -6.2953969464e-03f, -5.2656874061e-02f, 1.6316065565e-02f, 1.2338047475e-01f, -3.6588016897e-02f, -2.8103587031e-01f, 9.3377754092e-02f, 1.0560983419e+00f, 1.4899728298e+00f, 8.1421506405e-01f, -8.0934062600e-02f, -2.5154641271e-01f, 3.3087387681e-02f, 1.1161934584e-01f, -1.4667805284e-02f, -4.6720810235e-02f, 5.4573873058e-03f },
  { 1.6367148608e-02f, -3.0541424640e-03f, -5.1876664162e-02f, 7.9839210957e-03f, 1.2210410833e-01f, -1.7924206331e-02f, -2.7724918723e-01f, 4.5209005475e-02f, 9.9817371368e-01f, 1.4974889755e+00f, 8.7687587738e-01f, -4.2091596872e-02f, -2.6230517030e-01f, 1.7045322806e-02f, 1.1613944173e-01f, -7.5699691661e-03f, -4.8865661025e-02f, 2.8436598368e-03f },
};

const float32_t hfdl_symsync_dmf_pfb[HFDL_SYMSYNC_M][HFDL_SYMSYNC_SUBFILTER_LEN] = {
  { 2.9288528021e-03f, 1.4952663332e-03f, -7.7240727842e-03f, -2.9620577116e-03f, 1.7365893349e-02f, 7.4212094769e-03f, -4.3353538960e-02f, -6.0236591846e-02f, 0.0000000000e+00f, 6.0236591846e-02f, 4.3353538960e-02f, -7.4212094769e-03f, -1.7365893349e-02f, 2.9620577116e-03f, 7.7240727842e-03f, -1.4952663332e-03f, -2.9288528021e-03f, 3.3149268711e-04f },
  { 2.7101370506e-03f, 1.9259513356e-03f, -7.2840414941e-03f, -3.9947656915e-03f, 1.6431204975e-02f, 9.6313459799e-03f, -4.0191896260e-02f, -6.1628073454e-02f, -4.9795000814e-03f, 5.8491334319e-02f, 4.6371478587e-02f, -5.0131399184e-03f, -1.8169650808e-02f, 1.8458194099e-03f, 8.1025706604e-03f, -1.0219021933e-03f, -3.1263038982e-03f, 5.8672635350e-04f },
  { 2.4740460794e-03f, 2.3114006035e-03f, -6.7902277224e-03f, -4.9373167567e-03f, 1.5380702913e-02f, 1.1634239927e-02f, -3.6912392825e-02f, -6.2662050128e-02f, -9.9276835099e-03f, 5.6398991495e-02f, 4.9220297486e-02f, -2.4181629997e-03f, -1.8828123808e-02f, 6.5370957600e-04f, 8.4123266861e-03f, -5.0904101226e-04f, -3.2986120787e-03f, 4.2423969717e-04f },
  { 2.2244292777e-03f, 2.6496993378e-03f, -6.2505058013e-03f, -5.7842680253e-03f, 1.4229686931e-02f, 1.3422809541e-02f, -3.3541124314e-02f, -6.3337281346e-02f, -1.4813056216e-02f, 5.3969267756e-02f, 5.1875509322e-02f, 3.5068255966e-04f, -1.9327709451e-02f, -6.0530297924e-04f, 8.6464313790e-03f, 3.9493410441e-05f, -3.4420960583e-03f, 2.4236057652e-04f },
  { 1.9651714247e-03f, 2.9395879246e-03f, -5.6727961637e-03f, -6.5315160900e-03f, 1.2993928045e-02f, 1.4991823584e-02f, -3.0104450881e-02f, -6.3655212522e-02f, -1.9604420289e-02f, 5.1213003695e-02f, 5.4313559085e-02f, 3.2783749048e-03f, -1.9655670971e-02f, -1.9212653860e-03f, 8.7984269485e-03f, 6.1926292256e-04f, -3.5531800240e-03f, 4.2297058826e-05f },
  { 1.7000292428e-03f, 3.1803795137e-03f, -5.0651524216e-03f, -7.1760350838e-03f, 1.1689302512e-02f, 1.6338085756e-02f, -2.6628490537e-02f, -6.3619486988e-02f, -2.4272058159e-02f, 4.8144757748e-02f, 5.6511972100e-02f, 6.3483826816e-03f, -1.9800448790e-02f, -3.2831220888e-03f, 8.8625252247e-03f, 1.2252392480e-03f, -3.6284748930e-03f, -1.7442276294e-04f },
  { 1.4326022938e-03f, 3.3720026258e-03f, -4.4356146827e-03f, -7.7160224319e-03f, 1.0331619531e-02f, 1.7460446805e-02f, -2.3139143363e-02f, -6.3236713409e-02f, -2.8786841780e-02f, 4.4781696051e-02f, 5.8449923992e-02f, 9.5422947779e-03f, -1.9751552492e-02f, -4.6788481995e-03f, 8.8336942717e-03f, 1.8518503057e-03f, -3.6648842506e-03f, -4.0596278268e-04f },
  { 1.1663497426e-03f, 3.5149895120e-03f, -3.7920465693e-03f, -8.1507796422e-03f, 8.9366072789e-03f, 1.8359523267e-02f, -1.9661769271e-02f, -6.2516078353e-02f, -3.3121068031e-02f, 4.1142176837e-02f, 6.0107994825e-02f, 1.2840250507e-02f, -1.9499918446e-02f, -6.0955900699e-03f, 8.7076388299e-03f, 2.4929614738e-03f, -3.6596497521e-03f, -6.5015855944e-04f },
  { 9.0452824952e-04f, 3.6103653256e-03f, -3.1420257874e-03f, -8.4808049724e-03f, 7.5196139514e-03f, 1.9037904218e-02f, -1.6220936552e-02f, -6.1468604952e-02f, -3.7247683853e-02f, 3.7247683853e-02f, 6.1468604952e-02f, 1.6220936552e-02f, -1.9037904218e-02f, -7.5196139514e-03f, 8.4808049724e-03f, 3.1420257874e-03f, -3.6103653256e-03f, -9.0452824952e-04f },
  { 6.5015855944e-04f, 3.6596497521e-03f, -2.4929614738e-03f, -8.7076388299e-03f, 6.0955900699e-03f, 1.9499918446e-02f, -1.2840250507e-02f, -6.0107994825e-02f, -4.1142176837e-02f, 3.3121068031e-02f, 6.2516078353e-02f, 1.9661769271e-02f, -1.8359523267e-02f, -8.9366072789e-03f, 8.1507796422e-03f, 3.7920465693e-03f, -3.5149895120e-03f, -1.1663497426e-03f },
  { 4.0596278268e-04f, 3.6648842506e-03f, -1.8518503057e-03f, -8.8336942717e-03f, 4.6788481995e-03f, 1.9751552492e-02f, -9.5422947779e-03f, -5.8449923992e-02f, -4.4781696051e-02f, 2.8786841780e-02f, 6.3236713409e-02f, 2.3139143363e-02f, -1.7460446805e-02f, -1.0331619531e-02f, 7.7160224319e-03f, 4.4356146827e-03f, -3.3720026258e-03f, -1.4326022938e-03f },
  { 1.7442276294e-04f, 3.6284748930e-03f, -1.2252392480e-03f, -8.8625252247e-03f, 3.2831220888e-03f, 1.9800448790e-02f, -6.3483826816e-03f, -5.6511972100e-02f, -4.8144757748e-02f, 2.4272058159e-02f, 6.3619486988e-02f, 2.6628490537e-02f, -1.6338085756e-02f, -1.1689302512e-02f, 7.1760350838e-03f, 5.0651524216e-03f, -3.1803795137e-03f, -1.7000292428e-03f },
  { -4.2297058826e-05f, 3.5531800240e-03f, -6.1926292256e-04f, -8.7984269485e-03f, 1.9212653860e-03f, 1.9655670971e-02f, -3.2783749048e-03f, -5.4313559085e-02f, -5.1213003695e-02f, 1.9604420289e-02f, 6.3655212522e-02f, 3.0104450881e-02f, -1.4991823584e-02f, -1.2993928045e-02f, 6.5315160900e-03f, 5.6727961637e-03f, -2.9395879246e-03f, -1.9651714247e-03f },
  { -2.4236057652e-04f, 3.4420960583e-03f, -3.9493410441e-05f, -8.6464313790e-03f, 6.0530297924e-04f, 1.9327709451e-02f, -3.5068255966e-04f, -5.1875509322e-02f, -5.3969267756e-02f, 1.4813056216e-02f, 6.3337281346e-02f, 3.3541124314e-02f, -1.3422809541e-02f, -1.4229686931e-02f, 5.7842680253e-03f, 6.2505058013e-03f, -2.6496993378e-03f, -2.2244292777e-03f },
  { -4.2423969717e-04f, 3.2986120787e-03f, 5.0904101226e-04f, -8.4123266861e-03f, -6.5370957600e-04f, 1.8828123808e-02f, 2.4181629997e-03f, -4.9220297486e-02f, -5.6398991495e-02f, 9.9276835099e-03f, 6.2662050128e-02f, 3.6912392825e-02f, -1.1634239927e-02f, -1.5380702913e-02f, 4.9373167567e-03f, 6.7902277224e-03f, -2.3114006035e-03f, -2.4740460794e-03f },
  { -5.8672635350e-04f, 3.1263038982e-03f, 1.0219021933e-03f, -8.1025706604e-03f, -1.8458194099e-03f, 1.8169650808e-02f, 5.0131399184e-03f, -4.6371478587e-02f, -5.8491334319e-02f, 4.9795000814e-03f, 6.1628073454e-02f, 4.0191896260e-02f, -9.6313459799e-03f, -1.6431204975e-02f, 3.9947656915e-03f, 7.2840414941e-03f, -1.9259513356e-03f, -2.7101370506e-03f },
};

/* Loop filter coefficients - see hfdl_symsync.h for the full
 * derivation/override comment (moved there 20/08/2026 so
 * main.c's own boot-time build-config print, which needs to see
 * these names, doesn't have to include this whole .c file). */

void hfdl_symsync_reset(hfdl_symsync_t *q)
{
	for (uint32_t i = 0; i < HFDL_SYMSYNC_HIST_LEN; i++) {
		q->hist_i[i] = 0.0f;
		q->hist_q[i] = 0.0f;
	}
	/* rate = k/k_out - see module header's session-history note for
	 * why this specific value (NOT 1/M) matters. */
	q->rate = (float32_t)HFDL_SYMSYNC_K / (float32_t)HFDL_SYMSYNC_K_OUT;
	q->del = q->rate;
	q->b = 0;
	q->bf = 0.0f;
	q->tau = 0.0f;
	q->tau_decim = 0.0f;
	q->decim_counter = 0u;
	q->loop_y_prev = 0.0f;
}

void hfdl_symsync_lock(hfdl_symsync_t *q)
{
	q->is_locked = 1u;
}

void hfdl_symsync_unlock(hfdl_symsync_t *q)
{
	q->is_locked = 0u;
}

static void push_sample(float32_t *hist, float32_t new_val)
{
	for (uint32_t i = 0; i + 1 < HFDL_SYMSYNC_HIST_LEN; i++) {
		hist[i] = hist[i + 1];
	}
	hist[HFDL_SYMSYNC_HIST_LEN - 1] = new_val;
}

/* lag=0 uses the NEWEST 18-sample window (hist[HFDL_SYMSYNC_HIST_EXTRA..
 * HFDL_SYMSYNC_HIST_LEN-1]) - identical to the original, pre-T/2
 * dot_phase(). lag=N shifts that window N raw samples further into the
 * past (hist[HFDL_SYMSYNC_HIST_EXTRA-N .. HFDL_SYMSYNC_HIST_LEN-1-N]) -
 * caller (hfdl_symsync_step()) is responsible for clamping lag to
 * [0, HFDL_SYMSYNC_HIST_EXTRA], see HFDL_SYMSYNC_HIST_EXTRA's own
 * comment for why that range is expected to always be sufficient. */
static float32_t dot_phase_lagged(const float32_t *hist,
		const float32_t pfb[HFDL_SYMSYNC_M][HFDL_SYMSYNC_SUBFILTER_LEN],
		int32_t phase, int32_t lag)
{
	float32_t acc = 0.0f;
	int32_t base = (int32_t)HFDL_SYMSYNC_HIST_EXTRA - lag;
	for (uint32_t i = 0; i < HFDL_SYMSYNC_SUBFILTER_LEN; i++) {
		acc += hist[base + (int32_t)i] * pfb[phase][i];
	}
	return acc;
}

static float32_t dot_phase(const float32_t *hist, const float32_t pfb[HFDL_SYMSYNC_M][HFDL_SYMSYNC_SUBFILTER_LEN],
		int32_t phase)
{
	return dot_phase_lagged(hist, pfb, phase, 0);
}

float32_t hfdl_symsync_get_tau_decim(const hfdl_symsync_t *q)
{
    return q->tau_decim;
}

void hfdl_symsync_step(hfdl_symsync_t *q, float32_t i_in, float32_t q_in,
		float32_t *i_out, float32_t *q_out,
		float32_t *i_half_out, float32_t *q_half_out,
		uint8_t *half_lag_clamped_out, uint32_t *n_out)
{
	push_sample(q->hist_i, i_in);
	push_sample(q->hist_q, q_in);

	uint32_t n = 0;
	while (q->b < (int32_t)HFDL_SYMSYNC_M) {
		float32_t mf_i = dot_phase(q->hist_i, hfdl_symsync_mf_pfb, q->b);
		float32_t mf_q = dot_phase(q->hist_q, hfdl_symsync_mf_pfb, q->b);

		i_out[n] = mf_i / (float32_t)HFDL_SYMSYNC_K;
		q_out[n] = mf_q / (float32_t)HFDL_SYMSYNC_K;

		/* T/2 TAP (25/08/2026) - see hfdl_symsync_step()'s own header
		 * comment and HFDL_SYMSYNC_HIST_EXTRA's comment for the full
		 * rationale. Half a symbol, in this loop's own bf/phase
		 * units, is 0.5*del*M (del is THIS symbol's tau increment,
		 * i.e. the loop's current estimate of samples/symbol - using
		 * it rather than the nominal HFDL_SYMSYNC_K keeps the T/2
		 * offset correct even while the loop is still converging or
		 * tracking a real rate offset). Subtracting that from the
		 * main tap's own bf gives the half-symbol-EARLIER phase;
		 * normalizing it back into [0, M) walks `lag` up by exactly
		 * one raw sample for each full M it had to cross, which is
		 * exactly what dot_phase_lagged()'s own window-shift
		 * convention expects. */
		float32_t half_bf = q->bf - 0.5f * q->del * (float32_t)HFDL_SYMSYNC_M;
		int32_t half_phase = (int32_t)(half_bf + (half_bf >= 0.0f ? 0.5f : -0.5f));
		int32_t half_lag = 0;
		while (half_phase < 0) {
			half_phase += (int32_t)HFDL_SYMSYNC_M;
			half_lag++;
		}
		while (half_phase >= (int32_t)HFDL_SYMSYNC_M) {
			half_phase -= (int32_t)HFDL_SYMSYNC_M;
			half_lag--;
		}
		uint8_t clamped = 0u;
		if (half_lag < 0) {
			half_lag = 0;
			clamped = 1u; /* shouldn't happen - del would have to be negative */
		} else if (half_lag > (int32_t)HFDL_SYMSYNC_HIST_EXTRA) {
			half_lag = (int32_t)HFDL_SYMSYNC_HIST_EXTRA;
			clamped = 1u; /* see HFDL_SYMSYNC_HIST_EXTRA's own comment - del far outside HFDL's realistic range */
		}
		float32_t mf_i_half = dot_phase_lagged(q->hist_i, hfdl_symsync_mf_pfb, half_phase, half_lag);
		float32_t mf_q_half = dot_phase_lagged(q->hist_q, hfdl_symsync_mf_pfb, half_phase, half_lag);
		i_half_out[n] = mf_i_half / (float32_t)HFDL_SYMSYNC_K;
		q_half_out[n] = mf_q_half / (float32_t)HFDL_SYMSYNC_K;
		if (half_lag_clamped_out != (void *)0) {
			half_lag_clamped_out[n] = clamped;
		}

		if (q->decim_counter == HFDL_SYMSYNC_K_OUT) {
			q->decim_counter = 0u;
			if (!q->is_locked) {
				float32_t dmf_i = dot_phase(q->hist_i, hfdl_symsync_dmf_pfb, q->b);
				float32_t dmf_q = dot_phase(q->hist_q, hfdl_symsync_dmf_pfb, q->b);

				/* Re(conj(mf)*dmf) = mf_i*dmf_i + mf_q*dmf_q, per
				 * [Mengali:1997] Eq.(8.3.5) - see module header. */
				float32_t err = mf_i * dmf_i + mf_q * dmf_q;
				if (err > 1.0f) {
					err = 1.0f;
				} else if (err < -1.0f) {
					err = -1.0f;
				}

				float32_t q_hat = HFDL_SYMSYNC_LOOP_B0 * err
						+ HFDL_SYMSYNC_LOOP_NEG_A1 * q->loop_y_prev;
				q->loop_y_prev = q_hat;

				q->rate += HFDL_SYMSYNC_RATE_ADJ * q_hat;
				q->del = q->rate + q_hat;
				q->tau_decim = q->tau; /* save once-per-symbol timing estimate - mirrors liquid's own "tau_decim = tau" placement, inside the "not locked" branch (a real "continue" skips this too when locked - see module header) */
			}
		}
		q->decim_counter++;

		q->tau += q->del;
		q->bf = q->tau * (float32_t)HFDL_SYMSYNC_M;
		q->b = (int32_t)(q->bf + (q->bf >= 0.0f ? 0.5f : -0.5f)); /* round-to-nearest, matches roundf() */
		n++;
	}
	q->tau -= 1.0f;
	q->bf -= (float32_t)HFDL_SYMSYNC_M;
	q->b -= (int32_t)HFDL_SYMSYNC_M;

	*n_out = n;
}

/* ---- self-test ---- */

static uint32_t xorshift32(uint32_t *state)
{
	uint32_t x = *state;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	*state = x;
	return x;
}

uint8_t hfdl_symsync_selftest(void)
{
	/* --- Part 1: structural sanity - mf phase (M/2) should be
	 * symmetric-filter-like with its peak near 1.5 at the center tap
	 * (matches the literal table's own known values, a coarse
	 * corruption check) --- */
	{
		float32_t peak = 0.0f;
		for (uint32_t i = 0; i < HFDL_SYMSYNC_SUBFILTER_LEN; i++) {
			float32_t v = hfdl_symsync_mf_pfb[0][i];
			if (v > peak) {
				peak = v;
			}
		}
		if (fabsf(peak - 1.5f) > 0.01f) {
			return 0u;
		}
	}

	/* --- Part 2: stability over a moderate run - feed matched-filtered
	 * synthetic BPSK symbols (using hfdl_matched_filter's own real 19
	 * taps, so the input signal is realistic for what this stage
	 * actually expects - see module header's session-history note
	 * about why an earlier un-filtered test gave misleading results)
	 * through a fixed fractional sample-timing offset, and confirm:
	 *   (a) tau/rate never go NaN or diverge outside a sane range
	 *   (b) the sequence of tau_decim values is smooth (bounded
	 *       sample-to-sample change) rather than jumping erratically -
	 *       this is what actually broke in the session's first
	 *       (buggy, wrong initial rate) attempt: flat ~0.08 variance,
	 *       samples effectively uncorrelated from one to the next. */
	{
		static const float32_t mfilt[19] = {
			-0.0170974647427123f, 0.01148231492068473f, 0.03138375667422348f, 0.009454398851680437f,
			-0.04161644170893816f, -0.06451564801420356f, -0.005495792933327306f, 0.1316404671361545f,
			0.2759693160697777f, 0.3375901874933208f, 0.2759693160697777f, 0.1316404671361545f,
			-0.005495792933327306f, -0.06451564801420356f, -0.04161644170893816f, 0.009454398851680437f,
			0.03138375667422348f, 0.01148231492068473f, -0.0170974647427123f
		};

		uint32_t rng = 777u;
		const uint32_t n_symbols = 200u; /* reduced from 600 (10/08/2026) - this is a stability/plumbing check, not a convergence-time measurement, so a shorter run suffices; also brings the self-test's own static RAM footprint down from ~14KB to ~4.8KB, which matters when running several of these modules' selftests together on real hardware (see hfdl_demod_chain.c's own selftest for why this was checked with nm, not assumed) */
		const uint32_t n_samples = n_symbols * HFDL_SYMSYNC_K;

		static float32_t upsampled[600]; /* n_symbols * K, static to avoid a large stack array */
		for (uint32_t i = 0; i < n_samples; i++) {
			upsampled[i] = 0.0f;
		}
		for (uint32_t s = 0; s < n_symbols; s++) {
			upsampled[s * HFDL_SYMSYNC_K] = ((xorshift32(&rng) & 1u) != 0u) ? 1.0f : -1.0f;
		}

		static float32_t shaped[600];
		for (uint32_t i = 0; i < n_samples; i++) {
			float32_t acc = 0.0f;
			for (uint32_t j = 0; j < 19u; j++) {
				if (i >= j) {
					acc += upsampled[i - j] * mfilt[j];
				}
			}
			shaped[i] = acc;
		}

		hfdl_symsync_t q;
		hfdl_symsync_reset(&q);
		hfdl_symsync_unlock(&q);

		float32_t i_out[HFDL_SYMSYNC_M];
		float32_t q_out[HFDL_SYMSYNC_M];
		float32_t i_half_out[HFDL_SYMSYNC_M];
		float32_t q_half_out[HFDL_SYMSYNC_M];
		uint8_t half_clamped[HFDL_SYMSYNC_M];
		uint32_t n_written;

		float32_t prev_tau_decim = 0.0f;
		uint8_t have_prev = 0u;
		uint32_t tau_decim_updates = 0u;
		float32_t max_symbol_step = 0.0f;

		/* T/2 TAP CROSS-CHECK (25/08/2026, "ecualizador T/2" round,
		 * pieza 1) - independent reference computed the SAME way this
		 * test already builds its own `x` above (plain linear
		 * interpolation on shaped[], not this module's own Kaiser-
		 * windowed sinc interpolation), evaluated HFDL_SYMSYNC_K/2 raw
		 * samples earlier than the MAIN tap's own true sampling
		 * instant.
		 *
		 * GROUP DELAY (confirmed the hard way this session, not
		 * assumed): naively comparing against shaped[i+frac] (as if
		 * the output produced when this call's raw index is `i`
		 * corresponded to that same instant) gives large, essentially
		 * uncorrelated errors (~0.35 mean) - because hfdl_symsync's
		 * own interpolating filterbank (HFDL_SYMSYNC_SUBFILTER_LEN=18
		 * taps, symmetric-ish Kaiser window) has a real, fixed group
		 * delay of its own between "newest raw sample pushed" and
		 * "the effective instant its interpolated output represents".
		 * A shift search (trying every integer raw-sample offset -5..
		 * +20 and comparing mean absolute error against this same
		 * test's own main-tap output) found a clean, sharp minimum
		 * (mean error ~0.005, down from ~0.35 one sample either side)
		 * at a 9-raw-sample offset - i.e. the MAIN tap's true sampled
		 * instant is `i - 9 + frac`, not `i + frac`. With that
		 * correction applied, the T/2 tap (T/2 = HFDL_SYMSYNC_K/2
		 * raw samples earlier still) matches this same reference to
		 * ~0.012 mean / ~0.033 max absolute error - consistent with
		 * plain linear interpolation's own approximation error
		 * against the module's real sinc interpolation on a pulse-
		 * shaped (non-flat-spectrum) test signal, not a sign of a
		 * remaining bug. This is still an APPROXIMATE cross-check
		 * (linear-interpolation reference, not bit-exact), and this
		 * loop is UNLOCKED (del can drift off HFDL_SYMSYNC_K), so
		 * tolerance is kept generous - not a substitute for
		 * validating against a real captured signal or a locally-
		 * built liquid-dsp reference (see this module's header for
		 * that same caveat on the MAIN tap's own self-test, which
		 * checks stability, not accuracy, for exactly this reason). */
		#define HFDL_SYMSYNC_TEST_GROUP_DELAY 9
		float32_t half_max_abs_err = 0.0f;
		uint32_t half_checked = 0u;

		for (uint32_t i = 0; i + 1 < n_samples; i++) {
			/* Simple fractional-offset "resample" via linear
			 * interpolation between adjacent shaped[] samples -
			 * fixed 0.4-sample offset, same as the module's
			 * development-time cross-check against the real
			 * liquid-dsp object (see header). */
			float32_t frac = 0.4f;
			float32_t x = shaped[i] * (1.0f - frac) + shaped[i + 1] * frac;

			float32_t tau_decim_before = q.tau_decim;
			hfdl_symsync_step(&q, x, 0.0f, i_out, q_out,
					i_half_out, q_half_out, half_clamped, &n_written);

			if (n_written > HFDL_SYMSYNC_M) {
				return 0u; /* more outputs than the documented worst case - structural bug */
			}

			if (n_written > 0u && i > 30u * HFDL_SYMSYNC_K && !half_clamped[0]) {
				float32_t pos_main_ref = (float32_t)i - (float32_t)HFDL_SYMSYNC_TEST_GROUP_DELAY + frac;
				float32_t half_pos = pos_main_ref - 0.5f * (float32_t)HFDL_SYMSYNC_K;
				int32_t half_i0 = (int32_t)floorf(half_pos);
				float32_t half_frac = half_pos - (float32_t)half_i0;
				if (half_i0 >= 0 && (uint32_t)half_i0 + 1u < n_samples) {
					float32_t x_half_ref = shaped[half_i0] * (1.0f - half_frac) + shaped[half_i0 + 1] * half_frac;
					float32_t err = fabsf(i_half_out[0] - x_half_ref);
					if (err > half_max_abs_err) {
						half_max_abs_err = err;
					}
					half_checked++;
				}
			}

			/* NaN/divergence guard on the raw per-call state (this
			 * CAN legitimately jump by several units per call here,
			 * since rate=k/k_out=3 - see header - so this only
			 * checks it stays finite and within a generous bound,
			 * not that it changes smoothly call-to-call). */
			if (!(q.tau > -10.0f && q.tau < 10.0f)) {
				return 0u;
			}
			if (!(q.rate > 0.0f && q.rate < 10.0f)) {
				return 0u;
			}

			if (q.tau_decim != tau_decim_before) {
				/* A new once-per-symbol timing estimate was just
				 * captured - THIS is the value that should move
				 * smoothly from one symbol to the next (unlike raw
				 * per-call tau - see header). tau_decim lives in a
				 * CYCLIC space (mod 1 - it mirrors tau, which wraps
				 * by exactly -1.0f whenever the filterbank index
				 * rolls over), so a raw difference can show a
				 * spurious ~1.0 jump that's just the wrap, not real
				 * instability - unwrap it the standard way before
				 * judging smoothness. */
				if (have_prev) {
					float32_t raw_step = q.tau_decim - prev_tau_decim;
					if (raw_step > 0.5f) {
						raw_step -= 1.0f;
					} else if (raw_step < -0.5f) {
						raw_step += 1.0f;
					}
					float32_t step = fabsf(raw_step);
					if (step > 0.5f) {
						/* Half a filterbank-phase-cycle's worth of
						 * movement in ONE symbol, even after
						 * unwrapping, would indicate the loop is
						 * unstable/oscillating wildly, not just
						 * tracking a slow offset - bt=0.001 is far
						 * too narrow a loop to legitimately move
						 * this fast symbol-to-symbol. */
						return 0u;
					}
					if (step > max_symbol_step) {
						max_symbol_step = step;
					}
				}
				prev_tau_decim = q.tau_decim;
				have_prev = 1u;
				tau_decim_updates++;
			}
		}

		/* Sanity: tau_decim should have updated roughly once per
		 * symbol (n_symbols of them, give or take a few at start/end
		 * due to the pipeline's own latency) - far fewer would mean
		 * the decim_counter/k_out bookkeeping isn't working. */
		if (tau_decim_updates < n_symbols / 2u) {
			return 0u;
		}
		/* Confirm SOME timing tracking actually happened (all-zero
		 * movement the whole run would mean the loop never moved at
		 * all - a sign it's not doing anything, e.g. the error
		 * computation short-circuiting to always-zero). */
		if (max_symbol_step < 1.0e-6f) {
			return 0u;
		}

		/* T/2 tap cross-check (see this block's own comment above for
		 * why 0.25 is a deliberately generous tolerance, not a tight
		 * numerical bound) - require the check to have actually run
		 * on a meaningful number of symbols (not silently 0 due to a
		 * wiring mistake making the "if" above never true), and the
		 * worst-case error against the independent linear-
		 * interpolation reference to stay in a sane ballpark. */
		if (half_checked < n_symbols / 4u) {
			return 0u;
		}
		if (half_max_abs_err > 0.25f) {
			return 0u;
		}
	}

	return 1u;
}
