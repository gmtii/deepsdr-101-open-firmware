#include "demod_am.h"
#include "agc.h"
#include "nco.h"
#include "rds.h"
#include "nb.h"
#include "dcf77.h"
#include "analizador.h"
#include "sam.h" /* DEMOD_MODE_SAM - 21/08/2026 */
#include "rapido.h" /* rapido_atan2(): ver fm_discriminate() */
#include "bip.h"    /* bip_mete(): el pitido de los avisos, al final del bloque */
#include "config.h"
#include "sdr_rx.h"
#include "gd32_i2s.h"
#include "gd32f4xx.h"
#include "debug_uart.h"
#include "arm_math.h"
#include "nr_ss.h" /* Spectral Subtraction NR, AM/USB/LSB only - see
                    * this file's NR INTEGRATION comment above the
                    * decimate/interpolate instances it reuses. */
#include "hfdl_modo.h" /* HFDL se engancha a la I/Q diezmada - ver el
                        * comentario en el paso 1 del proceso. */
#include "anotch.h" /* notch automatico, etapa 35 - va justo detras de la
                     * reduccion de ruido y a la misma tasa. */
#include "audiofil.h" /* filtro de audio de dos cortes, etapa 34 */
#include "rtty.h" /* RTTY decoder, USB/LSB only - see this file's
                    * RTTY INTEGRATION comment. */
#include "cw.h"   /* decodificador de CW, USB/LSB, se engancha en el
                     * mismo sitio y sobre el mismo audio que el RTTY */
#include "navtex.h"
#include "psk31.h"     /* decodificador de NAVTEX - ver donde se llama abajo */
#include "wefax.h"      /* y el de WEFAX, en el mismo sitio */
#include "sstv.h"       /* y el de SSTV */
#include "hell.h"       /* y el de Feld-Hell */
#include "ft8_modo.h"
#include "wspr_modo.h"
#include "ais_modo.h"
#include "ale_modo.h"  /* y el de FT8 - etapa 30 */
#include "jtty_modo.h"
#include "stanag_modo.h" /* JTTY: banda lateral a 12 kHz, como FT8 y WSPR */
#include "ident.h"        /* y el IDENT, que come de ese mismo sitio */
#include "ax25.h"       /* y el de APRS, que se engancha en la rama de FM */
#include "dsc.h"        /* y el DSC, que come del camino de banda lateral */
#include "rtty_scope.h" /* dedicated audio-domain tuning scope for RTTY,
                           * see this file's RTTY INTEGRATION comment
                           * and rtty_scope.h's own "why a separate FFT"
                           * story. */
#include <math.h> /* atan2f() for the WFM discriminator - see demod_am.h's
                    * WFM note on why this uses libm instead of
                    * arm_atan2_f32() (not present in this project's
                    * pruned CMSIS-DSP tree). */

#define SPK_EN_PORT GPIOB
#define SPK_EN_PIN  GPIO_PIN_7

/*
 * EL FILTRO DE CANAL Y EL PASO BAJO DE AUDIO. Los dos son Butterworth
 * paso bajo de cuarto orden -dos biquads DF1 de CMSIS en cascada cada
 * uno-, diseñados aparte por transformada bilineal con precompensacion a
 * fs = 96 kHz (eran 48 kHz, y 192 antes de eso; el por que esta en el
 * comentario de SDR_RX_BLOCK_SAMPLES en sdr_rx.h). Se regeneraron con
 * exactamente el mismo metodo de "cada etapa de biquad normalizada por su
 * cuenta a ganancia unidad en continua" que ya usaban los valores de
 * antes, y eso se comprobo reproduciendo primero bit a bit los
 * coeficientes de 48 kHz con ese metodo -que a su vez ya estaban
 * comprobados contra los originales de 192- antes de fiarse de el para
 * estos:
 *
 *   CHF_COEFFS:  -3 dB a unos 4 kHz. El filtro de canal, aplicado a I y
 *                a Q.
 *   ALPF_COEFFS: -3 dB a unos 6 kHz. El paso bajo de audio, aplicado
 *                despues del bloqueador de continua; sustituye a la
 *                cascada vieja de dos secciones de un polo -unos 12 dB
 *                por octava- por una caida mas empinada de unos 24, con
 *                el mismo codo.
 *
 * El orden de los coeficientes es el convenio DF1 de CMSIS:
 * {b0,b1,b2,a1,a2}, con
 * y[n] = b0*x[n] + b1*x[n-1] + b2*x[n-2] + a1*y[n-1] + a2*y[n-2]
 * -a1 y a2 vienen ya con el signo que CMSIS espera, o sea cambiado
 * respecto a la forma de libro "-a1*y[n-1] - a2*y[n-2]"-. Los dos
 * filtros se comprobaron con numeros -respuesta en frecuencia en los
 * codos de diseño y bastante mas alla- antes de meterlos aqui. Para
 * retocar cualquiera de los dos codos se regenera el array de diez
 * valores entero: no se editan coeficientes sueltos a mano, porque los
 * cinco de cada etapa van juntos.
 *
 * ALPF_COEFFS es SOLO DEL NFM desde el 02/08/2026: AM, USB y LSB se
 * pasaron a su propio selector de tres posiciones -ALPF_4K0/2K3/1K8_COEFFS,
 * mas abajo- en vez de compartir este, asi que retocar ESTE array solo
 * afecta al NFM. Por que se mantienen separados en vez de añadir este
 * mismo codo de 6 kHz como cuarta opcion elegible esta en el comentario
 * de AUDIO_BW_4K0 en demod_am.h.
 *
 * *** 01/09/2026: se añadieron los equivalentes de 48 kHz
 * -CHF_48K_COEFFS y ALPF_48K_COEFFS- al lado de estos de 96, pedido por
 * el usuario; la historia entera de "la opcion de 48 kHz para mejor
 * relacion señal a ruido y mas holgura de procesador" esta en el
 * comentario de demod_am_set_active_rate() en demod_am.h. Esos valores de
 * 48 kHz NO son una deduccion nueva: se recuperaron literalmente del
 * codigo real de la epoca de 48 kHz de este mismo proyecto -un ZIP que el
 * usuario todavia guardaba de antes de la migracion a 96- y despues se
 * volvieron a comprobar por separado reproduciendolos de cero con scipy
 * -el mismo metodo de unidad por etapa, fc = 4000 y 6000, fs = 48000-
 * antes de fiarse de ellos. Las dos fuentes coinciden bit a bit.
 * demod_am_set_active_rate() elige que par de instancias de CMSIS corre
 * de verdad en cada bloque; el mecanismo entero esta en esa funcion -es
 * una reinicializacion de CMSIS de verdad, no un cambio de puntero de
 * coeficientes, porque los buffers de estado del diezmador, el
 * interpolador y el Hilbert de mas abajo tambien tienen que enterarse del
 * cambio de tamaño; ver el comentario de DEC_BLOCK_SAMPLES_MAX-.
 */
#define CHF_STAGES 2U
static const float32_t CHF_96K_COEFFS[CHF_STAGES * 5U] = {
    0.0137494f, 0.0274987f, 0.0137494f,  1.5590543f, -0.6140518f,
    0.0155017f, 0.0310034f, 0.0155017f,  1.7577536f, -0.8197604f
};
static const float32_t CHF_48K_COEFFS[CHF_STAGES * 5U] = {
    0.0458208332f, 0.0916416663f, 0.0458208332f,  1.1847620863f, -0.3680454189f,
    0.0562284500f, 0.1124569000f, 0.0562284500f,  1.4538656576f, -0.6787794575f
};

#define ALPF_STAGES 2U
static const float32_t ALPF_96K_COEFFS[ALPF_STAGES * 5U] = {
    0.0281188f, 0.0562375f, 0.0281188f,  1.3651172f, -0.4775923f,
    0.0331984f, 0.0663969f, 0.0331984f,  1.6117271f, -0.7445208f
};
static const float32_t ALPF_48K_COEFFS[ALPF_STAGES * 5U] = {
    0.0885793562f, 0.1771587125f, 0.0885793562f,  0.8553979328f, -0.2097153578f,
    0.1152580152f, 0.2305160305f, 0.1152580152f,  1.1130298542f, -0.5740619151f
};

/*
 * EL JUEGO DE FILTROS DE AUDIO DE AM Y BANDA LATERAL: tres anchos
 * elegibles, añadidos el 02/08/2026 -primero el de 2,3 kHz y ese mismo
 * dia ampliado a tres, pedido por el usuario; ver el comentario de
 * AUDIO_BW_4K0 en demod_am.h-. El metodo de diseño es el mismo en los
 * tres: Butterworth de cuarto orden, dos biquads DF1 de CMSIS en
 * cascada, transformada bilineal con scipy.signal.butter a fs = 96 kHz
 * (eran 48, y 192 antes de eso, ver el comentario de
 * SDR_RX_BLOCK_SAMPLES en sdr_rx.h), y el mismo b0 para las dos etapas
 * -la media geometrica de lo que seria el b0 de ganancia unidad en
 * continua de cada etapa POR SEPARADO-, que es el estilo que ya tenia
 * este trio, al contrario que el reparto independiente por etapa de
 * CHF_COEFFS y ALPF_COEFFS de mas arriba. Dos etapas en cascada dan la
 * misma respuesta total de las dos maneras: esto es solo coherencia
 * estetica y de precision numerica, y se reprodujo exactamente contra
 * los valores de 48 kHz -que a su vez ya estaban comprobados contra los
 * originales de 192- antes de fiarse de el para estos.
 *
 * Solo paso bajo, sin paso alto ni corte de graves: la misma forma que
 * ALPF_COEFFS, no un filtro de paso de banda de verdad para banda
 * lateral. Un corte de graves por los 300 Hz se podria añadir igual mas
 * adelante si se quiere.
 *
 * Que ALPF_4K0_COEFFS caiga en el mismo codo de -3 dB que CHF_COEFFS de
 * arriba es pura casualidad -CHF_COEFFS corre sobre el I/Q complejo de
 * radiofrecuencia antes de demodular y este sobre el audio real despues:
 * mismo codo, etapa y proposito completamente distintos-, no un diseño
 * compartido ni una errata. Comprobado con numeros, todo a fs = 96 kHz:
 *   ALPF_4K0: plano hasta 1,5 kHz, -3,02 dB a 4,0 kHz, -18,86 dB a 6,8
 *             kHz, -34,37 dB a 10,4 kHz
 *   ALPF_2K3: plano hasta 1 kHz, -3,00 dB a 2,3 kHz, -19,43 dB a 4,0
 *             kHz, -33,70 dB a 6,0 kHz, -52,28 dB a 10 kHz
 *   ALPF_1K8: plano hasta 1 kHz, -3,01 dB a 1,8 kHz, -18,57 dB a 3,06
 *             kHz, -33,43 dB a 4,68 kHz, -60,82 dB a 10 kHz
 * (Que los numeros de la banda eliminada salgan un poco MENOS empinados
 * que en el diseño de 48 kHz a los mismos desplazamientos relativos es lo
 * esperado y no hace daño: es la MISMA respuesta analogica de
 * Butterworth, solo que menos deformada por la transformada bilineal
 * ahora que el codo queda mas cerca de continua respecto al Nyquist
 * nuevo, mas alto. La precision en la banda de paso y en el codo sigue
 * siendo exacta de las dos maneras.)
 */
#define ALPF_4K0_STAGES 2U
static const float32_t ALPF_4K0_96K_COEFFS[ALPF_4K0_STAGES * 5U] = {
    0.0145993f, 0.0291985f, 0.0145993f,  1.5590543f, -0.6140518f,
    0.0145993f, 0.0291985f, 0.0145993f,  1.7577536f, -0.8197604f
};
static const float32_t ALPF_4K0_48K_COEFFS[ALPF_4K0_STAGES * 5U] = {
    0.0507585897f, 0.1015171793f, 0.0507585897f,  1.1847620863f, -0.3680454189f,
    0.0507585897f, 0.1015171793f, 0.0507585897f,  1.4538656576f, -0.6787794575f
};

#define ALPF_2K3_STAGES 2U
static const float32_t ALPF_2K3_96K_COEFFS[ALPF_2K3_STAGES * 5U] = {
    0.0051535f, 0.0103069f, 0.0051535f,  1.7367529f, -0.7566184f,
    0.0051535f, 0.0103069f, 0.0051535f,  1.8700597f, -0.8914501f
};
static const float32_t ALPF_2K3_48K_COEFFS[ALPF_2K3_STAGES * 5U] = {
    0.0188829178f, 0.0377658355f, 0.0188829178f,  1.4992832253f, -0.5698972927f,
    0.0188829178f, 0.0377658355f, 0.0188829178f,  1.7153763282f, -0.7961680677f
};

#define ALPF_1K8_STAGES 2U
static const float32_t ALPF_1K8_96K_COEFFS[ALPF_1K8_STAGES * 5U] = {
    0.0032200f, 0.0064401f, 0.0032200f,  1.7915877f, -0.8040928f,
    0.0032200f, 0.0064401f, 0.0032200f,  1.9006466f, -0.9139129f
};
static const float32_t ALPF_1K8_48K_COEFFS[ALPF_1K8_STAGES * 5U] = {
    0.0120050083f, 0.0240100166f, 0.0120050083f,  1.5997196711f, -0.6451760153f,
    0.0120050083f, 0.0240100166f, 0.0120050083f,  1.7852530573f, -0.8359813686f
};

/*
 * EL PASO BAJO DE AUDIO DEL WFM: Butterworth de cuarto orden -dos
 * biquads DF1 de CMSIS en cascada, la misma forma y el mismo convenio
 * que CHF_COEFFS y ALPF_COEFFS de mas arriba-, -3 dB a unos 15 kHz con
 * fs = 192 kHz. Diseñado aparte con scipy.signal.butter -bilineal, el
 * metodo de diseño de filtros que ya usa el proyecto- y comprobado con
 * numeros en los puntos que importan para la FM comercial:
 *
 *   de 100 Hz a 10 kHz: practicamente plano, de 0 a -0,15 dB. La banda
 *                       de audio comercial pasa casi sin tocar.
 *   15 kHz:             -3,01 dB, el codo de diseño.
 *   19 kHz:             -9,21 dB, que hunde el tono piloto de estereo.
 *   23 kHz:             -15,95 dB, la falda de arriba del piloto.
 *   38 kHz:             -36,52 dB, la subportadora de L-R, bien dentro
 *                       de la banda eliminada.
 *
 * *** 05/08/2026: vuelta a 192 kHz ***. Se rediseño brevemente para 48
 * kHz el 04/08/2026, cuando el proyecto paso AM, banda lateral y NFM a
 * 48, pero el WFM solo se quedo elegible a esa velocidad
 * -fundamentalmente demasiado estrecha, ver la nota del WFM en
 * demod_am.h- durante un dia: el 05/08/2026 el WFM paso a tener su
 * propio camino de proceso, solo a 192 kHz (demod_wfm_process_raw()),
 * asi que este filtro -que usa SOLO esa funcion- vuelve a su diseño
 * original, igual que estaba antes del 04/08/2026.
 *
 * Esta es una instancia y un estado APARTE de ALPF_COEFFS -unos 6 kHz,
 * el de AM y banda lateral-. El codo mas estrecho de voz de AM y banda
 * lateral se llevaria por delante casi todo lo que hace que merezca la
 * pena escuchar FM comercial, asi que el WFM tiene su propio filtro mas
 * ancho en vez de retocar el compartido. Por que el objetivo son 15 kHz
 * y no 53 o mas esta en la nota del WFM de demod_am.h: recepcion solo en
 * mono, sin decodificar la subportadora de estereo.
 */
/*
 * EL FILTRO DE CANAL DEL WFM, ANTES DEL DISCRIMINADOR (IFBW), añadido el
 * 01/09/2026 y pedido por el usuario. Al contrario que los
 * WFM_ALPF_WIDE/NORM/NARROW_COEFFS de arriba, que son de audio y dan
 * forma al sonido YA DEMODULADO, detras del discriminador, este filtra
 * el I/Q COMPLEJO EN CRUDO, antes de que fm_discriminate() lo vea
 * siquiera: ver el paso "0a. Desintercalar" de demod_wfm_process_raw(),
 * que es justo donde se aplica. La nota del WFM en demod_am.h explica
 * por que si no corre el discriminador sobre I/Q sin filtrar en
 * absoluto, con los +/-96 kHz enteros de Nyquist; eso sigue siendo la
 * opcion ANCHA y por defecto de aqui, que no se aplica a ningun canal
 * -se salta entera, es una no-operacion de verdad y no un filtro muy
 * ancho, asi que quien no toque este mando se queda con el mismo camino
 * de señal byte a byte que antes-. La opcion ESTRECHA aplica este MISMO
 * filtro igual a I y a Q -un paso bajo "complejo" en el sentido de que
 * trata igual la parte real y la imaginaria de la señal analitica, no de
 * que sus coeficientes sean complejos- con un codo de -3 dB de verdad
 * ancho, 80 kHz: ni de lejos los 4 kHz de CHF_COEFFS de AM y banda
 * lateral, ni siquiera los 6,25 de NFM_CHF_COEFFS, que es exactamente
 * por lo que esos filtros de canal nunca sirvieron para el WFM -ver la
 * nota del WFM en demod_am.h- y por lo que este es nuevo en vez de
 * reaprovechar ninguno.
 *
 * PARA QUE SIRVE: tirar el ruido y cualquier otra señal que ande por la
 * parte de fuera de la captura de +/-96 kHz -energia del canal de al
 * lado, ruido de banda ancha- antes de que llegue al discriminador, en
 * las emisoras que lo agradecen: señales flojas, o una banda llena. Los
 * canales de FM comercial van separados 200 kHz -aqui 400- en Europa, y
 * 100 -aqui 200- en otras regiones, asi que hay energia de fuera del
 * canal de verdad dentro del propio ancho de captura de este receptor
 * siempre que una emisora vecina tambien venga fuerte.
 *
 * POR QUE 80 kHz Y NO MAS ESTRECHO: el peor caso de la propia FM
 * comercial son +/-75 kHz de desviacion de pico -terreno de la regla de
 * Carson-. Con 80 kHz quedan solo 5 kHz de margen por encima de eso, y
 * se comprobo con numeros (scipy sosfreqz) que aun asi es lo bastante
 * suave justo en el borde: solo -0,41 dB de atenuacion y unos 22,5
 * microsegundos de muestra -4,3 muestras- de retardo de grupo a 75 kHz,
 * subiendo a rechazo de verdad -34,8 dB- en 90 kHz. Un codo mas apretado
 * empezaria a recortar y distorsionar picos legitimos de desviacion
 * completa, y no solo a quitar ruido. Aqui no hay piloto ni
 * subportadora de estereo que proteger -de eso se ocupan los WFM_ALPF_*,
 * mas adelante y sobre el audio ya demodulado, no este filtro-, asi que
 * los 80 kHz salen solo de la envolvente de desviacion y de ninguna
 * consideracion sobre subportadoras.
 *
 * Butterworth de cuarto orden (scipy `signal.butter(4, 80000, fs=192000,
 * output='sos')`), con el mismo cambio de signo para el DF1 de CMSIS que
 * explica el comentario de WFM_ALPF_WIDE/NORM/NARROW_COEFFS de arriba.
 * Comprobado con numeros, no sobre la placa:
 *   -3 dB en 79.992 Hz. 1 kHz: 0,00 dB; 60 kHz: -0,00; 75 kHz: -0,41;
 *   80 kHz: -3,01; 90 kHz: -34,77; 96 kHz: -239,86 -o sea que al borde
 *   de Nyquist no llega practicamente nada de energia-.
 *   Retardo de grupo: 0,35 muestras a 1 kHz, 1,23 a 60 kHz y 4,32 a 75
 *   kHz. Suave, sin pico cerca del codo que haga sospechar de
 *   resonancias en los saltos rapidos de desviacion.
 *
 * El propio wfm_ifbw_t -WFM_IFBW_WIDE y WFM_IFBW_NARROW- se declara en
 * demod_am.h, al lado de demod_am_set_wfm_ifbw() y
 * demod_am_get_wfm_ifbw(); el por que del mando esta alli. Este
 * comentario va solo del diseño del filtro.
 */
#define WFM_IFBW_STAGES 2U
static const float32_t WFM_IFBW_NARROW_COEFFS[WFM_IFBW_STAGES * 5U] = {
    0.4998149976f, 0.9996299951f, 0.4998149976f, -1.1847620863f, -0.3680454189f,
    1.0000000000f, 2.0000000000f, 1.0000000000f, -1.4538656576f, -0.6787794575f
};

#define WFM_ALPF_STAGES 2U
/*
 * *** 01/09/2026: tres anchos elegibles donde antes habia un solo filtro
 * fijo de 15 kHz (WFM_ALPF_COEFFS) ***, pedido por el usuario: darle al
 * WFM un mando de ancho de banda de verdad en vez de un codo clavado.
 * Reaprovecha el selector audio_bw_t/s_audio_bw y la casilla BW que YA
 * existian para el filtro de audio de tres posiciones de AM y banda
 * lateral -ver el comentario de demod_am_set_audio_bw() en demod_am.h-
 * en vez de añadir una cuarta casilla al menu: los tres valores de la
 * enumeracion significan ahora "15 kHz / 8 kHz / 4 kHz" estando en WFM,
 * en lugar de "4,0 / 2,3 / 1,8 kHz" estando en AM o banda lateral. El
 * mismo mando fisico, con significado segun el modo; el cambio de
 * rotulo esta en el caso de WFM de badges_draw() y en
 * menu_tile_bw_refresh(), en main.c, y por que esto es ya un mando vivo
 * tambien en WFM y no solo en AM, USB y LSB, en el comentario de
 * audio_bw_button_callback().
 *
 * ANCHO (15 kHz) es el filtro original sin tocar, con los coeficientes
 * byte a byte iguales a los que se entregaron antes de este cambio:
 * elegirlo no cambia el sonido en nada. NORMAL (8 kHz) y ESTRECHO (4
 * kHz) cambian fidelidad por menos siseo y menos ruido de multitrayecto
 * en emisoras flojas, con el mismo espiritu de "AUDIO_BW_2K3 y 1K8 para
 * señales flojas o sucias" que las opciones estrechas de AM y banda
 * lateral.
 *
 * Los tres son Butterworth de cuarto orden (scipy `signal.butter(4, fc,
 * fs=192000, output='sos')`), pasados a la forma [b0,b1,b2,a1,a2] del
 * DF1 de CMSIS con a1 y a2 CAMBIADOS DE SIGNO respecto al convenio de
 * scipy: el DF1 de CMSIS es
 * y[n] = b0*x[n] + b1*x[n-1] + b2*x[n-2] + a1*y[n-1] + a2*y[n-2], con
 * realimentacion POSITIVA, al reves que el y[n] = ... - a1*y[n-1] -
 * a2*y[n-2] de scipy. El metodo de generacion es el mismo que el del
 * propio WFM_ALPF_WIDE_COEFFS, y reproducir con el los coeficientes de
 * ANCHO sirvio de comprobacion de que el metodo era correcto antes de
 * diseñar NORMAL y ESTRECHO -eso esta en las notas de trabajo del
 * proyecto, no aqui-. Los puntos de -3 dB reales y las atenuaciones de
 * referencia, comprobados con numeros (scipy sosfreqz) y no medidos
 * sobre la placa:
 *
 *   ANCHO    (fc = 15000 Hz): -3 dB en 14.988 Hz. 1 kHz: -0,00 dB;
 *            8 kHz: -0,03; 15 kHz: -3,01; 19 kHz: -9,20; 23 kHz:
 *            -15,96; 38 kHz: -36,52. (Sin cambios respecto al
 *            WFM_ALPF_COEFFS original; por que importan justo esos
 *            puntos del piloto y la subportadora esta en la nota del
 *            WFM de demod_am.h: recepcion solo en mono, y el trabajo
 *            entero de este filtro, ademas de la fidelidad de voz y
 *            musica, es hundir el piloto de 19 kHz y la subportadora
 *            de L-R de 38 kHz lo bastante como para que no batan
 *            audiblemente contra la banda de audio.)
 *   NORMAL   (fc = 8000 Hz):  -3 dB en 7.992 Hz. 1 kHz: -0,00 dB;
 *            4 kHz: -0,02; 8 kHz: -3,02; 15 kHz: -22,37; 19 kHz:
 *            -30,99; 38 kHz: -58,87.
 *   ESTRECHO (fc = 4000 Hz):  -3 dB en 3.996 Hz. 1 kHz: -0,00 dB;
 *            2,5 kHz: -0,10; 4 kHz: -2,99; 8 kHz: -24,27; 15 kHz:
 *            -46,58; 19 kHz: -55,22.
 *
 * NORMAL y ESTRECHO hunden el piloto y la subportadora todavia mas que
 * ANCHO -caen mas rapido y empezando mas abajo-, asi que ninguno de los
 * dos necesito su propia comprobacion contra esos dos puntos de
 * referencia: ANCHO ya era por diseño el caso de margen mas apretado.
 */
static const float32_t WFM_ALPF_WIDE_COEFFS[WFM_ALPF_STAGES * 5U] = {
    0.0020570668f, 0.0041141336f, 0.0020570668f,  1.2287186181f, -0.3932293820f,
    1.0000000000f, 2.0000000000f, 1.0000000000f,  1.4942806865f, -0.6943470431f
};
static const float32_t WFM_ALPF_NORM_COEFFS[WFM_ALPF_STAGES * 5U] = {
    0.0002131387f, 0.0004262775f, 0.0002131387f,  1.5590543011f, -0.6140517819f,
    1.0000000000f, 2.0000000000f, 1.0000000000f,  1.7577536095f, -0.8197604429f
};
static const float32_t WFM_ALPF_NARROW_COEFFS[WFM_ALPF_STAGES * 5U] = {
    0.0000155517f, 0.0000311034f, 0.0000155517f,  1.7695043485f, -0.7847733318f,
    1.0000000000f, 2.0000000000f, 1.0000000000f,  1.8885559539f, -0.9048522288f
};

/*
 * EL DESENFASIS DEL WFM, un paso bajo de un polo:
 * y[n] = ALFA*y[n-1] + (1-ALFA)*x[n], con
 * ALFA = exp(-1/(fs*tau)), fs = 192 kHz y tau = 50 us -la constante de
 * tiempo de desenfasis de Europa, del CCIR; este receptor esta hecho y
 * se usa en España, ver la nota del WFM en demod_am.h. Las emisiones de
 * Estados Unidos usan 75 us, que serian ALFA = 0,9329119604f: si algun
 * dia hay que poder elegir, se añade una bandera en ejecucion, no se
 * cambia la constante a secas-. El codo sale en unos 3,18 kHz
 * (1/(2*pi*tau)), que es el que casa con el preenfasis que aplican los
 * transmisores comerciales. Saltarse esta etapa hace que el audio de
 * WFM suene aspero y cargado de agudos, no en otra frecuencia, asi que
 * es un fallo facil de no notar en el banco a la primera.
 *
 * De vuelta a su valor original de 192 kHz el 05/08/2026; el por que
 * esta en el comentario de WFM_ALPF_COEFFS, justo encima.
 */
#define WFM_DEEMPH_ALPHA 0.9010751057f

/*
 * LA GANANCIA DEL DISCRIMINADOR DE WFM: escala la salida en radianes
 * por muestra que da atan2f() antes de que entre en la cadena
 * compartida de bloqueador de continua, automatico de ganancia y
 * salida. Se eligio para que una desviacion entera de +/-75 kHz -un
 * salto de fase de unos 2,454 radianes a la velocidad vieja de 192 kHz,
 * ver demod_am.h- caiga cerca del AGC_TARGET de mas abajo. Tampoco es
 * que el valor exacto importe mucho, porque el automatico normaliza al
 * pico mas adelante de todas formas -paso 4-; esto solo deja su
 * excursion de ganancia en algo razonable en vez de arrancar desde una
 * señal casi muda o recortada a lo bruto.
 *
 * *** 04/08/2026: a la velocidad nueva de 48 kHz, esa misma desviacion
 * de +/-75 kHz sale como un salto de fase de unos 9,82 radianes, por
 * encima de +/-pi, lo que significa que el atan2f() del discriminador
 * da la vuelta y confunde en vez de seguirlo bien. Es el MISMO problema
 * de fondo que la nota de ancho de banda de radiofrecuencia de la
 * seccion del WFM en demod_am.h -que un canal entero de FM comercial ya
 * no cabe a esta velocidad de muestreo- asomando por segunda via: en el
 * propio demodulador y no solo en la ventana de captura de la entrada.
 * No es algo que esta constante de ganancia pueda arreglar. Se deja
 * como esta -sigue siendo el orden de magnitud correcto para el audio
 * de WFM que llegue, degradado y todo- porque de momento se acepta que
 * el WFM este peor pero se pueda elegir, decision del usuario; el plan
 * aplazado de doble velocidad que si arregla esto esta en la nota del
 * WFM de demod_am.h. */
#define WFM_DISC_GAIN 7300.0f

/*
 * NFM CHANNEL FILTER, 4th-order Butterworth (2 cascaded biquad DF1
 * stages, same shape/convention as CHF_COEFFS/WFM_ALPF_COEFFS), -3dB
 * at 6.25kHz @ fs=96kHz (was 48kHz, and 192kHz before that - see
 * sdr_rx.h's SDR_RX_BLOCK_SAMPLES comment) - applied to I and Q
 * post-down-mix, same spot CHF_COEFFS occupies for AM/SSB, just wider.
 * Designed offline via scipy.signal.butter (same method as the other
 * filters here, reproduced against the 48kHz values bit-for-bit - in
 * turn already verified against the original 192kHz ones - before
 * trusting it for these) and verified numerically:
 *
 *   500Hz-2.5kHz: essentially flat (0 to -0.0dB)
 *   5kHz:         -0.65dB
 *   6.25kHz:      -3.02dB (the design corner)
 *   8kHz:         -9.41dB
 *   10kHz:        -17.20dB
 *   12.5kHz:      -25.62dB (adjacent 12.5kHz-spaced channel, well
 *                 attenuated)
 *
 * Sized via Carson's rule for a 12.5kHz-spaced narrowband FM channel
 * (+/-2.5kHz max deviation, ~3kHz voice -> ~11kHz occupied bandwidth,
 * so a complex LPF corner around half that, 6.25kHz, passes the whole
 * modulated channel through to the discriminator) - see demod_am.h's
 * NFM note for why this can't just reuse CHF_COEFFS' ~4kHz corner.
 * SEPARATE CMSIS instance/state from both s_chf_i/q_inst (AM/SSB) and
 * anything WFM uses - NFM needs its own filter running, potentially
 * mid-session right after AM/SSB were using CHF_COEFFS' instances.
 */
#define NFM_CHF_STAGES 2U
static const float32_t NFM_CHF_96K_COEFFS[NFM_CHF_STAGES * 5U] = {
    0.0010801f, 0.0021602f, 0.0010801f,  1.3418845f, -0.4625531f,
    1.0000000f, 2.0000000f, 1.0000000f,  1.5925797f, -0.7357921f
};
static const float32_t NFM_CHF_48K_COEFFS[NFM_CHF_STAGES * 5U] = {
    0.0116848837f, 0.0233697674f, 0.0116848837f,  0.8165677084f, -0.1945244350f,
    1.0000000000f, 2.0000000000f, 1.0000000000f,  1.0686916613f, -0.5633465418f
};

/*
 * EL FILTRO DE CANAL DE AIS: el mismo sitio que el de NFM, mas ancho.
 * 27/09/2026.
 *
 * AIS es GMSK a 9.600 bits por segundo con +-2.400 Hz de desviacion. Por la
 * regla de Carson ocupa 2*(2400+4800) = 14,4 kHz, o sea que el filtro
 * complejo tiene que pasar +-7,2 kHz limpios. El de NFM corta a 6,25 y
 * redondearia los flancos de la GMSK hasta cerrar el ojo: no filtraria el
 * audio, destrozaria la informacion antes del discriminador. Es el mismo
 * razonamiento por el que NFM no puede usar el de AM (ver su comentario
 * arriba), un escalon mas arriba.
 *
 * Butterworth de cuarto orden a 9 kHz. Medido:
 *
 *      4,8 kHz   -0,02 dB    (donde vive el grueso de la senal)
 *      7,2 kHz   -0,62 dB    (el borde de Carson: practicamente plano)
 *      9,0 kHz   -3,01 dB
 *     25,0 kHz  -43,72 dB    (el canal de al lado, a 25 kHz)
 *
 * COMPARTE INSTANCIA Y ESTADO CON EL DE NFM y no es dejadez: AIS va por
 * debajo en NFM (como el APRS) y los dos no pueden estar a la vez, asi que
 * dos juegos de biquads serian 80 bytes de estado para que uno de los dos
 * este siempre parado. Lo que decide cual se carga es demod_am_chf_ancho().
 */
static const float32_t AIS_CHF_96K_COEFFS[NFM_CHF_STAGES * 5U] = {
    0.0038695f, 0.0077390f, 0.0038695f,  1.0988973f, -0.3216325f,
    1.0000000f, 2.0000000f, 1.0000000f,  1.3713747f, -0.6493383f
};
static const float32_t AIS_CHF_48K_COEFFS[NFM_CHF_STAGES * 5U] = {
    0.0379730f, 0.0759461f, 0.0379730f,  0.4129187f, -0.0790086f,
    1.0000000f, 2.0000000f, 1.0000000f,  0.5654501f, -0.4775923f
};

/* 1 mientras el modo AIS esta puesto: manda sobre que coeficientes se
 * cargan en el filtro de canal de NFM. */
static uint8_t s_ais_chf;

/*
 * LA GANANCIA DEL DISCRIMINADOR DE NFM: el mismo razonamiento que
 * WFM_DISC_GAIN, escalado a la desviacion mucho menor del NFM. El peor
 * caso, +/-5 kHz -que cubre los dos convenios de desviacion estrecha en
 * uso, el de 2,5 y el de 5-, da un salto de fase de unos 0,3273
 * radianes a 96 kHz; eran unos 0,6545 a 48 kHz y unos 0,1636 a los 192
 * originales de antes, ver el comentario de SDR_RX_BLOCK_SAMPLES en
 * sdr_rx.h. En cualquiera de los tres se queda comodamente por debajo
 * de +/-pi, asi que, al contrario que el caso del WFM de aqui arriba,
 * el discriminador de NFM sigue siendo valido a la velocidad que sea:
 * no da la vuelta. La amplitud en crudo antes del automatico de
 * ganancia es ahora proporcionalmente menor -mas o menos la mitad,
 * siguiendo al salto de fase, que tambien se ha partido por dos
 * respecto al valor de 48 kHz-, pero eso es justo lo que el automatico
 * de mas adelante se come solo: la ganancia de abajo se deja como
 * estaba en vez de reajustarla, con el mismo razonamiento de "el valor
 * exacto no importa mucho" que en WFM_DISC_GAIN.
 */
#define NFM_DISC_GAIN 110000.0f

/*
 * Las constantes del SILENCIADOR (AM y NFM). El razonamiento de diseño
 * entero -medida por nivel de radiofrecuencia, no por banda de ruido-
 * esta en el bloque del silenciador de demod_am.h, al lado de
 * agc_profile_t.
 *
 * SQUELCH_HYSTERESIS_DB: el ancho total de la zona muerta, repartido por
 * igual a los dos lados del umbral -+/-0,5 de esto- entre el punto de
 * disparo de ABRIR y el de CERRAR, para que una señal que se quede justo
 * en el umbral no castañetee. Tres dB son un margen modesto y muy usado:
 * bastante ancho para matar el castañeteo y bastante estrecho para que
 * el silenciador siga notandose donde lo pones, y no "unos dB mas tarde
 * de lo que esperabas".
 *
 * SQUELCH_LEVEL_ALPHA: el suavizado exponencial de la lectura de nivel
 * de radiofrecuencia de cada bloque, aplicado UNA VEZ POR BLOQUE y no
 * por muestra; o sea, una media exponencial a ritmo de bloque, al
 * contrario que la soltada de pico del automatico de ganancia, que va
 * por muestra. Con SDR_RX_BLOCK_SAMPLES = 128 muestras por bloque a 48
 * kHz -eran 512 a 192 kHz antes del 04/08/2026, ver el comentario de
 * sdr_rx.h; los mismos 2,667 ms por bloque de las dos maneras, a
 * proposito, asi que el valor de esta constante NO cambia-, un alfa de
 * 0,85 da una constante de tiempo de unos
 * -2,667 ms / ln(0,85) = unos 16 ms. Reacciona a una emisora que
 * aparece o se desvanece en un par de decenas de milesimas -lo bastante
 * rapido para no quedarse visiblemente detras de cuando el audio se
 * abre- y a la vez amortigua el baile de bloque a bloque lo suficiente
 * para que la histeresis de arriba sirva para lo que tiene que servir.
 */
#define SQUELCH_HYSTERESIS_DB 3.0f
#define SQUELCH_LEVEL_ALPHA   0.85f

/*
 * LA BANDA LATERAL (USB/LSB), con arquitectura DIEZMADA por el metodo de
 * desfase. Del 30/07/2026, sustituyendo al transformador de Hilbert a
 * velocidad entera de ese mismo dia, despues de que las cuentas de
 * ciclos de la interrupcion de verdad enseñaran que mataba de hambre al
 * bucle principal; la historia entera esta en la nota de banda lateral
 * de demod_am.h.
 *
 * UN FALLO EN EL ORDEN DE LOS COEFICIENTES, ENCONTRADO AL REHACER ESTO:
 * las funciones de FIR en crudo de CMSIS -arm_fir_f32,
 * arm_fir_decimate_f32 y arm_fir_interpolate_f32, al contrario que las
 * de biquad DF1, que usan otra forma de ecuacion en diferencias {b,a}-
 * piden todas los coeficientes en orden INVERTIDO EN EL TIEMPO:
 * {b[numTaps-1], ..., b[1], b[0]}, y no en orden cronologico natural. El
 * array HILBERT_COEFFS original, el de velocidad entera, estaba en orden
 * natural. Para un filtro ANTISIMETRICO como un transformador de
 * Hilbert, darle la vuelta al array es matematicamente lo mismo que
 * cambiarle el signo -invertido[i] = h[N-1-i] = -h[i]-, asi que ese
 * fallo estuvo calculando en silencio el Hilbert NEGADO todo el rato: un
 * error de orden, llano y arreglable, y no la situacion de "a lo mejor
 * hay que intercambiar en el banco, el sentido de I/Q de este hardware
 * es raro" que el interruptor SSB_SWAP_SIDEBANDS de mas abajo supuso al
 * principio. Los tres arrays de abajo se generan ya invertidos. Los
 * pasos bajos de diezmado e interpolado son simetricos -de fase
 * lineal-, asi que darles la vuelta no cambia nada de las dos maneras:
 * el unico orden que importaba de verdad era el del Hilbert.
 *
 * LA CADENA: despues de los pasos 0a a 0c compartidos -bajada en
 * frecuencia y filtro de canal, todavia a los 96 kHz enteros; eran 48, y
 * 192 antes de eso, ver el comentario de SDR_RX_BLOCK_SAMPLES en
 * sdr_rx.h-, la banda lateral se separa:
 *   1. DIEZMAR I y Q por 8 (de 96 kHz a 12 kHz, de 256 muestras por
 *      bloque a 32). Antes era diezmar por 4 -de 48 a 12, de 128 a 32- y
 *      antes por 16 -de 192 a 12, de 512 a 32-: SIEMPRE los mismos 12
 *      kHz de velocidad diezmada y SIEMPRE el mismo bloque diezmado de
 *      32 muestras, porque la gracia de cada mudanza era cambiar la
 *      velocidad ENTERA dejando a proposito este dominio diezmado como
 *      estaba; ver la cabecera de nr_ss.c, que depende de ello. Se hace
 *      con arm_fir_decimate_f32 y DECIM_COEFFS, un paso bajo de
 *      antialias de 96 tomas: -3 dB por los 5 o 6 kHz, -54,7 dB en 8
 *      kHz y -69 dB en 10 kHz, comprobado con numeros. Junto con la
 *      caida que ya tiene el filtro de canal, esto cubre de sobra todo
 *      lo que si no se doblaria dentro de la banda de Nyquist diezmada
 *      de 0 a 6 kHz. Son mas tomas que las 41 de cuando se diezmaba por
 *      4, porque un antialias de diezmado por 8 necesita mas o menos el
 *      doble de tomas para la misma pendiente de transicion en hercios
 *      absolutos, ahora que es una rebanada mas estrecha de una
 *      velocidad de muestreo que se ha doblado. Y aun con esas tomas de
 *      mas sigue saliendo BARATO: arm_fir_decimate_f32 solo evalua la
 *      suma del FIR a la velocidad de salida DIEZMADA -32 muestras-,
 *      asi que cuesta numTaps*32 = 3.072 multiplicaciones con
 *      acumulacion por canal, y no numTaps*256. Esa es justamente la
 *      gracia de hacerlo asi.
 *   2. DESPLAZAR con Hilbert la Q diezmada y retrasar la I diezmada
 *      para que casen -la misma tecnica de siempre, todavia a 12 kHz y
 *      SIN cambios por ninguna mudanza de velocidad, ver el paso 1-.
 *      HILBERT_COEFFS sigue teniendo 63 tomas, sigue a 12 kHz y cubre
 *      de 300 Hz a 6 kHz igual que siempre: |H| = 0,913 en 300 Hz y
 *      practicamente plano en 1,000 a partir de 500, comprobado con
 *      numeros. Despues se combinan como explica demod_am.h.
 *   3. INTERPOLAR el audio de 12 kHz que sale de vuelta a 96 kHz -de 32
 *      muestras a 256- con arm_fir_interpolate_f32 e INTERP_COEFFS,
 *      directamente dentro de s_env[]. A partir de ahi todo
 *      -bloqueador de continua, paso bajo de audio, automatico de
 *      ganancia y salida- es IDENTICO al de AM, sin cambios.
 *      INTERP_COEFFS es el MISMO diseño de paso bajo de 5 o 6 kHz que
 *      DECIM_COEFFS pero escalado a ganancia 8 en continua en vez de 1
 *      -era 4 a 48 kHz y 16 a los 192 originales: la ganancia que tiene
 *      que poner un filtro de reconstruccion es siempre exactamente el
 *      factor L de diezmado o interpolado-. Meter L-1 ceros entre
 *      muestras divide la energia por L, asi que el filtro de
 *      reconstruccion tiene que devolver esa L o el audio sale a la
 *      enesima parte del volumen. Eso se confirmo con una simulacion de
 *      ida y vuelta antes de escribir una linea de firmware -ver las
 *      notas de diseño citadas arriba-; si no, habria sido otro informe
 *      mas de "por que se oye tan bajo". El numero de tomas, 96, tiene
 *      que ser multiplo del factor de interpolacion, 8:
 *      arm_fir_interpolate_init_f32() lo exige y demod_am_init()
 *      comprueba lo que devuelve.
 */
#define DECIM_FACTOR_96K 8U /* era 4U a 48 kHz, y 16U antes, a 192; ver el
                          * bloque de comentario de arriba. AM, banda lateral
                          * y NFM pasaron de 48 a 96 kHz para cubrir mas
                          * radiofrecuencia, manteniendo el MISMO dominio
                          * diezmado de 12 kHz en el que ya corren el Hilbert,
                          * la reduccion de ruido y el interpolado: diezmar
                          * por 8 -de 96 a 12- sustituye a diezmar por 4 -de
                          * 48 a 12-, y la velocidad de salida es la misma de
                          * las dos maneras. */
/*
 * *** 01/09/2026: se añadieron DECIM_FACTOR_48K, DECIM_COEFFS_48K e
 * INTERP_COEFFS_48K al lado de lo de arriba, pedido por el usuario; la
 * historia de "la opcion de 48 kHz" esta en el comentario de
 * demod_am_set_active_rate() en demod_am.h. NO son una deduccion nueva:
 * son los valores reales de la epoca de 48 kHz de este mismo proyecto,
 * recuperados literalmente de un ZIP viejo que el usuario todavia
 * guardaba, de antes de que la migracion de 48 a 96 los sobrescribiera.
 * DECIM_FACTOR_48K = 4 -de 48 a 12 kHz- cae exactamente en el mismo
 * dominio diezmado de 12 kHz que el diezmado por 8 del camino de 96, asi
 * que el Hilbert y la reduccion de ruido se quedan intactos de las dos
 * maneras; ver sus propios comentarios.
 *
 * UNA DIFERENCIA IMPORTANTE CON EL PROYECTO DE LA EPOCA DE 48 kHz: alli
 * SDR_RX_BLOCK_SAMPLES encogia a la vez que la velocidad de muestreo
 * -de 512 a 128-, asi que DEC_BLOCK_SAMPLES se quedaba numericamente
 * CONSTANTE en 32 todo el rato. Esta vez SDR_RX_BLOCK_SAMPLES se queda
 * fijo en 256 -a proposito, por lo que ahorra de carga de procesador
 * tener bloques mas largos y menos por segundo; ver el comentario de
 * SDR_RX_BLOCK_SAMPLES en main.c-, y eso hace que DEC_BLOCK_SAMPLES_48K
 * salga 64 (256/4) y NO 32 como en el camino de 96 (256/8): los tamaños
 * de bloque diezmado de las dos velocidades ahora si son distintos de
 * verdad, al contrario que en todas las mudanzas de velocidad anteriores
 * de este proyecto. Por eso DEC_BLOCK_SAMPLES_MAX y varios buffers de
 * aqui abajo tuvieron que crecer. A nr_ss_process() le da igual -recibe
 * una longitud `n` en ejecucion y guarda en su propio anillo de tamaño
 * fijo pase lo que pase-, pero a los arrays de C de TAMAÑO FIJO que este
 * fichero declara para las señales del dominio diezmado no les da igual,
 * porque en C el tamaño de un array se decide al compilar.
 */
#define DECIM_FACTOR_48K 4U
#define DEC_BLOCK_SAMPLES_96K (SDR_RX_BLOCK_SAMPLES / DECIM_FACTOR_96K) /* 32, @ 12kHz - UNCHANGED
                                                                   * numeric value across every
                                                                   * Fs move this project has made
                                                                   * (192k/16, 48k/4, now 96k/8 -
                                                                   * all land on 32 samples @ 12kHz
                                                                   * by design, see the comment
                                                                   * above) - this is also why
                                                                   * nr_ss.h's NR_SS_BLOCK_SAMPLES
                                                                   * needed zero changes, again. */
#define DEC_BLOCK_SAMPLES_48K (SDR_RX_BLOCK_SAMPLES / DECIM_FACTOR_48K) /* 64, @ 12kHz - see the
                                                                   * "IMPORTANT DIFFERENCE" comment
                                                                   * just above for why this does
                                                                   * NOT match DEC_BLOCK_SAMPLES_96K
                                                                   * this time. */
#define DEC_BLOCK_SAMPLES_MAX 64U /* = DEC_BLOCK_SAMPLES_48K, the larger of the two - every fixed-
                                     * size decimated-domain buffer below is sized to this, not to
                                     * whichever rate happens to be active, so switching rates live
                                     * never needs to reallocate anything, only reinterpret how much
                                     * of an already-large-enough buffer is actually in use. */

/*
 * DECIM_COEFFS e INTERP_COEFFS, regenerados el 05/08/2026 para diezmar e
 * interpolar por 8 (96 kHz <-> 12 kHz), sustituyendo al par de por 4
 * (48 <-> 12). El OBJETIVO de banda de paso y banda eliminada es el
 * mismo que antes -pasar hasta unos 5 kHz y proteger del solapamiento a
 * partir de unos 8-, con diseño equirizado de Parks-McClellan/Remez via
 * scipy.signal.remez. NO es el mismo metodo de diseño que el filtro de
 * por 4 anterior: el algoritmo y la ventana exactos de aquel no se
 * sacaron bit a bit antes de este cambio, solo se conocia su
 * especificacion -tomas, codos y atenuacion declarada- y aqui se tomo
 * como OBJETIVO de diseño, no se reprodujo literalmente. Necesita
 * muchas mas tomas que la version de por 4 para el mismo ancho de
 * transicion en hercios -96 contra 41-: la banda de transicion
 * normalizada -de paso a eliminada, respecto a la velocidad de
 * muestreo- es mas o menos la MITAD de ancha ahora que la velocidad se
 * ha doblado para el mismo reparto en hercios absolutos, y la longitud
 * de un FIR crece mas o menos como la inversa de ese ancho normalizado
 * para una misma profundidad de banda eliminada. Comprobado con numeros
 * (scipy freqz): banda de paso practicamente plana hasta 5 kHz -0,01
 * dB-, -54,68 dB en 8 kHz y -69,06 dB en 10 kHz. Algo menos agresivo
 * que los -45 y -88 dB declarados del filtro de por 4, pero sigue
 * quedando de sobra por debajo de lo que necesita el suelo de ruido de
 * esta aplicacion; y lo que cuestan las tomas de mas -96 contra 41,
 * unas 2,3 veces- es despreciable frente al presupuesto de tiempo real
 * de la interrupcion (ver las cuentas de ciclos que saca sdr_tick).
 */
#define DECIM_COEFFS_96K_TAPS 96U
static const float32_t DECIM_COEFFS_96K[DECIM_COEFFS_96K_TAPS] = {
    0.0010989f, 0.0003992f, 0.0002785f, 0.0000067f, -0.0003927f, -0.0008565f,
    -0.0012870f, -0.0015684f, -0.0015915f, -0.0012798f, -0.0006144f, 0.0003447f,
    0.0014523f, 0.0025049f, 0.0032565f, 0.0034804f, 0.0030143f, 0.0018132f,
    -0.0000196f, -0.0022256f, -0.0044165f, -0.0061322f, -0.0069256f, -0.0064572f,
    -0.0045839f, -0.0014265f, 0.0026122f, 0.0068766f, 0.0105477f, 0.0127755f,
    0.0128414f, 0.0103201f, 0.0052114f, -0.0019850f, -0.0102740f, -0.0182575f,
    -0.0243083f, -0.0268031f, -0.0243839f, -0.0162023f, -0.0021094f, 0.0172491f,
    0.0404497f, 0.0654278f, 0.0897184f, 0.1107682f, 0.1262732f, 0.1344935f,
    0.1344935f, 0.1262732f, 0.1107682f, 0.0897184f, 0.0654278f, 0.0404497f,
    0.0172491f, -0.0021094f, -0.0162023f, -0.0243839f, -0.0268031f, -0.0243083f,
    -0.0182575f, -0.0102740f, -0.0019850f, 0.0052114f, 0.0103201f, 0.0128414f,
    0.0127755f, 0.0105477f, 0.0068766f, 0.0026122f, -0.0014265f, -0.0045839f,
    -0.0064572f, -0.0069256f, -0.0061322f, -0.0044165f, -0.0022256f, -0.0000196f,
    0.0018132f, 0.0030143f, 0.0034804f, 0.0032565f, 0.0025049f, 0.0014523f,
    0.0003447f, -0.0006144f, -0.0012798f, -0.0015915f, -0.0015684f, -0.0012870f,
    -0.0008565f, -0.0003927f, 0.0000067f, 0.0002785f, 0.0003992f, 0.0010989f
};

#define HILBERT_COEFFS_TAPS 63U
#define HILBERT_GROUP_DELAY_DEC ((HILBERT_COEFFS_TAPS - 1U) / 2U) /* 31 samples @ 12kHz */
static const float32_t HILBERT_COEFFS[HILBERT_COEFFS_TAPS] = {
    -0.0000000000f, 0.0000000000f, 0.0000823578f, 0.0000000000f, 0.0003687313f, 0.0000000000f,
    0.0009524519f, 0.0000000000f, 0.0019780775f, 0.0000000000f, 0.0036465115f, 0.0000000000f,
    0.0062203355f, 0.0000000000f, 0.0100344372f, 0.0000000000f, 0.0155222771f, 0.0000000000f,
    0.0232796443f, 0.0000000000f, 0.0342170741f, 0.0000000000f, 0.0499393402f, 0.0000000000f,
    0.0738018099f, 0.0000000000f, 0.1145266784f, 0.0000000000f, 0.2042978004f, 0.0000000000f,
    0.6339440957f, 0.0000000000f, -0.6339440957f, 0.0000000000f, -0.2042978004f, 0.0000000000f,
    -0.1145266784f, 0.0000000000f, -0.0738018099f, 0.0000000000f, -0.0499393402f, 0.0000000000f,
    -0.0342170741f, 0.0000000000f, -0.0232796443f, 0.0000000000f, -0.0155222771f, 0.0000000000f,
    -0.0100344372f, 0.0000000000f, -0.0062203355f, 0.0000000000f, -0.0036465115f, 0.0000000000f,
    -0.0019780775f, 0.0000000000f, -0.0009524519f, 0.0000000000f, -0.0003687313f, 0.0000000000f,
    -0.0000823578f, 0.0000000000f, 0.0000000000f
};

#define INTERP_COEFFS_96K_TAPS 96U /* must be a multiple of DECIM_FACTOR_96K (8) - was 40 @
                                 * diezmado por 4 a 48 kHz, y 160 con diezmado por 16 a
                                 * 192. La MISMA forma que DECIM_COEFFS de arriba -las
                                 * dos con 96 tomas ahora, y por primera vez con el mismo
                                 * numero exacto: el par de por 4 tenia 41 en el
                                 * diezmador, que no tiene que ser multiplo de L, y 40 en
                                 * el interpolador, donde
                                 * arm_fir_interpolate_init_f32() si lo exige; parecidas
                                 * pero no literalmente el mismo array-, escalada a
                                 * ganancia 8 en continua -el factor L de interpolacion-
                                 * en vez de 1. Por que el filtro de reconstruccion tiene
                                 * que devolver exactamente esa L despues de meter L-1
                                 * ceros entre muestras esta en el comentario que hay
                                 * encima del valor viejo de DECIM_COEFFS_96K_TAPS. */
static const float32_t INTERP_COEFFS_96K[INTERP_COEFFS_96K_TAPS] = {
    0.0087911f, 0.0031937f, 0.0022281f, 0.0000539f, -0.0031416f, -0.0068521f,
    -0.0102960f, -0.0125474f, -0.0127322f, -0.0102388f, -0.0049153f, 0.0027572f,
    0.0116186f, 0.0200389f, 0.0260523f, 0.0278433f, 0.0241143f, 0.0145053f,
    -0.0001567f, -0.0178046f, -0.0353319f, -0.0490577f, -0.0554052f, -0.0516576f,
    -0.0366711f, -0.0114122f, 0.0208977f, 0.0550129f, 0.0843813f, 0.1022039f,
    0.1027312f, 0.0825604f, 0.0416915f, -0.0158798f, -0.0821919f, -0.1460602f,
    -0.1944667f, -0.2144249f, -0.1950712f, -0.1296186f, -0.0168749f, 0.1379930f,
    0.3235973f, 0.5234220f, 0.7177471f, 0.8861453f, 1.0101856f, 1.0759478f,
    1.0759478f, 1.0101856f, 0.8861453f, 0.7177471f, 0.5234220f, 0.3235973f,
    0.1379930f, -0.0168749f, -0.1296186f, -0.1950712f, -0.2144249f, -0.1944667f,
    -0.1460602f, -0.0821919f, -0.0158798f, 0.0416915f, 0.0825604f, 0.1027312f,
    0.1022039f, 0.0843813f, 0.0550129f, 0.0208977f, -0.0114122f, -0.0366711f,
    -0.0516576f, -0.0554052f, -0.0490577f, -0.0353319f, -0.0178046f, -0.0001567f,
    0.0145053f, 0.0241143f, 0.0278433f, 0.0260523f, 0.0200389f, 0.0116186f,
    0.0027572f, -0.0049153f, -0.0102388f, -0.0127322f, -0.0125474f, -0.0102960f,
    -0.0068521f, -0.0031416f, 0.0000539f, 0.0022281f, 0.0031937f, 0.0087911f
};

/*
 * *** 01/09/2026: DECIM_COEFFS_48K/INTERP_COEFFS_48K, this project's
 * own real 48kHz-era values, recovered verbatim from an old source ZIP
 * (see DECIM_FACTOR_48K's own comment above for the full story) - not
 * a fresh design, a restoration. DECIM_COEFFS_48K (41 taps, DC gain=1)
 * anti-aliases 48kHz->12kHz; INTERP_COEFFS_48K (40 taps, DC gain=4=
 * the interpolation factor L, same "must supply back the gain zero-
 * stuffing removed" reasoning as the 96K pair above) reconstructs
 * 12kHz->48kHz. Both were reported, at the time, as verified
 * numerically via scipy - not independently re-verified in THIS
 * session beyond confirming the byte values match the recovered
 * source exactly (unlike the shorter biquad filters above, which were
 * cheap enough to also re-derive fresh and cross-check bit-for-bit).
 */
#define DECIM_COEFFS_48K_TAPS 41U
static const float32_t DECIM_COEFFS_48K[DECIM_COEFFS_48K_TAPS] = {
    0.0000464098f, 0.0001604721f, 0.0002054552f, -0.0001018100f, -0.0009420342f, -0.0019856045f,
    -0.0022039177f, -0.0003366122f, 0.0039477596f, 0.0088755645f, 0.0105351209f, 0.0047914879f,
    -0.0093236751f, -0.0267037662f, -0.0362087960f, -0.0248893350f, 0.0149572705f, 0.0796076886f,
    0.1526768100f, 0.2105814863f, 0.2326200509f, 0.2105814863f, 0.1526768100f, 0.0796076886f,
    0.0149572705f, -0.0248893350f, -0.0362087960f, -0.0267037662f, -0.0093236751f, 0.0047914879f,
    0.0105351209f, 0.0088755645f, 0.0039477596f, -0.0003366122f, -0.0022039177f, -0.0019856045f,
    -0.0009420342f, -0.0001018100f, 0.0002054552f, 0.0001604721f, 0.0000464098f
};

#define INTERP_COEFFS_48K_TAPS 40U /* must be a multiple of DECIM_FACTOR_48K (4) */
static const float32_t INTERP_COEFFS_48K[INTERP_COEFFS_48K_TAPS] = {
    0.0002119268f, 0.0005815043f, 0.0003992317f, -0.0013967222f, -0.0049632048f, -0.0080461863f,
    -0.0060033199f, 0.0052135661f, 0.0239471765f, 0.0393545157f, 0.0342535584f, -0.0040805948f,
    -0.0697381812f, -0.1303465951f, -0.1346151143f, -0.0361678797f, 0.1768670476f, 0.4640366160f,
    0.7403911421f, 0.9101015131f, 0.9101015131f, 0.7403911421f, 0.4640366160f, 0.1768670476f,
    -0.0361678797f, -0.1346151143f, -0.1303465951f, -0.0697381812f, -0.0040805948f, 0.0342535584f,
    0.0393545157f, 0.0239471765f, 0.0052135661f, -0.0060033199f, -0.0080461863f, -0.0049632048f,
    -0.0013967222f, 0.0003992317f, 0.0005815043f, 0.0002119268f
};

/*
 * El convenio de signo de la banda lateral:
 * audio = I_retrasada + signo*Hilbert(Q).
 * NOTA del 30/07/2026: la implementacion original a velocidad entera
 * tenia el fallo de orden de coeficientes que se explica encima de
 * HILBERT_COEFFS, y eso negaba en silencio la salida del Hilbert. Si en
 * aquella compilacion las bandas laterales parecian "cambiadas", ese
 * fallo -ya arreglado- era el motivo. Con el orden corregido, lo de
 * abajo es la mejor apuesta de partida: si USB y LSB siguen saliendo
 * cambiadas en el banco, se pone esto a 1 y no se toca nada mas.
 */
#define SSB_SWAP_SIDEBANDS 0
#if SSB_SWAP_SIDEBANDS
#define SSB_USB_SIGN (+1.0f)
#define SSB_LSB_SIGN (-1.0f)
#else
#define SSB_USB_SIGN (-1.0f)
#define SSB_LSB_SIGN (+1.0f)
#endif

/* DC blocker pole: y[n] = x[n] - x[n-1] + R*y[n-1]. R=0.9990 at
 * 96kHz (was 0.9980 @ 48kHz, and 0.9995 @ 192kHz before that - see
 * sdr_rx.h's SDR_RX_BLOCK_SAMPLES comment), same tau (~10.4ms) puts
 * the corner around the same ~15Hz regardless of Fs, since R=exp(-1/
 * (fs*tau)) - verified: fs=96000 gives 0.99900 to 5 decimal places.
 * Left as a scalar one-pole - cheap enough (1 MAC/sample) that a
 * CMSIS block call isn't worth the extra buffer/call overhead.
 *
 * *** 01/09/2026: two values now, selected by demod_am_set_active_
 * rate() into s_dcb_r below *** - DCB_R_48K is this project's own
 * real, previously-shipped 48kHz-era value (recovered verbatim from
 * an old source ZIP, then independently re-verified with the same
 * exp(-1/(fs*tau)) formula before trusting it). */
#define DCB_R_96K 0.9990f
#define DCB_R_48K 0.9980f

/* AGC_TARGET y AGC_GAIN_MAX viven ahora en agc.h, con el lazo que los
 * usa - ver alli por que el lazo dejo de estar copiado dos veces en este
 * fichero. Aqui se quedan los dos SUELOS DE PICO, que si son de cada
 * camino: la escala de la señal no es la misma a la salida del detector
 * de envolvente que a la del discriminador de FM. */
#define AGC_PEAK_MIN 40.0f
/*
 * EL SUELO DE PICO DEL AUTOMATICO DE GANANCIA, PROPIO DEL WFM. Añadido
 * el 05/08/2026, despues de que los registros de la placa de verdad
 * clavaran en esta constante exacta el informe de "la primera vez que
 * entro en WFM despues de arrancar suena mudo o distorsionado, la
 * segunda vez va bien". El AGC_PEAK_MIN de arriba -40,0f- esta ajustado
 * a la escala de señal de AM, banda lateral y NFM. El discriminador en
 * crudo del WFM llega de forma natural hasta mas o menos
 * +/-pi*WFM_DISC_GAIN -unos +/-22.900, confirmado en los registros- e
 * incluso despues de la cadena de bloqueador de continua, desenfasis y
 * paso bajo de audio, su envolvente tipica se queda en los miles bajos
 * -entre unos 3.000 y 9.000, tambien confirmado-. Arrancar el lazo de
 * s_wfm_agc_peak desde un suelo de 40 cuando la señal de verdad es de
 * 100 a 200 veces mayor hacia que los primeros bloques calcularan una
 * ganancia de AGC_TARGET/40 -recortada a AGC_GAIN_MAX = 300- y
 * empujaran la salida a recorte duro, justo en +/-AGC_TARGET (18.000),
 * muestra tras muestra, hasta que el seguimiento de pico se ponia al
 * dia varios bloques despues. Eso se oye como un zumbido aspero y
 * recortado, no como silencio, pero por lo visto en el banco no se
 * distingue de "no funciona". La SEGUNDA entrada en WFM sonaba bien
 * porque s_wfm_agc_peak se conserva entre sesiones de WFM -no se
 * reinicia salvo al arrancar, ver el comentario donde se declara- y
 * para entonces ya se habia adaptado a algo parecido al nivel real.
 * Aqui se pone a 4000,0f: comodamente dentro del margen de regimen
 * permanente observado, asi que el primer bloque arranca ya en el orden
 * correcto en vez de tener que subir hasta alli por las malas.
 */
#define WFM_AGC_PEAK_MIN 4000.0f

/*
 * Per-profile release coefficients (peak *= this, per sample @ 96kHz,
 * was 48kHz, and 192kHz before that - see sdr_rx.h's
 * SDR_RX_BLOCK_SAMPLES comment) - see agc_profile_t's comment in
 * demod_am.h for what each one is for. Computed offline as
 * exp(-1/(fs*tau)) for the target time constant tau (UNCHANGED tau
 * values across every Fs move - only fs itself moves), same method as
 * WFM_DEEMPH_ALPHA above:
 *
 *   AGC_RELEASE_SLOW   : tau=700ms -> 0.99998512f
 *   AGC_RELEASE_MEDIUM : tau=175ms -> 0.99994048f (this was, to 5
 *                         decimal places at the OLD 192kHz rate, the
 *                         ORIGINAL single AGC_RELEASE value this
 *                         project always used before profiles existed
 *                         - MEDIUM is the default specifically so
 *                         existing behavior doesn't change for anyone
 *                         who never touches the new badge)
 *   AGC_RELEASE_FAST   : tau=60ms  -> 0.99982640f
 *
 * AGC_PROFILE_MANUAL doesn't use any of these - see the loop in
 * demod_am_process_raw() and agc_profile_t's MANUAL note.
 *
 * *** 01/09/2026: _48K counterparts added, same tau targets, computed
 * at fs=48000 - this project's own real, previously-shipped 48kHz-era
 * values (recovered verbatim from an old source ZIP, then
 * independently re-verified with the same exp(-1/(fs*tau)) formula
 * before trusting them - see demod_am_set_active_rate()'s comment in
 * demod_am.h for the full "48kHz option" story). ***
 */
#define AGC_RELEASE_SLOW_96K   0.99998512f
#define AGC_RELEASE_MEDIUM_96K 0.99994048f
#define AGC_RELEASE_FAST_96K   0.99982640f
#define AGC_RELEASE_SLOW_48K   0.99997024f
#define AGC_RELEASE_MEDIUM_48K 0.99988096f
#define AGC_RELEASE_FAST_48K   0.99965284f

/*
 * WFM's OWN DC blocker pole + AGC release coefficients, at WFM's
 * actual 192kHz rate - added 05/08/2026 when WFM got its own separate
 * processing path (demod_wfm_process_raw()) running at 192kHz while
 * AM/SSB/NFM (DCB_R/AGC_RELEASE_* above) stayed at 48kHz. These are
 * simply the ORIGINAL values this project used everywhere before
 * 04/08/2026's 48kHz move - same tau targets (DCB ~15Hz corner;
 * AGC_RELEASE tau=700/175/60ms), just computed at fs=192kHz instead of
 * 48kHz, since exp(-1/(fs*tau)) genuinely depends on fs. WFM has its
 * own separate AGC state (s_wfm_agc_peak) and DC blocker state
 * (s_wfm_dcb_x1/y1) too - see demod_wfm_process_raw().
 */
#define WFM_DCB_R               0.9995f
#define WFM_AGC_RELEASE_SLOW    0.99999256f
#define WFM_AGC_RELEASE_MEDIUM  0.99997024f
#define WFM_AGC_RELEASE_FAST    0.99991320f

/*
 * LA BAJADA EN FRECUENCIA, GIRANDO A SR/4. Del 30/07/2026; el usuario
 * aporto el codigo, eficiente y ya probado en otro sitio. Todo el fondo
 * esta en la nota de SINTONIA POR FRECUENCIA INTERMEDIA BAJA de
 * demod_am.h, incluido el intento anterior con un oscilador a Fs/8 que
 * se deshizo, y el fallo de tiempos del I2C a pedales al pasar de -O0 a
 * -O2 -sin relacion ninguna- que resulto ser la causa de verdad de
 * aquella regresion.
 *
 * ARREGLO DEL SENTIDO, del 30/07/2026, el mismo dia: la primera version
 * de este giro usaba e^(+j*2*pi*n/4) -el espectro sube-, suponiendo que
 * la señal buena, con el oscilador local sintonizado
 * DEMOD_IF_OFFSET_HZ por debajo de la emisora elegida, iba a caer en
 * +SR/4. En el banco resulto que caia en -SR/4. La correspondencia de
 * canales I/Q de esta placa ya esta documentada en otra parte de este
 * codigo como que no es la de los libros -ver sdr_rx.h-, asi que el
 * signo de "para donde es arriba" no es algo que aqui se pueda suponer
 * desde los principios. Arreglado usando el giro conjugado,
 * e^(-j*2*pi*n/4), que recorre 1, -j, -1, +j en vez de 1, +j, -1, -j.
 * Sigue sin multiplicaciones: solo cambios de signo y un intercambio de
 * I con Q, aplicados a cada grupo de cuatro muestras:
 *   n%4==0: igual                        (por 1, sin tocar)
 *   n%4==1: (I,Q) -> (Q, -I)             (por -j)
 *   n%4==2: (I,Q) -> (-I, -Q)            (por -1)
 *   n%4==3: (I,Q) -> (-Q, I)             (por +j)
 * SDR_RX_BLOCK_SAMPLES (512) es multiplo entero de 4, asi que -el mismo
 * argumento que en la version de Fs/8 de antes- la fase no se va ni se
 * rompe en los limites de bloque: la n = 0 de cada bloque cae siempre en
 * el caso de "igual".
 */

/* CMSIS biquad instances + their state (state must be caller-owned,
 * 4 floats per stage: x[n-1], x[n-2], y[n-1], y[n-2]). chf_i/chf_q
 * share CHF_COEFFS (read-only) but each needs its own state, since
 * I and Q are independent signals through the same filter shape. */
static arm_biquad_casd_df1_inst_f32 s_chf_i_inst;
static arm_biquad_casd_df1_inst_f32 s_chf_q_inst;
static arm_biquad_casd_df1_inst_f32 s_alpf_inst; /* NFM-only as of 02/08/2026 - see ALPF_COEFFS' comment */
static float32_t s_chf_i_state[CHF_STAGES * 4U];
static float32_t s_chf_q_state[CHF_STAGES * 4U];
static float32_t s_alpf_state[ALPF_STAGES * 4U];

/* Las instancias y el estado del filtro de audio de AM y banda lateral:
 * TRES por separado -4K0, 2K3 y 1K8-. El juego de diseño esta en el
 * comentario de ALPF_4K0_COEFFS de mas arriba y el selector en si en el
 * de demod_am_set_audio_bw() de demod_am.h. Son instancias
 * independientes en vez de reapuntar una sola compartida a otros
 * coeficientes en cada cambio, y por un motivo: el buffer de estado del
 * biquad de CMSIS guarda el historico de x[n-1], x[n-2], y[n-1] e
 * y[n-2] de cada etapa, que solo es valido para los coeficientes que lo
 * produjeron. Cambiar los coeficientes por debajo de un estado vivo
 * seria darle a la ecuacion en diferencias del filtro NUEVO el
 * historico rancio del VIEJO, y eso chasquea unas cuantas muestras en
 * cada cambio. Con tres instancias enteras el problema no existe: cada
 * una solo ve datos a traves de sus propios coeficientes, asi que no hay
 * nada que resincronizar cuando demod_am_process_raw() cambia de cual
 * usa la SALIDA. El mismo razonamiento en el que ya se apoyan las
 * instancias aparte del WFM y el NFM, solo que aplicado a un selector en
 * ejecucion y no a un cambio de modo. */
static arm_biquad_casd_df1_inst_f32 s_alpf_4k0_inst;
static arm_biquad_casd_df1_inst_f32 s_alpf_2k3_inst;
static arm_biquad_casd_df1_inst_f32 s_alpf_1k8_inst;
static float32_t s_alpf_4k0_state[ALPF_4K0_STAGES * 4U];
static float32_t s_alpf_2k3_state[ALPF_2K3_STAGES * 4U];
static float32_t s_alpf_1k8_state[ALPF_1K8_STAGES * 4U];

/*
 * EL FILTRO DE DOS CORTES - etapa 34, 24/09/2026.
 *
 * Los tres de arriba (4K0/2K3/1K8) eran paso BAJO y estaban precalculados.
 * Este se DISEÑA en marcha a partir de los dos cortes que pida el
 * operador - ver audiofil.h-, asi que sus coeficientes cambian mientras
 * la radio suena.
 *
 * Y AHI ESTA LO DELICADO: los coeficientes los lee la interrupcion de
 * audio. Escribir quince floats encima de los que esta usando deja al
 * filtro, durante unas muestras, con media cascada vieja y media nueva -
 * que no es un filtro, es cualquier cosa, y con polos de cualquier cosa.
 *
 * Por eso hay DOS juegos completos y un indice. Se construye en el que NO
 * se esta usando y luego se cambia el indice, que es una sola escritura
 * de una palabra. La interrupcion lee el indice una vez por bloque y a
 * partir de ahi todo lo que toca es coherente.
 *
 * Cambiar de juego tira el historial del biquad, o sea que se oye un
 * chasquido al mover el filtro. Es honrado: mover un filtro chasquea
 * siempre, y lo unico que se podria hacer para evitarlo -copiar el
 * historial- seria darle al filtro nuevo una historia que no es la suya.
 */
/* Declarada aqui arriba porque el filtro la necesita y su definicion de
 * verdad viene mas abajo: los cortes estan diseñados PARA una tasa. */
static uint8_t s_active_rate_is_48k;
static void fil_construye(uint8_t fam);

#define FIL_JUEGOS 2U
static float32_t s_fil_coef[FIL_JUEGOS][AUDIOFIL_COEFS];
static float32_t s_fil_state[FIL_JUEGOS][AUDIOFIL_ETAPAS * 4U];
static arm_biquad_casd_df1_inst_f32 s_fil_inst[FIL_JUEGOS];
static volatile uint8_t s_fil_act;    /* cual usa la interrupcion */

/*
 * Los cortes, uno por familia de modo. Que cada familia recuerde el suyo
 * es lo que hace util tener dos mandos: el filtro que quieres en banda
 * lateral no es el que quieres en AM, y tener que reajustarlo cada vez
 * que cambias de modo convierte un ajuste fino en una molestia.
 *
 * Los valores de fabrica son los de SIEMPRE -4 kHz por arriba y nada por
 * abajo, que es exactamente lo que hacia AUDIO_BW_4K0- para que quien no
 * toque esto no note ningun cambio. NFM se queda con sus 6 kHz de
 * siempre, ahora ajustables.
 */
static uint16_t s_fil_lo[FIL_FAM_N] = {    0U,    0U,    0U,    0U };
static uint16_t s_fil_hi[FIL_FAM_N] = { 4000U, 4000U, 4000U, 6000U };
static uint8_t  s_fil_fam_puesta = 0xFFU;   /* la que hay construida */

/*
 * FILTRO DE CW - 22/09/2026, a peticion del usuario del proyecto.
 *
 * Paso banda de unos 500 Hz centrado en el tono de CW, que es lo que
 * lleva cualquier radio de telegrafia y lo que le faltaba a esta: el
 * filtro mas estrecho que habia eran 1800 Hz, o sea que en CW se oian
 * todas las estaciones de un trozo de banda a la vez.
 *
 * ES PARA EL OIDO, NO PARA EL DECODIFICADOR, y conviene tenerlo claro:
 * cw.c se alimenta de s_ssb_dec, que esta ANTES de este filtro en la
 * cadena (ver el comentario de la integracion del CW), asi que esto no
 * cambia ni para bien ni para mal lo que decodifica. Lo que cambia es
 * poder encontrar la estacion de oido y no tener que hacerlo mirando el
 * osciloscopio.
 *
 * Los coeficientes se calculan en marcha y no salen de una tabla, porque
 * el centro sigue al tono ajustado y ese lo mueve el usuario. Formula de
 * paso banda de Robert Bristow-Johnson con Q 0,9 en dos secciones, que
 * medido da 499 Hz de anchura a -3 dB y ganancia 1,000 en el centro a
 * las dos frecuencias de muestreo.
 */
/*
 * CUATRO etapas, no dos - 22/09/2026, pedido por el usuario: en CW hacia
 * falta un ancho mas estrecho, porque se colaban varias senales a la vez.
 *
 * El ancho no era el unico problema, ni el principal. Medido con
 * sim/cwbw.c, que evalua ESTOS coeficientes:
 *
 *   etapas   -3 dB     -20 dB    -40 dB
 *      2     250 Hz    1167 Hz   3851 Hz
 *      4     250 Hz     846 Hz   1725 Hz
 *      6     250 Hz     769 Hz   1364 Hz
 *
 * O sea: con dos etapas, aunque estreches el filtro a 250 Hz, una senal a
 * 1,9 kHz del tono sigue entrando a -40 dB, que se oye perfectamente de
 * fondo. La falda es lo que decide cuantas senales escuchas a la vez, no el
 * numero de -3 dB. Cuatro etapas la parten por la mitad; de seis en adelante
 * ya casi no se gana.
 *
 * Cuesta dos biquads mas por muestra de audio. arm_biquad_cascade_df1_f32()
 * son unos 10 ciclos por etapa y muestra: a 96 kHz, 2 Mciclos/s de 200, o
 * sea un 1% del reloj, y solo en CW.
 */
/*
 * DOS JUEGOS TAMBIEN AQUI - revision a fondo del 08/10/2026.
 *
 * Esto era UN solo juego de coeficientes y alpf_cw_build() los reescribia
 * encima, con la interrupcion de audio viva. Es exactamente la averia que
 * el comentario de s_fil_coef -unas lineas mas arriba- describe y evita
 * desde el 24/09, solo que al filtro de CW se le olvido darle la misma
 * solucion.
 *
 * Y AQUI DUELE MAS QUE EN EL OTRO, porque son cuatro secciones iguales en
 * cascada: si la interrupcion entra entre la escritura de a1 y la de a2,
 * la cascada entera corre con el a1 nuevo y el a2 viejo. Medido sobre la
 * formula de arriba, con el tono en 800 Hz y bajando el ancho de 500 a
 * 450 Hz, los polos de esa mezcla salen en 1,04; entre dos de los anchos
 * que ofrece la radio, en 1,21. Un polo fuera del circulo es una
 * exponencial creciente: unas pocas muestras bastan para llevar el audio
 * al tope de escala, y eso es el chasquido fuerte que se oye al mover el
 * mando de ancho o el de tono en CW.
 *
 * El arreglo es el mismo de s_fil_coef y por las mismas razones: se
 * construye en el juego que NO suena y luego se cambia el indice, que es
 * una sola escritura de una palabra. La interrupcion lo lee una vez por
 * bloque y a partir de ahi todo lo que toca es coherente. Lo que queda es
 * el chasquido honrado de tirar el historial del biquad, que es el que
 * hace cualquier radio al mover un filtro.
 */
#define ALPF_CW_STAGES 4U
static arm_biquad_casd_df1_inst_f32 s_alpf_cw_inst[FIL_JUEGOS];
static float32_t s_alpf_cw_state[FIL_JUEGOS][ALPF_CW_STAGES * 4U];
static float32_t s_alpf_cw_coeffs[FIL_JUEGOS][ALPF_CW_STAGES * 5U];
static volatile uint8_t s_alpf_cw_act;   /* cual usa la interrupcion */
static float     s_alpf_cw_hz = CONFIG_CW_PITCH_HZ;
/* Ancho a -3 dB del filtro de CW. 500 Hz de partida, que es el ancho
 * clasico de un filtro de CW de radio comercial. */
static float     s_alpf_cw_bw = 500.0f;

/* WFM audio LPF instance/state - separate from s_alpf_inst above
 * (NFM), see WFM_ALPF_COEFFS' comment for why. */
static arm_biquad_casd_df1_inst_f32 s_wfm_ifbw_i_inst;
static arm_biquad_casd_df1_inst_f32 s_wfm_ifbw_q_inst;
static arm_biquad_casd_df1_inst_f32 s_wfm_alpf_wide_inst;
static arm_biquad_casd_df1_inst_f32 s_wfm_alpf_norm_inst;
static arm_biquad_casd_df1_inst_f32 s_wfm_alpf_narrow_inst;
static float32_t s_wfm_ifbw_i_state[WFM_IFBW_STAGES * 4U];
static float32_t s_wfm_ifbw_q_state[WFM_IFBW_STAGES * 4U];
static float32_t s_wfm_alpf_wide_state[WFM_ALPF_STAGES * 4U];
static float32_t s_wfm_alpf_norm_state[WFM_ALPF_STAGES * 4U];
static float32_t s_wfm_alpf_narrow_state[WFM_ALPF_STAGES * 4U];

/* NFM channel filter instance/state - separate from s_chf_i/q_inst
 * above (AM/SSB) - see NFM_CHF_COEFFS' comment for why NFM can't just
 * reuse CHF_COEFFS' narrower corner. NFM has no audio-LPF instance of
 * its own (unlike WFM) - it reuses s_alpf_inst, see demod_am.h's NFM
 * note. */
/* La nota que deja demod_am_chf_ancho() y que aplica la interrupcion:
 * 0 = nada que hacer, 1 = cargar el de AIS, 2 = cargar el de NFM. */
static volatile uint8_t s_ais_chf_pide;
static arm_biquad_casd_df1_inst_f32 s_nfm_chf_i_inst;
static arm_biquad_casd_df1_inst_f32 s_nfm_chf_q_inst;
static float32_t s_nfm_chf_i_state[NFM_CHF_STAGES * 4U];
static float32_t s_nfm_chf_q_state[NFM_CHF_STAGES * 4U];

/*
 * Carga el filtro de canal de NFM con los coeficientes que tocan: los de
 * NFM de siempre, o los anchos de AIS si el modo esta puesto.
 *
 * arm_biquad_cascade_df1_init_f32() pone a cero el estado que le das, asi
 * que llamar a esto ademas deja el filtro sin transitorios - que es justo
 * lo que hace falta al entrar y salir del modo.
 */
static void nfm_chf_carga(uint8_t es_48k)
{
    const float32_t *c;

    if (s_ais_chf) { c = es_48k ? AIS_CHF_48K_COEFFS : AIS_CHF_96K_COEFFS; }
    else           { c = es_48k ? NFM_CHF_48K_COEFFS : NFM_CHF_96K_COEFFS; }

    arm_biquad_cascade_df1_init_f32(&s_nfm_chf_i_inst, NFM_CHF_STAGES, c,
                                    s_nfm_chf_i_state);
    arm_biquad_cascade_df1_init_f32(&s_nfm_chf_q_inst, NFM_CHF_STAGES, c,
                                    s_nfm_chf_q_state);
}

/*
 * Y SE PIDE DESDE FUERA, PERO SE APLICA DENTRO - 09/10/2026.
 *
 * Esto lo llama el bucle principal al entrar y salir de AIS. Y AIS, NFM y
 * APRS son los tres DEMOD_MODE_NFM a la misma tasa, asi que
 * apply_demod_mode() no toca el gancho de audio: la interrupcion sigue
 * corriendo y sigue filtrando CON ESTE MISMO filtro mientras
 * arm_biquad_cascade_df1_init_f32() le cambia los coeficientes y le pone el
 * array de estado a cero. Las dos cosas se entrelazan y el filtro acaba con
 * un estado que no es ni el viejo ni el nuevo: un golpe audible en NFM al
 * conmutar, y la primera rafaga de AIS pasando por un filtro descuadrado -
 * justo lo que el comentario del sitio donde se llama dice que este orden
 * existe para evitar.
 *
 * Este fichero ya resuelve dos veces el mismo problema bien: fil_construye()
 * y alpf_cw_build() montan el juego NUEVO aparte y lo ponen en servicio con
 * UNA escritura de una palabra. Aqui no hay dos juegos que alternar -el
 * estado es el mismo array- asi que se hace al reves y sale mas barato: se
 * deja una NOTA de una palabra, y quien la aplica es la propia
 * interrupcion, al principio del bloque, donde no hay nadie filtrando.
 */
void demod_am_chf_ancho(uint8_t si)
{
    s_ais_chf_pide = si ? 1U : 2U;   /* 1 = ancho de AIS, 2 = el de NFM */
}

/* FM discriminator state: previous block's last complex sample,
 * carried across blocks so the delay-and-conjugate-multiply (see
 * demod_am.h's WFM note) has a valid z[n-1] for n=0 of every new
 * block, same pattern as the SSB delay-match history below. SHARED
 * BETWEEN WFM AND NFM (added 31/07/2026) - both call the same
 * fm_discriminate() helper and are never active at once, so one set
 * of state is enough; see demod_am.h's NFM note. WFM de-emphasis
 * state (single-pole LPF, see WFM_DEEMPH_ALPHA) is NFM-irrelevant
 * (NFM doesn't de-emphasize, see demod_am.h) and stays WFM-only. */
static float32_t s_fm_i_prev;
static float32_t s_fm_q_prev;
static float s_wfm_deemph_y1;
/* La continua que se le esta quitando a la I y a la Q antes del
 * discriminador - 11/10/2026. El porque, las medidas y la constante de
 * tiempo estan en el comentario largo delante de fm_discriminate(); aqui
 * solo viven porque demod_am_init() las pone a cero y esta mas arriba. */
#define WFM_DC_ALFA (1.0f / 512.0f)   /* ~1,4 s con bloques de 2,667 ms */
/*
 * EL TOPE DE LA CORRECCION, Y ES LO QUE HACE QUE ESTO SEA SEGURO.
 *
 * Sin mezclador -esta cadena no lo lleva, a proposito: ver su comentario-
 * la portadora de WFM VIVE EN CONTINUA. Y entonces un quitador de continua
 * no puede distinguir el offset del convertidor de una portadora sin
 * modular: las dos son un vector quieto. No es un detalle de
 * implementacion, es una ambiguedad de verdad.
 *
 * El primer intento de esto no llevaba tope, y el banco lo cazo. Portadora
 * FUERTE, offset CERO, modulacion cada vez mas floja -o sea un pasaje
 * tranquilo, que es cuando mas se nota lo que suene mal-:
 *
 *   beta   J0(beta)   sin tope   con tope 512   antes de todo esto
 *   2,0      0,224       0,7 %        0,1 %          0,0 %
 *   0,8      0,846      17,7 %        0,0 %          0,0 %
 *   0,4      0,960      33,7 %        0,0 %          0,0 %
 *   0,2      0,990      44,8 %        0,0 %          0,0 %
 *
 * (beta = desviacion / frecuencia de audio; J0(beta) es la parte de la
 * senal que esta en continua. Con beta pequena casi TODA la senal es
 * continua, y quitarla es quitar la emisora.)
 *
 * Lo que deshace la ambiguedad no es mas matematica, es saber de que
 * tamano son las cosas: el offset de un convertidor son decenas o unos
 * pocos cientos de cuentas, y una portadora utilizable son miles. Asi que
 * la correccion se ACOTA a lo que un offset puede ser. Pasado el tope, lo
 * que hay delante ya no es un offset.
 *
 * 512 cuentas salieron del barrido de arriba: es el mayor que sigue
 * dejando el caso malo igual que estaba (0,1 % contra 0,0 %), y cubre de
 * sobra el offset de un codec. Con 1.024 ya se nota (0,3 %).
 */
#ifndef WFM_DC_TOPE
#define WFM_DC_TOPE 512
#endif
static float s_wfm_dc_i;
static float s_wfm_dc_q;
/* Bloques vistos desde el ultimo arranque, topado. Sirve para que al
 * principio el filtro sea una MEDIA CORRIENTE -alfa = 1/n, que es la
 * convergencia mas rapida que existe sin sesgo- y solo despues se quede en
 * el seguimiento lento de WFM_DC_ALFA. Sin esto, arrancar costaba segundo
 * y medio de emisora debil sin aparecer; con esto aparece en cuanto hay
 * audio. Es el mismo truco de arranque que usa el AGC de este fichero. */
static uint16_t s_wfm_dc_n;

/*
 * WFM's OWN buffers/state, at its OWN 512-sample/192kHz block size -
 * added 05/08/2026 when WFM got its own separate processing path
 * (demod_wfm_process_raw()), independent of AM/SSB/NFM's 128-sample/
 * 48kHz s_i_buf/s_q_buf/s_env/s_audio_out/s_dcb_x1/y1/s_agc_peak
 * above (see sdr_rx.h's SDR_RX_BLOCK_SAMPLES_MAX comment for why this
 * project went with fully separate buffers per rate rather than
 * making every shared buffer size-aware - the separate-path
 * decision). s_wfm_alpf_inst/state and s_fm_i_prev/q_prev and
 * s_wfm_deemph_y1 just above are REUSED as-is, not duplicated here -
 * CMSIS biquad DF1 state is always 4 floats/stage regardless of block
 * size, and the FM discriminator/de-emphasis states are single
 * scalars either way, so none of those needed a WFM-specific twin.
 */
/*
 * ======================================================================
 * LOS DOS CAMINOS COMPARTEN LOS BUFFERS - 10/10/2026
 * ======================================================================
 *
 * Eran ocho arrays: cuatro para WFM a 192 kHz (8.192 bytes) y cuatro para
 * AM/SSB/NFM a 96 kHz (4.096). El comentario de arriba explicaba por que
 * estaban separados -la ruta separada- y era una decision de claridad,
 * no de necesidad: cuesta 4.096 bytes de los 196.608 que tiene la radio.
 *
 * Y NO PUEDEN ESTAR VIVOS A LA VEZ. No es una suposicion:
 *
 *   - cada juego lo usa UNA sola funcion, y nada mas en todo el fichero:
 *     los de WFM solo demod_wfm_process_raw(), los otros solo
 *     demod_am_process_raw(). Comprobado con grep y vigilado en cada
 *     compilacion por tools/via_audio_check.py, que es lo que hace que
 *     esto siga siendo verdad el dia que alguien añada un getter;
 *   - y las dos funciones son EL MISMO GANCHO: main.c pone una o la otra
 *     con sdr_rx_set_block_hook() (ver "PRIMERO ARMAR, DESPUES ENGANCHAR"
 *     en main.c), que es UNA escritura de puntero. La interrupcion llama a
 *     la que haya, nunca a las dos.
 *
 * EL UNICO HUECO, y se cierra solo: si la interrupcion esta a mitad de un
 * bloque cuando main.c cambia el gancho, ese bloque acaba escribiendo en
 * bytes que el otro camino va a usar. Son 256 muestras de audio -5 ms- en
 * un cambio de modo que YA silencia 200 bloques a proposito
 * (WFM_SETTLE_MUTE_BLOCKS). O sea que cae dentro del silencio que ya habia.
 *
 * Medido: 190.991 -> 186.895 bytes de .bss. 4.096 exactos.
 *
 * LO QUE FALTA, Y SE DICE: demod_am.c es el unico fichero grande del arbol
 * que NINGUN banco compila -tiene 37 cabeceras propias- asi que esto no
 * esta probado en marcha, solo comprobado estaticamente. Es la deuda que
 * deja este cambio.
 */
static union {
    struct {
        float32_t i_buf[SDR_RX_BLOCK_SAMPLES_WFM];
        float32_t q_buf[SDR_RX_BLOCK_SAMPLES_WFM];
        float32_t env[SDR_RX_BLOCK_SAMPLES_WFM];
        int16_t   audio_out[SDR_RX_BLOCK_SAMPLES_WFM * 2U];
    } wfm;
    struct {
        float32_t i_buf[SDR_RX_BLOCK_SAMPLES];
        float32_t q_buf[SDR_RX_BLOCK_SAMPLES];
        float32_t env[SDR_RX_BLOCK_SAMPLES];
        int16_t   audio_out[SDR_RX_BLOCK_SAMPLES * 2U];
    } est;                      /* estrecho: AM, SSB, NFM */
} s_via;

#define s_wfm_i_buf      (s_via.wfm.i_buf)
#define s_wfm_q_buf      (s_via.wfm.q_buf)
#define s_wfm_env        (s_via.wfm.env)
#define s_wfm_audio_out  (s_via.wfm.audio_out)

/* Que el juego de WFM siga siendo el mayor: si algun dia el estrecho
 * creciera por encima, la union crecería y los 4.096 se perderian sin que
 * nadie se entere. */
_Static_assert(sizeof s_via.est <= sizeof s_via.wfm,
               "el camino estrecho se ha hecho mas grande que el de WFM: la union ya no sale gratis");
static float s_wfm_dcb_x1;
static float s_wfm_dcb_y1;
static float s_wfm_agc_peak = WFM_AGC_PEAK_MIN;
static volatile uint32_t s_wfm_last_cycles; /* mirrors demod_am_get_last_cycles() for WFM's own path */

/* Diagnostic-only: logs min/max at 3 checkpoints (raw discriminator
 * output, post-filter-chain pre-AGC, final int16 output) for the
 * first several demod_wfm_process_raw() calls after each WFM entry -
 * added 05/08/2026 to chase the "first WFM entry after boot is
 * silent, second one works" report, once the write_half()/DMA-
 * position theory (gd32_i2s.c) and the PLL-settling-time theory
 * (aic3204_set_rate_power_up()) were BOTH ruled out by real hardware
 * A/B logs - this checks whether the actual DEMODULATED AUDIO differs
 * between a failing and a working entry, to tell an RF/discriminator/
 * AGC problem apart from a TX/DMA problem the other diagnostics
 * didn't catch. Reset by demod_wfm_reset_diag() - see its own comment
 * for why main.c calls it right when switching INTO WFM. */
static uint32_t s_wfm_diag_count;

/*
 * *** 05/08/2026, APAGADO: devuelve siempre 0 ***. Este registro bloque
 * a bloque -valores del discriminador en crudo, despues del filtro, del
 * pico del automatico de ganancia y de la salida, mas el volcado de I/Q
 * en crudo condicionado a s_wfm_diag_count == 1- hizo su trabajo: es lo
 * que identifico como convergia el automatico de ganancia y, por el
 * volcado de I/Q, el fallo de sincronismo de trama del I2S que ya
 * arregla la logica de reintento de rx_lock en main.c.
 *
 * Pero despues los registros de la placa enseñaron que el problema era
 * el propio registro: con el arreglo de rx_lock garantizando una
 * captura limpia, el usuario seguia oyendo el WFM muy distorsionado y a
 * trompicones, y el diagnostico de write_half() de gd32_i2s.c -que
 * tambien se quito, ver su propia historia- enseñaba "LA MISMA MITAD
 * ESCRITA DOS VECES" disparandose en rachas apretadas justo despues de
 * cada uno de los bloques que ESTA funcion registraba. debug_print() y
 * debug_print_dec() salen por un puerto serie lento y bloqueante, y los
 * puntos de control de esta funcion sueltan varios por bloque
 * registrado, de forma sincrona dentro de la cadena de interrupcion de
 * audio, cada 2,67 ms. Apilando bastantes seguidos se come ese
 * presupuesto y desincroniza el propio buffer de ping-pong que se esta
 * reproduciendo, que se oye exactamente como el entrecortado que se
 * estaba persiguiendo. Resumiendo: el instrumento estaba midiendo su
 * propia huella.
 *
 * s_wfm_diag_count sigue incrementandose en quien llama -se deja como
 * contador libre de bloques por si algo lo necesita mas adelante-; lo
 * unico que se ha apagado aqui es la salida por el puerto serie. Si
 * algun dia hay que volver a esta clase de fallo, se cambia el
 * `return 0U;` de abajo por la comprobacion de rango original -esta en
 * el historico- y se interpretan los resultados con esta misma
 * advertencia delante.
 */
static uint8_t wfm_should_log_diag(void)
{
    (void)s_wfm_diag_count;
    return 0U;
}

/*
 * AM/SSB/NFM's own diagnostic counter, added 05/08/2026 after the
 * project owner's report that AM/SSB/NFM can ALSO come back broken
 * (no demodulation at all, very loud noise) specifically right after
 * a WFM session, even though the panadapter's own RF spectrum looks
 * correct at the same moment - pointing at the TX/audio-output side
 * rather than the RX/RF side. WFM's own equivalent diagnostic
 * (wfm_should_log_diag() above) already showed WFM's own AGC/filter
 * chain converging correctly, so this adds the SAME visibility to
 * demod_am_process_raw()'s side of a rate-switch transition. Reset by
 * demod_am_reset_diag() - called from main.c's apply_demod_mode()
 * whenever switching OUT of WFM (into AM/USB/LSB/NFM), mirroring
 * demod_wfm_reset_diag()'s own call on the way in. Sparse logging
 * pattern identical to wfm_should_log_diag() - see its comment - but
 * over a shorter span (100 blocks = ~267ms @ 128 samples/48kHz) since
 * AM/SSB/NFM doesn't have anything like WFM's long settle-mute window
 * to span past.
 */
static uint32_t s_am_diag_count;

/*
 * *** 05/08/2026, DISABLED (always returns 0) *** - same reasoning
 * and same fix as wfm_should_log_diag() above: this logging did its
 * job (confirmed AM/SSB/NFM's own AGC converges correctly across a
 * switch) but real-time UART output from inside the audio ISR chain
 * risks causing the exact choppiness it would be used to diagnose.
 * See wfm_should_log_diag()'s comment for the full story.
 */
static uint8_t am_should_log_diag(void)
{
    (void)s_am_diag_count;
    return 0U;
}

/*
 * AM/SSB/NFM's own settle-mute, added 05/08/2026 alongside the
 * s_mode race fix in main.c's apply_demod_mode() - fixing the race
 * (s_mode reading back as WFM/4 for the first 20-30 blocks) turned
 * out not to be the whole story: logs taken AFTER that fix still
 * showed s_agc_peak starting well above its normal steady-state range
 * right after a WFM->AM switch (e.g. 24227, vs a typical 8000-15000
 * once settled) - the SAME "transient poisons the slowly-decaying AGC
 * peak" mechanism WFM_SETTLE_MUTE_BLOCKS already exists to protect
 * against on the way INTO WFM, just never mirrored on the way OUT.
 * Same fix, same reasoning: let s_agc_peak/s_dcb_x1/y1 keep adapting
 * normally to whatever's actually arriving, only force the AUDIBLE
 * OUTPUT to silence for the first AM_SETTLE_MUTE_BLOCKS blocks. 50
 * blocks = ~133ms @ 128 samples/48kHz - shorter than WFM's 200-block
 * window since these logs show AM settling faster (steady-state
 * reached by roughly block 20-30 even without muting), but easy to
 * extend if a future log shows it's still not enough.
 */
#define AM_SETTLE_MUTE_BLOCKS 50U
static uint32_t s_am_mute_remaining;

void demod_am_reset_diag(void)
{
    s_am_diag_count = 0U;
    s_am_mute_remaining = AM_SETTLE_MUTE_BLOCKS;
}

/*
 * EL SILENCIO DE ASENTAMIENTO. Añadido el 05/08/2026 despues de que los
 * registros de la placa enseñaran que el I/Q de radiofrecuencia en
 * crudo que se captura para el panadaptador esta de verdad SATURADO
 * -clavado cerca de +/-32768- durante una ventana corta justo despues
 * de entrar en WFM en vivo, mientras que una entrada posterior -la
 * segunda de la misma sesion- captura I/Q limpio. Es un transitorio que
 * el codec y el DMA se arreglan solos en unas decenas de milesimas, y
 * que este proyecto no ha conseguido quitar del todo a nivel de
 * hardware ni de registros pese a todo lo ya intentado; esa historia
 * esta en los comentarios de aic3204_set_rate_registers() y de
 * sdr_rx_resync_spi().
 *
 * *** EL PRIMER BORRADOR ESTABA MAL, arreglado el 05/08/2026, el mismo
 * dia ***. Se saltaba la cadena ENTERA -discriminador, bloqueador de
 * continua, desenfasis, paso bajo y automatico de ganancia- durante la
 * ventana de silencio, con la teoria de que asi se protegia a
 * s_wfm_agc_peak de envenenarse con el transitorio. Los registros de la
 * placa demostraron que salia el tiro por la culata: saltarse TODO el
 * proceso significa tambien que s_wfm_agc_peak no tiene ocasion de
 * ADAPTARSE durante esa ventana, asi que seguia en su valor de arranque
 * en frio, WFM_AGC_PEAK_MIN, en el instante en que el silencio
 * terminaba. O sea que solo retrasaba exactamente la misma distorsion
 * de subida WFM_SETTLE_MUTE_BLOCKS bloques, en vez de arreglarla. El
 * motivo entero de que una SEGUNDA entrada en WFM suene limpia es que
 * s_wfm_agc_peak llega ya adaptado del final de la primera sesion: o
 * sea que adaptarse durante el rato en que no se oye es exactamente lo
 * que tiene que pasar, no lo que hay que impedir.
 *
 * LA VERSION ARREGLADA: correr la cadena ENTERA con normalidad en cada
 * bloque, para que s_wfm_agc_peak -y el estado del bloqueador de
 * continua, el desenfasis y el paso bajo- sigan adaptandose a lo que de
 * verdad esta llegando. Lo unico que se fuerza a silencio durante la
 * ventana es el ultimisimo paso, lo que se manda a
 * gd32_i2s_stream_write_half(). Para cuando se oye audio de verdad, el
 * automatico ya ha tenido WFM_SETTLE_MUTE_BLOCKS bloques para
 * converger, igual que los habria tenido de todas formas en la segunda
 * entrada.
 *
 * Lo reinicia demod_wfm_reset_diag() junto con el contador de
 * diagnostico, porque los dos tienen que volver a empezar en cada
 * entrada en WFM. Quince bloques son unos 40 ms con 512 muestras a 192
 * kHz: una suposicion de margen seguro, que convendria acortar en
 * cuanto se confirme cuanto dura de verdad el transitorio -el registro
 * de diagnostico que ya hay lo enseñaria, ahora que refleja bloques que
 * SI se estaban adaptando-.
 *
 * *** 05/08/2026: subido de 15 a 200 bloques ***. El PRIMER intento con
 * este numero -15 bloques, unos 40 ms- resulto quedarse cortisimo: un
 * registro posterior enseño el I/Q en crudo del panadaptador todavia
 * clavado y saturado en +/-32768 en la impresion de sdr_tick que sale
 * DESPUES de la ventana de silencio mas los ocho primeros bloques de
 * diagnostico, o sea unos 61 ms en total. Asi que sea lo que sea este
 * transitorio, dura mas de 61 ms, no digamos ya mas de los 40 que se
 * supusieron. Doscientos bloques son unos 520 ms: un margen mucho mas
 * generoso, pensado para quedar con seguridad por detras del
 * transitorio dure lo que dure, a costa de un silencio mas largo antes
 * de que empiece el audio de WFM -aceptable, porque un cambio de modo
 * ya trae algo de silencio de todas formas; ver el comentario de
 * apply_demod_mode() en main.c-. El registro de diagnostico de aqui
 * abajo se reparte ahora por la ventana entera -ver su propio
 * comentario- justamente para que el PROXIMO registro pueda enseñar
 * cuando acaba de verdad el transitorio, en vez de cubrir solo los
 * primeros ocho bloques como hacia antes; con eso, este numero podra
 * bajar a lo que de verdad haga falta.
 */
#define WFM_SETTLE_MUTE_BLOCKS 200U
static uint32_t s_wfm_mute_remaining;

void demod_wfm_reset_diag(void)
{
    s_wfm_diag_count = 0U;
    s_wfm_mute_remaining = WFM_SETTLE_MUTE_BLOCKS;
}

/* Local signed-decimal print helper - main.c has its own
 * debug_print_dec_signed(), but it's static there (not exported via
 * debug_uart.h), so this file needs its own copy for the diagnostics
 * below. Simplest form: print a '-' then the magnitude via the
 * shared debug_print_dec(), which only takes uint32_t. */
static void debug_print_dec_signed_local(const char *label, int32_t val)
{
    if (val < 0) {
        debug_print(label);
        debug_print(" = -");
        debug_print_dec("", (uint32_t)(-val));
    } else {
        debug_print_dec(label, (uint32_t)val);
    }
}

/* SSB decimated-chain instances + state (see the PIPELINE comment
 * above DECIM_COEFFS_96K). CMSIS state sizes:
 *   decimate:    numTaps + blockSize(INPUT, 256) - 1
 *   plain FIR:   numTaps + blockSize(decimated) - 1
 *   interpolate: (numTaps/L) + blockSize(decimated) - 1
 * I and Q each need their own decimator state (same coefficients,
 * independent signals - same pattern as the channel filter pair).
 *
 * *** 01/09/2026: sized to the LARGER of the 96K/48K requirements,
 * not just the 96K one *** - see DEC_BLOCK_SAMPLES_MAX's own comment
 * for why the two rates need genuinely different amounts here this
 * time (unlike every previous Fs move). DECIM_STATE_SIZE_MAX happens
 * to land on the 96K case (more taps there, 96 vs 41, outweighs its
 * smaller decimated block size) - HILBERT_STATE_SIZE_MAX and
 * INTERP_STATE_SIZE_MAX both land on the 48K case instead (same or
 * fewer taps, but a decimated block that's TWICE as big at 48K -
 * DEC_BLOCK_SAMPLES_48K=64 vs DEC_BLOCK_SAMPLES_96K=32 - dominates).
 * Verified by direct computation, not assumed: 96K decim-state=351 vs
 * 48K's 296 (96K wins); 96K hilbert-state=94 vs 48K's 126 (48K wins);
 * 96K interp-state=43 vs 48K's 73 (48K wins).
 */
#define DECIM_STATE_SIZE_MAX (DECIM_COEFFS_96K_TAPS + SDR_RX_BLOCK_SAMPLES - 1U) /* 351 */
#define HILBERT_STATE_SIZE_MAX (HILBERT_COEFFS_TAPS + DEC_BLOCK_SAMPLES_MAX - 1U) /* 126 */
#define INTERP_STATE_SIZE_MAX ((INTERP_COEFFS_48K_TAPS / DECIM_FACTOR_48K) + DEC_BLOCK_SAMPLES_MAX - 1U) /* 73 */
static arm_fir_decimate_instance_f32 s_decim_i_inst;
static arm_fir_decimate_instance_f32 s_decim_q_inst;
static arm_fir_instance_f32          s_hilbert_inst;
static arm_fir_interpolate_instance_f32 s_interp_inst;
/* *** 01/09/2026: mudados a la RAM del nucleo (TCM) ***. Son los
 * buffers de ESTADO de los FIR de CMSIS -diezmado, interpolado y
 * Hilbert-, y el DMA no los toca nunca: solo los leen y escriben
 * arm_fir_decimate_f32(), arm_fir_interpolate_f32() y arm_fir_f32(),
 * que son llamadas normales de CPU desde el codigo de esta misma
 * interrupcion. El por que entero esta en el comentario mas largo
 * sobre el TCM de fft.c: hacia falta soltar RAM principal para
 * ensanchar el panel de la cascada. */
#define TCMRAM_BSS __attribute__((section(".tcmram")))
static float32_t s_decim_i_state[DECIM_STATE_SIZE_MAX] TCMRAM_BSS;
static float32_t s_decim_q_state[DECIM_STATE_SIZE_MAX] TCMRAM_BSS;
static float32_t s_hilbert_state[HILBERT_STATE_SIZE_MAX] TCMRAM_BSS;
static float32_t s_interp_state[INTERP_STATE_SIZE_MAX] TCMRAM_BSS;

/* Decimated-rate (12kHz) working buffers - sized to
 * DEC_BLOCK_SAMPLES_MAX(64, the 48K case) now, not a single fixed
 * DEC_BLOCK_SAMPLES the way every previous Fs move left this - only
 * the first s_dec_block_samples (32 or 64, whichever rate is
 * currently active - see demod_am_set_active_rate()) entries of each
 * are actually used/valid in any given block. */
static float32_t s_i_dec[DEC_BLOCK_SAMPLES_MAX];         /* decimated I */
static float32_t s_q_dec[DEC_BLOCK_SAMPLES_MAX];         /* decimated Q */
static float32_t s_q_hilbert_out[DEC_BLOCK_SAMPLES_MAX]; /* Hilbert(Q), 90deg-shifted, @ 12kHz */
static float32_t s_ssb_dec[DEC_BLOCK_SAMPLES_MAX];       /* combined SSB audio @ 12kHz */

/*
 * --- NR INTEGRATION (Spectral Subtraction, 03/08/2026, revised 04/08/2026) ---
 *
 * AM/USB/LSB, mainly SSB - per the project owner, explicitly NOT
 * WFM/NFM. This decimator/interpolator pair is used for AM ONLY as of
 * 04/08/2026 - USB/LSB instead run NR inline on s_ssb_dec, the SSB
 * chain's OWN already-12kHz buffer (see step 2d's comment in
 * demod_am_process_raw()), which avoids interpolating SSB's audio up
 * to 192kHz only to immediately decimate it back down again just for
 * NR - a wasted round trip that was pushing SSB+NR up against the
 * ISR's cycle budget - the project owner reported SSB with NR already
 * running up against the controller's limit. AM has no
 * equivalent already-decimated buffer (its envelope only ever exists
 * at the full s_env[]), so it still needs this dedicated pair. Same
 * coefficients as the SSB chain above either way (passed by pointer,
 * not duplicated - CMSIS's init functions don't copy the coefficient
 * array), just this is a MONO signal (s_env[], not I/Q), hence its own
 * instances rather than sharing the SSB chain's I/Q pair. This is also
 * what gets nr_ss.c's Nstfft down to 128 instead of the 2048 a naive
 * full-192kHz-rate port would have needed - see nr_ss.c's header
 * comment for the full reasoning.
 *
 * Gated on nr_ss_get_enabled() (a genuine on/off switch, the bottom
 * bar's NR button - see nr_ss_set_enabled()'s comment in nr_ss.h) -
 * skipped ENTIRELY while off, not just a transparent bypass: zero
 * extra ISR cycles spent, for both the AM path here and USB/LSB's
 * inline path in step 2d. */
static arm_fir_decimate_instance_f32    s_nr_decim_inst;
static arm_fir_interpolate_instance_f32 s_nr_interp_inst;
static float32_t s_nr_decim_state[DECIM_STATE_SIZE_MAX] TCMRAM_BSS;
static float32_t s_nr_interp_state[INTERP_STATE_SIZE_MAX] TCMRAM_BSS;
static float32_t s_nr_buf[DEC_BLOCK_SAMPLES_MAX]; /* decimated audio (AM only), in place through nr_ss_process() */

/* nr_ss.h fixes its own block size independently (see its comment on
 * NR_SS_BLOCK_SAMPLES) - previously guaranteed via a compile-time
 * assert that it never silently drifted from demod_am.c's actual
 * decimated rate/block size, instead of failing subtly (wrong-length
 * overlap-add) at runtime.
 *
 * *** 01/09/2026: assert REMOVED, real bug avoided *** - with the
 * 48kHz rate option, DEC_BLOCK_SAMPLES_48K(64) genuinely differs from
 * NR_SS_BLOCK_SAMPLES(32), so a single compile-time equality no longer
 * holds for both rates - and nr_ss.h's own contract is strict about
 * this ("n must equal NR_SS_BLOCK_SAMPLES every call"), so simply
 * passing the (now variable) decimated block size straight through,
 * the way both call sites used to, would have silently fed nr_ss_
 * process() 64 samples at 48kHz when it can only correctly digest 32
 * per call - not a crash, a quiet loss of NR on half the block's
 * audio (and probably wrong-length overlap-add reconstruction on the
 * other half too). Fixed properly: nr_ss_process_chunks() below loops
 * calling nr_ss_process() exactly NR_SS_BLOCK_SAMPLES at a time,
 * however many chunks the active rate's decimated block actually
 * contains - 1 chunk at 96kHz (unchanged behavior), 2 at 48kHz. Both
 * call sites use it now instead of calling nr_ss_process() directly.
 */
static void nr_ss_process_chunks(float32_t *buf, uint32_t n)
{
    uint32_t off;
    for (off = 0U; off + NR_SS_BLOCK_SAMPLES <= n; off += NR_SS_BLOCK_SAMPLES) {
        nr_ss_process(buf + off, NR_SS_BLOCK_SAMPLES);
    }
}

/*
 * rtty_process_chunks() - added 01/09/2026, per the project owner:
 * RTTY decode showed no output at all with the 48kHz rate option
 * selected. Root cause: exactly the same class of bug nr_ss_process_
 * chunks() above already fixed - rtty.h's RTTY_BLOCK_SAMPLES(32) is a
 * fixed contract ("n must equal RTTY_BLOCK_SAMPLES every call"), and
 * rtty_process() itself enforces it at runtime (rtty.c: "if (n !=
 * RTTY_BLOCK_SAMPLES) { return; }") - silently doing NOTHING rather
 * than crashing or reading out of bounds. At 48kHz, s_dec_block_
 * samples is 64, not 32 - every single call was hitting that guard
 * and bailing out immediately, hence "no output at all" rather than
 * degraded/partial output. Same fix as NR: loop calling rtty_process()
 * exactly RTTY_BLOCK_SAMPLES at a time - 1 chunk at 96kHz (unchanged
 * behavior, byte-for-byte the same as before this function existed),
 * 2 at 48kHz. (rtty_scope_feed(), used right alongside this at the
 * same call site, did NOT need this treatment - it already loops
 * against a bounds check rather than requiring an exact n, genuinely
 * tolerant of either 32 or 64 as-is.)
 */
/*
 * Lo mismo que rtty_process_chunks() justo debajo, y por el mismo
 * motivo: cw_process() tiene el mismo contrato estricto de bloque
 * exacto, y si se le da otra cosa no hace nada en vez de decodificar
 * medio bloque. Son dos funciones casi identicas a proposito: cada una
 * respeta el contrato de SU modulo, y el dia que uno cambie de tamano de
 * bloque no se lleva al otro por delante.
 */
static void cw_process_chunks(const float32_t *buf, uint32_t n)
{
    uint32_t off;

    for (off = 0U; off + CW_BLOCK_SAMPLES <= n; off += CW_BLOCK_SAMPLES) {
        cw_process(buf + off, CW_BLOCK_SAMPLES);
    }
}

static void rtty_process_chunks(const float32_t *buf, uint32_t n)
{
    uint32_t off;
    for (off = 0U; off + RTTY_BLOCK_SAMPLES <= n; off += RTTY_BLOCK_SAMPLES) {
        rtty_process(buf + off, RTTY_BLOCK_SAMPLES);
    }
}

/* Delay-matching for decimated I: same technique as before (linear-
 * phase FIR group delay = (numTaps-1)/2, history carried across
 * blocks), just at the 12kHz rate now - so the history is only 31
 * samples of the 32-sample decimated block. */
static float32_t s_i_delay_hist[HILBERT_GROUP_DELAY_DEC];
static float32_t s_i_delayed[HILBERT_GROUP_DELAY_DEC + DEC_BLOCK_SAMPLES_MAX];

/* El modo de demodulacion; ver demod_am.h. Un uint8_t pelado, sin
 * seccion critica: main.c lo escribe desde el bucle principal y la
 * interrupcion lo lee una vez por bloque. Lo peor que puede pasar es que
 * un cambio de modo llegue un bloque tarde, que no se oye y no merece
 * parar la interrupcion. */
static uint8_t s_mode = (uint8_t)CONFIG_START_MODE; /* see config.h - was DEMOD_MODE_WFM - changed
                                    * 05/08/2026: harmless before WFM got its own
                                    * separate 192kHz path (demod_wfm_process_raw()),
                                    * since demod_am_process_raw() used to handle WFM
                                    * too - now that it doesn't, defaulting to WFM here
                                    * while main.c's boot sequence registers
                                    * demod_am_process_raw() as the hook (matching the
                                    * codec/DMA, which always boot at 48kHz - see
                                    * aic3204_phase2_init()) would boot into a genuine
                                    * mismatch: UI shows WFM selected, audio runs
                                    * through the 48kHz AM/SSB/NFM path regardless. If
                                    * WFM as the boot default is wanted back, it needs
                                    * to go through apply_demod_mode() in main.c
                                    * AFTER the normal 48kHz boot sequence completes,
                                    * not just this static initializer - see that
                                    * function's comment for why (it drives a real
                                    * codec/DMA reconfiguration, not just a UI label). */

void demod_am_set_mode(demod_mode_t mode)
{
    s_mode = (uint8_t)mode;
    /* Cada familia recuerda sus dos cortes, asi que cambiar de modo puede
     * cambiar de filtro. Se reconstruye AQUI y no en la interrupcion: ahi
     * costaria un diseño entero -senos, cosenos y divisiones- dentro del
     * camino de audio, y ademas solo hace falta cuando algo cambia. */
    if (demod_am_filtro_familia() != s_fil_fam_puesta) {
        fil_construye(demod_am_filtro_familia());
    }
}

demod_mode_t demod_am_get_mode(void)
{
    return (demod_mode_t)s_mode;
}

/* SAM carrier-frequency-offset state (21/08/2026) - file scope (not a
 * static local inside demod_am_process_raw()'s DEMOD_MODE_SAM branch
 * anymore) specifically so demod_am_get_sam_carrier_hz() below can
 * expose the live reading to main.c's own UI code, for the MS5351 PPM
 * calibration on-screen readout. */
static sam_t s_sam;
static uint8_t s_sam_init_done = 0u;

float32_t demod_am_get_sam_carrier_hz(void)
{
    return s_sam.carrier_hz;
}

/*
 * SI ESE NUMERO SIGNIFICA ALGO - 05/10/2026.
 *
 * El error de PLL se enseñaba siempre, hubiera portadora o no, y un numero
 * en una pantalla se cree. 269 PPM son 7 kHz de error en un cristal de 26
 * MHz: eso no es un cristal desviado, es un lazo que no esta cogido a nada.
 * Esto es lo que permite decirlo en vez de enseñar el numero igual.
 *
 * Ver sam_enganche() para de donde sale. No es un invento de aqui: es la
 * media de cuanto se mueve el detector de fase, que es exactamente la
 * diferencia entre haber enganchado y no.
 */
uint8_t demod_am_sam_enganchado(void)
{
    return sam_enganchado(&s_sam);
}

float32_t demod_am_sam_enganche(void)
{
    return sam_enganche(&s_sam);
}

/* AM/SSB audio filter width - see demod_am_set_audio_bw()'s comment in
 * demod_am.h. Same "plain uint8_t-sized enum, no critical section"
 * reasoning as s_mode above. Default AUDIO_BW_4K0 (the widest of the
 * three - see ALPF_4K0_COEFFS' comment). */
static audio_bw_t s_audio_bw = AUDIO_BW_4K0;

void demod_am_set_audio_bw(audio_bw_t bw)
{
    s_audio_bw = bw;
}

/* La familia del modo que hay puesto. CW no es una familia: tiene su
 * propio paso banda, que manda sobre esto (ver el paso 3 del proceso). */
uint8_t demod_am_filtro_familia(void)
{
    switch ((demod_mode_t)s_mode) {
    case DEMOD_MODE_AM:  return (uint8_t)FIL_FAM_AM;
    case DEMOD_MODE_SAM: return (uint8_t)FIL_FAM_SAM;
    case DEMOD_MODE_NFM: return (uint8_t)FIL_FAM_NFM;
    default:             return (uint8_t)FIL_FAM_SSB;
    }
}

/*
 * Construye el filtro de la familia `fam` en el juego que NO se esta
 * usando y lo pone en servicio. Ver el comentario de s_fil_coef.
 */
static void fil_construye(uint8_t fam)
{
    uint8_t otro = (uint8_t)(s_fil_act ^ 1U);
    float fs = s_active_rate_is_48k ? 48000.0f : 96000.0f;
    float lo, hi;

    if (fam >= (uint8_t)FIL_FAM_N) { return; }

    audiofil_disena((float)s_fil_lo[fam], (float)s_fil_hi[fam], fs,
                    s_fil_coef[otro], &lo, &hi);
    /* Los cortes que de verdad han salido se guardan de vuelta, para que
     * la pantalla enseñe lo que HAY y no lo que se pidio. */
    s_fil_lo[fam] = (uint16_t)(lo + 0.5f);
    s_fil_hi[fam] = (uint16_t)(hi + 0.5f);

    arm_biquad_cascade_df1_init_f32(&s_fil_inst[otro], AUDIOFIL_ETAPAS,
                                    s_fil_coef[otro], s_fil_state[otro]);
    s_fil_act = otro;
    s_fil_fam_puesta = fam;
}

void demod_am_set_filtro(uint8_t fam, uint16_t lo, uint16_t hi)
{
    if (fam >= (uint8_t)FIL_FAM_N) { return; }
    s_fil_lo[fam] = lo;
    s_fil_hi[fam] = hi;
    /* Solo se reconstruye si es la que esta sonando; las demas se
     * construyen solas al cambiar de modo. */
    if (fam == demod_am_filtro_familia()) { fil_construye(fam); }
    else {
        /* Aun asi hay que recortarlo ahora, no cuando se use: si no, la
         * pantalla enseñaria un valor imposible hasta que alguien
         * cambiase de modo. */
        float l, h;
        float fs = s_active_rate_is_48k ? 48000.0f : 96000.0f;
        float tmp[AUDIOFIL_COEFS];
        audiofil_disena((float)lo, (float)hi, fs, tmp, &l, &h);
        s_fil_lo[fam] = (uint16_t)(l + 0.5f);
        s_fil_hi[fam] = (uint16_t)(h + 0.5f);
    }
}

void demod_am_get_filtro(uint8_t fam, uint16_t *lo, uint16_t *hi)
{
    if (fam >= (uint8_t)FIL_FAM_N) { return; }
    if (lo) { *lo = s_fil_lo[fam]; }
    if (hi) { *hi = s_fil_hi[fam]; }
}

audio_bw_t demod_am_get_audio_bw(void)
{
    return s_audio_bw;
}

/* WFM pre-discriminator channel filter selector - see WFM_IFBW_NARROW_
 * COEFFS' comment above for what this actually does (filters the raw
 * I/Q ahead of fm_discriminate(), NOT the demodulated audio - that's
 * s_audio_bw/WFM_ALPF_*, a completely separate control). Same "plain
 * uint8_t-sized enum, no critical section" reasoning as s_mode/
 * s_audio_bw. Default WFM_IFBW_WIDE (no filter) - matches this
 * project's usual "the new control's default is the old, already-
 * validated behavior" rule (see agc_profile_cycle()'s MANUAL note,
 * the ATT tile's comment, etc.) - nobody who never touches this tile
 * gets a different signal path than before 01/09/2026. */
static wfm_ifbw_t s_wfm_ifbw = WFM_IFBW_WIDE;

/*
 * LA BANDA DE SAM: las dos, solo la de arriba o solo la de abajo.
 *
 * Por omision AMBAS, que es lo que SAM ha hecho siempre: un ajuste nuevo
 * no puede cambiarle el sonido a nadie que no lo toque.
 *
 * Es un AJUSTE y no dos modos nuevos, y eso es deliberado. Meter modos
 * obliga a tocar la tabla de modos, las chapas, las familias de filtro y
 * las listas de panel - que es la trampa de "una lista escrita a mano en
 * N sitios" con la que este fichero y main.c llevan tropezando todo el
 * proyecto (cuatro veces en un solo dia, el 02/10). El precedente bueno
 * es el ancho de canal de WFM, aqui arriba: un ajuste del modo.
 */
static uint8_t s_sam_banda = (uint8_t)SAM_BANDA_AMBAS;

/*
 * CUANTO DE LO QUE SUENA ES DEMODULACION SINCRONA Y CUANTO ENVOLVENTE.
 * 1 = sincrona del todo, 0 = AM de toda la vida. Ver el comentario largo
 * en la rama de SAM de demod_am_process_raw().
 */
static float32_t s_sam_mezcla;


void demod_am_set_sam_banda(sam_banda_t b)
{
    s_sam_banda = (uint8_t)b;
}

sam_banda_t demod_am_get_sam_banda(void)
{
    return (sam_banda_t)s_sam_banda;
}

/*
 * QUE LADO DE LA PORTADORA SE ESTA OYENDO - 15/10/2026.
 *
 * Y era cierto. La banda sombreada del espectro se decidia con una cadena
 * de if sobre el MODO, escrita en main.c cuando SAM solo tenia una forma:
 *
 *     if (modo == USB)  ... a la derecha
 *     else if (modo == LSB) ... a la izquierda
 *     else  ...  simetrica        <- "AM y SAM: doble banda lateral"
 *
 * SAM-U y SAM-L son un AJUSTE de SAM, no modos propios (ver s_sam_banda
 * aqui arriba y por que se hizo asi), asi que los dos caian en ese `else`
 * y se pintaban a dos bandas mientras la radio demodulaba una. Es
 * exactamente el mismo fallo que ya se arreglo cuatro comentarios mas
 * arriba con el ancho del filtro: la pantalla enseñando una cosa y la
 * radio haciendo otra. Quien mira la banda sombreada para saber que esta
 * oyendo, se la cree.
 *
 * LA REGLA VIVE AQUI Y NO EN main.c, y eso es la mitad del arreglo. En
 * main.c no hay banco que la mire -el simulador no enlaza main.c- y por
 * eso el fallo ha durado desde que existen SAM-U y SAM-L. Aqui se compila
 * en sim/demodtest.c y se mide.
 *
 * Devuelve +1 si solo se oye por encima de la portadora, -1 si solo por
 * debajo, y 0 si las dos.
 */
int8_t demod_am_banda_lado(void)
{
    switch (s_mode) {
    case DEMOD_MODE_USB: return  1;
    case DEMOD_MODE_LSB: return -1;
    case DEMOD_MODE_SAM:
        if (s_sam_banda == (uint8_t)SAM_BANDA_USB) { return  1; }
        if (s_sam_banda == (uint8_t)SAM_BANDA_LSB) { return -1; }
        return 0;
    default:             return 0;   /* AM, NFM, WFM: simetricos */
    }
}

/*
 * EL MARGEN DE ENGANCHE - 12/10/2026. Ver sam.h y demod_am.h.
 *
 * `volatile` y una segunda copia con lo ULTIMO APLICADO: esto lo escribe la
 * pantalla y lo lee la interrupcion, y quien tiene que tocar el sam_t es la
 * interrupcion, no la pantalla. Un uint16_t se escribe de una pieza en este
 * Cortex-M4, asi que no hace falta nada mas que las dos variables.
 */
const uint16_t k_sam_margen_hz[SAM_MARGEN_OPCIONES] = { 100U, 250U, 500U, 4000U };

static volatile uint16_t s_sam_margen_hz = SAM_MARGEN_DEF_HZ;
static uint16_t s_sam_margen_puesto;     /* el ultimo que ha visto el lazo */

void demod_am_sam_margen_set(uint16_t hz)
{
    uint8_t i;
    /* Solo uno de los cuatro: lo que llegue de un CONFIG.CSV editado a mano
     * no puede meter un margen de 50 kHz. */
    for (i = 0U; i < (uint8_t)SAM_MARGEN_OPCIONES; i++) {
        if (hz == k_sam_margen_hz[i]) { s_sam_margen_hz = hz; return; }
    }
    s_sam_margen_hz = (uint16_t)SAM_MARGEN_DEF_HZ;
}

uint16_t demod_am_sam_margen_get(void)
{
    return s_sam_margen_hz;
}

uint8_t demod_am_sam_margen_idx(void)
{
    uint8_t i;
    for (i = 0U; i < (uint8_t)SAM_MARGEN_OPCIONES; i++) {
        if (s_sam_margen_hz <= k_sam_margen_hz[i]) { return i; }
    }
    return (uint8_t)(SAM_MARGEN_OPCIONES - 1U);
}

void demod_am_set_wfm_ifbw(wfm_ifbw_t bw)
{
    s_wfm_ifbw = bw;
}

wfm_ifbw_t demod_am_get_wfm_ifbw(void)
{
    return s_wfm_ifbw;
}

/*
 * s_agc_profile/s_agc_release: same "plain aligned variable, no
 * critical section" reasoning as s_mode above - main.c writes,
 * the ISR reads once per block. s_agc_release is the ALREADY-RESOLVED
 * coefficient (not re-switched on s_agc_profile every block) so the
 * hot loop in demod_am_process_raw() is just "peak *= s_agc_release",
 * same cost as before profiles existed - the switch only happens here,
 * in demod_am_set_agc_profile(), on the rare event of the user
 * actually changing it.
 */
/*
 * s_active_rate_is_48k: which of the two AM/USB/LSB/NFM coefficient
 * sets (96kHz default, or 48kHz) is currently configured on every
 * rate-dependent CMSIS instance in this file - see demod_am_set_
 * active_rate()'s comment (declared further down, near the CMSIS
 * instances it actually reconfigures) for the full mechanism. Declared
 * here, ahead of that function, because demod_am_set_agc_profile()
 * right below also needs to consult it (AGC release coefficients are
 * rate-dependent too, independent of which CMSIS filter instances are
 * active).
 */
/* Definicion (la declaracion esta arriba, junto al filtro de dos cortes). */
static uint8_t s_active_rate_is_48k = 0U;

/*
 * Rediseña el filtro de CW centrado en `centre_hz`. Se llama al cambiar
 * el tono, al cambiar de frecuencia de muestreo y al arrancar.
 *
 * Las dos secciones son identicas: en cascada dan un paso banda de
 * cuarto orden. Ojo con el convenio de CMSIS, que espera a1 y a2 con el
 * SIGNO CAMBIADO respecto a la formula habitual - es el mismo convenio
 * que siguen las tablas de coeficientes de arriba.
 */
/*
 * Monta el filtro de CW: un paso banda centrado en el tono, del ancho
 * pedido, en ALPF_CW_STAGES etapas iguales.
 *
 * LA Q SALE DE UNA MEDIDA, NO DE UNA FORMULA DE LIBRO. La Q de UNA etapa no
 * es f0/ancho cuando hay varias en serie: cada una estrecha a la siguiente.
 * sim/cwbw.c calcula la respuesta exacta de la cascada y busca la Q que da
 * el ancho pedido; el resultado es que
 *
 *     Q = 0,434 * f0 / ancho
 *
 * con cuatro etapas, y esa constante sale igual (0,4339 a 0,4347) para
 * anchos de 150 a 1000 Hz y tonos de 400 a 1000 Hz. Por eso aqui hay una
 * multiplicacion y no una tabla. Si algun dia se cambia ALPF_CW_STAGES hay
 * que volver a medirla: para dos etapas valia 0,643.
 */
#define ALPF_CW_Q_K 0.434f

static void alpf_cw_build(void)
{
    float fs = s_active_rate_is_48k ? 48000.0f : 96000.0f;
    float Q  = ALPF_CW_Q_K * (s_alpf_cw_hz / s_alpf_cw_bw);
    float w0, alpha, a0, b0n, a1n, a2n;
    uint32_t k;
    /* El juego que NO esta sonando - revision a fondo del 08/10/2026, ver
     * el comentario de s_alpf_cw_coeffs. Antes se escribia encima del que
     * usaba la interrupcion y una cascada a medio cambiar es inestable. */
    uint8_t otro = (uint8_t)(s_alpf_cw_act ^ 1U);

    if (Q < 0.20f)  { Q = 0.20f; }
    if (Q > 20.0f)  { Q = 20.0f; }

    w0    = 2.0f * 3.14159265358979f * (s_alpf_cw_hz / fs);
    alpha = sinf(w0) / (2.0f * Q);
    a0    = 1.0f + alpha;
    b0n   =  alpha / a0;
    a1n   =  (2.0f * cosf(w0)) / a0;   /* signo ya cambiado, convenio CMSIS */
    a2n   = -(1.0f - alpha) / a0;

    for (k = 0U; k < ALPF_CW_STAGES; k++) {
        s_alpf_cw_coeffs[otro][k * 5U + 0U] =  b0n;
        s_alpf_cw_coeffs[otro][k * 5U + 1U] =  0.0f;
        s_alpf_cw_coeffs[otro][k * 5U + 2U] = -b0n;
        s_alpf_cw_coeffs[otro][k * 5U + 3U] =  a1n;
        s_alpf_cw_coeffs[otro][k * 5U + 4U] =  a2n;
    }
    arm_biquad_cascade_df1_init_f32(&s_alpf_cw_inst[otro], ALPF_CW_STAGES,
                                    s_alpf_cw_coeffs[otro], s_alpf_cw_state[otro]);
    /* Y SOLO AHORA se pone en servicio: una escritura de una palabra, con
     * el juego nuevo ya entero. Igual que fil_construye(). */
    s_alpf_cw_act = otro;
}

void demod_am_set_cw_filter_hz(float centre_hz)
{
    if (centre_hz < 200.0f)  { centre_hz = 200.0f; }
    if (centre_hz > 2000.0f) { centre_hz = 2000.0f; }
    s_alpf_cw_hz = centre_hz;
    alpf_cw_build();
}

/*
 * Ancho a -3 dB. Los tres que ofrece la radio son 1000, 500 y 250 Hz; el
 * limite de abajo no es un capricho: por debajo de unos 150 Hz el filtro
 * empieza a alargar los puntos y el decodificador lo nota antes que el oido.
 */
void demod_am_set_cw_bw_hz(float bw_hz)
{
    if (bw_hz < 150.0f)  { bw_hz = 150.0f; }
    if (bw_hz > 2000.0f) { bw_hz = 2000.0f; }
    s_alpf_cw_bw = bw_hz;
    alpf_cw_build();
}

float demod_am_get_cw_bw_hz(void) { return s_alpf_cw_bw; }

float demod_am_get_cw_filter_hz(void) { return s_alpf_cw_hz; }


static agc_profile_t s_agc_profile = AGC_PROFILE_MEDIUM;
static float s_agc_release = AGC_RELEASE_MEDIUM_96K;
/* El coeficiente de soltada ya resuelto, PROPIO del WFM, con los valores
 * de 192 kHz de WFM_AGC_RELEASE_*. Añadido el 05/08/2026 junto con
 * demod_wfm_process_raw(). Va de la mano de s_agc_release de arriba y
 * sale de la MISMA eleccion de perfil -un solo ajuste de perfil del
 * automatico de ganancia, del que se resuelven dos numeros, uno para
 * cada velocidad de muestreo- y no de un segundo ajuste independiente:
 * el operador elige "como de rapido reacciona el automatico", no "como
 * de rapido reacciona en cada modo". */
static float s_wfm_agc_release = WFM_AGC_RELEASE_MEDIUM;

void demod_am_set_agc_profile(agc_profile_t profile)
{
    s_agc_profile = profile;
    switch (profile) {
    case AGC_PROFILE_SLOW:
        s_agc_release = s_active_rate_is_48k ? AGC_RELEASE_SLOW_48K : AGC_RELEASE_SLOW_96K;
        s_wfm_agc_release = WFM_AGC_RELEASE_SLOW;
        break;
    case AGC_PROFILE_FAST:
        s_agc_release = s_active_rate_is_48k ? AGC_RELEASE_FAST_48K : AGC_RELEASE_FAST_96K;
        s_wfm_agc_release = WFM_AGC_RELEASE_FAST;
        break;
    case AGC_PROFILE_MANUAL: break; /* unused in MANUAL - see the AGC loop */
    case AGC_PROFILE_MEDIUM:
    default:
        s_agc_release = s_active_rate_is_48k ? AGC_RELEASE_MEDIUM_48K : AGC_RELEASE_MEDIUM_96K;
        s_wfm_agc_release = WFM_AGC_RELEASE_MEDIUM;
        break;
    }
}

agc_profile_t demod_am_get_agc_profile(void)
{
    return s_agc_profile;
}

/*
 * El estado del silenciador (AM y NFM), con el mismo razonamiento de
 * "variables peladas y alineadas, sin seccion critica" que s_mode y
 * s_agc_profile de arriba. demod_am_process_raw() LEE s_squelch_open en
 * cada bloque -para decidir si callar s_env[]- y tambien lo ESCRIBE,
 * porque ahi es donde se toma la decision de la histeresis. main.c no lo
 * toca nunca directamente: como mucho lo lee por el accesor para un
 * indicador en pantalla, si lo quiere.
 */
static float s_squelch_threshold_db = 0.0f; /* effectively OFF - see demod_am.h */
static float s_squelch_level_db = -100.0f;
static uint8_t s_squelch_open = 1U; /* start OPEN - matches "OFF by default" above */

void demod_am_set_squelch_db(float threshold_db)
{
    s_squelch_threshold_db = threshold_db;
}

float demod_am_get_squelch_db(void)
{
    return s_squelch_threshold_db;
}

float demod_am_get_squelch_level_db(void)
{
    return s_squelch_level_db;
}

uint8_t demod_am_get_squelch_open(void)
{
    return s_squelch_open;
}

/*
 * EL INSTRUMENTO QUE MIDE LO QUE TARDA LA INTERRUPCION. Del 30/07/2026,
 * añadido despues de un informe de que el sistema entero se colgaba al
 * pasar a USB o LSB; ver el comentario sobre el numero de tomas que hay
 * encima de HILBERT_COEFFS. Es barato a proposito: dos lecturas del
 * CYCCNT del DWT y una resta por bloque, y aqui no se toca el puerto
 * serie. debug_print() escribe caracter a caracter por un puerto serie
 * bloqueante y por sondeo -ver debug_uart.c-, y llamarlo desde esta
 * interrupcion, que va con prioridad 6 en el NVIC, se comeria un trozo
 * del presupuesto de 2,67 ms de cada bloque cada vez que saltara: el
 * instrumento pasaria a ser parte del problema en vez de diagnosticarlo.
 * En su lugar, el bucle principal de main.c lee
 * demod_am_get_last_cycles() de vez en cuando e imprime ALLI, donde un
 * puerto serie bloqueante sale barato porque no hay nada mas con prisa a
 * ese nivel de prioridad. Hace falta que el CYCCNT del DWT ya este
 * corriendo: main.c lo enciende una sola vez, antes de la primera
 * llamada a esta interrupcion, en la inicializacion de
 * sdr_spectrum_waterfall_tick(). */
static volatile uint32_t s_last_cycles = 0U;

uint32_t demod_am_get_last_cycles(void)
{
    return s_last_cycles;
}

/*
 * El lector de tiempos de la interrupcion, PROPIO del WFM (05/08/2026).
 * Es el espejo exacto de demod_am_get_last_cycles(), con el mismo
 * razonamiento: capturar barato el CYCCNT del DWT dentro de la
 * interrupcion y leerlo e imprimirlo desde el bucle principal de main.c,
 * donde un puerto serie bloqueante se puede pagar. s_wfm_last_cycles ya
 * se capturaba en cada bloque dentro de demod_wfm_process_raw(), pero
 * hasta ahora NO tenia lector publico, asi que la comprobacion periodica
 * de tiempos de main.c -que llamaba siempre a
 * demod_am_get_last_cycles(), sin condiciones, incluso con el WFM
 * activo- no tenia forma de sacarlo: el presupuesto de tiempo real del
 * WFM no se habia comprobado nunca de verdad. Ver en el bucle principal
 * de main.c el arreglo correspondiente, que por fin lee esto. */
uint32_t demod_wfm_get_last_cycles(void)
{
    return s_wfm_last_cycles;
}

/*
 * Resta que no da la vuelta - 02/10/2026.
 *
 * Las cuatro etapas del desglose se calculan restando: "lo que llevo menos
 * lo que ya conte". Con la cuenta bien hecha eso nunca es negativo, pero
 * son uint32_t, asi que SI la cuenta se tuerce el resultado no es un
 * numero pequeño ni cero: son cuatro mil millones de ciclos, que en la
 * pantalla salen como el tope. O sea que un error de un par de ciclos y un
 * error de bulto se ven exactamente igual, y los dos se ven enormes.
 *
 * Saturando a cero, un descuadre sale como una etapa a cero y entonces las
 * cinco NO suman el total de la fila de arriba. Esa suma es la
 * comprobacion, y asi se puede hacer de un vistazo en la radio.
 */
static uint32_t resta0(uint32_t a, uint32_t b)
{
    return (a > b) ? (a - b) : 0U;
}

/* El reparto por etapas; ver el comentario de
 * demod_am_get_last_cycles_breakdown() en demod_am.h. Son palabras
 * volatile de 32 bits peladas, con el mismo razonamiento de "no merece
 * una seccion critica" que s_mode: lo peor que puede pasar es que el
 * bucle principal lea una etapa del bloque actual y otra del anterior, y
 * para una lectura de diagnostico eso da igual. */
static volatile uint32_t s_last_cycles_frontend = 0U;
static volatile uint32_t s_last_cycles_extract  = 0U;
static volatile uint32_t s_last_cycles_audio    = 0U;
static volatile uint32_t s_last_cycles_nr       = 0U;
static volatile uint32_t s_last_cycles_agc_out  = 0U;

demod_am_cycles_breakdown_t demod_am_get_last_cycles_breakdown(void)
{
    demod_am_cycles_breakdown_t b;
    b.frontend = s_last_cycles_frontend;
    b.extract  = s_last_cycles_extract;
    b.audio    = s_last_cycles_audio;
    b.nr       = s_last_cycles_nr;
    b.agc_out  = s_last_cycles_agc_out;
    return b;
}


/* DC blocker + AGC state (scalar stages, unchanged from before). */
static float s_dcb_x1, s_dcb_y1;
static float s_agc_peak;

/* La fuerza de señal para el medidor de S de la interfaz. Añadida el
 * 05/09/2026, sustituyendo al apaño de "reaprovechar s_agc_peak"; el por
 * que entero esta en el bloque de comentario que hay justo encima de
 * donde se actualiza, dentro de demod_am_process_raw(). En resumen:
 * s_agc_peak es el pico de s_env[] DESPUES de que el bloqueador de
 * continua se haya llevado ya el nivel de portadora -el paso 2, el que
 * "quita el nivel de portadora en AM"-, asi que lo que seguia era la
 * energia de AUDIO y de modulacion que quedaba, no la señal de
 * radiofrecuencia. Una portadora fuerte y quieta, con poca modulacion,
 * leia BAJO ahi -no queda nada cuando se va la continua- mientras que el
 * ruido de banda, que si tiene contenido alterno de sobra, leia parecido
 * o mas. O sea que el medidor parecia estar siguiendo al suelo de ruido
 * en vez de a la señal sintonizada, que es exactamente lo que estaba
 * haciendo.
 *
 * s_sig_peak lo arregla siguiendo el pico del modulo |I+jQ| EN CRUDO de
 * s_i_buf y s_q_buf: la señal compleja ya bajada en frecuencia y
 * filtrada de canal, ANTES de que el bloqueador de continua toque nada.
 * Es el mismo punto de la cadena del que ya lee la medida de nivel de
 * radiofrecuencia del silenciador de AM y NFM, solo que por pico en vez
 * de por media; ver el comentario del bloque del silenciador. Asi
 * responde al nivel real de radiofrecuencia pase lo que pase con la
 * modulacion, igual que un medidor de S de verdad. Los tiempos son los
 * mismos que antes -ataque instantaneo y s_agc_release para la soltada,
 * unos 180 ms en MEDIO, que es lo que dice el selector de perfil-, solo
 * que tomados del punto correcto. Se actualiza en todos los modos que
 * pasan por demod_am_process_raw(): AM, USB, LSB y NFM. El WFM tiene su
 * propio camino de medidor (s_wfm_agc_peak), al que esto no toca. Va en
 * unidades de fondo de escala tipo entero de 16 bits, igual que antes; la
 * interfaz lo pasa a dB o a unidades S ella misma, FUERA de la
 * interrupcion.
 *
 * *** 22/09/2026: WFM TAMBIEN lo escribe *** - demod_wfm_process_raw()
 * rellena este mismo pico desde su propia I/Q filtrada de canal, en el mismo
 * punto de la cadena y con las mismas unidades. Hasta entonces WFM no lo
 * tocaba y el medidor marcaba S0 permanentemente. Ver el bloque S-METER EN
 * WFM en esa funcion. */
static float s_sig_peak;

float demod_am_get_signal_peak(void)
{
    return s_sig_peak;
}

/*
 * La deteccion de recorte en la entrada de radiofrecuencia; el por que
 * entero esta en el comentario de su lector en demod_am.h.
 * s_rf_clip_flag solo se PONE desde dentro de una interrupcion
 * -rf_clip_scan(), a la que llaman tanto demod_am_process_raw() como
 * demod_wfm_process_raw()- y solo se BORRA desde el bucle principal, en
 * el lector de aqui abajo. Es una bandera de un solo sentido, no un
 * contador, asi que no hay riesgo de perder una actualizacion que
 * merezca mas proteccion.
 *
 * El razonamiento del umbral y la cuenta: RF_CLIP_THRESHOLD (32000) cae
 * cerca del fondo de escala real del convertidor (+/-32768) sin exigir
 * el valor clavado del tope. Una onda recortada de verdad se queda plana
 * en el tope, o muy cerca, durante VARIAS muestras seguidas, no una
 * sola. Exigir RF_CLIP_MIN_COUNT (3) muestras por encima de ese umbral
 * dentro del mismo bloque -de entre 256 y 512- descarta un unico pico
 * legitimo cerca del fondo de escala, porque una señal fuerte que NO
 * esta recortando puede perfectamente tocar valores altos en una
 * muestra, y aun asi caza el recorte de verdad rapido: una entrada
 * genuinamente saturada tendra MUCHAS mas de tres muestras pegadas al
 * tope por bloque. O sea que este umbral peca de dispararse facil antes
 * que de no ver un recorte real.
 */
#define RF_CLIP_THRESHOLD  CONFIG_RF_CLIP_THRESHOLD /* see config.h */
#define RF_CLIP_MIN_COUNT  CONFIG_RF_CLIP_MIN_COUNT /* see config.h */
static volatile uint8_t s_rf_clip_flag = 0U;

/* La suma de |I+jQ| del bloque, que en NFM necesita el silenciador. La deja
 * hecha el bucle del medidor de señal, que ya calcula I²+Q² para cada
 * muestra. Ver su comentario. */
static float s_sq_sum_mag;

/*
 * *** ESTE BUCLE CORRIA FUERA DE LA VENTANA QUE MIDE CICLOS - 10/10/2026. ***
 *
 * rf_clip_scan() se llamaba en la linea 2913 y `cyc_start = DWT->CYCCNT` esta
 * en la 2918. O sea que su coste NO aparecia en s_last_cycles ni en ninguna
 * de las cinco etapas del desglose: llevaba desde que existe siendo
 * invisible para la propia instrumentacion del fichero. Quien mirara el
 * desglose para decidir donde optimizar, no podia verlo.
 *
 * Y no era poco: 512 enteros en AM/SSB/NFM y 1.024 en WFM, recorridos
 * ENTEROS en el caso normal -el `break` solo salta cuando hay recorte, que
 * es justo lo que no pasa casi nunca-, para poner una bandera que enciende
 * un indicador en pantalla.
 *
 * Ahora la comparacion va dentro del bucle que YA carga esas mismas muestras
 * para pasarlas a float (el deinterleave de la etapa 0a, y su gemelo en
 * WFM). Es la misma comparacion sobre las mismas muestras con el mismo
 * umbral -bit a bit el mismo resultado- pero sin una pasada entera de mas
 * sobre el buffer crudo.
 *
 * Esto es lo que queda de la funcion: el veredicto, que sigue siendo suyo
 * para que el umbral y el minimo vivan en un solo sitio.
 */
static void rf_clip_veredicto(uint32_t hits)
{
    if (hits >= RF_CLIP_MIN_COUNT) { s_rf_clip_flag = 1U; }
}

/* 1 si esta muestra esta pegada al tope del conversor. En linea: se llama
 * dos veces por muestra dentro del bucle de deinterleave. */
static inline uint8_t rf_clip_una(int16_t v)
{
    return (uint8_t)(v >= RF_CLIP_THRESHOLD || v <= -RF_CLIP_THRESHOLD);
}

uint8_t demod_am_get_and_clear_rf_clip_flag(void)
{
    uint8_t v = s_rf_clip_flag;
    s_rf_clip_flag = 0U;
    return v;
}

/* El si/no de la bajada en frecuencia, que mantiene de acuerdo con el
 * oscilador local de verdad quien sintoniza -ver demod_am.h-. El mismo
 * patron de seguridad que en el intento anterior de Fs/8: por defecto
 * APAGADO, para que demod_am_init(), que corre antes de la primera
 * sintonia -que sigue siendo la repeticion byte a byte de lo capturado,
 * sin desplazamiento; ver main.c-, no se desincronice nunca con el
 * oscilador. Solo tune_encoder_poll() lo enciende, y solo una vez que ha
 * programado de verdad el oscilador con el desplazamiento. */
/*
 * El desplazamiento digital, en hercios y con signo. Ver el bloque grande de
 * demod_am.h: es UN dato, no un si/no mas unos hercios.
 */
static float s_mix_hz = 0.0f;

/* Lo derivado, que se recalcula solo cuando cambia alguna de sus dos
 * entradas (los hercios pedidos y la Fs que esta corriendo). La comprobacion
 * va en el bucle de bloque, no en los setters: hay DOS entradas y dos
 * setters, y recalcular en cada setter es la forma clasica de que un dia
 * alguien anada un tercer camino y se olvide. Cuesta dos comparaciones por
 * bloque de 256 muestras. */
static nco_t   s_mix_nco;
static float   s_mix_hz_puesto = 1e30f;   /* imposible: fuerza el primer calculo */
static float   s_mix_fs_puesto = 0.0f;
static uint8_t s_mix_rapido = 0U;         /* 1 = es Fs/4 exacto */

void demod_am_set_mix_hz(float hz)
{
    s_mix_hz = hz;
}

float demod_am_get_mix_hz(void)
{
    return s_mix_hz;
}

/*
 * Per-block scratch buffers. Static - this runs in ISR context, keep
 * it off the stack. See demod_am.h for the RAM budget.
 */
/* Comparten sitio con los de WFM: ver la union s_via, mas arriba. */
#define s_i_buf  (s_via.est.i_buf)       /* I rail, deinterleaved */
#define s_q_buf  (s_via.est.q_buf)       /* Q rail, deinterleaved */
/* El buffer entrelazado para arm_cmplx_mag_f32() ya no existe: la magnitud
 * se calcula leyendo s_i_buf/s_q_buf donde estan. Ver el paso 1 de AM.
 * Eran 2 kB de TCM y 192.000 accesos a memoria por segundo. */
#define s_env  (s_via.est.env)           /* |I+jQ|, then DC-blocked, then audio-LPF'd */

/* Output assembly buffer: one TX half (stereo interleaved). Static -
 * this runs in ISR context, keep it off the stack. */
#define s_audio_out  (s_via.est.audio_out)

void demod_am_init(void)
{
    rcu_periph_clock_enable(RCU_GPIOB);
    gpio_mode_set(SPK_EN_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, SPK_EN_PIN);
    gpio_output_options_set(SPK_EN_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_2MHZ, SPK_EN_PIN);
    gpio_bit_set(SPK_EN_PORT, SPK_EN_PIN); /* speaker amp ON */

    arm_biquad_cascade_df1_init_f32(&s_wfm_ifbw_i_inst, WFM_IFBW_STAGES, WFM_IFBW_NARROW_COEFFS, s_wfm_ifbw_i_state);
    arm_biquad_cascade_df1_init_f32(&s_wfm_ifbw_q_inst, WFM_IFBW_STAGES, WFM_IFBW_NARROW_COEFFS, s_wfm_ifbw_q_state);
    arm_biquad_cascade_df1_init_f32(&s_wfm_alpf_wide_inst, WFM_ALPF_STAGES, WFM_ALPF_WIDE_COEFFS, s_wfm_alpf_wide_state);
    arm_biquad_cascade_df1_init_f32(&s_wfm_alpf_norm_inst, WFM_ALPF_STAGES, WFM_ALPF_NORM_COEFFS, s_wfm_alpf_norm_state);
    arm_biquad_cascade_df1_init_f32(&s_wfm_alpf_narrow_inst, WFM_ALPF_STAGES, WFM_ALPF_NARROW_COEFFS, s_wfm_alpf_narrow_state);

    /* WFM discriminator/de-emphasis state - see the field comments
     * above. A stale s_fm_i_prev/s_fm_q_prev only matters for the
     * very first sample after boot or after switching INTO WFM mode
     * (one glitched sample from a bogus delta), same tradeoff the SSB
     * delay-match history below makes; not worth guarding with a
     * mode-switch reset. */
    s_fm_i_prev = 0.0f;
    s_fm_q_prev = 0.0f;
    s_wfm_dc_i = 0.0f;
    s_wfm_dc_q = 0.0f;
    s_wfm_dc_n = 0U;
    s_wfm_deemph_y1 = 0.0f;
    /* WFM's own DC blocker/AGC state (05/08/2026, separate processing
     * path - see demod_wfm_process_raw()'s comment) - same
     * "not worth a mode-switch reset" reasoning as above. */
    s_wfm_dcb_x1 = 0.0f;
    s_wfm_dcb_y1 = 0.0f;
    s_wfm_agc_peak = WFM_AGC_PEAK_MIN;

    /* AM/USB/LSB/NFM's own rate-dependent CMSIS instances (CHF/ALPF/
     * NFM_CHF biquads, SSB decimate/Hilbert/interpolate, NR's own
     * decimate/interpolate) - all handled by demod_am_set_active_
     * rate(), called here with the default (96kHz, is_48k=0) so cold
     * boot ends up in exactly the state this function always
     * initialized it to before 01/09/2026's 48kHz option existed. See
     * that function's own comment for the full mechanism. */
    demod_am_set_active_rate(0U);

    nr_ss_init();
    anotch_init(DEMOD_DEC_FS_HZ);   /* ver DEMOD_DEC_FS_HZ en demod_am.h */
    rtty_init();
    /* CW: aqui al lado del RTTY porque se alimenta del mismo sitio y en
     * el mismo momento. El tono y la siembra de velocidad salen de
     * config.h; nace apagado, como el RTTY, y lo enciende el modo CW del
     * selector de modos. */
    cw_init();
    cw_set_pitch_hz(CONFIG_CW_PITCH_HZ);
    cw_set_wpm_hint(CONFIG_CW_WPM_HINT);
    demod_am_set_cw_filter_hz(CONFIG_CW_PITCH_HZ);
    /* El filtro de dos cortes, construido por primera vez: sin esto, el
     * primer bloque de audio iria por una cascada de ceros. */
    s_fil_act = 0U;
    fil_construye(demod_am_filtro_familia());
    rtty_scope_init();
    {
        uint32_t k;
        for (k = 0; k < HILBERT_GROUP_DELAY_DEC; k++) {
            s_i_delay_hist[k] = 0.0f;
        }
    }

    s_dcb_x1 = 0.0f;
    s_dcb_y1 = 0.0f;
    s_agc_peak = AGC_TARGET; /* start at unity-ish gain, settles fast */
    s_sig_peak = 0.0f; /* S-meter's own peak, separate from s_agc_peak now -
                         * see demod_am_get_signal_peak()'s comment. Starts
                         * at 0 rather than AGC_TARGET: instant attack means
                         * it climbs to the real level within one block of
                         * an actual signal, no benefit to a warm-started
                         * guess the way the AGC's gain-control peak wants. */
    s_squelch_level_db = s_squelch_threshold_db; /* no false "signal detected" blip before the first real block */
    s_squelch_open = 1U; /* matches the OFF-by-default threshold - see demod_am.h */

    debug_print("demod_am: PB7 speaker enable high, CMSIS-DSP biquads + decimated "
                "SSB chain init, state reset\n");
}

/*
 * s_dec_block_samples/s_decim_factor: the ACTIVE rate's decimated-
 * domain block size and decimation factor - what demod_am_process_
 * raw() actually uses at runtime, instead of the old fixed DEC_BLOCK_
 * SAMPLES/DECIM_FACTOR #defines. s_dcb_r: same idea for the DC
 * blocker pole. All three are set by demod_am_set_active_rate() below,
 * never anywhere else - default values here just match demod_am_
 * init()'s own default rate (96kHz) so nothing's uninitialized before
 * the first real call.
 */
static uint32_t s_dec_block_samples = DEC_BLOCK_SAMPLES_96K;
static uint32_t s_decim_factor = DECIM_FACTOR_96K;
static float s_dcb_r = DCB_R_96K;

/*
 * demod_am_active_fs_hz() - the ACTUAL current AM/USB/LSB/NFM sample
 * rate in Hz, added 01/09/2026 alongside the 48kHz rate option. Used
 * anywhere a function genuinely needs Fs as a plain number rather
 * than a pre-derived coefficient (currently just sam_init()/sam_step()
 * below - everything else in this file works off coefficient tables
 * selected by s_active_rate_is_48k directly, never re-deriving them
 * from a raw Fs value at runtime).
 */
static float demod_am_active_fs_hz(void)
{
    return s_active_rate_is_48k ? 48000.0f : 96000.0f;
}

/* La misma, expuesta. La necesita el APRS, que se engancha al discriminador
 * de FM y por tanto trabaja a ESTA tasa y no a los 12 kHz del camino de banda
 * lateral. Se pregunta en vez de copiar el numero: el ajuste de tasa lo puede
 * cambiar en caliente. */
float demod_am_get_active_fs_hz(void)
{
    return demod_am_active_fs_hz();
}

/*
 * demod_am_set_active_rate(). El por que entero -la opcion de 48 kHz
 * para mejor relacion señal a ruido y mas holgura de procesador- esta en
 * el comentario donde se declara, en demod_am.h. La llaman
 * demod_am_init() -con is_48k = 0, lo normal- y apply_demod_mode() de
 * main.c siempre que cambia de verdad la velocidad del codec para AM,
 * USB, LSB o NFM: la casilla RATE, o un cambio de modo que conserva la
 * velocidad ya elegida.
 *
 * Reinicializa de verdad todas las instancias de CMSIS que dependen de
 * la velocidad, con la tabla de coeficientes, el numero de tomas y el
 * factor de diezmado que le tocan a la velocidad nueva. NO es un cambio
 * de puntero de coeficientes, porque el diezmador y el interpolador de
 * banda lateral, y los de la reduccion de ruido, se diferencian en
 * DECIM_FACTOR y en numero de tomas entre las dos velocidades. Los
 * biquads de CHF, ALPF y NFM_CHF solo se diferencian en los VALORES de
 * los coeficientes, con el mismo numero de ETAPAS en los dos casos, asi
 * que esos cuatro son reinicializaciones mas simples. Todos los buffers
 * de estado implicados -s_decim_i_state y s_decim_q_state,
 * s_hilbert_state, s_interp_state, s_nr_decim_state y
 * s_nr_interp_state- estan ya dimensionados a DECIM_STATE_SIZE_MAX,
 * HILBERT_STATE_SIZE_MAX e INTERP_STATE_SIZE_MAX -ver sus propios
 * comentarios-, o sea lo bastante grandes para lo que de verdad pide
 * CUALQUIERA de las dos velocidades. Asi que reinicializar aqui no puede
 * desbordarlos nunca: simplemente usa menos de un buffer que ya era
 * bastante grande cuando se elige el caso de 96 kHz, que necesita menos
 * estado que el de 48 para los buffers del dominio diezmado -ver el
 * comentario de DEC_BLOCK_SAMPLES_MAX-.
 *
 * Tambien vuelve a deducir s_agc_release para el perfil de automatico de
 * ganancia que haya puesto -los coeficientes de soltada tambien dependen
 * de la velocidad, cosa muy aparte de que instancias de filtro de CMSIS
 * estan activas-, y lo hace simplemente volviendo a llamar a
 * demod_am_set_agc_profile() con el perfil actual: sale barato y deja la
 * logica de "que tabla hay que leer" en un solo sitio, esa funcion, en
 * vez de duplicar aqui el mismo switch.
 *
 * NO toca nada del WFM: el WFM siempre ha corrido su propio camino
 * aparte a 192 kHz, diga lo que diga esta bandera; ver el comentario de
 * cabecera de esta funcion en demod_am.h.
 */
void demod_am_set_active_rate(uint8_t is_48k)
{
    s_active_rate_is_48k = is_48k ? 1U : 0U;
    /* El filtro esta diseñado PARA una tasa: con otra, sus cortes caen en
     * otro sitio. Se rehace. */
    fil_construye(demod_am_filtro_familia());

    /* arm_biquad_cascade_df1_init_f32() zeroes the state buffer it's
     * given, so switching rates also gives every one of these a clean
     * transient-free restart - matches how a mode switch already
     * resets other state elsewhere in this file. */
    if (s_active_rate_is_48k) {
        arm_biquad_cascade_df1_init_f32(&s_chf_i_inst, CHF_STAGES, CHF_48K_COEFFS, s_chf_i_state);
        arm_biquad_cascade_df1_init_f32(&s_chf_q_inst, CHF_STAGES, CHF_48K_COEFFS, s_chf_q_state);
        arm_biquad_cascade_df1_init_f32(&s_alpf_inst, ALPF_STAGES, ALPF_48K_COEFFS, s_alpf_state);
        arm_biquad_cascade_df1_init_f32(&s_alpf_4k0_inst, ALPF_4K0_STAGES, ALPF_4K0_48K_COEFFS, s_alpf_4k0_state);
        arm_biquad_cascade_df1_init_f32(&s_alpf_2k3_inst, ALPF_2K3_STAGES, ALPF_2K3_48K_COEFFS, s_alpf_2k3_state);
        arm_biquad_cascade_df1_init_f32(&s_alpf_1k8_inst, ALPF_1K8_STAGES, ALPF_1K8_48K_COEFFS, s_alpf_1k8_state);
        nfm_chf_carga(1U);
        s_dcb_r = DCB_R_48K;
        s_decim_factor = DECIM_FACTOR_48K;
        s_dec_block_samples = DEC_BLOCK_SAMPLES_48K;
    } else {
        arm_biquad_cascade_df1_init_f32(&s_chf_i_inst, CHF_STAGES, CHF_96K_COEFFS, s_chf_i_state);
        arm_biquad_cascade_df1_init_f32(&s_chf_q_inst, CHF_STAGES, CHF_96K_COEFFS, s_chf_q_state);
        arm_biquad_cascade_df1_init_f32(&s_alpf_inst, ALPF_STAGES, ALPF_96K_COEFFS, s_alpf_state);
        arm_biquad_cascade_df1_init_f32(&s_alpf_4k0_inst, ALPF_4K0_STAGES, ALPF_4K0_96K_COEFFS, s_alpf_4k0_state);
        arm_biquad_cascade_df1_init_f32(&s_alpf_2k3_inst, ALPF_2K3_STAGES, ALPF_2K3_96K_COEFFS, s_alpf_2k3_state);
        arm_biquad_cascade_df1_init_f32(&s_alpf_1k8_inst, ALPF_1K8_STAGES, ALPF_1K8_96K_COEFFS, s_alpf_1k8_state);
        nfm_chf_carga(0U);
        s_dcb_r = DCB_R_96K;
        s_decim_factor = DECIM_FACTOR_96K;
        s_dec_block_samples = DEC_BLOCK_SAMPLES_96K;
    }
    /* El de CW se calcula, no sale de tabla, asi que se rehace aqui con
     * la tasa nueva - si no, al cambiar de 96 a 48 kHz el filtro se
     * quedaria centrado al doble de frecuencia. */
    demod_am_set_cw_filter_hz(s_alpf_cw_hz);

    /* La cadena diezmada de banda lateral. Las inicializaciones del
     * diezmador y del interpolador COMPRUEBAN sus argumentos
     * -blockSize % M y numTaps % L- y devuelven un estado: hay que
     * mirarlo y gritar por el puerto serie si alguna vez falla, porque
     * una inicializacion fallida aqui deja la instancia inservible y el
     * camino de banda lateral reventaria o soltaria basura sin ninguna
     * otra pista. La inicializacion del FIR pelado no puede fallar
     * -devuelve void-. Las tres ponen a cero sus buffers de estado por
     * dentro. */
    if (s_active_rate_is_48k) {
        if (arm_fir_decimate_init_f32(&s_decim_i_inst, DECIM_COEFFS_48K_TAPS, s_decim_factor,
                                        DECIM_COEFFS_48K, s_decim_i_state,
                                        SDR_RX_BLOCK_SAMPLES) != ARM_MATH_SUCCESS) {
            debug_print("demod_am: *** decimator I init FAILED (48K, blockSize %% M != 0?) ***\n");
        }
        if (arm_fir_decimate_init_f32(&s_decim_q_inst, DECIM_COEFFS_48K_TAPS, s_decim_factor,
                                        DECIM_COEFFS_48K, s_decim_q_state,
                                        SDR_RX_BLOCK_SAMPLES) != ARM_MATH_SUCCESS) {
            debug_print("demod_am: *** decimator Q init FAILED (48K) ***\n");
        }
        arm_fir_init_f32(&s_hilbert_inst, HILBERT_COEFFS_TAPS, HILBERT_COEFFS,
                          s_hilbert_state, s_dec_block_samples);
        if (arm_fir_interpolate_init_f32(&s_interp_inst, s_decim_factor, INTERP_COEFFS_48K_TAPS,
                                           INTERP_COEFFS_48K, s_interp_state,
                                           s_dec_block_samples) != ARM_MATH_SUCCESS) {
            debug_print("demod_am: *** interpolator init FAILED (48K, numTaps %% L != 0?) ***\n");
        }
        if (arm_fir_decimate_init_f32(&s_nr_decim_inst, DECIM_COEFFS_48K_TAPS, s_decim_factor,
                                        DECIM_COEFFS_48K, s_nr_decim_state,
                                        SDR_RX_BLOCK_SAMPLES) != ARM_MATH_SUCCESS) {
            debug_print("demod_am: *** NR decimator init FAILED (48K, blockSize %% M != 0?) ***\n");
        }
        if (arm_fir_interpolate_init_f32(&s_nr_interp_inst, s_decim_factor, INTERP_COEFFS_48K_TAPS,
                                           INTERP_COEFFS_48K, s_nr_interp_state,
                                           s_dec_block_samples) != ARM_MATH_SUCCESS) {
            debug_print("demod_am: *** NR interpolator init FAILED (48K, numTaps %% L != 0?) ***\n");
        }
    } else {
        if (arm_fir_decimate_init_f32(&s_decim_i_inst, DECIM_COEFFS_96K_TAPS, s_decim_factor,
                                        DECIM_COEFFS_96K, s_decim_i_state,
                                        SDR_RX_BLOCK_SAMPLES) != ARM_MATH_SUCCESS) {
            debug_print("demod_am: *** decimator I init FAILED (96K, blockSize %% M != 0?) ***\n");
        }
        if (arm_fir_decimate_init_f32(&s_decim_q_inst, DECIM_COEFFS_96K_TAPS, s_decim_factor,
                                        DECIM_COEFFS_96K, s_decim_q_state,
                                        SDR_RX_BLOCK_SAMPLES) != ARM_MATH_SUCCESS) {
            debug_print("demod_am: *** decimator Q init FAILED (96K) ***\n");
        }
        arm_fir_init_f32(&s_hilbert_inst, HILBERT_COEFFS_TAPS, HILBERT_COEFFS,
                          s_hilbert_state, s_dec_block_samples);
        if (arm_fir_interpolate_init_f32(&s_interp_inst, s_decim_factor, INTERP_COEFFS_96K_TAPS,
                                           INTERP_COEFFS_96K, s_interp_state,
                                           s_dec_block_samples) != ARM_MATH_SUCCESS) {
            debug_print("demod_am: *** interpolator init FAILED (96K, numTaps %% L != 0?) ***\n");
        }
        if (arm_fir_decimate_init_f32(&s_nr_decim_inst, DECIM_COEFFS_96K_TAPS, s_decim_factor,
                                        DECIM_COEFFS_96K, s_nr_decim_state,
                                        SDR_RX_BLOCK_SAMPLES) != ARM_MATH_SUCCESS) {
            debug_print("demod_am: *** NR decimator init FAILED (96K, blockSize %% M != 0?) ***\n");
        }
        if (arm_fir_interpolate_init_f32(&s_nr_interp_inst, s_decim_factor, INTERP_COEFFS_96K_TAPS,
                                           INTERP_COEFFS_96K, s_nr_interp_state,
                                           s_dec_block_samples) != ARM_MATH_SUCCESS) {
            debug_print("demod_am: *** NR interpolator init FAILED (96K, numTaps %% L != 0?) ***\n");
        }
    }

    /* Re-derive s_agc_release for the CURRENTLY selected profile using
     * the newly-active rate's table - see this function's own header
     * comment for why this just re-calls demod_am_set_agc_profile()
     * rather than duplicating its switch. */
    demod_am_set_agc_profile(s_agc_profile);

    /* SAM's PLL bakes sample_rate_hz directly into its own loop-filter
     * state at init time (see sam_init()'s omega_min/omega_max) - and
     * s_sam_init_done normally gates a ONE-TIME-EVER init (SAM's PLL
     * state is meant to persist/keep tracking across ordinary AM
     * demod calls, not reset every block). A rate change invalidates
     * that baked-in state just as surely as a first-ever init would
     * need it, so force the next AM-mode call to redo sam_init() with
     * the newly correct demod_am_active_fs_hz(). */
    s_sam_init_done = 0u;

    debug_print(s_active_rate_is_48k ? "demod_am: active rate set to 48kHz\n"
                                       : "demod_am: active rate set to 96kHz\n");
}

uint8_t demod_am_get_active_rate_is_48k(void)
{
    return s_active_rate_is_48k;
}
/*
 * EL OSCILOSCOPIO DE TONO EN APRS - 30/09/2026.
 *
 * Y era cierto.: en APRS no habia NADA encima del panel de texto. No es que
 * el espectro se parase, es que no lo pintaba nadie.
 *
 * POR QUE. APRS es un modo digital, asi que digi_panel_active() dice que si
 * y el bucle principal pinta el osciloscopio de tono EN LUGAR del espectro
 * de radiofrecuencia. Pero a ese osciloscopio solo le llegaba audio desde
 * la rama de BANDA LATERAL (rtty_scope_feed() con s_ssb_dec), y APRS es el
 * unico modo digital que va en FM: su audio sale del discriminador. O sea
 * que el osciloscopio se quedaba sin una sola muestra y pintaba el hueco
 * vacio. En NFM a secas el espectro se mueve porque ahi digi_panel_active()
 * dice que no y no se cede el sitio - por eso el fallo solo salia en APRS.
 *
 * LA TASA. El osciloscopio tiene 12 kHz metidos en el hueso
 * (rtty_scope_hz_per_bin() = 12000/512), y el discriminador entrega a la
 * tasa de radiofrecuencia: 48 o 96 kHz segun el ajuste. Asi que aqui se
 * diezma por 4 u 8 antes de dar de comer. Promediando los N, no cogiendo
 * uno de cada N: el promedio es un filtro paso bajo de los pobres, y sin
 * el, todo lo que hay por encima de 6 kHz se dobla hacia abajo y aparece
 * como tonos que no existen justo en la zona que interesa mirar.
 *
 * Y lo que se ve al final son los dos tonos del AFSK -1.200 y 2.200 Hz-,
 * que es exactamente lo que sirve para saber si estas sintonizado.
 */
static void aprs_scope_feed(const float *x, uint32_t n)
{
    static float    s_suma;
    static uint16_t s_cuenta;
    float    lote[64];
    uint16_t m = 0U;
    uint32_t k;
    uint16_t dec = (uint16_t)(demod_am_active_fs_hz() / DEMOD_DEC_FS_HZ + 0.5f);
    float    inv_dec;

    if (dec < 1U) { dec = 1U; }

    /*
     * EL INVERSO, UNA VEZ Y NO TREINTA Y DOS - 10/10/2026.
     *
     * `dec` no cambia dentro del bloque, asi que la division tampoco. En el
     * Cortex-M4 una division en coma flotante son unos catorce ciclos y una
     * multiplicacion uno; con 256 muestras y dec = 8 salen 32 divisiones por
     * bloque, o sea unos 450 ciclos dentro de la interrupcion por una cuenta
     * que ya se sabia.
     *
     * Multiplicar por el inverso no da exactamente el mismo bit que dividir.
     * Esto alimenta el osciloscopio de sintonia -una imagen- y la diferencia
     * esta en el ultimo bit de un float; ahi no se ve.
     */
    inv_dec = 1.0f / (float)dec;

    for (k = 0U; k < n; k++) {
        s_suma += x[k];
        s_cuenta++;
        if (s_cuenta < dec) { continue; }
        lote[m] = s_suma * inv_dec;
        m++;
        s_suma = 0.0f;
        s_cuenta = 0U;
        if (m >= (uint16_t)(sizeof lote / sizeof lote[0])) {
            rtty_scope_feed(lote, m);
            m = 0U;
        }
    }
    if (m > 0U) { rtty_scope_feed(lote, m); }
}

/*
 * ======================================================================
 * LA CONTINUA SE QUITA DE LA I Y LA Q, NO DEL AUDIO - 11/10/2026
 * ======================================================================
 *
 * Esta cadena YA tenia un quitador de continua (s_dcb_x1/y1), pero esta en
 * el AUDIO, DETRAS del discriminador. Y ahi no sirve de nada contra esto,
 * porque lo que un offset en I/Q produce al otro lado NO es una continua
 * quitable: el discriminador saca arg(z[n] . conj(z[n-1])), y sumarle un
 * vector constante D a z no desplaza la salida, la TUERCE.
 *
 * LO QUE SE MIDIO (sim/wfm_dc.c, que mete muestras crudas por
 * demod_wfm_process_raw() igual que el codec). Offset fijo de 1.000
 * cuentas, bajando la portadora:
 *
 *   portadora  dc/portadora   tono recuperado   lo que no es el tono
 *     16000        6,2 %           25498              0,0 %
 *      2000       50,0 %           25536              0,0 %
 *      1000      100,0 %             187             98,8 %   <- se cae
 *       500      200,0 %             119             97,5 %
 *
 * Y con poca desviacion -un pasaje flojo- empieza a ensuciarse mucho
 * antes: al 50 % ya hay un 9,9 % de la salida que no es la senal.
 *
 * EL CORTE ESTA CLAVADO EN PORTADORA = OFFSET, y no es casualidad: cuando
 * |D| > |A|, el vector A.exp(j.fi) + D DEJA DE DAR LA VUELTA AL ORIGEN. Ya
 * no rota, solo se balancea, y el discriminador no tiene de donde sacar la
 * frecuencia. Es un umbral matematico duro, y el banco lo encontro solo.
 *
 * Traducido: una emisora cuya portadora quede por debajo del offset NO SE
 * PUEDE DEMODULAR. Que es exactamente lo que decia el aviso de la ganancia
 * del codec: subirla sube A y D se queda donde estaba.
 *
 * COMO SE QUITA. Una media MUY lenta de la I y de la Q, restada antes de
 * nada. Se acumula dentro del bucle del deinterleave, que ya recorre todas
 * las muestras, asi que son dos restas y dos sumas por muestra - nada al
 * lado del atan2 que esta cadena ya lleva por muestra.
 *
 * LA CONSTANTE DE TIEMPO ES LARGA A PROPOSITO: 1,4 segundos. El offset del
 * hardware es practicamente fijo, y la senal de FM tiene media cero en ese
 * plazo porque la portadora esta girando. Una constante corta se comeria
 * la portadora cuando la desviacion pasa por cero. Con 1,4 s haria falta
 * que una emisora se quedara sin modular ni piloto mas de un segundo para
 * que esto la tocara, y eso en radiodifusion no pasa.
 *
 * NO LLEVA INTERRUPTOR. Con offset cero resta cero: no hay nada que
 * apagar, y un interruptor seria un sitio mas donde quedarse en la
 * posicion mala.
 */
/*
 * Lo que este quitador esta restando ahora mismo, en cuentas del
 * convertidor. Publicado a proposito: si sale cero, esta radio no tiene
 * offset y todo lo de arriba no hace nada; si sale un numero, ese numero
 * ES el suelo de sensibilidad de WFM - ninguna emisora con menos portadora
 * que esto se puede demodular. Mejor un numero que una suposicion.
 */
void demod_wfm_get_dc(float *dc_i, float *dc_q)
{
    if (dc_i != (float *)0) { *dc_i = s_wfm_dc_i; }
    if (dc_q != (float *)0) { *dc_q = s_wfm_dc_q; }
}

/*
 * El discriminador de FM compartido -retrasar, conjugar, multiplicar y
 * sacar el angulo con atan2f(); la deduccion entera esta en la nota del
 * WFM de demod_am.h-. Se saco a una funcion aparte el 31/07/2026, al
 * añadir el NFM, para que el WFM y el NFM no llevaran dos copias del
 * mismo bucle que solo se diferencian en que buffers de I/Q lo alimentan
 * y que ganancia escala la salida. Usa y actualiza el estado
 * COMPARTIDO s_fm_i_prev y s_fm_q_prev -ver su propio comentario; no da
 * problemas porque el WFM y el NFM nunca estan activos a la vez-.
 * `i_buf` y `q_buf` solo se leen; `env` se escribe, y puede solaparse
 * sin problema con la memoria de `i_buf` o `q_buf` en quien llama,
 * porque esto nunca lee env[n] despues de haberlo escrito -el mismo
 * razonamiento sobre solapamiento que las demas llamadas a filtros en el
 * sitio de este fichero-.
 */
static void fm_discriminate(const float32_t *i_buf, const float32_t *q_buf,
                             float32_t *env, float32_t gain, uint32_t n_samples)
{
    float32_t i_prev = s_fm_i_prev;
    float32_t q_prev = s_fm_q_prev;
    uint32_t n;

    for (n = 0; n < n_samples; n++) {
        float32_t i_now = i_buf[n];
        float32_t q_now = q_buf[n];
        /* z[n]*conj(z[n-1]): re = I.I'+Q.Q', im = Q.I'-I.Q' */
        float32_t re = i_now * i_prev + q_now * q_prev;
        float32_t im = q_now * i_prev - i_now * q_prev;

        /*
         * rapido_atan2() y no atan2f() - 04/10/2026.
         *
         * Esta era la segunda de las dos llamadas por muestra que habia en
         * la interrupcion (la otra estaba en sam.c). Aqui cuesta 96.000
         * llamadas por segundo en NFM y 192.000 en WFM -512 muestras por
         * bloque a 192 kHz-, o sea un 10% y un 19% del presupuesto del
         * bloque en una sola linea.
         *
         * El error de la aproximacion son seis decimas de grado sobre un
         * angulo que luego se multiplica por la ganancia del discriminador y
         * se va al altavoz: queda muy por debajo del ruido del canal, y por
         * debajo tambien del rizado del propio filtro de audio.
         */
        env[n] = rapido_atan2(im, re) * gain;

        i_prev = i_now;
        q_prev = q_now;
    }
    s_fm_i_prev = i_prev;
    s_fm_q_prev = q_prev;
}

/*
 * WFM's OWN, SEPARATE processing path - added 05/08/2026, per the
 * project owner's separate-path decision, when WFM was reactivated
 * at its native 192kHz/512-sample rate while AM/SSB/NFM stayed at
 * 48kHz/128 samples in demod_am_process_raw() above. Registered as
 * the sdr_rx block hook (sdr_rx_set_block_hook()) ONLY while WFM is
 * the active mode - main.c swaps the hook (and the codec's own clock
 * registers, and sdr_rx's/gd32_i2s's block-size configuration) on
 * every mode transition into or out of WFM. demod_am_process_raw()
 * above is NEVER called while WFM is selected, and this function is
 * NEVER called otherwise - the two do not interleave.
 *
 * Mirrors the WFM-specific steps demod_am_process_raw() used to
 * contain before this split (deinterleave -> discriminate -> de-
 * emphasis -> audio LPF -> DC blocker -> AGC -> output), at
 * SDR_RX_BLOCK_SAMPLES_WFM (512) instead of SDR_RX_BLOCK_SAMPLES
 * (128), using WFM's own dedicated buffers/state (s_wfm_*) and its
 * own 192kHz-tuned constants (WFM_DCB_R, WFM_AGC_RELEASE_*) instead of
 * the 48kHz ones AM/SSB/NFM use. No down-mix, no channel filter, no
 * squelch, no NR - see demod_am.h's WFM note for why WFM never used
 * the first two even before this split, and squelch/NR were never
 * wired up for WFM either (see their own comments).
 */
void demod_wfm_process_raw(const int16_t *raw_interleaved)
{
    uint32_t n;
    uint8_t muted;
    uint8_t do_log;

    /*
     * *** 05/08/2026: parche puesto despues de agotar la investigacion
     * practica de la causa; ver el comentario de
     * sdr_rx_last_block_corrupted() ***. Un bloque que llego con
     * SPI_STAT_FERR puesto puede traer las muestras mal repartidas entre
     * canales: se tira entero en vez de meterlo por este discriminador,
     * que es sensible a la fase. Se sale a proposito ANTES de tocar
     * ningun estado de aqui abajo -la cuenta atras del silencio, los
     * contadores de diagnostico, el automatico de ganancia- para que el
     * siguiente bloque bueno siga exactamente donde lo dejo el anterior
     * bueno. El hueco de este bloque en el buffer de ping-pong de
     * transmision se queda simplemente con lo que ya tenia de la vuelta
     * anterior -a gd32_i2s_stream_write_half() no se le llama para este
     * bloque-, lo que repite unos 2,67 ms de audio ya reproducido en
     * lugar de dejar pasar uno corrupto: eso no se oye como fallo, al
     * contrario que soltar la salida del discriminador en crudo.
     */
    if (sdr_rx_last_block_corrupted()) {
        return;
    }

    /* El recorte de radiofrecuencia se mide DENTRO del deinterleave de aqui
     * abajo, que ya carga estas mismas muestras. Ver rf_clip_veredicto().
     * Sigue siendo sobre el bloque CRUDO y despues de la guarda de bloque
     * corrupto, que es lo que importaba: las muestras de un bloque con FERR
     * pueden estar cambiadas de canal y no serian una lectura de recorte. */

    muted = (s_wfm_mute_remaining > 0U) ? 1U : 0U;
    do_log = wfm_should_log_diag(); /* decide BEFORE incrementing below */

    if (muted) {
        s_wfm_mute_remaining--;
    }
    if (do_log) {
        debug_print_dec("demod_wfm: block# (muted=1/0 follows)", s_wfm_diag_count);
        debug_print_dec("demod_wfm: muted", (uint32_t)muted);
    }
    s_wfm_diag_count++; /* unconditional - see wfm_should_log_diag()'s comment */

    /*
     * Raw sample dump - added 05/08/2026 after AGC data looked fully
     * converged/reasonable on both sides of a switch (WFM and AM)
     * while the project owner still heard the exact same loud noise
     * and whistle, with no recognizable voice, as before any of the
     * AGC/mute fixes - meaning those fixes aren't the issue. min/max
     * alone can't distinguish real signal from scrambled-but-
     * plausible-magnitude noise (e.g. an I2S word-select/frame
     * alignment glitch on re-enable would produce exactly that: bit-
     * shifted samples that still fall in a "normal" numeric range but
     * carry no real waveform). Dumping the actual sequential raw I/Q
     * values - only at block 0 of each entry, only when logging - is
     * the next thing needed to tell "genuinely bad samples" apart
     * from "fine samples, something else is wrong downstream".
     */
    if (do_log && s_wfm_diag_count == 1U) {
        for (n = 0; n < 8U; n++) {
            debug_print_dec_signed_local("demod_wfm: raw I", (int32_t)raw_interleaved[2U * n]);
            debug_print_dec_signed_local("demod_wfm: raw Q", (int32_t)raw_interleaved[2U * n + 1U]);
        }
    }

    {
    float dcb_x1 = s_wfm_dcb_x1;
    float dcb_y1 = s_wfm_dcb_y1;
    float peak = s_wfm_agc_peak;
    uint32_t cyc_start = DWT->CYCCNT;

    /* 0a. Deinterleave - same plain-cast, no-normalization convention
     * as demod_am_process_raw()'s own step 0a, y con el recorte dentro por
     * lo mismo: en WFM eran 1.024 enteros de pasada extra. */
    {
        uint32_t hits = 0U;
        /* La continua que se resta es la de ANTES de este bloque, y la
         * media que se acumula es la del bloque CRUDO. Separar las dos
         * cosas es lo que hace que esto sea una estimacion directa y no un
         * lazo realimentado: converge a la media de la entrada, y la media
         * de una portadora de FM que gira es cero. Ver WFM_DC_ALFA. */
        float32_t dci = s_wfm_dc_i;
        float32_t dcq = s_wfm_dc_q;
        float32_t sum_i = 0.0f;
        float32_t sum_q = 0.0f;

        for (n = 0; n < SDR_RX_BLOCK_SAMPLES_WFM; n++) {
            int16_t vi = raw_interleaved[2U * n];
            int16_t vq = raw_interleaved[2U * n + 1U];
            float32_t fi = (float32_t)vi;
            float32_t fq = (float32_t)vq;
            hits += rf_clip_una(vi);
            hits += rf_clip_una(vq);
            /* El recorte se mide sobre la muestra CRUDA, arriba: el
             * recorte es del convertidor, y restarle la continua antes de
             * mirarlo seria medir el recorte de otra cosa. */
            sum_i += fi;
            sum_q += fq;
            s_wfm_i_buf[n] = fi - dci;
            s_wfm_q_buf[n] = fq - dcq;
        }
        rf_clip_veredicto(hits);

        sum_i *= (1.0f / (float32_t)SDR_RX_BLOCK_SAMPLES_WFM);
        sum_q *= (1.0f / (float32_t)SDR_RX_BLOCK_SAMPLES_WFM);
        {
            float32_t a = WFM_DC_ALFA;
            if (s_wfm_dc_n < 512U) {
                s_wfm_dc_n++;
                a = 1.0f / (float32_t)s_wfm_dc_n;   /* media corriente */
            }
            s_wfm_dc_i += a * (sum_i - s_wfm_dc_i);
            s_wfm_dc_q += a * (sum_q - s_wfm_dc_q);
            /* EL TOPE, que es lo que hace que esto sea seguro. Ver su
             * comentario delante de fm_discriminate(). */
            if (s_wfm_dc_i >  (float32_t)WFM_DC_TOPE) { s_wfm_dc_i =  (float32_t)WFM_DC_TOPE; }
            if (s_wfm_dc_i < -(float32_t)WFM_DC_TOPE) { s_wfm_dc_i = -(float32_t)WFM_DC_TOPE; }
            if (s_wfm_dc_q >  (float32_t)WFM_DC_TOPE) { s_wfm_dc_q =  (float32_t)WFM_DC_TOPE; }
            if (s_wfm_dc_q < -(float32_t)WFM_DC_TOPE) { s_wfm_dc_q = -(float32_t)WFM_DC_TOPE; }
        }
    }

    /* 0b. Pre-discriminator channel filter (IFBW), NARROW only - see
     * WFM_IFBW_NARROW_COEFFS' comment. WIDE (default) skips this
     * entirely: zero extra ISR cycles, byte-for-byte the same signal
     * path as before this control existed. */
    if (s_wfm_ifbw == WFM_IFBW_NARROW) {
        arm_biquad_cascade_df1_f32(&s_wfm_ifbw_i_inst, s_wfm_i_buf, s_wfm_i_buf, SDR_RX_BLOCK_SAMPLES_WFM);
        arm_biquad_cascade_df1_f32(&s_wfm_ifbw_q_inst, s_wfm_q_buf, s_wfm_q_buf, SDR_RX_BLOCK_SAMPLES_WFM);
    }

    /*
     * S-METER EN WFM - 22/09/2026. En WFM marcaba S0 todo el rato.
     *
     * Porque no lo actualizaba NADIE. s_sig_peak solo se escribia en
     * demod_am_process_raw(), que es el camino de AM/USB/LSB/NFM; WFM tiene
     * su propio camino y nunca pasaba por ahi, asi que el medidor se quedaba
     * en el 0 del arranque para siempre. El comentario de
     * demod_am_get_signal_peak() ya lo decia ("WFM has its own separate
     * S-meter path") - pero ese otro camino, s_wfm_agc_peak, sigue el
     * AUDIO de salida del discriminador, que en FM no dice nada del nivel de
     * RF: la FM es de envolvente constante, una emisora fuerte y una debil
     * dan la misma amplitud de audio y lo que cambia es el ruido. Usar eso
     * de S-metro habria sido peor que no tener ninguno, porque marcaria
     * segun lo que se este emitiendo en ese momento.
     *
     * Asi que se mide donde toca y donde lo mide el resto de modos: el
     * modulo |I+jQ| de la senal compleja ya filtrada de canal, ANTES del
     * discriminador. Mismo punto de la cadena, mismas unidades (las dos
     * ramas parten del entero de 16 bits del ADC convertido a float sin
     * normalizar), asi que la calibracion del medidor vale igual en WFM que
     * en los demas.
     *
     * Se guarda el pico al CUADRADO y se hace UNA raiz por bloque en vez de
     * 512: el bloque de WFM es el doble de largo que el de AM y esto va
     * dentro de la ISR. Para que la caida siga siendo la misma en dB por
     * segundo, la constante de relajacion se eleva tambien al cuadrado
     * (pico *= r por muestra equivale a pico2 *= r*r), y se usa la de WFM,
     * que es la que esta ajustada a 192 kHz. Va antes del reparto por perfil
     * de AGC para que tambien funcione en MANUAL.
     */
    {
        float sp2 = s_sig_peak * s_sig_peak;
        float r2  = s_wfm_agc_release * s_wfm_agc_release;

        for (n = 0; n < SDR_RX_BLOCK_SAMPLES_WFM; n++) {
            float m2 = s_wfm_i_buf[n] * s_wfm_i_buf[n]
                     + s_wfm_q_buf[n] * s_wfm_q_buf[n];
            sp2 *= r2;
            if (m2 > sp2) { sp2 = m2; }
        }
        s_sig_peak = sqrtf(sp2);
    }

    /* 1. Discriminate - delay-and-conjugate-multiply, straight on the
     * RAW (unfiltered unless IFBW NARROW just applied above, un-down-
     * mixed) I/Q, at the full 192kHz rate - see demod_am.h's WFM note
     * for the full derivation and why atan2f() rather than
     * arm_atan2_f32() here. */
    fm_discriminate(s_wfm_i_buf, s_wfm_q_buf, s_wfm_env, WFM_DISC_GAIN, SDR_RX_BLOCK_SAMPLES_WFM);

    if (do_log) {
        float mn = s_wfm_env[0], mx = s_wfm_env[0];
        for (n = 1; n < SDR_RX_BLOCK_SAMPLES_WFM; n++) {
            if (s_wfm_env[n] < mn) { mn = s_wfm_env[n]; }
            if (s_wfm_env[n] > mx) { mx = s_wfm_env[n]; }
        }
        debug_print_dec_signed_local("demod_wfm: [1] raw discriminator min", (int32_t)mn);
        debug_print_dec_signed_local("demod_wfm: [1] raw discriminator max", (int32_t)mx);
    }

    /* 2. El bloqueador de continua, con estado propio -s_wfm_dcb_x1 e
     * y1- y coeficiente propio -WFM_DCB_R, ajustado para 192 kHz, ver su
     * comentario-. *** ARREGLO DEL 05/08/2026 ***: esto TIENE que correr
     * justo aqui, inmediatamente despues del discriminador y ANTES del
     * desenfasis y del paso bajo de audio de mas abajo. El primer
     * borrador de esta funcion lo tenia DESPUES de esas dos etapas, lo
     * que no coincide con la cadena de WFM original ya validada antes de
     * la migracion -discriminar, bloquear la continua, desenfasis, paso
     * bajo- y es el principal sospechoso del aviso de que la WFM sonaba
     * como una NFM, una vez que se confirmo que el reloj estaba
     * bien (BCLK = 6,144 MHz y WCLK = 192 kHz, medidos). El desenfasis
     * es el mismo un paso bajo, asi que correrlo antes del bloqueador de
     * continua deja que la desviacion de continua que saca el
     * discriminador se quede dentro de la señal -y se mezcle con el
     * estado interno de dos etapas mas de filtro- durante un bloque
     * entero de mas antes de que se quite, en vez de arrancarla de
     * inmediato como hace el diseño validado. */
    for (n = 0; n < SDR_RX_BLOCK_SAMPLES_WFM; n++) {
        float y = s_wfm_env[n] - dcb_x1 + WFM_DCB_R * dcb_y1;
        dcb_x1 = s_wfm_env[n];
        dcb_y1 = y;
        s_wfm_env[n] = y;
    }

    /*
     * 2b. EL RDS SE PINCHA AQUI, y el sitio es lo importante. Lo que hay en
     * s_wfm_env[] en este punto es la señal MULTIPLEX entera, tal y como sale
     * del discriminador: audio, piloto de 19 kHz, estereo y la subportadora
     * de datos a 57 kHz. Dos lineas mas abajo entra la deenfasis, que es un
     * paso bajo, y despues el filtro de audio de 15 kHz: cualquiera de los
     * dos deja los 57 kHz en nada. O se coge aqui o no se coge.
     *
     * Solo cuesta algo si el RDS esta encendido; apagado, esto es una
     * comparacion por bloque. Ver User/rds.h.
     */
    if (rds_activo()) {
        rds_feed(s_wfm_env, SDR_RX_BLOCK_SAMPLES_WFM);
    }

    /* 3a. De-emphasis, single-pole LPF (see WFM_DEEMPH_ALPHA's
     * comment) - scalar, same reasoning as the DC blocker above for
     * not using a CMSIS block call. */
    {
        float y1 = s_wfm_deemph_y1;

        for (n = 0; n < SDR_RX_BLOCK_SAMPLES_WFM; n++) {
            y1 = WFM_DEEMPH_ALPHA * y1 + (1.0f - WFM_DEEMPH_ALPHA) * s_wfm_env[n];
            s_wfm_env[n] = y1;
        }
        s_wfm_deemph_y1 = y1;
    }

    /* 3b. WFM audio LPF, 4th-order Butterworth, width per s_audio_bw -
     * see WFM_ALPF_WIDE/NORM/NARROW_COEFFS' comment (01/09/2026: now
     * selectable, was unconditionally WIDE/15kHz before). Same
     * selector-pointer shape as AM/SSB's own BW switch in
     * demod_am_process_raw() just above. */
    {
        arm_biquad_casd_df1_inst_f32 *wfm_alpf;

        switch (s_audio_bw) {
        case AUDIO_BW_2K3: wfm_alpf = &s_wfm_alpf_norm_inst;   break; /* "NORM", 8kHz */
        case AUDIO_BW_1K8: wfm_alpf = &s_wfm_alpf_narrow_inst; break; /* "NARROW", 4kHz */
        case AUDIO_BW_4K0:
        default:            wfm_alpf = &s_wfm_alpf_wide_inst;   break; /* "WIDE", 15kHz - original/default */
        }
        arm_biquad_cascade_df1_f32(wfm_alpf, s_wfm_env, s_wfm_env, SDR_RX_BLOCK_SAMPLES_WFM);
    }

    if (do_log) {
        float mn = s_wfm_env[0], mx = s_wfm_env[0];
        for (n = 1; n < SDR_RX_BLOCK_SAMPLES_WFM; n++) {
            if (s_wfm_env[n] < mn) { mn = s_wfm_env[n]; }
            if (s_wfm_env[n] > mx) { mx = s_wfm_env[n]; }
        }
        debug_print_dec_signed_local("demod_wfm: [2] post-filter (pre-AGC) min", (int32_t)mn);
        debug_print_dec_signed_local("demod_wfm: [2] post-filter (pre-AGC) max", (int32_t)mx);
        debug_print_dec("demod_wfm: [2] s_wfm_agc_peak going in", (uint32_t)s_wfm_agc_peak);
        debug_print_dec("demod_wfm: [2] s_agc_profile (0=MAN,1=SLOW,2=MED,3=FAST)", (uint32_t)s_agc_profile);
    }

    /* 4. AGC (instant attack, slow release) + 5. clamp/duplicate to
     * L/R int16 - own state (s_wfm_agc_peak), own release coefficient
     * (s_wfm_agc_release, resolved from the SAME s_agc_profile
     * selector as AM/SSB/NFM's s_agc_release - see
     * demod_am_set_agc_profile()'s comment). Same MANUAL-bypass
     * structure as demod_am_process_raw()'s own step 4. */
    /*
     * LA DIVISION QUE SE QUEDO AQUI CUANDO SE QUITO DE LA OTRA - 09/10/2026.
     *
     * Este lazo era una copia a mano del de demod_am_process_raw(). El
     * 05/10/2026 a aquel se le saco la division de coma flotante de dentro
     * del bucle, y a este NO: seguia haciendo `AGC_TARGET / pk` por
     * muestra, o sea 192.000 divisiones por segundo -el doble que el otro,
     * porque la FM ancha va a 192 kHz- en el camino de interrupcion que
     * menos holgura tiene de todos, con un atan2f por muestra ya encima.
     * Son 7.168 ciclos por bloque de los 533.333 que hay: el 1,3%.
     *
     * No se arregla copiando otra vez la version buena: se arregla
     * borrando la copia. Los dos caminos llaman ahora a agc_bloque(), que
     * esta en agc.c y que tiene banco propio -`make agc`-. Ver agc.h.
     */
    if (s_agc_profile == AGC_PROFILE_MANUAL) {
        agc_manual(s_wfm_env, s_wfm_audio_out, SDR_RX_BLOCK_SAMPLES_WFM,
                   DEMOD_AM_GAIN);
    } else {
        s_wfm_agc_peak = agc_bloque(s_wfm_env, s_wfm_audio_out,
                                    SDR_RX_BLOCK_SAMPLES_WFM, peak,
                                    s_wfm_agc_release, WFM_AGC_PEAK_MIN,
                                    DEMOD_AM_GAIN);
    }

    s_wfm_dcb_x1 = dcb_x1;
    s_wfm_dcb_y1 = dcb_y1;

    if (do_log) {
        int16_t mn = s_wfm_audio_out[0], mx = s_wfm_audio_out[0];
        for (n = 1; n < SDR_RX_BLOCK_SAMPLES_WFM; n++) {
            if (s_wfm_audio_out[2U * n] < mn) { mn = s_wfm_audio_out[2U * n]; }
            if (s_wfm_audio_out[2U * n] > mx) { mx = s_wfm_audio_out[2U * n]; }
        }
        debug_print_dec_signed_local("demod_wfm: [3] final int16 output min", (int32_t)mn);
        debug_print_dec_signed_local("demod_wfm: [3] final int16 output max", (int32_t)mx);
    }

    if (muted) {
        /* Se calla SOLO LA SALIDA: todo lo de arriba -discriminador,
         * bloqueador de continua, desenfasis, paso bajo y automatico de
         * ganancia- ya ha corrido con normalidad sobre los datos de
         * verdad y ha actualizado su estado, exactamente igual que si no
         * hubiera silencio ninguno. Por que esa es justamente la gracia
         * esta en el comentario de WFM_SETTLE_MUTE_BLOCKS. Los valores
         * reales de s_wfm_audio_out, los de antes del silencio, ya los
         * ha capturado el bloque de diagnostico de justo encima, asi que
         * el registro sigue enseñando lo que el automatico de ganancia
         * calculo de verdad aunque esto ponga a cero lo que se oye. */
        for (n = 0; n < SDR_RX_BLOCK_SAMPLES_WFM; n++) {
            s_wfm_audio_out[2U * n]      = 0;
            s_wfm_audio_out[2U * n + 1U] = 0;
        }
    }

    /* Y el pitido de los avisos, LO ULTIMO: tapa el audio en vez de
     * mezclarse con el. Ver bip.h. Apagado no cuesta nada. */
    bip_mete(s_wfm_audio_out, (uint32_t)SDR_RX_BLOCK_SAMPLES_WFM,
             192000.0f);   /* la tasa de WFM, ver SDR_RX_BLOCK_SAMPLES_WFM */
    gd32_i2s_stream_write_half(s_wfm_audio_out);

    s_wfm_last_cycles = DWT->CYCCNT - cyc_start;
    }
}

/*
 * ======================================================================
 * EL METODO DE FASES, EN UN SOLO SITIO - 11/10/2026
 * ======================================================================
 *
 * Esto estaba escrito en linea dentro de la rama de USB/LSB. Ahora lo usan
 * DOS: esa rama y la de SAM, que para elegir banda hace exactamente lo
 * mismo con la senal ya desrotada por la portadora que recupera su PLL
 * (ver sam_bloque_cuad() en sam.h).
 *
 * Se SACA a una funcion en vez de copiarse, y no es manía: este fichero
 * tiene documentados varios fallos de "la misma cuenta escrita en dos
 * sitios que acabaron diciendo cosas distintas". Aqui ademas hay un
 * retardo que cruza el limite de bloque y una guarda con una historia
 * propia -ver dentro-, o sea de lo que peor se copia.
 *
 * `i_dec`/`q_dec` vienen a 12 kHz y ya diezmados. `sign` elige la banda y
 * sale de SSB_USB_SIGN / SSB_LSB_SIGN, los mismos para los dos llamadores:
 * asi "superior" quiere decir lo mismo en SAM que en banda lateral, que es
 * lo unico que se puede pedir de un ajuste que se llama igual.
 */
static void fases_combina(const float32_t *i_dec, const float32_t *q_dec,
                          float32_t sign, float32_t *fuera)
{
    uint32_t k;

    /* 2a. Hilbert-shift the decimated Q (90 degrees across the
     * audio band, now with proper coverage down to ~300Hz - see
     * the design numbers above HILBERT_COEFFS). */
    arm_fir_f32(&s_hilbert_inst, q_dec, s_q_hilbert_out, s_dec_block_samples);

    /* 2b. Delay-match the decimated I by the Hilbert's group
     * delay: history (previous block's tail) followed by the
     * current block, so s_i_delayed[k] for k=0..DEC_BLOCK_SAMPLES-1
     * equals I_dec[k - HILBERT_GROUP_DELAY_DEC] across the block
     * boundary. */
    for (k = 0; k < HILBERT_GROUP_DELAY_DEC; k++) {
        s_i_delayed[k] = s_i_delay_hist[k];
    }
    for (k = 0; k < s_dec_block_samples; k++) {
        s_i_delayed[HILBERT_GROUP_DELAY_DEC + k] = i_dec[k];
    }
    /* GCC's -Warray-bounds flags the line below at -O2 (a false
     * positive, exhaustively checked: s_dec_block_samples only
     * ever takes two real values, DEC_BLOCK_SAMPLES_96K(32) or
     * DEC_BLOCK_SAMPLES_48K(64), set exclusively by demod_am_set_
     * active_rate() - for k=0..HILBERT_GROUP_DELAY_DEC-1(30), the
     * index range is [1..31] @ 32, or [33..63] @ 64, always inside
     * i_dec[DEC_BLOCK_SAMPLES_MAX=64]'s real bounds. GCC can't
     * track that invariant across the demod_am_set_active_rate()/
     * demod_am_process_raw() function boundary at this
     * optimization level - the local `base` here doesn't change
     * the actual computed indices at all, just gives the compiler
     * a narrower expression to range-analyze in one place instead
     * of inside the subscript itself, which happens to suppress
     * the warning in practice for this specific access pattern. */
    {
        uint32_t base = s_dec_block_samples - HILBERT_GROUP_DELAY_DEC;
        /*
         * LA RED SUJETABA UN TRAPECIO QUE NO ERA - revision a fondo del
         * 08/10/2026.
         *
         * Aqui ponia `if (base > DEC_BLOCK_SAMPLES_MAX)`, y el indice
         * mas alto que se lee tres lineas mas abajo NO es base: es
         * base + HILBERT_GROUP_DELAY_DEC - 1, porque el bucle suma k.
         * O sea que la comprobacion dejaba pasar justo los treinta
         * valores de base que se salen del array por el otro extremo:
         * con base = DEC_BLOCK_SAMPLES_MAX la guarda decia que si y la
         * ultima lectura caia 29 posiciones fuera.
         *
         * Hoy no se llega ahi -s_dec_block_samples solo vale 32 o 64,
         * ver el comentario de arriba-, asi que esto no arregla ningun
         * sintoma: arregla la red. Una guarda que comprueba otra cosa
         * de la que protege es peor que no tenerla, porque el que la
         * lee se queda tranquilo. Comprobando el indice de verdad, la
         * red sujeta lo que dice sujetar, y GCC sigue teniendo su cota
         * explicita para dejar de avisar.
         *
         * La condicion esta escrita restando y no sumando -o sea
         * `base > MAX - RETARDO` en vez de `base + RETARDO > MAX`-
         * porque base es sin signo y viene de una RESTA: si algun dia
         * s_dec_block_samples fuese menor que el retardo, base seria un
         * numero enorme y sumarle 30 daria la vuelta al contador,
         * colandose por la guarda justo en el caso que mas falta hace
         * atrapar. Restando no hay vuelta posible: los dos son
         * constantes de compilacion y MAX es mayor que el retardo.
         */
        if (base > DEC_BLOCK_SAMPLES_MAX - HILBERT_GROUP_DELAY_DEC) { base = 0U; } /* unreachable in practice (see
                                                             * comment above) - just gives
                                                             * GCC's range analysis an
                                                             * explicit upper bound so it
                                                             * stops flagging the access
                                                             * below as possibly underflowed */
        for (k = 0; k < HILBERT_GROUP_DELAY_DEC; k++) {
            s_i_delay_hist[k] = i_dec[base + k];
        }
    }

    /* 2c. Combine: one sideband adds, the other cancels. */
    for (k = 0; k < s_dec_block_samples; k++) {
        fuera[k] = s_i_delayed[k] + sign * s_q_hilbert_out[k];
    }
}

/*
 * LA CADENA DE UNA SOLA BANDA, con el destino elegible. Era el cuerpo del
 * else de la rama de SAM y sale aqui porque desde el 13/10/2026 se llama
 * desde DOS sitios -enganchado escribe en la salida de audio, sin
 * enganchar escribe aparte para poder mezclarla con la envolvente- y dos
 * copias de una cadena de cuatro filtros es como se consigue que un dia
 * solo una de las dos tenga el signo bueno.
 *
 * LOS BUFFERS ENTRAN POR ARGUMENTO Y NO POR NOMBRE, y no es manía: los
 * del camino estrecho comparten bytes con los de WFM (la union s_via), y
 * lo que hace correcto compartirlos es que se toquen SOLO dentro de
 * demod_am_process_raw(). tools/via_audio_check.py lo comprueba leyendo el
 * fichero, y se puso rojo en cuanto esta funcion los nombro. Era cierto:
 * nombrarlos aqui es exactamente el "mover un trozo de proceso a una
 * funcion auxiliar" contra el que avisa su cabecera.
 */
static void sam_banda_lateral(float32_t *ib, float32_t *qb, float32_t *salida)
{
    float32_t sign = (s_sam_banda == (uint8_t)SAM_BANDA_USB)
                   ? SSB_USB_SIGN : SSB_LSB_SIGN;

    sam_bloque_cuad(&s_sam, ib, qb, ib, qb,
                    (uint32_t)SDR_RX_BLOCK_SAMPLES, demod_am_active_fs_hz());
    arm_fir_decimate_f32(&s_decim_i_inst, ib, s_i_dec, SDR_RX_BLOCK_SAMPLES);
    arm_fir_decimate_f32(&s_decim_q_inst, qb, s_q_dec, SDR_RX_BLOCK_SAMPLES);
    fases_combina(s_i_dec, s_q_dec, sign, s_ssb_dec);
    arm_fir_interpolate_f32(&s_interp_inst, s_ssb_dec, salida,
                            s_dec_block_samples);
}

void demod_am_process_raw(const int16_t *raw_interleaved)
{
    uint32_t n;
    float dcb_x1;
    float dcb_y1;
    float peak;
    uint32_t cyc_start;
    uint8_t do_log;
    uint8_t muted;
    uint32_t nr_ssb_cycles = 0U;

    /*
     * *** 05/08/2026, parche: ver la guarda identica de
     * demod_wfm_process_raw() y el comentario de
     * sdr_rx_last_block_corrupted() para el razonamiento entero ***. Se
     * tira un bloque que llego con SPI_STAT_FERR antes de tocar ningun
     * estado, con lo que el hueco del buffer de transmision se queda con
     * lo que ya tenia -una repeticion breve que no se oye- en vez de
     * reproducir muestras que podrian venir mal repartidas entre
     * canales.
     */
    if (sdr_rx_last_block_corrupted()) {
        return;
    }

    /* El recorte se mide dentro del deinterleave (etapa 0a), que ya carga
     * estas mismas muestras. Ver rf_clip_veredicto(). Sigue siendo sobre el
     * bloque crudo y despues de la guarda de bloque corrupto. */

    dcb_x1 = s_dcb_x1;
    dcb_y1 = s_dcb_y1;
    peak = s_agc_peak;
    cyc_start = DWT->CYCCNT;
    do_log = am_should_log_diag(); /* decide BEFORE incrementing below */
    muted = (s_am_mute_remaining > 0U) ? 1U : 0U; /* see AM_SETTLE_MUTE_BLOCKS' comment */
    /* NR cost when done INLINE on s_ssb_dec for USB/LSB (see step 1's
     * SSB branch, and the 3b. NR block's comment below for why SSB
     * doesn't go through the separate decimate/interpolate path AM
     * uses). Stays 0 for every other case (AM, WFM, NFM, or SSB with
     * NR off) - see the 3b. NR block for how it's actually used. */

    if (do_log) {
        debug_print_dec("demod_am: block# (mode 0=AM,1=USB,2=LSB,3=NFM follows)",
                         s_am_diag_count);
        debug_print_dec("demod_am: mode", (uint32_t)s_mode);
    }
    s_am_diag_count++; /* unconditional - see am_should_log_diag()'s comment */
    if (muted) {
        s_am_mute_remaining--;
    }

    /* Raw sample dump - see demod_wfm_process_raw()'s identical block
     * for the full reasoning (same suspicion, same test, both sides
     * of every switch). */
    if (do_log && s_am_diag_count == 1U) {
        for (n = 0; n < 8U; n++) {
            debug_print_dec_signed_local("demod_am: raw I", (int32_t)raw_interleaved[2U * n]);
            debug_print_dec_signed_local("demod_am: raw Q", (int32_t)raw_interleaved[2U * n + 1U]);
        }
    }

    /* 0a. Deinterleave raw int16 L/R (I/Q) into separate float rails.
     * Plain cast, same scale as before (no -1..1 normalization) so
     * the AGC's int16-scale targeting downstream doesn't change.
     *
     * Y DE PASO EL RECORTE DE RADIOFRECUENCIA, que hasta el 10/10/2026 era
     * una pasada entera aparte sobre este mismo buffer. Ver
     * rf_clip_veredicto(). */
    {
        uint32_t hits = 0U;
        for (n = 0; n < SDR_RX_BLOCK_SAMPLES; n++) {
            int16_t vi = raw_interleaved[2U * n];
            int16_t vq = raw_interleaved[2U * n + 1U];
            hits += rf_clip_una(vi);
            hits += rf_clip_una(vq);
            s_i_buf[n] = (float32_t)vi;
            s_q_buf[n] = (float32_t)vq;
        }
        rf_clip_veredicto(hits);
    }

    /* 0a-bis. NOISE BLANKER (23/09/2026). Va EXACTAMENTE aqui y no una
     * linea mas abajo: a estas alturas el bloque sigue teniendo los 96 kHz
     * enteros, que es donde un pulso todavia dura las pocas muestras que
     * dura. En cuanto pase por el filtro de canal de 0c, ese mismo pulso se
     * convierte en el timbre del filtro -milisegundos, y ya sin destacar
     * sobre nada-, y no habria forma de cazarlo. Ver nb.h. Con el nivel a 0
     * sale por la puerta sin tocar el bloque. */
    (void)nb_process(s_i_buf, s_q_buf, SDR_RX_BLOCK_SAMPLES);

    /* 0b/0c: LOW-IF DOWN-MIX + CHANNEL FILTER - unconditional now
     * (05/08/2026): WFM moved to its OWN separate processing path
     * (demod_wfm_process_raw(), see its comment) with its own hook,
     * registered/unregistered via sdr_rx_set_block_hook() on mode
     * switch (see main.c) - this function is never even CALLED while
     * WFM is selected anymore, so the old "if (s_mode != WFM)" guard
     * here was becoming dead-but-misleading code, not a real
     * conditional. NFM stays in this branch (it needs the down-mix
     * just as much as AM/SSB do) - it just gets its OWN, wider channel
     * filter instead of CHF_COEFFS, selected inside 0c below - see
     * demod_am.h's NFM note. */
    {
        /* 0b. LA BAJADA EN FRECUENCIA (giro a SR/4; ver el bloque de
         * comentario que hay encima de s_mix_hz): trae la señal buena de
         * vuelta a continua, para que CHF_COEFFS -diseñado para una
         * señal centrada en continua- siga valiendo sin tocar nada. Solo
         * corre mientras el oscilador local esta de verdad sintonizado
         * con el desplazamiento; por que esto tiene que ir de acuerdo
         * con el oscilador real en lugar de estar siempre encendido esta
         * en la nota de SINTONIA POR FRECUENCIA INTERMEDIA BAJA de
         * demod_am.h. Sin multiplicaciones: solo cambios de signo e
         * intercambios, y en el sitio. */
        if (s_mix_hz != 0.0f) {
            float fs = demod_am_active_fs_hz();

            /* Lo derivado, recalculado solo si alguna de sus dos entradas ha
             * cambiado - ver s_mix_nco. Dos comparaciones por bloque. */
            if (s_mix_hz != s_mix_hz_puesto || fs != s_mix_fs_puesto) {
                s_mix_hz_puesto = s_mix_hz;
                s_mix_fs_puesto = fs;
                s_mix_rapido = (s_mix_hz == fs * 0.25f) ? 1U : 0U;
                nco_freq(&s_mix_nco, s_mix_hz, fs);
            }

            if (s_mix_rapido) {
                /* El de toda la vida: a Fs/4 exactos la rotacion pasa por
                 * 1, -j, -1, +j y no hay nada que multiplicar. Es el modo
                 * normal del aparato, asi que sigue costando cero. */
                for (n = 0; n < SDR_RX_BLOCK_SAMPLES; n += 4U) {
                    float32_t hh1, hh2;

                    /* n+0: identity, leave as-is. */

                    /* n+1: (I,Q) -> (Q, -I), i.e. x -j */
                    hh1 =  s_q_buf[n + 1U];
                    hh2 = -s_i_buf[n + 1U];
                    s_i_buf[n + 1U] = hh1;
                    s_q_buf[n + 1U] = hh2;

                    /* n+2: (I,Q) -> (-I, -Q), i.e. x -1 */
                    hh1 = -s_i_buf[n + 2U];
                    hh2 = -s_q_buf[n + 2U];
                    s_i_buf[n + 2U] = hh1;
                    s_q_buf[n + 2U] = hh2;

                    /* n+3: (I,Q) -> (-Q, I), i.e. x +j */
                    hh1 = -s_q_buf[n + 3U];
                    hh2 =  s_i_buf[n + 3U];
                    s_i_buf[n + 3U] = hh1;
                    s_q_buf[n + 3U] = hh2;
                }
                /*
                 * Y se avanza la fase del NCO como si la hubiera generado el,
                 * aunque no se haya usado. Cuesta una multiplicacion por
                 * BLOQUE y evita un chasquido: con el NCO de sintonia puesto,
                 * la distancia pasa por Fs/4 exactos al aparcar, asi que se
                 * salta de este camino al otro y vuelta. Sin esto, cada salto
                 * seria un corte de fase.
                 */
                s_mix_nco.fase += s_mix_nco.inc * (uint32_t)SDR_RX_BLOCK_SAMPLES;
            } else {
                /* Cualquier otra distancia: NCO. Solo se llega aqui con el
                 * NCO de sintonia puesto. */
                nco_mezcla(&s_mix_nco, s_i_buf, s_q_buf, SDR_RX_BLOCK_SAMPLES);
            }
        }

        /* 0c. CHANNEL FILTER: identical 4th-order Butterworth LPF on I
         * and Q via CMSIS-DSP (in-place: pSrc==pDst is safe here, see
         * arm_biquad_cascade_df1_f32.c - each sample is read before it's
         * overwritten, no lookahead). Complex LPF centered on the VFO.
         * NFM uses its OWN, wider filter (NFM_CHF_COEFFS) instead of
         * CHF_COEFFS - see demod_am.h's NFM note on why AM/SSB's ~4kHz
         * corner would clip narrowband FM's modulated sidebands. */
        if (s_mode == (uint8_t)DEMOD_MODE_NFM) {
            /* La nota de demod_am_chf_ancho(), aplicada aqui y no alli:
             * ver su comentario. Una comparacion por bloque. */
            if (s_ais_chf_pide != 0U) {
                s_ais_chf = (uint8_t)(s_ais_chf_pide == 1U);
                s_ais_chf_pide = 0U;
                nfm_chf_carga(s_active_rate_is_48k);
            }
            arm_biquad_cascade_df1_f32(&s_nfm_chf_i_inst, s_i_buf, s_i_buf, SDR_RX_BLOCK_SAMPLES);
            arm_biquad_cascade_df1_f32(&s_nfm_chf_q_inst, s_q_buf, s_q_buf, SDR_RX_BLOCK_SAMPLES);
        } else {
            arm_biquad_cascade_df1_f32(&s_chf_i_inst, s_i_buf, s_i_buf, SDR_RX_BLOCK_SAMPLES);
            arm_biquad_cascade_df1_f32(&s_chf_q_inst, s_q_buf, s_q_buf, SDR_RX_BLOCK_SAMPLES);
        }
    }

    s_last_cycles_frontend = DWT->CYCCNT - cyc_start;

    /* EL MEDIDOR DE S, añadido el 05/09/2026: sigue el pico del modulo
     * |I+jQ| EN CRUDO de s_i_buf y s_q_buf, ya bajados en frecuencia y
     * filtrados de canal, ANTES de que ninguno de los pasos 1 a 4 de
     * abajo los toque. Asi la lectura refleja el nivel real de
     * radiofrecuencia y no lo que quede de la envolvente despues de que
     * el bloqueador de continua se lleve la portadora. El por que entero
     * esta en el comentario de demod_am_get_signal_peak(). Los tiempos
     * son los mismos que usa el seguidor de pico del automatico de
     * ganancia -ataque instantaneo y s_agc_release para la soltada, paso
     * 4 mas abajo-, solo que con su propio pico y su propio punto de
     * toma: este es solo para enseñarlo y no alimenta nunca el camino de
     * ganancia del audio. Corre en todos los modos que llegan a esta
     * funcion -AM, USB, LSB y NFM por igual-, igual que la medida del
     * silenciador de justo debajo lo hace para AM y NFM; al contrario
     * que el silenciador, esto no se limita a esos dos modos, porque una
     * lectura de fuerza de señal tiene el mismo sentido en banda
     * lateral. */
    /*
     * AL CUADRADO, Y UNA SOLA RAIZ POR BLOQUE - 05/10/2026.
     *
     * Aqui habia una raiz cuadrada POR MUESTRA: 256 por bloque, 96.000 por
     * segundo, en AM, SAM, NFM, USB y LSB -o sea todos los modos que pasan
     * por aqui, sin interruptor que lo apague-. La VSQRT del coprocesador
     * son ~14 ciclos, asi que son 1,5 millones de ciclos por segundo, el
     * 0,8% del chip, para un numero que solo mueve una barra en la pantalla.
     *
     * Y el arreglo ya estaba escrito EN ESTE MISMO FICHERO, veinte lineas
     * mas arriba, en el camino de WFM: seguir el pico AL CUADRADO y sacar
     * una sola raiz al final del bloque. Comparar cuadrados ordena igual que
     * comparar raices -las dos cantidades son positivas-, asi que el pico
     * que sale es EXACTAMENTE el mismo; y para que la caida siga siendo la
     * misma en dB por segundo, la constante de relajacion se eleva tambien
     * al cuadrado, porque pico *= r por muestra equivale a pico2 *= r*r.
     *
     * El camino de AM nunca recibio ese arreglo. Ahora si.
     */
    /*
     * Y DE PASO SE DEJA |z| HECHO - 10/10/2026.
     *
     * Este bucle calcula I²+Q² para cada muestra... y veinte lineas mas
     * abajo el camino de AM lo volvia a calcular DESDE CERO para sacarle la
     * raiz, y el de NFM otra vez mas para el silenciador. Tres pasadas sobre
     * los mismos dos buffers calculando el mismo numero.
     *
     * Ahora se guarda aqui, en s_env[], que es justo donde el camino de AM
     * lo quiere. Los que no lo necesitan -SSB- lo pisan enseguida con su
     * propia extraccion, asi que no cuesta nada.
     *
     * El resultado es bit a bit el mismo: sqrtf() del MISMO m2.
     */
    {
        float sp2 = s_sig_peak * s_sig_peak;
        float r2  = s_agc_release * s_agc_release;
        /*
         * Y EN NFM, LA SUMA DE MAGNITUDES PARA EL SILENCIADOR.
         *
         * En AM el silenciador reutiliza s_env[], que lleva la magnitud. En
         * NFM no puede, porque alli s_env lo pisa el discriminador, asi que
         * recorria OTRA VEZ los dos buffers sacando 256 raices mas sobre el
         * mismo I²+Q² que este bucle acaba de calcular.
         *
         * La raiz hay que hacerla igual -es la metrica, media de magnitud-
         * pero las dos lecturas, los dos productos y la suma se ahorran. La
         * rama se predice perfecta: el modo no cambia dentro del bloque.
         */
        const uint8_t es_nfm = (uint8_t)(s_mode == (uint8_t)DEMOD_MODE_NFM);

        s_sq_sum_mag = 0.0f;
        for (n = 0; n < SDR_RX_BLOCK_SAMPLES; n++) {
            float m2 = s_i_buf[n] * s_i_buf[n] + s_q_buf[n] * s_q_buf[n];
            s_env[n] = m2;              /* al CUADRADO todavia: ver abajo */
            if (es_nfm) { s_sq_sum_mag += sqrtf(m2); }
            sp2 *= r2;
            if (m2 > sp2) { sp2 = m2; }
        }
        s_sig_peak = sqrtf(sp2);
    }

    /* 1. Audio extraction - branches by mode. All paths write into
     * s_env[], which steps 2-5 below (DC blocker, audio LPF, AGC,
     * output) consume - WFM's LPF sub-step differs, see step 3. */
    if (s_mode == (uint8_t)DEMOD_MODE_AM) {
        /*
         * |z| = |I + jQ|, SIN COPIARLOS ANTES - 05/10/2026.
         *
         * Aqui se entrelazaban I y Q en un tercer buffer SOLO para poder
         * llamar a arm_cmplx_mag_f32(), que es lo que pide CMSIS. Son 512
         * lecturas y 512 escrituras por bloque -192.000 accesos a memoria
         * por segundo- que no calculan nada: solo mueven los mismos numeros
         * de un sitio a otro.
         *
         * La raiz cuadrada la hace el coprocesador en hardware (VSQRT.F32),
         * asi que el bucle de aqui hace exactamente lo mismo que la rutina
         * de CMSIS, leyendo los dos buffers donde ya estan. El resultado es
         * bit a bit el mismo: es la misma operacion.
         */
        /* El I²+Q² ya lo dejo el bucle del medidor de señal en s_env[]
         * (10/10/2026): aqui solo queda sacarle la raiz. La misma VSQRT
         * sobre el mismo numero, o sea bit a bit lo mismo que antes, pero
         * sin volver a leer los dos buffers ni repetir dos productos y una
         * suma por muestra. */
        for (n = 0; n < SDR_RX_BLOCK_SAMPLES; n++) {
            s_env[n] = sqrtf(s_env[n]);
        }

        /* DCF77 (23/09/2026): una muestra de envolvente por bloque, que a
         * 96 kHz son 375 al segundo - de sobra para distinguir marcas de
         * 100 y 200 ms. Se toma la MEDIA del bloque y no una muestra suelta:
         * promediar 256 muestras ya son 24 dB de ruido menos, gratis, antes
         * incluso del filtro del propio decodificador.
         *
         * Se llama desde la interrupcion a proposito, no desde el bucle
         * principal: las duraciones se miden CONTANDO muestras, asi que
         * perder una porque el bucle principal estuviera ocupado pintando la
         * cascada falsearia un bit. Lo que corre aqui es minimo -un polo, un
         * seguidor de pico y una comparacion- y solo una vez por bloque. */
        if (dcf77_activo()) {
            /*
             * Cuantas muestras por bloque y de que -envolvente o fase- lo
             * dice la tabla de emisoras de dcf77.c, no este fichero. Aqui
             * solo se parte el bloque en `sub` trozos iguales y se promedia
             * cada uno.
             *
             * Se llama desde la interrupcion a proposito, no desde el bucle
             * principal: las duraciones se miden CONTANDO muestras, asi que
             * perder una porque el bucle principal estuviera ocupado
             * pintando la cascada falsearia un bit.
             *
             * Promediar y no coger una muestra suelta: 256 muestras
             * promediadas son 24 dB de ruido menos, gratis, antes incluso
             * del filtro del propio decodificador. Y en las de fase se
             * promedian I y Q POR SEPARADO y se saca el angulo del
             * promedio - promediar angulos no vale, dan la vuelta -.
             */
            /* El `sub` de la tabla tiene que dividir al bloque: si no
             * dividiera, el ultimo trozo se quedaria corto y las duraciones
             * saldrian mal SIN que nada fallara al compilar. Los valores que
             * usa la tabla son 1, 4 y 8, y 256 los divide a todos. */
            _Static_assert((SDR_RX_BLOCK_SAMPLES % 8U) == 0U
                           && (SDR_RX_BLOCK_SAMPLES % 4U) == 0U,
                           "SDR_RX_BLOCK_SAMPLES no es divisible por los `sub` de k_emisoras[]");
            uint32_t sub = dcf77_submuestras(dcf77_emisora());
            uint32_t tam = SDR_RX_BLOCK_SAMPLES / sub;
            uint32_t q, j;
            float inv = 1.0f / (float)tam;

            if (!dcf77_usa_fase(dcf77_emisora())) {
                for (q = 0U; q < sub; q++) {
                    float suma = 0.0f;
                    for (j = 0U; j < tam; j++) { suma += s_env[q * tam + j]; }
                    dcf77_feed(suma * inv);
                }
            } else {
                for (q = 0U; q < sub; q++) {
                    float si = 0.0f, sq = 0.0f;
                    for (j = 0U; j < tam; j++) {
                        si += s_i_buf[q * tam + j];
                        sq += s_q_buf[q * tam + j];
                    }
                    /* rapido.c y no la biblioteca: esto corre hasta ocho
                     * veces por bloque dentro de la interrupcion, y lo que
                     * se mide despues son duraciones de 100 y 200 ms. Seis
                     * decimas de grado no cambian ni un bit de eso, y
                     * ademas ya vienen promediadas 32 muestras de I y de Q
                     * antes de llegar aqui. 05/10/2026. */
                    /* El paso bajo de 40 Hz y el atan2 viven ahora en
                     * dcf77.c -dcf77_fase_iq()-, que es donde hay banco que
                     * los mire. Aqui se promediaba y se llamaba a atan2 a
                     * pelo, con ~750 Hz de ancho de ruido entrando en una
                     * medida que luego se INTEGRA. Ver su comentario. */
                    dcf77_feed(dcf77_fase_iq(si, sq));
                }
            }
        }
    } else if (s_mode == (uint8_t)DEMOD_MODE_SAM) {
        /* Synchronous AM (both sidebands) - see sam.h/sam.c. Same
         * s_i_buf/s_q_buf tap AM's own envelope detector uses (96kHz,
         * post channel-filter) - apply_lo_tune() already treats every
         * non-WFM mode (AM included) with the same low-IF tuning
         * scheme, so the real carrier is already correctly centered
         * near DC here regardless of mode, no special tuning handling
         * needed for SAM specifically. */
        {
        if (!s_sam_init_done) {
            s_sam_margen_puesto = s_sam_margen_hz;
            sam_init(&s_sam, demod_am_active_fs_hz(),
                     (float32_t)s_sam_margen_puesto, 200.0f, 65.0f / 75.0f);
            s_sam_init_done = 1u;
        } else if (s_sam_margen_puesto != s_sam_margen_hz) {
            /* El ajuste se ha movido con la radio andando. Una comparacion
             * por bloque -cada 2,7 ms- y nada mas; ver demod_am.h. */
            s_sam_margen_puesto = s_sam_margen_hz;
            sam_margen_pon(&s_sam, (float32_t)s_sam_margen_puesto,
                           demod_am_active_fs_hz());
        }
        /*
         * EL BLOQUE ENTERO DE UNA LLAMADA - 05/10/2026.
         *
         * Aqui habia un bucle llamando a sam_step() 256 veces. Lo que cuesta
         * eso de mas no es el bucle: es que entre llamada y llamada el
         * compilador tiene que dar por perdido todo lo que tenia en
         * registros, asi que el estado del PLL -doce variables- iba y venia
         * a memoria en CADA muestra. Con el bucle dentro de sam.c va una
         * vez, al final. Ver sam_bloque().
         */
        /*
         * ==============================================================
         * Y UNA SOLA BANDA, SI SE PIDE - 11/10/2026
         * ==============================================================
         *
         * sam.h lo tenia escrito como pendiente desde agosto: el codigo
         * del que se porto traia SAML/SAMU y se dejaron fuera porque "no
         * habia coeficientes Hilbert verificados" y se prefirio entregar
         * un SAM de dos bandas correcto antes que adivinarlos.
         *
         * ESE BLOQUEO YA NO EXISTE. Esta radio tiene desde entonces un
         * Hilbert de 63 derivaciones a 12 kHz, verificado y con banco
         * propio: el que hace USB y LSB. Lo que faltaba no eran
         * coeficientes, era darse cuenta de que ya estaban.
         *
         * Y NO HACE FALTA NADA MAS, porque el PLL ya calcula por muestra
         * la senal desrotada por la portadora que ha recuperado -sus c0 y
         * c1-, y eso es exactamente lo que come la cadena de banda
         * lateral. SAM superior es "demodulacion USB con el oscilador
         * enganchado a la portadora en vez de libre".
         *
         * SE REUTILIZAN EL DIEZMADOR, EL HILBERT Y EL INTERPOLADOR de
         * USB/LSB, sin estado nuevo: SAM y banda lateral estan en el
         * mismo else-if, o sea que nunca corren a la vez. Es el mismo
         * criterio con el que WFM y NFM comparten s_fm_i_prev, y esta
         * dicho alli. Cambiar de modo deja un transitorio del largo del
         * filtro -3,7 ms- que no se oye.
         *
         * Y SIN UN BUFFER NUEVO: sam_nucleo() lee las dos entradas a
         * locales ANTES de escribir ninguna salida, asi que las salidas
         * pueden ir encima de las entradas, cada una en su sitio.
         *
         * El filtro de canal, por cierto, NO sirve para esto: es un
         * biquad real aplicado a I y a Q por separado, o sea simetrico
         * por construccion. Y despues del detector las dos bandas estan
         * sumadas y ya no se separan. Tenia que ser aqui, en complejo y
         * antes de quedarse con la parte real.
         */
        /*
         * ==============================================================
         * SIN ENGANCHE NO HAY DEMODULACION SINCRONA: HAY AM - 13/10/2026
         * ==============================================================
         *
         * Pedido por un usuario.
         *
         * Y es exactamente lo que tiene que pasar con un detector de
         * producto cuyo oscilador no esta enganchado: lo que sale es la
         * señal batida contra un oscilador que esta a otra frecuencia, y
         * esa diferencia es un tono. Cuanto mas desafinado, mas agudo.
         *
         * LA V4.61 LO EMPEORO, y hay que decirlo: al bajar el margen de
         * enganche de 4.000 a 100 Hz -para que el lazo no se fuera con la
         * emisora de al lado- se gano eso y se perdio esto, porque ahora
         * una portadora a 400 Hz ya no se puede alcanzar y el lazo se
         * queda contra el tope pitando. Antes enganchaba y no pitaba.
         *
         * LA SALIDA NO ES ELEGIR ENTRE LAS DOS COSAS. Un SAM desenganchado
         * no tiene nada que ofrecer: su gracia -no distorsionar cuando la
         * portadora se desvanece- solo existe estando enganchado. Asi que
         * cuando no lo esta, lo que sale es la ENVOLVENTE, o sea AM de
         * toda la vida: no pita, suena como el modo AM, y en cuanto el
         * lazo engancha se pasa a la demodulacion sincrona sin que nadie
         * tenga que tocar nada.
         *
         * SE CRUZA, NO SE CONMUTA: `s_sam_mezcla` va de 0 a 1 con una
         * constante de tiempo de unos 50 ms. Conmutar de golpe entre dos
         * señales que no tienen la misma continua es un golpe en el
         * altavoz, y el enganche entra y sale varias veces seguidas
         * cuando la portadora esta justo en el limite.
         *
         * Y SIN UN BUFFER NUEVO: s_env trae todavia I^2+Q^2 -lo dejo el
         * bucle del medidor de señal-, asi que la envolvente sale de ahi
         * con una raiz, y la salida del SAM se escribe encima de s_i_buf,
         * que a partir de ese punto ya no lo lee nadie.
         */
        {
            uint8_t eng = sam_enganchado(&s_sam);
            float32_t objetivo = eng ? 1.0f : 0.0f;

            /* Un bloque son 256 muestras: a 96 kHz, 2,7 ms. 0,05 por
             * bloque son unos 50 ms de constante de tiempo. */
            s_sam_mezcla += 0.05f * (objetivo - s_sam_mezcla);
            if (s_sam_mezcla > 0.999f) { s_sam_mezcla = 1.0f; }
            if (s_sam_mezcla < 0.001f) { s_sam_mezcla = 0.0f; }
        }

        if (s_sam_mezcla >= 1.0f) {
            /* Enganchado del todo: el camino de siempre, sin pagar ni una
             * multiplicacion de mas. */
            if (s_sam_banda == (uint8_t)SAM_BANDA_AMBAS) {
                sam_bloque(&s_sam, s_i_buf, s_q_buf, s_env,
                           (uint32_t)SDR_RX_BLOCK_SAMPLES,
                           demod_am_active_fs_hz());
            } else {
                sam_banda_lateral(s_i_buf, s_q_buf, s_env);
            }
        } else {
            float32_t media = 0.0f;
            float32_t k = s_sam_mezcla;
            uint32_t m;

            /* La envolvente, de lo que s_env ya traia, y su media para
             * quitarle la portadora: la salida del SAM no la lleva -se la
             * quita su nivelador- y mezclar una con continua y otra sin
             * ella movería la continua con la mezcla. */
            for (m = 0U; m < (uint32_t)SDR_RX_BLOCK_SAMPLES; m++) {
                s_env[m] = sqrtf(s_env[m]);
                media += s_env[m];
            }
            media /= (float32_t)SDR_RX_BLOCK_SAMPLES;

            if (s_sam_banda == (uint8_t)SAM_BANDA_AMBAS) {
                sam_bloque(&s_sam, s_i_buf, s_q_buf, s_i_buf,
                           (uint32_t)SDR_RX_BLOCK_SAMPLES,
                           demod_am_active_fs_hz());
            } else {
                sam_banda_lateral(s_i_buf, s_q_buf, s_i_buf);
            }
            for (m = 0U; m < (uint32_t)SDR_RX_BLOCK_SAMPLES; m++) {
                s_env[m] = k * s_i_buf[m] + (1.0f - k) * (s_env[m] - media);
            }
        }
        }
    } else if (s_mode == (uint8_t)DEMOD_MODE_NFM) {
        /* NFM: same discriminator as WFM (see fm_discriminate()'s
         * comment), but on the down-mixed + NFM_CHF_COEFFS-filtered
         * I/Q from steps 0b/0c above, not the raw signal - see
         * demod_am.h's NFM note on why NFM keeps that narrowband
         * front-end instead of skipping it like WFM does. */
        fm_discriminate(s_i_buf, s_q_buf, s_env, NFM_DISC_GAIN, SDR_RX_BLOCK_SAMPLES);

        /*
         * APRS, 24/09/2026. Se engancha AQUI y no donde los otros cinco:
         * el paquete de VHF va en FM, asi que sus dos tonos salen del
         * discriminador, no del camino de banda lateral.
         *
         * Y se coge JUSTO al salir del discriminador, antes del filtro de
         * audio y antes del silenciador. Lo del silenciador no es un detalle:
         * en paquete no hay portadora continua, las tramas son rafagas de
         * medio segundo con silencio en medio, y un silenciador que abre
         * cuando ya ha empezado la trama se come las banderas del principio -
         * que son precisamente con las que el lazo de reloj se engancha-.
         */
        if (ax25_activo()) {
            ax25_process(s_env, SDR_RX_BLOCK_SAMPLES);
            aprs_scope_feed(s_env, SDR_RX_BLOCK_SAMPLES);
        }
        /*
         * Y AIS, del mismo sitio exacto y por las mismas razones: crudo, a
         * la salida del discriminador, antes del filtro de audio y ANTES
         * DEL SILENCIADOR.
         *
         * Lo del silenciador importa aqui todavia mas que en el APRS: una
         * rafaga de AIS dura 26,7 milisegundos - la mitad que una de
         * paquete - y empieza con 24 bits de preambulo que son justo con
         * los que el lazo de reloj se engancha. Un silenciador que abra
         * cuando la rafaga ya ha empezado se come el preambulo entero y no
         * queda nada con lo que sincronizar.
         */
        if (ais_modo_activo()) {
            ais_modo_mete(s_env, (uint16_t)SDR_RX_BLOCK_SAMPLES);
        }
        /*
         * SSTV POR FM - 13/10/2026.
         *
         * Pedido por un usuario.
         *
         * Y es verdad: en onda corta la SSTV va por banda lateral, pero en
         * dos metros va en FM, que es donde esta la ISS. El decodificador
         * es EL MISMO -mide la frecuencia instantanea de un tono de audio,
         * y le da igual de donde venga ese audio-; lo unico que cambia es
         * por donde se le mete.
         *
         * Es exactamente el reparto que ya hacen el APRS y el AIS aqui
         * arriba: el mismo decodificador, dos sitios de entrada segun el
         * modo. Lo que se elige con "SSTV" o "SSTV-FM" en la lista de
         * modos no es otro decodificador, es otro camino.
         *
         * Y AQUI SE DIEZMA, que es lo unico que tiene miga. Los otros dos
         * se comen el audio a la tasa del discriminador -48 o 96 kHz- pero
         * el de SSTV arranca con sstv_start(DEMOD_DEC_FS_HZ), o sea a 12
         * kHz, igual por los dos caminos: asi no hay dos relojes de linea
         * distintos segun el modo, que es la clase de diferencia que luego
         * sale como una foto torcida sin que se vea por que. A 96 kHz,
         * ademas, su discriminador costaria ocho veces mas dentro de la
         * interrupcion de audio.
         *
         * SE REUSAN EL DIEZMADOR Y EL BUFFER DE LA REDUCCION DE RUIDO, y
         * no es dejadez: ese par solo trabaja con AM o SAM y con el NR o
         * el notch puestos (ver el paso 2e), o sea que en NFM esta parado.
         * Montar un segundo juego serian cientos de bytes de SRAM, y hoy
         * quedan menos de tres mil, para que uno de los dos este siempre
         * sin usar. Los dos usos no pueden coincidir: aquel pide AM o SAM
         * y este es NFM.
         *
         * CRUDO, antes del filtro de audio y ANTES DEL SILENCIADOR, por lo
         * mismo que el APRS y el AIS: una foto son dos minutos seguidos de
         * señal y un silenciador que abra tarde se come la cabecera VIS,
         * que es con lo que el decodificador sabe que modo es.
         */
        if (sstv_activo()) {
            arm_fir_decimate_f32(&s_nr_decim_inst, s_env, s_nr_buf,
                                 SDR_RX_BLOCK_SAMPLES);
            sstv_process(s_nr_buf, s_dec_block_samples);
        }
    } else {
        /* SSB (USB/LSB), phasing method at a DECIMATED rate - see the
         * PIPELINE comment above DECIM_COEFFS and demod_am.h's SSB
         * note. */
        float32_t sign = (s_mode == (uint8_t)DEMOD_MODE_USB) ? SSB_USB_SIGN : SSB_LSB_SIGN;

        /* 1. Decimate channel-filtered I and Q by DECIM_FACTOR (4): 128
         * samples @ 48kHz -> 32 samples @ 12kHz (was by 16: 512 @
         * 192kHz -> 32 @ 12kHz before 04/08/2026 - see sdr_rx.h's
         * SDR_RX_BLOCK_SAMPLES comment), anti-aliased by DECIM_COEFFS
         * inside the same call. */
        arm_fir_decimate_f32(&s_decim_i_inst, s_i_buf, s_i_dec, SDR_RX_BLOCK_SAMPLES);
        arm_fir_decimate_f32(&s_decim_q_inst, s_q_buf, s_q_dec, SDR_RX_BLOCK_SAMPLES);

        /*
         * HFDL SE ENGANCHA AQUI. 25/09/2026.
         *
         * Justo despues del diezmado y ANTES del Hilbert, porque HFDL quiere
         * la I/Q a 12 kHz tal cual - su mezclador se lleva la subportadora de
         * 1.440 Hz a banda base el solo (ver hfdl_iq_mixer.h). Lo que viene
         * despues en esta funcion es la maquinaria de banda lateral, que a
         * HFDL no le sirve de nada.
         *
         * Es el mismo punto de la cadena del que sale su version, y ademas
         * el unico donde existe la pareja I/Q: dos pasos mas abajo ya solo
         * hay audio real.
         *
         * ESTO CORRE EN LA INTERRUPCION DEL DMA. Lo que hace por bloque es
         * la parte barata de HFDL -mezclar, remuestrear, filtrar,
         * sincronizar-; el Viterbi NO esta aqui, se queda pendiente y lo
         * corre hfdl_modo_poll() desde el bucle principal. Si algun dia
         * alguien mueve el Viterbi aqui, el sintoma sera el audio cortado,
         * no un error de compilacion - por eso queda dicho.
         */
        if (hfdl_modo_activo()) {
            hfdl_modo_mete(s_i_dec, s_q_dec, s_dec_block_samples);
        }

        fases_combina(s_i_dec, s_q_dec, sign, s_ssb_dec);

        /*
         * --- RTTY INTEGRATION (added 08/08/2026, first draft) ---
         *
         * USB/LSB only, per the project owner - RTTY is two audio
         * tones inside an SSB passband, there's no meaning to it in
         * AM/NFM/WFM. Reads s_ssb_dec BEFORE NR (2d, right below)
         * touches it - deliberately RAW SSB audio, not NR'd: spectral
         * subtraction is tuned for voice intelligibility, and could
         * smear or distort two narrow FSK tones in ways that hurt
         * Goertzel tone detection (rtty.c's whole decode depends on
         * accurately telling two specific frequencies apart) more
         * than it helps. Same "s_ssb_dec is ALREADY at the rate this
         * wants, no extra decimation needed" reuse as NR just above -
         * see nr_ss_get_enabled()'s comment for that reasoning, it
         * applies here identically. Gated on rtty_get_enabled() the
         * same way NR is: skipped entirely while off, not just a
         * transparent no-op - zero extra ISR cycles (see rtty.h's
         * RTTY_ENABLED comment for how this defaults to on/off).
         *
         * READ-ONLY - rtty_process() never modifies s_ssb_dec, unlike
         * nr_ss_process() right below, so call order relative to NR
         * only matters for what RTTY sees, not for correctness of
         * what NR sees afterward.
         */
        if (rtty_get_enabled()) {
            rtty_process_chunks(s_ssb_dec, s_dec_block_samples);
        }
        /*
         * CW, 21/09/2026. Mismo sitio, mismo buffer y mismas razones que
         * el RTTY de justo arriba: audio SSB crudo antes de la reduccion
         * de ruido, porque la reduccion esta pensada para que se entienda
         * una voz y lo que hace con un tono estrecho no ayuda a un
         * detector que vive de medir ese tono; y ya esta a 12 kHz, que es
         * lo que este modulo quiere, asi que no se diezma dos veces.
         * Tambien de solo lectura, tambien saltado del todo cuando esta
         * apagado.
         */
        if (cw_get_enabled()) {
            cw_process_chunks(s_ssb_dec, s_dec_block_samples);
        }
        /*
         * NAVTEX, 24/09/2026. Mismo sitio, mismo buffer, mismas razones:
         * audio de banda lateral crudo antes de la reduccion de ruido -que
         * esta pensada para que se entienda una voz y lo que hace con dos
         * tonos estrechos no ayuda a un detector que vive de medirlos- y ya
         * a 12 kHz, que es lo que este modulo quiere. Tambien de solo
         * lectura y tambien saltado del todo cuando esta apagado.
         *
         * A diferencia del RTTY y del CW, este NO se trocea: navtex_process()
         * acepta cualquier n porque trabaja muestra a muestra, sin ventana de
         * bloque. Su ventana es la media movil de un bit, que lleva por
         * dentro.
         */
        if (navtex_activo()) {
            navtex_process(s_ssb_dec, s_dec_block_samples);
        }
        /*
         * Paquete de HF, 07/10/2026. Mismo sitio, mismo buffer y mismas
         * razones que los tres de arriba: audio de banda lateral crudo
         * antes de la reduccion de ruido, y ya a 12 kHz. El APRS de VHF
         * usa ESTE MISMO ax25_process() pero desde el otro sitio -la
         * salida del discriminador de FM, mas arriba en este fichero-, y
         * los dos no pueden estar activos a la vez porque el modo es uno
         * solo: aquel es NFM y este banda lateral.
         *
         * ax25_process() acepta cualquier n: trabaja muestra a muestra
         * contra su propio reloj de bit, sin ventana de bloque.
         */
        if (ax25_activo()) {
            ax25_process(s_ssb_dec, s_dec_block_samples);
        }
        /*
         * DSC, 07/10/2026. Mismo sitio, mismo buffer y mismas razones que
         * los anteriores: audio de banda lateral crudo antes de la
         * reduccion de ruido -que esta pensada para que se entienda una voz
         * y lo que hace con dos tonos estrechos no ayuda a un detector que
         * vive de medirlos- y ya a DEMOD_DEC_FS_HZ, que es lo que quiere.
         *
         * Acepta cualquier n: trabaja muestra a muestra contra su propio
         * reloj de bit, sin ventana de bloque.
         */
        if (dsc_activo()) {
            dsc_process(s_ssb_dec, s_dec_block_samples);
        }
        /*
         * PSK31, 28/09/2026. Mismo sitio, mismo buffer, mismas razones, y
         * aqui la de la reduccion de ruido es la mas clara de todas: lo
         * que este decodificador mide es la FASE de una portadora de
         * 31 Hz de ancho, y la sustraccion espectral cambia amplitudes por
         * bandas - o sea que le mueve la fase justo donde vive la señal.
         * Cruda.
         *
         * Tampoco se trocea: psk31_process() acepta cualquier n porque
         * trabaja muestra a muestra. Lo que hace por muestra es girar un
         * fasor (cuatro multiplicaciones) y acumular; el filtro adaptado
         * es una suma corrida, O(1), y corre una de cada doce muestras.
         * No hay nada caro aqui dentro.
         */
        if (psk31_activo()) {
            psk31_process(s_ssb_dec, s_dec_block_samples);
        }
        /*
         * WEFAX, 24/09/2026. El cuarto oyente del mismo buffer. Aqui la
         * reduccion de ruido importaria MAS que en los otros tres si se
         * aplicara: lo que este decodificador mide es la FRECUENCIA
         * instantanea de un tono, y la sustraccion espectral cambia
         * amplitudes por bandas - o sea que le movería el brillo de la imagen
         * en funcion de la frecuencia. Cruda, como los demas.
         */
        if (wefax_activo()) {
            wefax_process(s_ssb_dec, s_dec_block_samples);
        }
        /* SSTV, 24/09/2026. El quinto oyente del mismo buffer, y por las
         * mismas razones que el fax: mide la frecuencia instantanea de un
         * tono, asi que la reduccion de ruido le movería el brillo de la foto
         * segun la frecuencia. Cruda. */
        if (sstv_activo()) {
            sstv_process(s_ssb_dec, s_dec_block_samples);
        }
        /*
         * FELD-HELL, 06/10/2026. El sexto oyente del mismo buffer, y el mas
         * barato de todos: solo mide la ENVOLVENTE -valor absoluto y un
         * suavizado de un cuarto de punto-, porque lo que se manipula ahi es
         * la portadora entera. Ni transformadas ni discriminador. Crudo como
         * los demas: la sustraccion espectral le movería el brillo de los
         * puntos y aqui el brillo ES el dato.
         */
        if (hell_activo()) {
            hell_process(s_ssb_dec, s_dec_block_samples);
        }
        /*
         * FT8, 25/09/2026. El sexto oyente del mismo buffer, y el unico que
         * NO decodifica aqui dentro.
         *
         * ft8_modo_come() solo convierte a entero y remuestrea de 12000 a
         * 3200: coste acotado y proporcional al bloque, como los cinco de
         * arriba. Lo caro del FT8 -la FFT de cada subbloque y, al cerrarse la
         * ranura de 15 s, el decodificador entero, que se lleva sus buenos
         * cientos de milisegundos- corre en ft8_modo_tick(), desde el bucle
         * principal. Meterlo aqui seria colgar el audio cada 15 segundos.
         *
         * Cruda, como las otras cinco, y por el mismo motivo de siempre: lo
         * que el FT8 mide son ocho tonos muy juntos y muy debiles, justo lo
         * que la sustraccion espectral se lleva por delante.
         */
        if (ft8_modo_activo()) {
            ft8_modo_come(s_ssb_dec, s_dec_block_samples);
        }
        /*
         * Y WSPR, del mismo sitio y por las mismas razones que FT8: la
         * banda lateral ya diezmada a 12 kHz, cruda -lo que WSPR busca son
         * cuatro tonos separados 1,46 Hz y treinta dB por debajo del ruido,
         * o sea exactamente lo que la sustraccion espectral se lleva-.
         *
         * Lo que hace aqui por bloque es mezclar a banda base y diezmar por
         * 32 hasta 375 Hz: coste acotado y proporcional al bloque. La
         * correlacion de sincronismo y el decodificador de Fano NO estan
         * aqui: corren en wspr_modo_poll(), desde el bucle principal, una
         * vez cada dos minutos. Meterlos aqui cortaria el audio durante
         * cientos de milisegundos cada vez que acaba una emision.
         *
         * wspr_modo_mete() se va sin hacer nada mientras no hay captura
         * abierta, que son dos de cada dos minutos y medio.
         */
        if (wspr_modo_activo()) {
            wspr_modo_mete(s_ssb_dec, s_dec_block_samples);
        }
        /*
         * Y ALE, del mismo sitio: banda lateral ya diezmada a 12 kHz. Lo que
         * hace por bloque son ocho Goertzel de 96 muestras cuatro veces por
         * simbolo; el Golay y la busqueda de palabra van detras, en el mismo
         * sitio, porque tambien son baratos (299 vueltas de un XOR).
         *
         * Cruda como las demas: los ocho tonos de ALE estan separados 250 Hz
         * y la sustraccion espectral, que se lleva bien lo que parece ruido,
         * se llevaria por delante los que llegan justos.
         */
        if (ale_modo_activo()) {
            ale_modo_mete(s_ssb_dec, (uint16_t)s_dec_block_samples);
        }
        /*
         * Y JTTY, del mismo sitio y cruda como las demas: la banda lateral
         * ya diezmada a 12 kHz. Lo que busca son cuatro tonos separados
         * 31,25 Hz, o sea justo del tamaño que la sustraccion espectral se
         * lleva por delante tomandolo por ruido.
         *
         * Lo que hace por bloque es mezclar, diezmar por 24 y una DFT de
         * 16 puntos en 32 frecuencias cada media fila de simbolo -cada 16
         * ms-, mas la correlacion de sincronismo. Todo acotado y
         * proporcional al bloque. El Viterbi de 512 estados NO esta aqui:
         * corre en jtty_modo_poll(), desde el bucle principal, porque ahi
         * dentro cortaria el audio.
         */
        if (jtty_modo_activo()) {
            jtty_modo_mete(s_ssb_dec, (uint16_t)s_dec_block_samples);
        }
        /*
         * Y el identificador de tramas, del mismo sitio y por la misma
         * razon que los cuatro de arriba: la banda lateral ya diezmada a
         * 12 kHz. CRUDA, ademas, y aqui importa mas que nunca -lo que mide
         * es cuanto se parece la señal a si misma, y la sustraccion
         * espectral cambia justo eso-.
         *
         * Lo que hace por bloque es un oscilador de ocho valores y una
         * suma: acotado y proporcional al bloque. Las dos transformadas de
         * 2048 puntos NO estan aqui, van en stanag_modo_poll() desde el
         * bucle principal.
         */
        if (stanag_modo_activo()) {
            stanag_modo_mete(s_ssb_dec, (uint16_t)s_dec_block_samples);
        }
        /*
         * Y el modo IDENT, que come del MISMO sitio: la banda lateral ya
         * diezmada a 12 kHz. Comparte el motor con STANAG -stanag_det.c- y
         * la misma RAM prestada, asi que los dos no pueden estar encendidos
         * a la vez; de eso se encarga la tabla de modos, que son dos filas
         * distintas. Ver ident.h.
         */
        if (ident_modo_activo()) {
            ident_modo_mete(s_ssb_dec, (uint16_t)s_dec_block_samples);
        }
        /* El osciloscopio de sintonia lo comparten los dos: al RTTY le
         * ensena donde caen las dos frecuencias y al CW donde cae el
         * tono, que es exactamente lo que hay que mirar para sintonizar
         * en cada caso. Se alimenta UNA vez, aunque nunca puedan estar
         * los dos encendidos a la vez: si algun dia lo estuvieran, dos
         * llamadas meterian el audio dos veces en el mismo acumulador.
         * Ver rtty_scope.h para por que esta FFT es aparte de la del
         * espectro. Barato: aqui solo se acumula en un anillo, la FFT
         * corre luego desde el bucle principal. */
        /*
         * Y EL PAQUETE DE HF FALTABA EN ESTA LISTA - 19/10/2026.
         *
         * Avisado por el usuario: al elegir PKT300 el espectro se quedaba
         * sin moverse.
         *
         * Y no se movia porque en PKT300 el espectro YA NO ESTA: es un modo
         * digital, asi que digi_panel_active() dice que si y el bucle
         * principal pinta el osciloscopio de sintonia en su sitio. Pero el
         * osciloscopio no estaba nombrado aqui, o sea que no recibia una
         * sola muestra: rtty_scope_frame_ready() nunca decia que si,
         * rtty_scope_draw() se volvia sin pintar nada, y en pantalla
         * quedaba congelado lo ultimo que hubiera dibujado el espectro. El
         * decodificador funcionaba perfectamente; desde fuera parecia una
         * radio colgada.
         *
         * Es EL MISMO fallo que el del APRS -ver aprs_scope_feed(), unas
         * cuantas lineas mas arriba- una rama mas alla: aquel era el camino
         * de FM y este el de banda lateral. El APRS se arreglo alli y esta
         * lista se quedo como estaba, porque ax25_activo() ya aparecia...
         * en la otra rama, que en LSB no corre.
         *
         * Lo que impide la sexta vez es tools/osci_check.py, que deriva de
         * k_demod_modes[] que modos ensenan el osciloscopio y exige que
         * cada uno este nombrado en la rama de audio que le toca.
         */
        if (rtty_get_enabled() || cw_get_enabled() || navtex_activo()
            || wefax_activo() || sstv_activo() || ft8_modo_activo()
            || psk31_activo() || dsc_activo() || ax25_activo()) {
            rtty_scope_feed(s_ssb_dec, s_dec_block_samples);
        }

        /* 2d. NR (Spectral Subtraction), USB/LSB - IN PLACE on
         * s_ssb_dec, which is ALREADY at the 12kHz rate nr_ss_process()
         * wants (see nr_ss.h's NR_SS_BLOCK_SAMPLES comment) - unlike
         * AM below, SSB needs NO separate decimate/interpolate pair
         * around it: piggybacking on this chain's own existing
         * decimation avoids interpolating this audio up to 192kHz
         * (step 3, right below) only to immediately decimate it back
         * down again for NR and interpolate AGAIN - a wasted round
         * trip discovered 04/08/2026 once NR pushed the ISR up against
         * the controller's limit in SSB specifically, as the project
         * owner reported - this is that fix. AM has no equivalent
         * already-decimated buffer to reuse (its envelope only ever
         * exists at the full 512-sample/192kHz s_env[]), so it still
         * goes through its own dedicated decimate/interpolate pair -
         * see the 3b. NR block below. */
        if (nr_ss_get_enabled()) {
            uint32_t nr_t0 = DWT->CYCCNT;
            nr_ss_process_chunks(s_ssb_dec, s_dec_block_samples);
            nr_ssb_cycles = DWT->CYCCNT - nr_t0;
        }

        /*
         * Notch automatico, etapa 35. Va AQUI, en el mismo sitio y a la
         * misma tasa de 12 kHz que la reduccion de ruido, y DESPUES de
         * ella a proposito: la reduccion trabaja por sustraccion
         * espectral y no le molesta una portadora, pero al notch si le
         * molesta el ruido, asi que cuanto mas limpio le llegue el audio
         * mejor predice. Apagado no cuesta ni un ciclo - ver
         * anotch_process(). */
        anotch_process(s_ssb_dec, s_dec_block_samples);

        /* 3. Interpolar el audio de banda lateral de 12 kHz -ya con la
         * reduccion de ruido aplicada arriba, si estaba encendida- de
         * vuelta a la velocidad de radiofrecuencia ACTIVA: 96 o 48 kHz,
         * ver demod_am_active_fs_hz(); s_env[] son 256 muestras. Lo de
         * "192 kHz (32 muestras -> 512)" que ponia aqui es de antes de
         * que bajara la velocidad -corregido el 30/09/2026; el factor de
         * interpolacion por 16 de abajo es el que sigue siendo cierto, y
         * es lo que importa aqui-. Va directo a s_env[], y todo lo que
         * viene detras -bloqueador de continua, paso bajo de audio,
         * automatico de ganancia y salida- es compartido con el AM, sin
         * cambios. INTERP_COEFFS lleva dentro la ganancia por 16 que
         * compensa lo que se pierde al meter ceros; ver el comentario de
         * LA CADENA: sin ella, esto sale a la decimosexta parte del
         * volumen. */
        arm_fir_interpolate_f32(&s_interp_inst, s_ssb_dec, s_env, s_dec_block_samples);
    }

    /*
     * --- Squelch (AM + NFM only - see demod_am.h's squelch block
     * comment) --- RF-level metric: average magnitude of the SAME
     * down-mixed, channel-filtered I/Q that just fed AM's envelope
     * detector or NFM's discriminator above (s_i_buf/s_q_buf are
     * read-only in both, still valid here) - a hardware-VSQRT
     * sqrtf() per sample, same cost class as AM's own
     * arm_cmplx_mag_f32() call, just done as a scalar accumulation
     * since only the block's average is needed here, not a full
     * per-sample magnitude array.
     *
     * WFM and SSB don't get a squelch: WFM's front-end skips the
     * channel filter entirely (see demod_am.h's WFM note), so there's
     * no equivalent "in-channel RF level" to measure the same way;
     * SSB could in principle reuse this exact metric (it shares the
     * same down-mix + CHF_COEFFS front-end AM does), but wasn't asked
     * for and isn't wired up - straightforward to add the same way if
     * it's ever wanted.
     */
    if (s_mode == (uint8_t)DEMOD_MODE_AM || s_mode == (uint8_t)DEMOD_MODE_NFM) {
        float32_t sum = 0.0f;
        float mag_db;
        float half_hyst = SQUELCH_HYSTERESIS_DB * 0.5f;

        /*
         * EN AM ESTAS RAICES YA ESTAN HECHAS - 05/10/2026.
         *
         * Otras 256 raices cuadradas por bloque, 96.000 por segundo, y en
         * modo AM son EXACTAMENTE las mismas que arm_cmplx_mag_f32() acaba
         * de escribir en s_env[] en el paso 1. Entre aquel bucle y este no
         * hay nadie que toque s_env -el DCF77 lo lee, no lo escribe-, asi
         * que la magnitud sigue ahi entera.
         *
         * En NFM no se puede reutilizar: alli s_env lleva la salida del
         * discriminador, que es otra cosa. De ahi el reparto por modo.
         */
        if (s_mode == (uint8_t)DEMOD_MODE_AM) {
            for (n = 0; n < SDR_RX_BLOCK_SAMPLES; n++) {
                sum += s_env[n];
            }
        } else {
            /* Y EN NFM YA ESTA SUMADA - 10/10/2026. Aqui habia otra pasada
             * entera sobre s_i_buf/s_q_buf con 256 raices, sobre el mismo
             * I²+Q² que el medidor de señal habia calculado veinte lineas
             * antes. Ahora la suma la deja hecha aquel bucle. Las raices son
             * las mismas y el orden de acumulacion tambien, asi que el
             * numero es bit a bit el de antes. */
            sum = s_sq_sum_mag;
        }
        /* +1.0f before the log so true silence (sum=0) gives
         * 20*log10(1)=0dB instead of log10(0)=-inf - a harmless
         * floor, not a meaningful "0dB" reading. */
        mag_db = 20.0f * log10f((sum / (float)SDR_RX_BLOCK_SAMPLES) + 1.0f);

        s_squelch_level_db = SQUELCH_LEVEL_ALPHA * s_squelch_level_db
                              + (1.0f - SQUELCH_LEVEL_ALPHA) * mag_db;

        /* Schmitt-trigger hysteresis: different trip points for
         * opening vs closing so a signal sitting right at the
         * threshold doesn't chatter block-to-block. */
        if (s_squelch_open) {
            if (s_squelch_level_db < s_squelch_threshold_db - half_hyst) {
                s_squelch_open = 0U;
            }
        } else {
            if (s_squelch_level_db > s_squelch_threshold_db + half_hyst) {
                s_squelch_open = 1U;
            }
        }

        if (!s_squelch_open) {
            /* Muted: silence this block's audio before it reaches the
             * DC blocker/audio LPF/AGC below - cheaper and simpler
             * than threading a mute flag through every downstream
             * stage, and those stages handle a silent block
             * gracefully already (the AGC peak just decays toward
             * AGC_PEAK_MIN, same as any quiet signal). */
            for (n = 0; n < SDR_RX_BLOCK_SAMPLES; n++) {
                s_env[n] = 0.0f;
            }
        }
    }

    {
        uint32_t cyc_now = DWT->CYCCNT;
        /* nr_ssb_cycles subtracted out - see this function's top
         * comment on it, and the 2d. NR block above - otherwise SSB's
         * NR cost would silently hide inside "extract" instead of
         * showing up in its own "nr" bucket the way AM's does. */
        s_last_cycles_extract = resta0(cyc_now - cyc_start,
                                       s_last_cycles_frontend + nr_ssb_cycles);
    }

    /* 2. DC blocker (removes the carrier level in AM; a harmless
     * small-DC-bias cleanup in SSB, which has no carrier). Scalar -
     * cheap, stateful single-pole HPF, not worth a CMSIS block call. */
    for (n = 0; n < SDR_RX_BLOCK_SAMPLES; n++) {
        float y = s_env[n] - dcb_x1 + s_dcb_r * dcb_y1;
        dcb_x1 = s_env[n];
        dcb_y1 = y;
        s_env[n] = y;
    }

    /* 3. Audio LPF, 4th-order Butterworth via CMSIS-DSP (in-place,
     * same reasoning as the channel filter above). WFM no longer
     * reaches this function at all as of 05/08/2026 (see
     * demod_wfm_process_raw()) - the old "if WFM {...} else {...}"
     * split here collapsed to just this AM/SSB/NFM path.
     *
     * NFM: unconditionally s_alpf_inst (ALPF_COEFFS, ~6kHz) -
     * unaffected by the AM/SSB selector below even though it lands
     * in this same branch. NFM's own channel filter (NFM_CHF_COEFFS)
     * already sets its bandwidth upstream, and letting the AM/SSB
     * selector also touch NFM's audio would narrow narrowband FM voice
     * every time someone picks a tighter AM/SSB filter, which the
     * project owner never asked for - see AUDIO_BW_4K0's comment
     * in demod_am.h for why these are kept as fully separate
     * instances instead of one shared "current audio filter".
     *
     * AM/USB/LSB: whichever of the three s_audio_bw picked - see
     * demod_am_set_audio_bw()'s comment in demod_am.h. */
    {
        if (cw_get_enabled()) {
            /* En CW manda el filtro de CW y los dos cortes no pintan
             * nada: pedir 4 kHz de audio en telegrafia no es una
             * preferencia, es no tener filtro. Ver
             * demod_am_set_cw_filter_hz(). */
            /* El indice se lee UNA vez, igual que el del filtro de dos
             * cortes de abajo - revision a fondo del 08/10/2026, ver el
             * comentario de s_alpf_cw_coeffs. */
            uint8_t jc = s_alpf_cw_act;
            arm_biquad_cascade_df1_f32(&s_alpf_cw_inst[jc], s_env, s_env,
                                       SDR_RX_BLOCK_SAMPLES);
        } else {
            /*
             * El filtro de dos cortes de la familia que suena. El indice
             * se lee UNA vez aqui y no dentro del bucle: si cambiara a
             * mitad de bloque, la mitad del bloque iria por una cascada y
             * la otra mitad por otra. Ver el comentario de s_fil_coef.
             */
            uint8_t j = s_fil_act;
            arm_biquad_cascade_df1_f32(&s_fil_inst[j], s_env, s_env,
                                       SDR_RX_BLOCK_SAMPLES);
        }
    }

    {
        uint32_t cyc_now = DWT->CYCCNT;
        /*
         * nr_ssb_cycles TAMBIEN se resta aqui - 02/10/2026, y faltaba
         * desde el 04/08, que es cuando el NR de banda lateral se metio
         * dentro del paso 2d.
         *
         * Ese 99 es el tope de la celda, o sea un numero disparado. Y la
         * cuenta de por que es corta: "extract" ya lleva restado el NR
         * -justo arriba, para que el NR salga en su propio cajon y no
         * escondido ahi dentro-, asi que al calcular "audio" restandole
         * "extract" se le devuelve el NR SIN QUERER. El audio salia
         * inflado en lo que cuesta el NR, y el AGC, que se calcula por
         * diferencia al final, salia rebajado en lo mismo. Como el AGC
         * cuesta menos que el NR, la resta se iba por debajo de cero, y en
         * un unsigned eso son cuatro mil millones de ciclos: el 99 % de la
         * celda.
         *
         * Llevaba mintiendo desde agosto. No se habia visto porque el
         * desglose solo salia por el puerto serie -apagado en la
         * compilacion de verdad- y a la pantalla solo llegaba el cajon del
         * NR, que si estaba bien medido. Sacar los otros cuatro a la
         * pantalla lo destapo en la PRIMERA lectura.
         *
         * Con esto, las cinco etapas vuelven a sumar el total de la fila
         * de arriba, y esa suma es la comprobacion: si algun dia no suman,
         * es que una de estas restas ha vuelto a descuadrarse.
         */
        s_last_cycles_audio = resta0(cyc_now - cyc_start, s_last_cycles_frontend
                                     + s_last_cycles_extract + nr_ssb_cycles);
    }

    /* 3b. NR (Spectral Subtraction), AM ONLY here - USB/LSB already got
     * theirs done back in step 2d, inline on the already-12kHz
     * s_ssb_dec, specifically to AVOID this decimate/interpolate pair
     * (see step 2d's comment for why - the project owner hit the
     * controller's cycle limit in SSB+NR before this split existed).
     * AM has no equivalent already-decimated buffer (its envelope only
     * ever exists at the full s_env[]), so it still needs its own
     * dedicated decimate-down/NR/interpolate-back-up here. Gated the
     * same way as before: only while the bottom bar's NR button has
     * actually turned it on (nr_ss_get_enabled() - a genuine on/off
     * switch, independent of the strength value - see
     * nr_ss_set_enabled()'s comment in nr_ss.h). Placed AFTER the
     * audio LPF (so NR works on already band-limited, DC-free audio,
     * not raw envelope content) and BEFORE the AGC (so the NR strength
     * control's fixed threshold sees a consistent, un-normalized
     * signal level rather than chasing the AGC's own gain changes -
     * the operator tunes the NR strength by ear anyway, the same way
     * they'd tune an RF gain or squelch control, so this ordering
     * isn't load-bearing the way it would be for an automatic/
     * adaptive threshold). */
    /*
     * Y EL NOTCH AUTOMATICO TAMBIEN PASA POR AQUI - revision a fondo del
     * 08/10/2026.
     *
     * anotch.h dice, desde que se escribio: "En AM, SAM y banda lateral;
     * en FM no". Y hasta hoy el unico anotch_process() que habia en este
     * fichero estaba dentro de la rama de banda lateral (paso 2d), que es
     * la unica que llega hasta alli: AM, SAM y NFM salen por su propio
     * camino mucho antes. O sea que en AM y en SAM el notch NO SE APLICABA
     * NUNCA, y el mando de la pantalla no esta limitado por modo: el usuario
     * lo mueve estando en AM, la etiqueta cambia de NOTCH a la
     * fuerza elegida, y el heterodino sigue sonando igual. Un mando que
     * cambia de etiqueta y no cambia el audio es peor que no tenerlo.
     *
     * Se engancha AQUI, en el diezmado/interpolado que ya tenia montado la
     * reduccion de ruido de AM, y no en otro sitio, por tres razones: es
     * la misma tasa de 12 kHz a la que el notch esta medido (ver anotch.h
     * y sim/anotchtest.c), es DESPUES de la reduccion de ruido igual que
     * en banda lateral -al notch le estorba el ruido, a la reduccion no le
     * estorba una portadora-, y no cuesta un segundo par de filtros.
     *
     * Lo que cambia en la condicion de entrada es que este bloque ya no se
     * monta solo cuando esta encendida la reduccion de ruido: tambien
     * cuando lo que esta encendido es el notch, y tambien en SAM, que
     * hasta ahora no entraba aqui en absoluto. Lo que NO cambia es quien
     * hace que: la reduccion de ruido sigue siendo solo de AM -en SAM no
     * estaba y no se le añade-, y en NFM no entra ninguno de los dos,
     * porque alli un tono fijo es parte de lo que se escucha.
     *
     * Apagado no cuesta ni un ciclo: con los dos apagados no se entra, y
     * anotch_process() con el notch apagado se va sin tocar el audio.
     */
    /*
     * ==================================================================
     * Y LA REDUCCION DE RUIDO TAMBIEN EN SAM - 11/10/2026
     * ==================================================================
     *
     * Era cierto, y ademas era peor que "no funciona": el boton de Ruido
     * NO esta limitado por modo. En SAM se podia encender, se quedaba
     * encendido, ponia "activo" en la barra de abajo... y aqui dentro no
     * entraba. Un control que dice una cosa y hace otra.
     *
     * El parrafo de arriba decia "la reduccion de ruido sigue siendo solo
     * de AM -en SAM no estaba y no se le añade-". Eso era una decision de
     * ALCANCE del dia que se metio el notch -no cambiar nada mas que lo
     * que se venia a cambiar- y no un motivo tecnico: lo que sale de SAM
     * por s_env es audio demodulado igual que la envolvente de AM, a la
     * misma tasa y por el mismo sitio. El diezmado y el interpolado ya se
     * montaban en SAM para el notch, asi que esto no añade ni un filtro.
     *
     * En NFM sigue sin entrar ninguno de los dos, y eso si es tecnico:
     * alli un tono fijo es parte de lo que se escucha.
     */
    {
    const uint8_t am_o_sam = (uint8_t)((s_mode == (uint8_t)DEMOD_MODE_AM)
                                    || (s_mode == (uint8_t)DEMOD_MODE_SAM));
    if (am_o_sam && (nr_ss_get_enabled() || anotch_activo())) {
        arm_fir_decimate_f32(&s_nr_decim_inst, s_env, s_nr_buf, SDR_RX_BLOCK_SAMPLES);
        if (nr_ss_get_enabled()) {
            nr_ss_process_chunks(s_nr_buf, s_dec_block_samples);
        }
        anotch_process(s_nr_buf, s_dec_block_samples);
        arm_fir_interpolate_f32(&s_nr_interp_inst, s_nr_buf, s_env, s_dec_block_samples);

        {
            uint32_t cyc_now = DWT->CYCCNT;
            s_last_cycles_nr = cyc_now - cyc_start - s_last_cycles_frontend
                                - s_last_cycles_extract - s_last_cycles_audio;
        }
    } else {
        /* nr_ssb_cycles carries USB/LSB's already-measured step-2d
         * cost here (0 for every other case: AM con el NR apagado, WFM,
         * NFM) - see this function's top comment on that variable. */
        s_last_cycles_nr = nr_ssb_cycles;
    }
    }

    /*
     * 3b. El analizador de audio se alimenta AQUI, y el sitio importa:
     * despues del filtro -para que lo que se vea sea lo que se oye- pero
     * ANTES del AGC. Con el AGC en medio, la altura de una raya dependeria
     * de cuanto lleve amplificando el control automatico en ese momento, o
     * sea que el analizador estaria midiendo su propio AGC. Un instrumento
     * que se mide a si mismo no sirve de nada.
     */
    if (analiz_activo()) {
        analiz_feed(s_env, SDR_RX_BLOCK_SAMPLES);
    }

    /* 4. AGC (instant attack, slow release) + 5. clamp/duplicate to
     * L/R int16. Scalar - a branchy control loop, not a filter.
     * MANUAL profile bypasses the peak-tracking entirely (fixed unity
     * gain) - see agc_profile_t's MANUAL note in demod_am.h. Branching
     * on s_agc_profile ONCE outside the loop (not per-sample) keeps
     * both paths as cheap as they were before profiles existed. */
    /*
     * LA DIVISION DEL AGC, UNA VEZ POR ATAQUE Y NO POR MUESTRA.
     * 05/10/2026, y el lazo se mudo a agc.c el 09/10/2026.
     *
     * El razonamiento entero -por que el inverso se arrastra multiplicando
     * y por que se resincroniza con una division de verdad en cada
     * bloque- esta en agc.h, junto al codigo que lo hace. Aqui estaba
     * COPIADO del de FM ancha, y esa es justo la averia que se arreglo al
     * mudarlo: la optimizacion se aplico a esta copia y no a la otra, y
     * nada en el arbol miraba las dos.
     *
     * De paso se fue la ultima division por muestra que quedaba viva: la
     * del caso SIN señal, cuando el pico cae por debajo de su suelo. Ver
     * inv_min en agc.c.
     */
    if (s_agc_profile == AGC_PROFILE_MANUAL) {
        agc_manual(s_env, s_audio_out, SDR_RX_BLOCK_SAMPLES, DEMOD_AM_GAIN);
        /* peak/s_agc_peak deliberately NOT updated here - so the
         * S-meter (demod_am_get_signal_peak()) and the automatic
         * profiles both pick up cleanly wherever they left off if you
         * switch back to SLW/MED/FST later, rather than resuming from
         * a stale or zeroed peak. */
    } else {
        s_agc_peak = agc_bloque(s_env, s_audio_out, SDR_RX_BLOCK_SAMPLES,
                                peak, s_agc_release, AGC_PEAK_MIN,
                                DEMOD_AM_GAIN);
    }

    s_dcb_x1 = dcb_x1;
    s_dcb_y1 = dcb_y1;

    if (do_log) {
        int16_t mn = s_audio_out[0], mx = s_audio_out[0];
        for (n = 1; n < SDR_RX_BLOCK_SAMPLES; n++) {
            if (s_audio_out[2U * n] < mn) { mn = s_audio_out[2U * n]; }
            if (s_audio_out[2U * n] > mx) { mx = s_audio_out[2U * n]; }
        }
        debug_print_dec("demod_am: s_agc_peak going out", (uint32_t)s_agc_peak);
        debug_print_dec_signed_local("demod_am: [3] final int16 output min", (int32_t)mn);
        debug_print_dec_signed_local("demod_am: [3] final int16 output max", (int32_t)mx);
    }

    if (muted) {
        /* Silence the OUTPUT only - everything above (DC blocker,
         * audio LPF, AGC, NR) already ran normally on the real data
         * and updated its state accordingly - see AM_SETTLE_MUTE_BLOCKS'
         * comment for why that's the whole point. The diagnostic
         * block just above already captured the real (pre-mute)
         * output values, so the log still shows what the AGC actually
         * computed even while this zeroes out what's actually heard. */
        for (n = 0; n < SDR_RX_BLOCK_SAMPLES; n++) {
            s_audio_out[2U * n]      = 0;
            s_audio_out[2U * n + 1U] = 0;
        }
    }

    /* Y el pitido de los avisos, LO ULTIMO: tapa el audio en vez de
     * mezclarse con el. Ver bip.h. Apagado no cuesta nada. */
    bip_mete(s_audio_out, (uint32_t)SDR_RX_BLOCK_SAMPLES, demod_am_active_fs_hz());
    gd32_i2s_stream_write_half(s_audio_out);

    s_last_cycles = DWT->CYCCNT - cyc_start;
    s_last_cycles_agc_out = resta0(s_last_cycles,
                                   s_last_cycles_frontend + s_last_cycles_extract
                                   + s_last_cycles_audio + s_last_cycles_nr);
}
