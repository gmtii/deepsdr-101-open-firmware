#include <string.h>
#include "hfdl_preamble_sync.h"
#include "hfdl_ram.h" /* la region prestada - ver build_refs_once() */

/* Reference sequences, built ONCE (they're constant) and cached here
 * rather than in hfdl_preamble_sync_t itself - keeps the struct
 * smaller when a caller only needs one instance's worth of framer
 * state, since all instances would share identical reference data
 * anyway (no reason to duplicate 127+8*127 floats per instance). */
/*
 * LAS TABLAS DE REFERENCIA VIVEN EN LA RAM PRESTADA. 25/09/2026, cambio
 * NUESTRO sobre el codigo del proyecto padre.
 *
 * Eran dos arrays estaticos de 4.572 bytes -la secuencia A y los ocho giros
 * de M1- ocupados SIEMPRE, tambien en AM, en FT8 y con la radio apagada de
 * HFDL. En esta radio quedaban 884 bytes libres de SRAM, asi que eran el
 * cinco por uno de todo lo que sobraba para un modo que se usa a ratos.
 *
 * Ahora salen de la cabecera de la region prestada (ver hfdl_ram.h), que
 * solo existe mientras el modo esta puesto.
 *
 * LO QUE ESO LE QUITA AL DECODIFICADOR son esos mismos 4.608 bytes, y se ha
 * mirado antes de hacerlo: de los ocho tipos de trama, los seis que caben
 * hoy siguen cabiendo -el mayor de ellos, el giro 3, pide 33.088 de los
 * 45.216 que quedan- y los dos que no cabian (giros 6 y 7, ranura doble)
 * tampoco cabian antes. No se rompe nada que funcionara.
 *
 * Y SE RECONSTRUYEN EN CADA ARRANQUE, no una sola vez como antes. El
 * "s_refs_built" original valia cuando la memoria era estatica y nadie la
 * tocaba; aqui la cascada borra la union entera al recuperarla, asi que una
 * bandera de "ya estan hechas" apuntaria a tablas en blanco la segunda vez
 * que se entra en HFDL. Rehacerlas son 1.143 senos y cosenos una vez por
 * entrada al modo: ni se nota, y quita de en medio una clase entera de
 * fallo.
 */
static float32_t *s_a_ref;
static float32_t (*s_m1_ref)[HFDL_M1_LEN];

/*
 * 1 si las tablas existen. Sin region prestada no existen, y entonces este
 * modulo TIENE que quedarse quieto: correlacionar contra un puntero nulo es
 * exactamente el fallo que costo el cuelgue con el pitido continuo cuando
 * FT8 uso una tabla sin inicializar como indice.
 *
 * Lo caza el banco del PC con un segfault -que es la forma amable de
 * enterarse- porque su propia autoprueba llama a build_refs_once() sin pedir
 * la region. Antes no pasaba nada porque las tablas eran estaticas.
 */
static uint8_t s_refs_ok;

#if HFDL_DIAG
/* Solo para el banco: la tabla de A, para poder correlar en paralelo
 * sobre la toma de medio simbolo. Ver hfdl_modo.c, s_dg_half. */
const float32_t *hfdl_preamble_sync_ref_a(void);
#endif

static void build_refs_once(void)
{
	uint8_t *cab = hfdl_ram_cabecera();

	if (cab == (uint8_t *)0) {
		/* Fuera del modo no hay donde ponerlas. Se deja en NULL y el
		 * modulo se queda inerte: process_symbol() no encuentra nada que
		 * correlacionar y no engancha. Es lo correcto - buscar preambulo
		 * sin region prestada seria buscar dentro de la cascada. */
		s_a_ref = (float32_t *)0;
		s_m1_ref = (float32_t (*)[HFDL_M1_LEN])0;
		s_refs_ok = 0u;
		return;
	}

	s_a_ref  = (float32_t *)(void *)cab;
	s_m1_ref = (float32_t (*)[HFDL_M1_LEN])(void *)(cab + HFDL_A_LEN * sizeof(float32_t));

	hfdl_preamble_seq_build_a(s_a_ref);
	for (uint32_t k = 0; k < HFDL_M_SHIFT_CNT; k++) {
		hfdl_preamble_seq_build_m1_variant(k, s_m1_ref[k]);
	}
	s_refs_ok = 1u;
}

void hfdl_preamble_sync_init(hfdl_preamble_sync_t *p)
{
	build_refs_once();

	/* NON-COHERENT BLOCK COMBINING (16/08/2026, round 5 - see
	 * hfdl_framer.h's top comment for the full reasoning): a single
	 * 127-symbol coherent correlation requires phase consistency
	 * across the whole window, and every seeding/loop-tuning approach
	 * tried against real captures so far capped scores around
	 * ~0.35-0.40 regardless. HFDL_FRAMER_N_BLOCKS splits that window
	 * into this many independently-correlated, non-coherently-combined
	 * sub-blocks instead - trades some coherent processing gain for
	 * tolerance to phase drift across the window.
	 *
	 * VALUE (16/08/2026, updated after the project's first real
	 * LOCKED): 4 was the original, unverified first guess - it DID
	 * produce that first LOCKED, but a single field data point can't
	 * tell "4 is good" apart from "4 happened to be good enough that
	 * one time". Properly tuned via host_test_harness/
	 * n_blocks_sweep.c - a Monte Carlo sweep across (n_blocks, phase
	 * drift rate, SNR), CRITICALLY including a false-alarm control
	 * (pure noise, no real signal) alongside detection probability.
	 * That control matters a lot: naive detection-probability alone
	 * kept climbing all the way to n_blocks=63, but false-alarm rate
	 * climbs even faster past n_blocks~12 (very short blocks - e.g.
	 * 2-symbol blocks at n_blocks=63 - can "match" a reference by pure
	 * chance, and this module's per-block |score| never goes negative,
	 * so noise alone gets systematically biased toward a high score as
	 * blocks shrink) - n_blocks=16 already false-alarms on PURE NOISE
	 * ~55-60% of the time, making its apparent "detection" gain
	 * meaningless. Within the range where false-alarm stays low
	 * (<=~6%, since a false HFDL_FRAMER_PREAMBLE_FOUND is only
	 * tentative - still needs A2 confirmation, so it's not free but
	 * isn't catastrophic either), n_blocks=8 gave the best detection
	 * probability of the candidates tested (1,2,4,6,8), at every phase
	 * drift rate and both SNR levels checked - e.g. at -9dB SNR,
	 * ramp=0.02 rad/symbol: n_blocks=4 detects 12% of the time,
	 * n_blocks=8 detects 28%, both with false-alarm well under 10%.
	 * Re-run that sweep tool (block_len = 127/n_blocks = 15 symbols at
	 * n_blocks=8) if real captures suggest a different operating point
	 * than what this synthetic model assumed - it does NOT know the
	 * true relationship between hfdl_scope's broadband signal/floor
	 * ratio and per-symbol coherent SNR, see the sweep tool's own
	 * top comment. */
#define HFDL_FRAMER_N_BLOCKS 8u

	/*
	 * ======================================================================
	 * A1 VUELVE A SER COHERENTE - 14/10/2026
	 * ======================================================================
	 *
	 * Doscientos cincuenta y seis enganches de A1 para cincuenta y dos
	 * ráfagas reales. El troceado en ocho bloques con suma de valores
	 * absolutos es el culpable, y el propio comentario de arriba ya lo
	 * avisaba: "el ruido se sesga solo hacia puntuaciones altas segun
	 * encogen los bloques".
	 *
	 * POR QUE SE PUSO EN AGOSTO: porque la portadora derivaba. Una
	 * correlacion coherente de 127 simbolos se destruye si la fase gira
	 * durante la ventana, y sin nada que corrigiera la frecuencia antes
	 * del preambulo eso pasaba siempre. Trocear y sumar magnitudes dejaba
	 * que cada trozo encontrara su propia fase: menos discriminacion a
	 * cambio de aguantar la deriva. Era el cambio correcto ENTONCES.
	 *
	 * POR QUE YA NO HACE FALTA: desde hoy el pre-key mide el desvio sobre
	 * los 249 ms de tono puro y se lo da al lazo ANTES de que llegue el
	 * preambulo (ver prekey_simbolo() y hfdl_costas_empuja_freq() en
	 * hfdl_modo.c). La portadora ya no deriva durante A1, asi que la
	 * excusa para trocear desaparecio y lo unico que quedaba era el
	 * precio: falsas alarmas a espuertas.
	 *
	 * MEDIDO en deepsdr-ui/sim/hfdl_aire.c sobre la grabacion de 54
	 * segundos, barriendo el numero de bloques:
	 *
	 *     bloques   A1   A2   M1   tramas  buenas
	 *        8      58   22   19     12      8
	 *        4      17   14   14     14      9
	 *        2      16   14   14     14      9
	 *        1      14   14   14     14      9
	 *
	 * Con uno solo, A1 dispara CATORCE veces, que son exactamente las
	 * ráfagas que hay en la grabacion, y las catorce confirman. Las
	 * cuarenta y cuatro falsas alarmas desaparecen, y con ellas los
	 * ~130 simbolos de sordera que costaba cada una.
	 *
	 * Y no se paga en sensibilidad, que era el miedo: en sim/hfdl_extremo.c
	 * el barrido de ruido sube o se queda igual en las siete
	 * configuraciones que caben, y el de desvio de portadora pasa a 10/10
	 * en BPSK hasta los 400 Hz que pide la norma. Ademas la grabacion
	 * HFDL.wav, que llevaba todo el dia dando cero, empieza a decodificar.
	 *
	 * OJO: los correladores de M1 SIGUEN TROCEADOS (HFDL_FRAMER_N_BLOCKS,
	 * abajo). Ahi el troceado no estorba igual -son ocho rotaciones de la
	 * misma secuencia y la decision se toma entre ellas, no contra el
	 * ruido- y cambiarlo no se ha medido. Una cosa cada vez.
	 */
	#define HFDL_A1_BLOQUES 1u
	hfdl_framer_init(&p->a_framer, s_a_ref, HFDL_A_LEN, HFDL_A1_BLOQUES, HFDL_CORR_THRESHOLD_A1, HFDL_CORR_THRESHOLD_A1);
#if HFDL_DIAG
	(void)0;
#endif
	for (uint32_t k = 0; k < HFDL_M_SHIFT_CNT; k++) {
		hfdl_framer_init(&p->m1_framer[k], s_m1_ref[k], HFDL_M1_LEN, HFDL_FRAMER_N_BLOCKS, HFDL_CORR_THRESHOLD_M1, HFDL_CORR_THRESHOLD_M1);
	}

	hfdl_preamble_sync_reset(p);
}

void hfdl_preamble_sync_reset(hfdl_preamble_sync_t *p)
{
	hfdl_framer_reset(&p->a_framer);
	p->a_framer.on_thresh = HFDL_CORR_THRESHOLD_A1; /* A2_SEARCH may have lowered this - restore for the next A1_SEARCH */
	for (uint32_t k = 0; k < HFDL_M_SHIFT_CNT; k++) {
		hfdl_framer_reset(&p->m1_framer[k]);
	}
	p->state = HFDL_PREAMBLE_A1_SEARCH;
	p->skip_remaining = 0u;
	p->search_retries = 0u;
	p->a1_polarity = 0;
	p->a1_siguiendo = 0u;
	p->a1_desde_pico = 0u;
	p->a1_mejor = 0.0f;
	p->a2_siguiendo = 0u;
	p->a2_desde_pico = 0u;
	p->a2_mejor = 0.0f;
	p->m1_siguiendo = 0u;
	p->m1_desde_pico = 0u;
	p->m1_mejor = 0.0f;
	p->m1_mejor_idx = -1;
	p->locked_m1_index = -1;
}

/* Ver hfdl_preamble_sync.h. La version de siempre llama a esta con
 * permitido=1, asi que nada de lo que ya existia cambia. */
hfdl_preamble_state_t hfdl_preamble_sync_process_symbol(hfdl_preamble_sync_t *p, float32_t i_sym, float32_t q_sym)
{
	return hfdl_preamble_sync_process_symbol_ex(p, i_sym, q_sym, 1u);
}

hfdl_preamble_state_t hfdl_preamble_sync_process_symbol_ex(hfdl_preamble_sync_t *p,
		float32_t i_sym, float32_t q_sym, uint8_t permitido)
{
	if (!s_refs_ok) {
		/* Ver s_refs_ok: sin tablas no se busca nada. */
		return HFDL_PREAMBLE_A1_SEARCH;
	}
	/*
	 * LOS CORRELADORES DE M1 COMEN SIEMPRE - 14/10/2026.
	 *
	 * Hasta hoy solo recibian simbolos durante SU PROPIO salto, los
	 * M1_LEN-1 que siguen al enganche de A2. Eso funcionaba mientras el
	 * salto fuera entero: llegaban justo los 127 que su ventana necesita.
	 *
	 * Al empezar a enganchar A2 por el PICO -ver a2_siguiendo en el .h-
	 * el salto se acorta en los simbolos transcurridos desde el maximo, y
	 * entonces su anillo se queda con unos cuantos simbolos viejos dentro:
	 * la ventana de 127 ya no es la de M1 y no engancha JAMAS. Medido:
	 * dieciocho enganches de M1 pasaron a cero.
	 *
	 * Con el anillo siempre al dia, el salto solo decide CUANDO se mira,
	 * no QUE se mira, que es como tiene que ser. Cuesta ocho escrituras
	 * de un byte por simbolo y resuelve el acoplamiento entero.
	 */
	if (p->state == HFDL_PREAMBLE_A1_SEARCH || p->state == HFDL_PREAMBLE_A2_SEARCH) {
		for (uint32_t k = 0; k < HFDL_M_SHIFT_CNT; k++) {
			hfdl_framer_push_only(&p->m1_framer[k], i_sym, q_sym);
		}
	}

	switch (p->state) {

	case HFDL_PREAMBLE_A1_SEARCH: {
		/* Ver a1_siguiendo en hfdl_preamble_sync.h: se engancha con el
		 * PICO de la correlacion, no con su primer cruce del umbral. */
		#define HFDL_A1_ESPERA 4u   /* simbolos que se siguen tras el ultimo maximo */
		uint8_t hit = hfdl_framer_process_symbol(&p->a_framer, i_sym, q_sym);
		float32_t sc = hfdl_framer_get_last_score(&p->a_framer);
		float32_t mag = (sc < 0.0f) ? -sc : sc;

		if (p->a1_siguiendo) {
			if (mag > p->a1_mejor) {
				p->a1_mejor = mag;
				p->a1_desde_pico = 0u;
				p->a1_polarity = (sc >= 0.0f) ? 1 : -1;
			} else if (p->a1_desde_pico < 255u) {
				p->a1_desde_pico++;
			}
			if (p->a1_desde_pico >= HFDL_A1_ESPERA) {
				/* Ya no sube: el pico fue hace a1_desde_pico simbolos, y
				 * el salto hasta A2 se descuenta esos mismos. */
				p->a1_siguiendo = 0u;
				p->a_framer.on_thresh = HFDL_CORR_THRESHOLD_A2; /* looser threshold for the A2 confirmation check - see hfdl_preamble_seq.h */
				hfdl_framer_rearm(&p->a_framer); /* re-arm WITHOUT wiping the ring */
				p->state = HFDL_PREAMBLE_A2_SEARCH;
				p->skip_remaining = (HFDL_A_LEN - 1u > (uint32_t)p->a1_desde_pico)
				                  ? (HFDL_A_LEN - 1u - (uint32_t)p->a1_desde_pico)
				                  : 0u;
				p->search_retries = 0u;
			}
		} else if (hit && permitido) {
			/* Ver `permitido` en la cabecera: fuera de la franja del
			 * pre-key el correlador se alimenta igual pero no salta. */
			p->a1_siguiendo = 1u;
			p->a1_mejor = mag;
			p->a1_desde_pico = 0u;
			p->a1_polarity = (sc >= 0.0f) ? 1 : -1;
		}
		break;
	}

	case HFDL_PREAMBLE_A2_SEARCH: {
		if (p->skip_remaining > 0u) {
			hfdl_framer_push_only(&p->a_framer, i_sym, q_sym); /* keep the sliding register current while skipping ahead */
			p->skip_remaining--;
		} else {
			/*
			 * A2 SE QUEDA CON EL PRIMER CRUCE, y no por pereza.
			 *
			 * Tambien se probo por el pico el 14/10/2026. Obligo ademas a
			 * alimentar los correladores de M1 en todos los estados (ver
			 * mas arriba, y esa parte SI se queda porque quita un
			 * acoplamiento feo). Resultado en aire real: identico, 8
			 * buenas de doce antes y despues.
			 *
			 * Tiene sentido que A1 gane y A2 no: A1 busca contra ruido
			 * puro con el umbral estricto, y ahi llegar pronto es facil.
			 * A2 ya sabe donde mirar -llega detras de un salto de 127
			 * simbolos desde A1, que ahora esta bien puesto- asi que su
			 * correlacion arranca ya cerca del maximo.
			 */
			uint8_t hit = hfdl_framer_process_symbol(&p->a_framer, i_sym, q_sym);
			if (hit) {
				for (uint32_t k = 0; k < HFDL_M_SHIFT_CNT; k++) {
					hfdl_framer_rearm(&p->m1_framer[k]);
				}
				p->state = HFDL_PREAMBLE_M1_SEARCH;
				p->skip_remaining = HFDL_M1_LEN - 1u;
				p->search_retries = 0u;
			} else {
				p->search_retries++;
				if (p->search_retries >= HFDL_MAX_SEARCH_RETRIES) {
					hfdl_preamble_sync_reset(p); /* dumphfdl: framer_reset(c) - give up, back to A1_SEARCH */
				}
				/* else: skip_remaining is already 0, so every subsequent symbol re-checks immediately - matches dumphfdl's real post-first-miss behavior (see hfdl_preamble_sync.h's header) */
			}
		}
		break;
	}

	case HFDL_PREAMBLE_M1_SEARCH: {
		if (p->skip_remaining > 0u) {
			for (uint32_t k = 0; k < HFDL_M_SHIFT_CNT; k++) {
				hfdl_framer_push_only(&p->m1_framer[k], i_sym, q_sym);
			}
			p->skip_remaining--;
		} else {
			/*
			 * LA MEJOR DE LAS OCHO, NO LA PRIMERA. 14/10/2026.
			 *
			 * Aqui habia un "la primera que cruce el umbral gana", con un
			 * comentario que lo llamaba "una simplificacion razonable".
			 * No lo era, por dos motivos que se midieron el 14/10/2026 en
			 * deepsdr-ui/sim/hfdl_m1.c:
			 *
			 * 1. Las ocho referencias son rotaciones ciclicas de la MISMA
			 *    secuencia. En los 127 simbolos enteros son casi
			 *    ortogonales, pero este correlador no mira los 127 de una
			 *    pieza: parte la ventana en 8 bloques de 15 y suma
			 *    VALORES ABSOLUTOS (ver HFDL_FRAMER_N_BLOCKS mas arriba).
			 *    En bloques de 15 simbolos la ortogonalidad se pierde, y
			 *    el propio hfdl_framer.h ya avisa de que ese |.| sesga el
			 *    ruido hacia puntuaciones altas segun encogen los
			 *    bloques. Resultado: varias rotaciones cruzan 0,30 a la
			 *    vez.
			 *
			 * 2. El bucle las recorre en orden 0..7, asi que cuando dos
			 *    cruzan gana SIEMPRE la de indice mas bajo, no la que
			 *    mejor correla. Y el indice no es una etiqueta: dice el
			 *    giro, cuantos segmentos de datos vienen, el ritmo del
			 *    codigo y el desplazamiento del desentrelazador
			 *    (HFDL_FRAME_PARAMS[]). Elegir mal no pierde un bit:
			 *    decodifica una trama distinta de la que hay en el aire, y
			 *    el CRC no puede salir bien jamas.
			 *
			 * Lo peor es que el fallo es INVISIBLE en el contador de la
			 * pantalla: los indices 0, 1, 4 y 5 son los cuatro BPSK, y
			 * confundir el 0 con el 1 -ritmo de codigo 4 contra 2- sigue
			 * contando como "BPSK" mientras el CRC falla.
			 *
			 * dumphfdl se queda con la mejor de las ocho (match_sequence()).
			 * Esto hace lo mismo. Medido en sim/hfdl_m1.c: 205 elecciones
			 * equivocadas de 946 enganches antes del cambio, 140 despues,
			 * y el reparto entre los ocho indices pasa de sesgado
			 * (146/130/125/121/106/117/100/101) a plano
			 * (118/115/121/120/113/122/118/119).
			 *
			 * Se lee la puntuacion con get_last_score() en vez del valor
			 * de retorno de process_symbol() a proposito: ese retorno solo
			 * vale en el flanco BUSCANDO->ENCONTRADO, y aqui hace falta la
			 * puntuacion de las ocho en ESTE simbolo para poder compararlas.
			 */
			/*
			 * PROBADO Y RETIRADO el mismo 14/10/2026: decidir con una
			 * correlacion COHERENTE de los 127 simbolos enteros en vez de
			 * con la puntuacion de bloques. La idea era buena sobre el
			 * papel -las ocho rotaciones son casi ortogonales de una
			 * pieza, +-1/127, y en trozos de 15 no lo son- y salio IGUAL:
			 * 140 elecciones equivocadas con la de bloques, 146 con la
			 * coherente, sobre 946 enganches. Dentro del ruido del banco.
			 *
			 * Que no mejore DICE ALGO, y es lo que hay que mirar cuando se
			 * vuelva aqui: si distinguir mejor no arregla nada, es que las
			 * que quedan mal no se eligen mal por parecido sino porque la
			 * VENTANA ESTA DESPLAZADA. Con la ventana corrida un simbolo,
			 * la rotacion buena correla tan mal como las otras siete y
			 * cualquier criterio es una moneda al aire. El desplazamiento
			 * viene de mas arriba -A1 y A2 enganchan en el primer simbolo
			 * que cruza el umbral, no en el maximo- y ahi es donde hay que
			 * ir, no aqui.
			 */
			/*
			 * PROBADO Y RETIRADO - 14/10/2026, de madrugada.
			 *
			 * Se intento elegir tambien AQUI por el pico, siguiendo unos
			 * simbolos y quedandose con el mejor par (rotacion, instante),
			 * y descontando luego el retraso del M2. Es lo que arreglo A1
			 * -alli subio de 6 buenas a 8 sobre doce ráfagas reales-, asi
			 * que parecia el mismo bicho.
			 *
			 * No lo era. En el aire real no movio NADA (8 buenas antes, 8
			 * despues) y en sim/hfdl_m1.c dejo su prueba principal SIN
			 * MUESTRAS: como el enganche se cierra a proposito cuatro
			 * simbolos tarde, el "engancha en su sitio -> 344 de 344" paso
			 * a "0 de 0". Una prueba que pasa por vacia es peor que una
			 * que falla, porque no avisa.
			 *
			 * La diferencia con A1 es que alli el retraso se absorbe en un
			 * salto que ya existia; aqui hay que arrastrarlo hasta el M2 y
			 * cualquier descuadre se lleva la trama entera. Mas riesgo y
			 * cero beneficio medido: fuera.
			 */
			int32_t matched = -1;
			float32_t mejor = 0.0f;
			for (uint32_t k = 0; k < HFDL_M_SHIFT_CNT; k++) {
				float32_t sc;
				(void)hfdl_framer_process_symbol(&p->m1_framer[k], i_sym, q_sym);
				sc = hfdl_framer_get_last_score(&p->m1_framer[k]);
				if (sc < 0.0f) { sc = -sc; } /* el camino de un solo bloque da puntuacion con signo */
				if (sc >= HFDL_CORR_THRESHOLD_M1 && sc > mejor) {
					mejor = sc;
					matched = (int32_t)k;
				}
			}
			if (matched >= 0) {
				p->locked_m1_index = matched;
				p->state = HFDL_PREAMBLE_LOCKED;
			} else {
				p->search_retries++;
				if (p->search_retries >= HFDL_MAX_SEARCH_RETRIES) {
					hfdl_preamble_sync_reset(p); /* dumphfdl: framer_reset(c) */
				}
			}
		}
		break;
	}

	case HFDL_PREAMBLE_LOCKED:
	default:
		/* Nothing to do - caller drives the next hfdl_preamble_sync_reset() once this
		 * frame's preamble has been consumed (see hfdl_preamble_sync.h). */
		break;
	}

	return p->state;
}

hfdl_preamble_state_t hfdl_preamble_sync_get_state(const hfdl_preamble_sync_t *p)
{
	return p->state;
}

uint8_t hfdl_preamble_sync_retardo(const hfdl_preamble_sync_t *p)
{
	return p->m1_desde_pico;
}

int32_t hfdl_preamble_sync_get_m1_index(const hfdl_preamble_sync_t *p)
{
	return (p->state == HFDL_PREAMBLE_LOCKED) ? p->locked_m1_index : -1;
}

const hfdl_frame_params_t *hfdl_preamble_sync_get_frame_params(const hfdl_preamble_sync_t *p)
{
	return &HFDL_FRAME_PARAMS[p->locked_m1_index];
}

float32_t hfdl_preamble_sync_get_a_score(const hfdl_preamble_sync_t *p)
{
	return hfdl_framer_get_last_score(&p->a_framer);
}

float32_t hfdl_preamble_sync_peek_a_score(const hfdl_preamble_sync_t *p)
{
	return hfdl_framer_peek_score(&p->a_framer);
}

/* ==========================================================================
 * Self-test - see hfdl_preamble_sync.h's header: real A/M1 reference
 * sequences, synthetic clean stream (no real DSP chain in front of it).
 * ========================================================================== */

static uint32_t xorshift32(uint32_t *state)
{
	uint32_t x = *state;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	*state = x;
	return x;
}

uint8_t hfdl_preamble_sync_selftest(void)
{
	build_refs_once();

	const uint32_t TEST_M1_INDEX = 3u; /* arbitrary pick (1800bps single slot) - exercises a non-zero index, not just index 0 */

	hfdl_preamble_sync_t p;
	hfdl_preamble_sync_init(&p);

	uint32_t seed = 0xDEADBEEFu;
	hfdl_preamble_state_t last_state = HFDL_PREAMBLE_A1_SEARCH;

	#define FEED_NOISE(n) \
		for (uint32_t i = 0; i < (n); i++) { \
			float32_t b = (xorshift32(&seed) & 1u) ? 1.0f : -1.0f; \
			last_state = hfdl_preamble_sync_process_symbol(&p, b, 0.0f); \
		}
	#define FEED_SEQ(arr, n) \
		for (uint32_t i = 0; i < (n); i++) { \
			last_state = hfdl_preamble_sync_process_symbol(&p, (arr)[i], 0.0f); \
		}

	FEED_NOISE(300u); /* pre-preamble noise/silence */
	if (last_state != HFDL_PREAMBLE_A1_SEARCH) {
		return 0u; /* must NOT have false-locked on noise */
	}

	FEED_SEQ(s_a_ref, HFDL_A_LEN); /* A1 occurrence: last symbol completes the window and triggers the hit */
	if (last_state != HFDL_PREAMBLE_A2_SEARCH) {
		return 0u;
	}

	/* A2's occurrence, fed as a single contiguous HFDL_A_LEN-symbol
	 * block, same as the real signal would present it: the first
	 * HFDL_A_LEN-1 of these are consumed by process_symbol()'s
	 * internal skip-ahead (pushed into the ring, not checked - see
	 * HFDL_PREAMBLE_A2_SEARCH's skip_remaining handling), and the
	 * LAST one is the symbol where skip_remaining reaches 0 and the
	 * correlation check actually runs - which is exactly when the
	 * ring's last HFDL_A_LEN entries equal the full A pattern. */
	FEED_SEQ(s_a_ref, HFDL_A_LEN);
	if (last_state != HFDL_PREAMBLE_M1_SEARCH) {
		return 0u; /* A2 should have been confirmed */
	}

	/* Same shape for the M1 occurrence - HFDL_M1_LEN symbols, first
	 * HFDL_M1_LEN-1 skipped-but-pushed, last one triggers the check. */
	FEED_SEQ(s_m1_ref[TEST_M1_INDEX], HFDL_M1_LEN);
	if (last_state != HFDL_PREAMBLE_LOCKED) {
		return 0u; /* M1 should have matched */
	}
	if (hfdl_preamble_sync_get_m1_index(&p) != (int32_t)TEST_M1_INDEX) {
		return 0u; /* must identify the CORRECT rotation, not just any */
	}
	const hfdl_frame_params_t *fp = hfdl_preamble_sync_get_frame_params(&p);
	if (fp->mod_arity != HFDL_FRAME_PARAMS[TEST_M1_INDEX].mod_arity) {
		return 0u;
	}

	/* Reset and confirm a fresh search doesn't spuriously "remember"
	 * the previous lock. */
	hfdl_preamble_sync_reset(&p);
	if (hfdl_preamble_sync_get_state(&p) != HFDL_PREAMBLE_A1_SEARCH) {
		return 0u;
	}
	if (hfdl_preamble_sync_get_m1_index(&p) != -1) {
		return 0u;
	}

	#undef FEED_NOISE
	#undef FEED_SEQ

	return 1u;
}

#if HFDL_DIAG
const float32_t *hfdl_preamble_sync_ref_a(void) { build_refs_once(); return s_a_ref; }
#endif
