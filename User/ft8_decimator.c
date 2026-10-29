#include "ft8_shared_ram.h"
#include "ft8_decimator.h"
#include "ft8_waterfall_adapter.h"
#include "arm_math.h"

#if defined(__arm__)
#include "gd32f4xx.h" /* __disable_irq()/__enable_irq() - see ft8_decimator_reset()'s critical section */
#else
/* Los tapones para el simulador: alli todo corre en un hilo y no hay
 * interrupcion contra la que competir, asi que esto NO simula la seccion
 * critica, simplemente no hace falta. (Antes esta nota citaba un
 * test/host/ que ya no existe; los bancos viven en deepsdr-ui/sim.) */
#define __disable_irq() ((void)0)
#define __enable_irq()  ((void)0)
#endif

/*
 * Polyphase rational resampler coefficients: 960-tap Blackman-windowed
 * -sinc lowpass, cutoff 3200Hz referenced to the 96kHz virtual
 * (post-interpolation) rate, gain=L=8 (compensates the interpolation
 * zero-stuffing loss) - see ft8_decimator.h's comment for the
 * frequency response summary. Pre-split into L=8 polyphase phases
 * (120 taps each) at generation time, NOT deinterleaved at runtime -
 * phase r's coefficients are h[r], h[r+8], h[r+16], ..., i.e. every
 * 8th original tap starting at offset r, which is exactly what a
 * direct-form polyphase decomposition needs.
 *
 * *** Y EL GENERADOR EXISTE: deepsdr-ui/tools/gen_ft8_fir.py. Hasta el
 * 28/10/2026 aqui ponia "generated offline (Python)" y ese Python no
 * estaba en ningun sitio, asi que la tabla no se podia ni rehacer ni
 * comprobar. Ahora se comprueba sola:
 *
 *     python3 tools/gen_ft8_fir.py --comprueba ../User/ft8_decimator.c
 *
 * Eran 480 coeficientes con L=4 y corte 1.600 Hz hasta ese dia; el
 * generador reproduce AQUELLA tabla tambien, que es como se comprobo que
 * es el mismo diseño y no uno parecido. ***
 */
#define FT8_RESAMPLER_TAPS_PER_PHASE 120U
static const float32_t s_resampler_phase[FT8_RESAMPLER_L][FT8_RESAMPLER_TAPS_PER_PHASE] = {
{
    /* phase 0 */
    7.703814787e-21f, -1.307488096e-06f, 1.685751417e-06f, 1.146596630e-05f,
    -1.143305724e-05f, -2.968393711e-05f, 3.638036360e-05f, 5.105319720e-05f,
    -8.327158897e-05f, -6.738796465e-05f, 1.575038159e-04f, 6.689082662e-05f,
    -2.617857603e-04f, -3.414770887e-05f, 3.944375144e-04f, -4.931553264e-05f,
    -5.474733788e-04f, 2.037073037e-04f, 7.047208652e-04f, -4.487441095e-04f,
    -8.403174809e-04f, 8.005143925e-04f, 9.179714287e-04f, -1.267573338e-03f,
    -8.913569490e-04f, 1.846565632e-03f, 7.059324276e-04f, -2.517806057e-03f,
    -3.023230846e-04f, 3.241328000e-03f, -3.787851967e-04f, -3.953910385e-03f,
    1.390513784e-03f, 4.567492141e-03f, -2.772905458e-03f, -4.969162546e-03f,
    4.545937778e-03f, 5.022556536e-03f, -6.703178947e-03f, -4.569948867e-03f,
    9.206809884e-03f, 3.433532253e-03f, -1.198457553e-02f, -1.413021439e-03f,
    1.492888323e-02f, -1.725853007e-03f, -1.789763482e-02f, 6.283173368e-03f,
    2.071515622e-02f, -1.270316001e-02f, -2.316873255e-02f, 2.177011436e-02f,
    2.498773969e-02f, -3.515483584e-02f, -2.575906151e-02f, 5.729644450e-02f,
    2.454818636e-02f, -1.049669664e-01f, -1.710037769e-02f, 3.391941882e-01f,
    5.323561635e-01f, 2.926663709e-01f, -4.746314742e-02f, -9.395347932e-02f,
    3.845422144e-02f, 4.941783519e-02f, -3.370431507e-02f, -2.850500964e-02f,
    2.967570131e-02f, 1.600203568e-02f, -2.576360492e-02f, -7.737874501e-03f,
    2.187471058e-02f, 2.097147119e-03f, -1.806157349e-02f, 1.704093930e-03f,
    1.442158073e-02f, -4.126109178e-03f, -1.105685159e-02f, 5.488200000e-03f,
    8.054484685e-03f, -6.041394600e-03f, -5.476315776e-03f, 5.997666904e-03f,
    3.354575939e-03f, -5.540433768e-03f, -1.691834371e-03f, 4.826970512e-03f,
    4.641436325e-04f, -3.987859814e-03f, 3.735883308e-04f, 3.126084474e-03f,
    -8.809584084e-04f, -2.317057614e-03f, 1.125073821e-03f, 1.610097352e-03f,
    -1.174003314e-03f, -1.031349703e-03f, 1.091296337e-03f, 5.878473594e-04f,
    -9.319692779e-04f, -2.722217423e-04f, 7.401192152e-04f, 6.753598599e-05f,
    -5.481029032e-04f, 4.824659619e-05f, 3.770390144e-04f, -9.852813708e-05f,
    -2.382738843e-04f, 1.052911072e-04f, 1.354033020e-04f, -8.731548875e-05f,
    -6.646224700e-05f, 5.930856465e-05f, 2.596803764e-05f, -3.175487585e-05f,
    -6.610251870e-06f, 1.124229244e-05f, 4.998563738e-07f, -1.021006587e-06f
},
{
    /* phase 1 */
    -6.353482263e-09f, -1.549190418e-06f, 3.087430979e-06f, 1.105029056e-05f,
    -1.632396641e-05f, -2.588386048e-05f, 4.600835858e-05f, 3.967980461e-05f,
    -9.739657787e-05f, -4.300010075e-05f, 1.736714113e-04f, 2.325292438e-05f,
    -2.745681943e-04f, 3.498137918e-05f, 3.947438204e-04f, -1.490023285e-04f,
    -5.220790421e-04f, 3.362591242e-04f, 6.362019013e-04f, -6.117758210e-04f,
    -7.075847009e-04f, 9.848038142e-04f, 6.975799153e-04f, -1.454924798e-03f,
    -5.597106962e-04f, 2.007969468e-03f, 2.424178757e-04f, -2.612221870e-03f,
    3.067025034e-04f, 3.215419380e-03f, -1.135338368e-03f, -3.743011615e-03f,
    2.279949846e-03f, 4.097992532e-03f, -3.758852326e-03f, -4.162358103e-03f,
    5.565543852e-03f, 3.799855205e-03f, -7.662921081e-03f, -2.859143109e-03f,
    9.978870342e-03f, 1.175691550e-03f, -1.240334636e-02f, 1.430576430e-03f,
    1.478632811e-02f, -5.167441413e-03f, -1.693462096e-02f, 1.030370042e-02f,
    1.860232632e-02f, -1.725026600e-02f, -1.946150884e-02f, 2.676216367e-02f,
    1.901300910e-02f, -4.050420925e-02f, -1.628939077e-02f, 6.302840910e-02f,
    8.585333453e-03f, -1.121722888e-01f, 1.828937269e-02f, 3.829197112e-01f,
    5.245821918e-01f, 2.444873007e-01f, -7.236524491e-02f, -7.986998167e-02f,
    4.986753008e-02f, 3.983042489e-02f, -3.985804475e-02f, -2.088736436e-02f,
    3.292224885e-02f, 9.729932890e-03f, -2.716838377e-02f, -2.579947301e-03f,
    2.205709058e-02f, -2.069426237e-03f, -1.743982297e-02f, 4.974440164e-03f,
    1.330237285e-02f, -6.594450695e-03f, -9.672647412e-03f, 7.255593063e-03f,
    6.580985570e-03f, -7.215490571e-03f, -4.042008718e-03f, 6.688970715e-03f,
    2.047374297e-03f, -5.857292515e-03f, -5.649500346e-04f, 4.870585596e-03f,
    -4.579988571e-04f, -3.848111260e-03f, 1.089246253e-03f, 2.878612720e-03f,
    -1.404951998e-03f, -2.021780038e-03f, 1.482967139e-03f, 1.311122056e-03f,
    -1.396911845e-03f, -7.580868240e-04f, 1.211585877e-03f, 3.570122196e-04f,
    -9.800057178e-04f, -9.037548042e-05f, 7.421091152e-04f, -6.618618654e-05f,
    -5.249527576e-04f, 1.395214545e-04f, 3.440733364e-04f, -1.555692020e-04f,
    -2.055985118e-04f, 1.370452240e-04f, 1.086834905e-04f, -1.021749173e-04f,
    -4.790177632e-05f, 6.430767077e-05f, 1.531737330e-05f, -3.216944823e-05f,
    -2.087486025e-06f, 1.048556505e-05f, -4.343010890e-07f, -7.320382799e-07f
},
{
    /* phase 2 */
    -4.120863358e-08f, -1.697848091e-06f, 4.644710411e-06f, 9.914856048e-06f,
    -2.101895726e-05f, -2.036757114e-05f, 5.429793270e-05f, 2.549259671e-05f,
    -1.079686983e-04f, -1.501217862e-05f, 1.827948211e-04f, -2.389104157e-05f,
    -2.754725293e-04f, 1.059223852e-04f, 3.771476167e-04f, -2.463698688e-04f,
    -4.720227417e-04f, 4.590412133e-04f, 5.363641312e-04f, -7.534535411e-04f,
    -5.382538582e-04f, 1.131426960e-03f, 4.384143604e-04f, -1.583377555e-03f,
    -1.923499094e-04f, 2.084726390e-03f, -2.460898410e-04f, -2.592912008e-03f,
    9.198004127e-04f, 3.045491451e-03f, -1.862440681e-03f, -3.359722689e-03f,
    3.091832399e-03f, 3.433831001e-03f, -4.603320525e-03f, -3.149870793e-03f,
    6.363705604e-03f, 2.377697309e-03f, -8.306254863e-03f, -9.790337891e-04f,
    1.032700266e-02f, -1.190131554e-03f, -1.228196534e-02f, 4.281816398e-03f,
    1.398379207e-02f, -8.468400474e-03f, -1.519413896e-02f, 1.397608918e-02f,
    1.560287181e-02f, -2.116545995e-02f, -1.477104406e-02f, 3.073117191e-02f,
    1.196805343e-02f, -4.426369068e-02f, -5.640334943e-03f, 6.624362031e-02f,
    -8.885757479e-03f, -1.149134025e-01f, 5.810251696e-02f, 4.227424762e-01f,
    5.092392382e-01f, 1.958236732e-01f, -9.155074813e-02f, -6.350868555e-02f,
    5.847487692e-02f, 2.901942542e-02f, -4.403865057e-02f, -1.266237334e-02f,
    3.463899216e-02f, 3.238537159e-03f, -2.735846987e-02f, 2.543231737e-03f,
    2.128101743e-02f, -6.037281540e-03f, -1.607929754e-02f, 7.947808152e-03f,
    1.163472118e-02f, -8.717170122e-03f, -7.902927973e-03f, 8.666074598e-03f,
    4.857899809e-03f, -8.048538207e-03f, -2.467483704e-03f, 7.073452625e-03f,
    6.838939496e-04f, -5.912377244e-03f, 5.576984638e-04f, 4.702022157e-03f,
    -1.336014398e-03f, -3.545377372e-03f, 1.738133798e-03f, 2.513337170e-03f,
    -1.853116818e-03f, -1.647545321e-03f, 1.765903954e-03f, 9.645341277e-04f,
    -1.552281117e-03f, -4.608496290e-04f, 1.275379243e-03f, 1.186618033e-04f,
    -9.838836155e-04f, 8.869307474e-05f, 7.118726132e-04f, -1.917354073e-04f,
    -4.800087901e-04f, 2.207993452e-04f, 2.976813032e-04f, -2.031250757e-04f,
    -1.656527696e-04f, 1.611139867e-04f, 7.878992101e-05f, -1.116237028e-04f,
    -2.854085852e-05f, 6.607305011e-05f, 4.940511611e-06f, -3.107494372e-05f,
    1.944579847e-06f, 9.318715255e-06f, -1.104222759e-06f, -4.737037857e-07f
},
{
    /* phase 3 */
    -1.243528512e-07f, -1.703351669e-06f, 6.274165814e-06f, 8.013714243e-06f,
    -2.523393092e-05f, -1.324108917e-05f, 6.071512318e-05f, 8.958849114e-06f,
    -1.142312910e-04f, 1.548878329e-05f, 1.840298378e-04f, -7.255066203e-05f,
    -2.638423879e-04f, 1.755122564e-04f, 3.416234811e-04f, -3.368849528e-04f,
    -3.985361820e-04f, 5.660992667e-04f, 4.084999408e-04f, -8.665858420e-04f,
    -3.386236544e-04f, 1.232468409e-03f, 1.507955175e-04f, -1.645223137e-03f,
    1.954091806e-04f, 2.070752692e-03f, -7.384999091e-04f, -2.457356598e-03f,
    1.509705901e-03f, 2.735034162e-03f, -2.526841276e-03f, -2.816422471e-03f,
    3.787913118e-03f, 2.599449099e-03f, -5.265055915e-03f, -1.971472200e-03f,
    6.899331688e-03f, 8.142920071e-04f, -8.596731896e-03f, 9.910712764e-04f,
    1.022528859e-02f, -3.561608207e-03f, -1.161241550e-02f, 7.014425589e-03f,
    1.254012823e-02f, -1.147876713e-02f, -1.273275006e-02f, 1.712812450e-02f,
    1.182445721e-02f, -2.425653732e-02f, -9.273754003e-03f, 3.346670082e-02f,
    4.124157219e-03f, -4.620113638e-02f, 5.776264747e-03f, 6.665827668e-02f,
    -2.721845721e-02f, -1.126425233e-01f, 1.015792827e-01f, 4.576463294e-01f,
    4.867308272e-01f, 1.478228950e-01f, -1.049433914e-01f, -4.568693693e-02f,
    6.409087083e-02f, 1.749608889e-02f, -4.615391283e-02f, -4.202403339e-03f,
    3.480525980e-02f, -3.187118062e-03f, -2.636188458e-02f, 7.412176805e-03f,
    1.960588456e-02f, -9.640390461e-03f, -1.405757177e-02f, 1.050306192e-02f,
    9.504233274e-03f, -1.041078914e-02f, -5.833635068e-03f, 9.667141686e-03f,
    2.965908533e-03f, -8.512681180e-03f, -8.244152478e-04f, 7.141974287e-03f,
    -6.753360847e-04f, -5.709876134e-03f, 1.627518419e-03f, 4.334086277e-03f,
    -2.132968893e-03f, -3.097166940e-03f, 2.293918304e-03f, 2.049401964e-03f,
    -2.208166323e-03f, -1.212906003e-03f, 1.963845854e-03f, 5.868441108e-04f,
    -1.635494794e-03f, -1.533250692e-04f, 1.281791835e-03f, -1.165885207e-04f,
    -9.450047425e-04f, 2.572999139e-04f, 6.519545113e-04f, -3.039705675e-04f,
    -4.161295619e-04f, 2.889678202e-04f, 2.404976041e-04f, -2.395692554e-04f,
    -1.205565284e-04f, 1.768548448e-04f, 4.722742826e-05f, -1.155842248e-04f,
    -9.302517470e-06f, 6.477546727e-05f, -4.706430048e-06f, -2.867907571e-05f,
    5.342652140e-06f, 7.871993985e-06f, -1.518527653e-06f, -2.678794872e-07f
},
{
    /* phase 4 */
    -2.678794872e-07f, -1.518527653e-06f, 7.871993985e-06f, 5.342652140e-06f,
    -2.867907571e-05f, -4.706430048e-06f, 6.477546727e-05f, -9.302517470e-06f,
    -1.155842248e-04f, 4.722742826e-05f, 1.768548448e-04f, -1.205565284e-04f,
    -2.395692554e-04f, 2.404976041e-04f, 2.889678202e-04f, -4.161295619e-04f,
    -3.039705675e-04f, 6.519545113e-04f, 2.572999139e-04f, -9.450047425e-04f,
    -1.165885207e-04f, 1.281791835e-03f, -1.533250692e-04f, -1.635494794e-03f,
    5.868441108e-04f, 1.963845854e-03f, -1.212906003e-03f, -2.208166323e-03f,
    2.049401964e-03f, 2.293918304e-03f, -3.097166940e-03f, -2.132968893e-03f,
    4.334086277e-03f, 1.627518419e-03f, -5.709876134e-03f, -6.753360847e-04f,
    7.141974287e-03f, -8.244152478e-04f, -8.512681180e-03f, 2.965908533e-03f,
    9.667141686e-03f, -5.833635068e-03f, -1.041078914e-02f, 9.504233274e-03f,
    1.050306192e-02f, -1.405757177e-02f, -9.640390461e-03f, 1.960588456e-02f,
    7.412176805e-03f, -2.636188458e-02f, -3.187118062e-03f, 3.480525980e-02f,
    -4.202403339e-03f, -4.615391283e-02f, 1.749608889e-02f, 6.409087083e-02f,
    -4.568693693e-02f, -1.049433914e-01f, 1.478228950e-01f, 4.867308272e-01f,
    4.576463294e-01f, 1.015792827e-01f, -1.126425233e-01f, -2.721845721e-02f,
    6.665827668e-02f, 5.776264747e-03f, -4.620113638e-02f, 4.124157219e-03f,
    3.346670082e-02f, -9.273754003e-03f, -2.425653732e-02f, 1.182445721e-02f,
    1.712812450e-02f, -1.273275006e-02f, -1.147876713e-02f, 1.254012823e-02f,
    7.014425589e-03f, -1.161241550e-02f, -3.561608207e-03f, 1.022528859e-02f,
    9.910712764e-04f, -8.596731896e-03f, 8.142920071e-04f, 6.899331688e-03f,
    -1.971472200e-03f, -5.265055915e-03f, 2.599449099e-03f, 3.787913118e-03f,
    -2.816422471e-03f, -2.526841276e-03f, 2.735034162e-03f, 1.509705901e-03f,
    -2.457356598e-03f, -7.384999091e-04f, 2.070752692e-03f, 1.954091806e-04f,
    -1.645223137e-03f, 1.507955175e-04f, 1.232468409e-03f, -3.386236544e-04f,
    -8.665858420e-04f, 4.084999408e-04f, 5.660992667e-04f, -3.985361820e-04f,
    -3.368849528e-04f, 3.416234811e-04f, 1.755122564e-04f, -2.638423879e-04f,
    -7.255066203e-05f, 1.840298378e-04f, 1.548878329e-05f, -1.142312910e-04f,
    8.958849114e-06f, 6.071512318e-05f, -1.324108917e-05f, -2.523393092e-05f,
    8.013714243e-06f, 6.274165814e-06f, -1.703351669e-06f, -1.243528512e-07f
},
{
    /* phase 5 */
    -4.737037857e-07f, -1.104222759e-06f, 9.318715255e-06f, 1.944579847e-06f,
    -3.107494372e-05f, 4.940511611e-06f, 6.607305011e-05f, -2.854085852e-05f,
    -1.116237028e-04f, 7.878992101e-05f, 1.611139867e-04f, -1.656527696e-04f,
    -2.031250757e-04f, 2.976813032e-04f, 2.207993452e-04f, -4.800087901e-04f,
    -1.917354073e-04f, 7.118726132e-04f, 8.869307474e-05f, -9.838836155e-04f,
    1.186618033e-04f, 1.275379243e-03f, -4.608496290e-04f, -1.552281117e-03f,
    9.645341277e-04f, 1.765903954e-03f, -1.647545321e-03f, -1.853116818e-03f,
    2.513337170e-03f, 1.738133798e-03f, -3.545377372e-03f, -1.336014398e-03f,
    4.702022157e-03f, 5.576984638e-04f, -5.912377244e-03f, 6.838939496e-04f,
    7.073452625e-03f, -2.467483704e-03f, -8.048538207e-03f, 4.857899809e-03f,
    8.666074598e-03f, -7.902927973e-03f, -8.717170122e-03f, 1.163472118e-02f,
    7.947808152e-03f, -1.607929754e-02f, -6.037281540e-03f, 2.128101743e-02f,
    2.543231737e-03f, -2.735846987e-02f, 3.238537159e-03f, 3.463899216e-02f,
    -1.266237334e-02f, -4.403865057e-02f, 2.901942542e-02f, 5.847487692e-02f,
    -6.350868555e-02f, -9.155074813e-02f, 1.958236732e-01f, 5.092392382e-01f,
    4.227424762e-01f, 5.810251696e-02f, -1.149134025e-01f, -8.885757479e-03f,
    6.624362031e-02f, -5.640334943e-03f, -4.426369068e-02f, 1.196805343e-02f,
    3.073117191e-02f, -1.477104406e-02f, -2.116545995e-02f, 1.560287181e-02f,
    1.397608918e-02f, -1.519413896e-02f, -8.468400474e-03f, 1.398379207e-02f,
    4.281816398e-03f, -1.228196534e-02f, -1.190131554e-03f, 1.032700266e-02f,
    -9.790337891e-04f, -8.306254863e-03f, 2.377697309e-03f, 6.363705604e-03f,
    -3.149870793e-03f, -4.603320525e-03f, 3.433831001e-03f, 3.091832399e-03f,
    -3.359722689e-03f, -1.862440681e-03f, 3.045491451e-03f, 9.198004127e-04f,
    -2.592912008e-03f, -2.460898410e-04f, 2.084726390e-03f, -1.923499094e-04f,
    -1.583377555e-03f, 4.384143604e-04f, 1.131426960e-03f, -5.382538582e-04f,
    -7.534535411e-04f, 5.363641312e-04f, 4.590412133e-04f, -4.720227417e-04f,
    -2.463698688e-04f, 3.771476167e-04f, 1.059223852e-04f, -2.754725293e-04f,
    -2.389104157e-05f, 1.827948211e-04f, -1.501217862e-05f, -1.079686983e-04f,
    2.549259671e-05f, 5.429793270e-05f, -2.036757114e-05f, -2.101895726e-05f,
    9.914856048e-06f, 4.644710411e-06f, -1.697848091e-06f, -4.120863358e-08f
},
{
    /* phase 6 */
    -7.320382799e-07f, -4.343010890e-07f, 1.048556505e-05f, -2.087486025e-06f,
    -3.216944823e-05f, 1.531737330e-05f, 6.430767077e-05f, -4.790177632e-05f,
    -1.021749173e-04f, 1.086834905e-04f, 1.370452240e-04f, -2.055985118e-04f,
    -1.555692020e-04f, 3.440733364e-04f, 1.395214545e-04f, -5.249527576e-04f,
    -6.618618654e-05f, 7.421091152e-04f, -9.037548042e-05f, -9.800057178e-04f,
    3.570122196e-04f, 1.211585877e-03f, -7.580868240e-04f, -1.396911845e-03f,
    1.311122056e-03f, 1.482967139e-03f, -2.021780038e-03f, -1.404951998e-03f,
    2.878612720e-03f, 1.089246253e-03f, -3.848111260e-03f, -4.579988571e-04f,
    4.870585596e-03f, -5.649500346e-04f, -5.857292515e-03f, 2.047374297e-03f,
    6.688970715e-03f, -4.042008718e-03f, -7.215490571e-03f, 6.580985570e-03f,
    7.255593063e-03f, -9.672647412e-03f, -6.594450695e-03f, 1.330237285e-02f,
    4.974440164e-03f, -1.743982297e-02f, -2.069426237e-03f, 2.205709058e-02f,
    -2.579947301e-03f, -2.716838377e-02f, 9.729932890e-03f, 3.292224885e-02f,
    -2.088736436e-02f, -3.985804475e-02f, 3.983042489e-02f, 4.986753008e-02f,
    -7.986998167e-02f, -7.236524491e-02f, 2.444873007e-01f, 5.245821918e-01f,
    3.829197112e-01f, 1.828937269e-02f, -1.121722888e-01f, 8.585333453e-03f,
    6.302840910e-02f, -1.628939077e-02f, -4.050420925e-02f, 1.901300910e-02f,
    2.676216367e-02f, -1.946150884e-02f, -1.725026600e-02f, 1.860232632e-02f,
    1.030370042e-02f, -1.693462096e-02f, -5.167441413e-03f, 1.478632811e-02f,
    1.430576430e-03f, -1.240334636e-02f, 1.175691550e-03f, 9.978870342e-03f,
    -2.859143109e-03f, -7.662921081e-03f, 3.799855205e-03f, 5.565543852e-03f,
    -4.162358103e-03f, -3.758852326e-03f, 4.097992532e-03f, 2.279949846e-03f,
    -3.743011615e-03f, -1.135338368e-03f, 3.215419380e-03f, 3.067025034e-04f,
    -2.612221870e-03f, 2.424178757e-04f, 2.007969468e-03f, -5.597106962e-04f,
    -1.454924798e-03f, 6.975799153e-04f, 9.848038142e-04f, -7.075847009e-04f,
    -6.117758210e-04f, 6.362019013e-04f, 3.362591242e-04f, -5.220790421e-04f,
    -1.490023285e-04f, 3.947438204e-04f, 3.498137918e-05f, -2.745681943e-04f,
    2.325292438e-05f, 1.736714113e-04f, -4.300010075e-05f, -9.739657787e-05f,
    3.967980461e-05f, 4.600835858e-05f, -2.588386048e-05f, -1.632396641e-05f,
    1.105029056e-05f, 3.087430979e-06f, -1.549190418e-06f, -6.353482263e-09f
},
{
    /* phase 7 */
    -1.021006587e-06f, 4.998563738e-07f, 1.124229244e-05f, -6.610251870e-06f,
    -3.175487585e-05f, 2.596803764e-05f, 5.930856465e-05f, -6.646224700e-05f,
    -8.731548875e-05f, 1.354033020e-04f, 1.052911072e-04f, -2.382738843e-04f,
    -9.852813708e-05f, 3.770390144e-04f, 4.824659619e-05f, -5.481029032e-04f,
    6.753598599e-05f, 7.401192152e-04f, -2.722217423e-04f, -9.319692779e-04f,
    5.878473594e-04f, 1.091296337e-03f, -1.031349703e-03f, -1.174003314e-03f,
    1.610097352e-03f, 1.125073821e-03f, -2.317057614e-03f, -8.809584084e-04f,
    3.126084474e-03f, 3.735883308e-04f, -3.987859814e-03f, 4.641436325e-04f,
    4.826970512e-03f, -1.691834371e-03f, -5.540433768e-03f, 3.354575939e-03f,
    5.997666904e-03f, -5.476315776e-03f, -6.041394600e-03f, 8.054484685e-03f,
    5.488200000e-03f, -1.105685159e-02f, -4.126109178e-03f, 1.442158073e-02f,
    1.704093930e-03f, -1.806157349e-02f, 2.097147119e-03f, 2.187471058e-02f,
    -7.737874501e-03f, -2.576360492e-02f, 1.600203568e-02f, 2.967570131e-02f,
    -2.850500964e-02f, -3.370431507e-02f, 4.941783519e-02f, 3.845422144e-02f,
    -9.395347932e-02f, -4.746314742e-02f, 2.926663709e-01f, 5.323561635e-01f,
    3.391941882e-01f, -1.710037769e-02f, -1.049669664e-01f, 2.454818636e-02f,
    5.729644450e-02f, -2.575906151e-02f, -3.515483584e-02f, 2.498773969e-02f,
    2.177011436e-02f, -2.316873255e-02f, -1.270316001e-02f, 2.071515622e-02f,
    6.283173368e-03f, -1.789763482e-02f, -1.725853007e-03f, 1.492888323e-02f,
    -1.413021439e-03f, -1.198457553e-02f, 3.433532253e-03f, 9.206809884e-03f,
    -4.569948867e-03f, -6.703178947e-03f, 5.022556536e-03f, 4.545937778e-03f,
    -4.969162546e-03f, -2.772905458e-03f, 4.567492141e-03f, 1.390513784e-03f,
    -3.953910385e-03f, -3.787851967e-04f, 3.241328000e-03f, -3.023230846e-04f,
    -2.517806057e-03f, 7.059324276e-04f, 1.846565632e-03f, -8.913569490e-04f,
    -1.267573338e-03f, 9.179714287e-04f, 8.005143925e-04f, -8.403174809e-04f,
    -4.487441095e-04f, 7.047208652e-04f, 2.037073037e-04f, -5.474733788e-04f,
    -4.931553264e-05f, 3.944375144e-04f, -3.414770887e-05f, -2.617857603e-04f,
    6.689082662e-05f, 1.575038159e-04f, -6.738796465e-05f, -8.327158897e-05f,
    5.105319720e-05f, 3.638036360e-05f, -2.968393711e-05f, -1.143305724e-05f,
    1.146596630e-05f, 1.685751417e-06f, -1.307488096e-06f, 7.703814787e-21f
}
};

/*
 * EL HISTORIAL, QUE AHORA ES UN ANILLO - 19/10/2026.
 *
 * Antes esto era un registro de desplazamiento de verdad: por CADA muestra
 * de entrada se corrian 119 floats una posicion. El compilador lo convertia
 * en un memmove de 476 bytes, unos 480 ciclos, y entra una muestra cada
 * 1/12000 s. Medido: 15.400 ciclos por bloque de audio, DOS VECES Y MEDIA
 * lo que cuesta el filtro polifasico al que alimenta.
 *
 * Ahora la muestra nueva se escribe en su sitio y ya esta; lo que gira es el
 * indice. s_hist_w apunta a donde ira la PROXIMA, que es tambien la posicion
 * de la MAS VIEJA de las que hay. El filtro lo recorre en dos tramos para no
 * pagar un modulo por toma.
 *
 * El doble buffer -escribir cada muestra dos veces para que el tramo salga
 * contiguo- habria sido mas simple, pero son 480 bytes y de SRAM quedan
 * 1.436 en toda la radio.
 */
static float32_t s_history[FT8_RESAMPLER_TAPS_PER_PHASE];
static uint16_t  s_hist_w;   /* la proxima a escribir, y la mas vieja de las que hay */
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
/* Caida exponencial lenta. LA CONSTANTE DE TIEMPO SON 1,67 s, no "unos
 * cientos de milisegundos" como decia aqui: a 12 kHz, 1/0,00005 son 20.000
 * muestras. Corregido el 23/10/2026 - el numero que habia hacia pensar que
 * el AGC se recupera dentro de una ranura de 15 s cuatro o cinco veces, y
 * lo que hace es recuperarse una. (Que la cifra fuera 1,67 s y no 0,3 no
 * cambia nada medible: ver el banco sim/ft8_banda.c, donde arrancar cada
 * ranura con el AGC a tope no cuesta ni una decodificacion.) */
#define FT8_AGC_RELEASE 0.99995f
static float32_t s_agc_peak;

/*
 * DOUBLE-BUFFERED handoff to the main loop (09/2026) - CRITICAL FIX.
 * ft8_decimator_feed_sample() runs inside the audio DMA ISR (see
 * demod_am.c's own "EXECUTION CONTEXT" comment on that hook) - this
 * used to call ft8_waterfall_feed_subblock() DIRECTLY from there once
 * a subblock filled, which runs a full 1024-point FFT (ft8_fft.c)
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
/* En la memoria compartida desde el 10/10/2026: 2.048 bytes que estaban
 * fuera de la union y se sumaban a la cascada. Solo vive con FT8 puesto, que
 * es cuando la cascada no se dibuja. Ver ft8_shared_ram.h. */
#define s_out_block (g_ft8_shared_ram.ft8.deci_out)
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
static volatile uint16_t s_perdidos;         /* ver produce_output_sample() */
static volatile bool s_out_block_ready[2];   /* true = full, waiting for ft8_decimator_poll() to consume it */

void ft8_decimator_reset_perdidos(void) { s_perdidos = 0U; }

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
     * sometimes, matching the reported freeze happening often but not
     * always.
     */
    __disable_irq();

    for (i = 0; i < FT8_RESAMPLER_TAPS_PER_PHASE; i++)
    {
        s_history[i] = 0.0f;
    }
    s_hist_w = 0U;
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
    /* s_perdidos NO se borra: cuenta desde que se encendio FT8, que es
     * justo el periodo sobre el que hay que juzgarlo. Se borra en
     * ft8_decimator_reset_perdidos(), que llama ft8_modo_start(). */

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
    /* El anillo se recorre en dos tramos: de s_hist_w al final y del
     * principio a s_hist_w. `p` es la posicion LOGICA -0 la mas vieja,
     * TAPS_PER_PHASE-1 la mas nueva-, que es con la que emparejan los
     * coeficientes igual que antes. Las mismas sumas en el mismo orden que
     * con el registro de desplazamiento: bit a bit lo mismo. */
    {
        uint16_t pp, p = 0U;
        for (pp = s_hist_w; pp < FT8_RESAMPLER_TAPS_PER_PHASE; pp++, p++) {
            acc += s_resampler_phase[phase][FT8_RESAMPLER_TAPS_PER_PHASE - 1U - p]
                   * s_history[pp];
        }
        for (pp = 0U; p < FT8_RESAMPLER_TAPS_PER_PHASE; pp++, p++) {
            acc += s_resampler_phase[phase][FT8_RESAMPLER_TAPS_PER_PHASE - 1U - p]
                   * s_history[pp];
        }
        (void)j;
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
        /*
         * ¿SE HA PERDIDO UN SUBBLOQUE? - 25/09/2026.
         *
         * Aqui solo hay DOS buffers, o sea 320 ms de colchon. Si el bucle
         * principal tarda mas que eso en pasar por ft8_decimator_poll(), el
         * que estaba lleno y sin vaciar se pisa y ese trozo de audio no llega
         * nunca al analisis. El comentario de abajo ya lo decia -"safety
         * fallback, not the intended steady state"- pero lo hacia EN
         * SILENCIO, y eso es lo que hay que cambiar: un hueco en la captura
         * no se ve en pantalla, se ve como que FT8 decodifica peor y nadie
         * sabe por que.
         *
         * Con la ventana en 1.600 Hz la decodificacion tarda bastante mas, y
         * mientras corre nadie vacia esto. Este contador es el que dice si esa
         * sospecha es cierta o es cuento.
         */
        if (s_out_block_ready[s_out_block_writing]) { s_perdidos++; }

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

    /* La muestra nueva ocupa el sitio de la mas vieja y el indice avanza.
     * Ver el comentario de s_history: antes esto corria 119 floats por cada
     * muestra que entraba. */
    s_history[s_hist_w] = sample_f;
    s_hist_w++;
    if (s_hist_w >= FT8_RESAMPLER_TAPS_PER_PHASE) { s_hist_w = 0U; }
    (void)i;

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
    /*
     * ======================================================================
     * Y EL MAS VIEJO PRIMERO, QUE NO ES EL NUMERO 0 - 23/10/2026
     * ======================================================================
     *
     * Aqui se recorrian los dos buffers por indice: primero el 0 y despues
     * el 1. Con uno solo lleno da igual, que es lo que pasa casi siempre.
     *
     * Con LOS DOS llenos no da igual, y los dos llenos es exactamente el
     * caso que este bucle dice cubrir ("so a single main-loop iteration
     * that's briefly fallen behind can catch back up"). La interrupcion los
     * llena alternando, asi que si el que se lleno antes fue el 1, este
     * orden metia en mag[] el subbloque NUEVO delante del VIEJO: dos
     * simbolos de la emision cambiados de sitio, que es ruido para el
     * decodificador justo en la ranura en la que el bucle ya iba mal.
     *
     * Cual es el viejo se sabe sin guardar nada: la interrupcion marca uno
     * como lleno y SE CAMBIA al otro, asi que el que esta escribiendo ahora
     * -s_out_block_writing- es el que lleva mas tiempo lleno de los dos. Se
     * empieza por ese.
     *
     * Se lee UNA vez: si la interrupcion cambia a media vuelta es que ya se
     * ha perdido un subbloque entero, y eso lo cuenta s_perdidos.
     */
    uint8_t viejo = s_out_block_writing;
    uint8_t i;
    for (i = 0; i < 2U; i++)
    {
        uint8_t k = (uint8_t)((viejo + i) & 1U);

        if (s_out_block_ready[k])
        {
            /*
             * CONSUMIR Y DESPUES BAJAR LA BANDERA - 09/10/2026.
             *
             * Estaba al reves, y eso rompia justo el diagnostico que hay
             * para esto: la interrupcion usa s_out_block_ready[] como su
             * unica prueba de "¿lo sigue usando el bucle principal?", asi
             * que bajandola ANTES de la FFT el bloque se veia libre, se
             * sobreescribia mientras se leia y ademas NO se contaba en
             * s_perdidos. El contador que existe para decir "el bucle se ha
             * quedado atras" marcaba cero exactamente cuando se quedaba
             * atras.
             */
            ft8_waterfall_feed_subblock(s_out_block[k]);
            s_out_block_ready[k] = false;
        }
    }
}

uint16_t ft8_decimator_perdidos(void) { return s_perdidos; }

uint32_t ft8_decimator_get_raw_sample_count(void)
{
    return s_raw_sample_count;
}
