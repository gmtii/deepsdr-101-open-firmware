#ifndef ARM_MATH_H_SUCEDANEO
#define ARM_MATH_H_SUCEDANEO

#include <stdint.h>
#include <stddef.h>
#include <math.h>

/*
 * Sucedaneo del arm_math.h de CMSIS-DSP, SOLO para el simulador del PC.
 *
 * POR QUE (25/09/2026). Los modulos de HFDL usan del CMSIS exactamente DOS
 * funciones -arm_fir_init_f32 y arm_fir_f32- y el tipo float32_t. Nada mas:
 * se comprobo con un grep sobre los 23 ficheros antes de escribir esto.
 *
 * Se intento compilar el CMSIS de verdad aqui. El arm_math.h solo, compila;
 * el arm_fir_f32.c arrastra cmsis_compiler.h -> core_cmInstr.h, o sea el
 * nucleo del Cortex-M, y sin el se cae con "redefinition of __QADD8" porque
 * hay dos juegos de intrinsecas peleandose. Meter medio CMSIS en el
 * simulador para probar un FIR no compensa.
 *
 * QUE ES REAL Y QUE NO, dicho claro para que nadie lea de mas en un banco
 * que pase:
 *
 *   REAL: los 23 modulos de HFDL. Es SU codigo el que se ejecuta, el mismo
 *         que se compila para la radio.
 *   NO REAL: estas dos funciones. Son un FIR en forma directa escrito aqui,
 *         diez lineas, matematicamente lo mismo que hace el CMSIS -que es
 *         el mismo bucle con las vueltas desenrolladas de cuatro en cuatro-
 *         pero NO es su codigo.
 *
 * O sea: si un banco de HFDL pasa aqui, dice que la logica de HFDL esta
 * bien. NO dice nada sobre el FIR del CMSIS en la radio. Para eso hace
 * falta la radio.
 *
 * SI ALGUN DIA HFDL USA OTRA FUNCION DEL CMSIS, esto falla al enlazar con
 * un simbolo que no existe, bien visible. Que es como tiene que fallar un
 * sucedaneo: no inventando un resultado.
 */

/* El arm_math.h de verdad define PI (y no usa el M_PI de la libreria).
 * Dos modulos de HFDL lo usan. Mismo valor que el suyo. */
#ifndef PI
#define PI 3.14159265358979323846
#endif

typedef float  float32_t;
typedef double float64_t;

/* Mismos campos y mismo orden que el de verdad (CMSIS/DSP/Include/dsp/
 * filtering_functions.h). Aqui la disposicion solo tiene que ser coherente
 * consigo misma -el sucedaneo y quien lo usa se compilan juntos- pero se
 * copia igual para que leer los dos ficheros no de sorpresas. */
typedef struct {
          uint16_t   numTaps;
          float32_t *pState;
    const float32_t *pCoeffs;
} arm_fir_instance_f32;

static inline void arm_fir_init_f32(arm_fir_instance_f32 *S, uint16_t numTaps,
                                    const float32_t *pCoeffs, float32_t *pState,
                                    uint32_t blockSize)
{
    uint32_t i, n = (uint32_t)numTaps + blockSize - 1u;

    S->numTaps = numTaps;
    S->pCoeffs = pCoeffs;
    S->pState  = pState;
    /* El de verdad tambien deja el estado a cero: si no, el primer bloque
     * sale con la basura que hubiera en el buffer del que llama. */
    for (i = 0u; i < n; i++) { pState[i] = 0.0f; }
}

/*
 * FIR en forma directa, con el mismo contrato que el del CMSIS: el estado
 * guarda las numTaps-1 muestras viejas al principio, las nuevas se anaden
 * detras, y al acabar se corren hacia delante. Admite pSrc == pDst, que es
 * como lo llama hfdl_matched_filter.c.
 *
 * *** EL ORDEN DE LOS COEFICIENTES ESTABA DEL REVES - 10/10/2026. ***
 *
 * Esta funcion decia en un comentario "pCoeffs[0] multiplica la muestra MAS
 * NUEVA, igual que el CMSIS", y hacia eso. El CMSIS hace lo contrario:
 *
 *     acc = 0;  px = pState;  pb = pCoeffs;
 *     i = numTaps;  while (i--) { acc += *px++ * *pb++; }
 *
 * o sea pCoeffs[0] contra pState[0], que es la muestra MAS VIEJA de la
 * ventana. Aplicar los coeficientes al reves es filtrar con h[-n].
 *
 * NO SE HABIA NOTADO, Y POR UNA RAZON QUE NO ERA SUERTE DEL TODO: el unico
 * que usaba esto era el filtro adaptado de HFDL, 19 coeficientes y
 * SIMETRICO (comprobado: h[i] == h[18-i] para los 19). Dar la vuelta a un
 * filtro simetrico no cambia nada, asi que los bancos de HFDL salian igual
 * de bien estando mal.
 *
 * SE IBA A NOTAR HOY. El banco de demod_am.c compila el segundo usuario de
 * arm_fir_f32 en el arbol: HILBERT_COEFFS, 63 coeficientes y
 * ANTISIMETRICO (comprobado: h[i] == -h[62-i] para los 63). Dar la vuelta a
 * un filtro antisimetrico lo MULTIPLICA POR -1. Un Hilbert cambiado de
 * signo es la banda lateral contraria: USB sonando donde se pide LSB, en el
 * simulador y solo en el simulador, sin un solo aviso.
 *
 * Arreglado al orden del CMSIS. Para HFDL es, demostrablemente, no hacer
 * nada: sus 19 coeficientes son simetricos. Comprobado ademas corriendo sus
 * bancos antes y despues.
 */
static inline void arm_fir_f32(const arm_fir_instance_f32 *S, const float32_t *pSrc,
                               float32_t *pDst, uint32_t blockSize)
{
    uint32_t   nt = (uint32_t)S->numTaps;
    float32_t *st = S->pState;
    uint32_t   i, k;

    for (i = 0u; i < blockSize; i++) { st[nt - 1u + i] = pSrc[i]; }

    for (i = 0u; i < blockSize; i++) {
        float32_t acc = 0.0f;
        /* pCoeffs[0] contra la muestra MAS VIEJA de la ventana, que es lo
         * que hace el CMSIS. Ver el comentario de arriba. */
        for (k = 0u; k < nt; k++) {
            acc += S->pCoeffs[k] * st[i + k];
        }
        pDst[i] = acc;
    }

    for (i = 0u; i < nt - 1u; i++) { st[i] = st[blockSize + i]; }
}


/*
 * ======================================================================
 * LO QUE HIZO FALTA PARA COMPILAR demod_am.c - 10/10/2026
 * ======================================================================
 *
 * demod_am.c es el fichero grande que ningun banco compilaba, y la deuda
 * estaba escrita en el propio fichero, al lado de la union s_via:
 *
 *     "demod_am.c es el unico fichero grande del arbol que NINGUN banco
 *      compila -tiene 37 cabeceras propias- asi que esto no esta probado
 *      en marcha, solo comprobado estaticamente."
 *
 * Del CMSIS usa cuatro familias y nada mas (grep sobre el fichero entero):
 * el biquad en forma directa I, el FIR diezmador, el FIR interpolador y el
 * FIR de arriba. Aqui estan las tres que faltaban.
 *
 * QUE ES REAL Y QUE NO, con el mismo criterio que lo de arriba:
 *
 *   REAL: demod_am.c. Es SU codigo el que corre, el mismo que se compila
 *         para la radio, con sus tablas de coeficientes y su union.
 *   NO REAL: estas funciones. Son la misma matematica escrita aqui en
 *         forma directa; el CMSIS hace lo mismo con las vueltas
 *         desenrolladas y, en el Cortex-M4, con instrucciones de coma
 *         flotante de una sola operacion. Los numeros pueden diferir en el
 *         ultimo bit por el orden de las sumas. No son su codigo.
 *
 * SE COPIA EL CONTRATO AL DETALLE, NO "ALGO QUE FILTRE". Un sucedaneo que
 * se inventa el convenio es peor que no tenerlo, porque el banco pasa y lo
 * que ha medido es otra cosa. Eso acaba de pasar con el orden de los
 * coeficientes del FIR de arriba: iba del reves y lo tapaba un filtro
 * simetrico. Asi que de cada una se copia:
 *
 *   - la disposicion del estado y en que orden se guarda;
 *   - a que muestra multiplica cada coeficiente;
 *   - el signo de los coeficientes del denominador del biquad;
 *   - y que devuelve init() cuando los tamaños no cuadran.
 *
 * Hay un banco que lo comprueba: sim/armmath_test.c, que mide estas cuatro
 * contra resultados calculados a mano.
 */

typedef enum {
    ARM_MATH_SUCCESS        =  0,
    ARM_MATH_ARGUMENT_ERROR = -1,
    ARM_MATH_LENGTH_ERROR   = -2,
    ARM_MATH_SIZE_MISMATCH  = -3,
    ARM_MATH_NANINF         = -4,
    ARM_MATH_SINGULAR       = -5,
    ARM_MATH_TEST_FAILURE   = -6
} arm_status;

/* ---------------------------------------------------------------------
 * Biquad en cascada, forma directa I, coma flotante.
 *
 * EL CONVENIO DEL CMSIS, QUE NO ES EL DE UN LIBRO DE TEXTO:
 *
 *   pCoeffs: 5 por etapa, en el orden {b0, b1, b2, a1, a2}, y a1/a2 ya
 *            vienen CAMBIADOS DE SIGNO. O sea que la ecuacion es
 *
 *                y[n] = b0 x[n] + b1 x[n-1] + b2 x[n-2]
 *                              + a1 y[n-1] + a2 y[n-2]
 *
 *            con un MAS delante de a1 y a2. Quien escribe las tablas ya lo
 *            sabe -ver las de demod_am.c- y poner aqui un menos haria que
 *            cada filtro del fichero fuese otro filtro, probablemente
 *            inestable.
 *   pState:  4 por etapa, {x[n-1], x[n-2], y[n-1], y[n-2]}.
 *
 * La salida de una etapa es la entrada de la siguiente. Admite pSrc ==
 * pDst, que es como lo llama demod_am.c.
 * --------------------------------------------------------------------- */
typedef struct {
          uint32_t   numStages;
          float32_t *pState;
    const float32_t *pCoeffs;
} arm_biquad_casd_df1_inst_f32;

static inline void arm_biquad_cascade_df1_init_f32(arm_biquad_casd_df1_inst_f32 *S,
                                                   uint8_t numStages,
                                                   const float32_t *pCoeffs,
                                                   float32_t *pState)
{
    uint32_t i, n = 4u * (uint32_t)numStages;

    S->numStages = (uint32_t)numStages;
    S->pCoeffs   = pCoeffs;
    S->pState    = pState;
    /* El de verdad tambien pone el estado a cero aqui. */
    for (i = 0u; i < n; i++) { pState[i] = 0.0f; }
}

static inline void arm_biquad_cascade_df1_f32(const arm_biquad_casd_df1_inst_f32 *S,
                                              const float32_t *pSrc,
                                              float32_t *pDst,
                                              uint32_t blockSize)
{
    const float32_t *pc = S->pCoeffs;
    float32_t       *st = S->pState;
    uint32_t         etapa, i;
    const float32_t *ent = pSrc;

    for (etapa = 0u; etapa < S->numStages; etapa++) {
        const float32_t b0 = pc[0], b1 = pc[1], b2 = pc[2];
        const float32_t a1 = pc[3], a2 = pc[4];
        float32_t x1 = st[0], x2 = st[1], y1 = st[2], y2 = st[3];

        for (i = 0u; i < blockSize; i++) {
            float32_t x0 = ent[i];
            float32_t y0 = b0 * x0 + b1 * x1 + b2 * x2 + a1 * y1 + a2 * y2;
            x2 = x1; x1 = x0;
            y2 = y1; y1 = y0;
            pDst[i] = y0;
        }

        st[0] = x1; st[1] = x2; st[2] = y1; st[3] = y2;
        pc += 5;
        st += 4;
        /* De la segunda etapa en adelante se trabaja sobre la salida. */
        ent = pDst;
    }
}

/* ---------------------------------------------------------------------
 * FIR diezmador.
 *
 * Se meten M muestras, sale una. El estado guarda numTaps-1 viejas delante,
 * como el FIR normal, y avanza de M en M. pCoeffs[0] contra la muestra mas
 * vieja de la ventana, igual que arriba.
 *
 * init() devuelve ARM_MATH_LENGTH_ERROR si blockSize no es multiplo de M,
 * y demod_am.c MIRA ese valor (ver los dos `if (arm_fir_decimate_init_f32(
 * ...) != ARM_MATH_SUCCESS)`), asi que devolverlo bien no es un detalle.
 * --------------------------------------------------------------------- */
typedef struct {
          uint8_t    M;
          uint16_t   numTaps;
    const float32_t *pCoeffs;
          float32_t *pState;
} arm_fir_decimate_instance_f32;

static inline arm_status arm_fir_decimate_init_f32(arm_fir_decimate_instance_f32 *S,
                                                   uint16_t numTaps, uint8_t M,
                                                   const float32_t *pCoeffs,
                                                   float32_t *pState,
                                                   uint32_t blockSize)
{
    uint32_t i, n;

    if (M == 0u || (blockSize % (uint32_t)M) != 0u) {
        return ARM_MATH_LENGTH_ERROR;
    }
    S->M       = M;
    S->numTaps = numTaps;
    S->pCoeffs = pCoeffs;
    S->pState  = pState;
    n = (uint32_t)numTaps + blockSize - 1u;
    for (i = 0u; i < n; i++) { pState[i] = 0.0f; }
    return ARM_MATH_SUCCESS;
}

static inline void arm_fir_decimate_f32(const arm_fir_decimate_instance_f32 *S,
                                        const float32_t *pSrc, float32_t *pDst,
                                        uint32_t blockSize)
{
    uint32_t   nt = (uint32_t)S->numTaps;
    uint32_t   M  = (uint32_t)S->M;
    float32_t *st = S->pState;
    uint32_t   i, k, salidas = blockSize / M;

    for (i = 0u; i < blockSize; i++) { st[nt - 1u + i] = pSrc[i]; }

    for (i = 0u; i < salidas; i++) {
        float32_t acc = 0.0f;
        for (k = 0u; k < nt; k++) {
            acc += S->pCoeffs[k] * st[i * M + k];
        }
        pDst[i] = acc;
    }

    for (i = 0u; i < nt - 1u; i++) { st[i] = st[blockSize + i]; }
}

/* ---------------------------------------------------------------------
 * FIR interpolador (polifasico).
 *
 * Entra una muestra, salen L. El CMSIS no filtra sobre la señal expandida:
 * parte los numTaps coeficientes en L fases de phaseLength = numTaps/L, y
 * cada fase da una de las L salidas. El detalle que hay que copiar es QUE
 * FASE SALE PRIMERO:
 *
 *     j = 1;  i = L;
 *     while (i--) { ptr2 = pCoeffs + (L - j);  ... ptr2 += L; ...  j++; }
 *
 * o sea que la primera salida usa los coeficientes L-1, 2L-1, 3L-1..., la
 * segunda L-2, 2L-2..., y la ultima 0, L, 2L... Al reves de lo que uno
 * escribiria sin mirar, y cambiarlo es un retardo de fraccion de muestra:
 * no rompe nada que se vea, solo mueve el instante de muestreo.
 *
 * init() devuelve ARM_MATH_LENGTH_ERROR si numTaps no es multiplo de L.
 * demod_am.c tambien mira ese valor.
 * --------------------------------------------------------------------- */
typedef struct {
          uint8_t    L;
          uint16_t   phaseLength;
    const float32_t *pCoeffs;
          float32_t *pState;
} arm_fir_interpolate_instance_f32;

static inline arm_status arm_fir_interpolate_init_f32(arm_fir_interpolate_instance_f32 *S,
                                                      uint8_t L, uint16_t numTaps,
                                                      const float32_t *pCoeffs,
                                                      float32_t *pState,
                                                      uint32_t blockSize)
{
    uint32_t i, n;

    if (L == 0u || ((uint32_t)numTaps % (uint32_t)L) != 0u) {
        return ARM_MATH_LENGTH_ERROR;
    }
    S->L           = L;
    S->phaseLength = (uint16_t)((uint32_t)numTaps / (uint32_t)L);
    S->pCoeffs     = pCoeffs;
    S->pState      = pState;
    n = blockSize + (uint32_t)S->phaseLength - 1u;
    for (i = 0u; i < n; i++) { pState[i] = 0.0f; }
    return ARM_MATH_SUCCESS;
}

static inline void arm_fir_interpolate_f32(const arm_fir_interpolate_instance_f32 *S,
                                           const float32_t *pSrc, float32_t *pDst,
                                           uint32_t blockSize)
{
    uint32_t   L  = (uint32_t)S->L;
    uint32_t   pl = (uint32_t)S->phaseLength;
    float32_t *st = S->pState;
    uint32_t   n, j, k, o = 0u;

    for (n = 0u; n < blockSize; n++) { st[pl - 1u + n] = pSrc[n]; }

    for (n = 0u; n < blockSize; n++) {
        for (j = 1u; j <= L; j++) {
            float32_t acc = 0.0f;
            for (k = 0u; k < pl; k++) {
                acc += S->pCoeffs[(L - j) + k * L] * st[n + k];
            }
            pDst[o++] = acc;
        }
    }

    for (n = 0u; n < pl - 1u; n++) { st[n] = st[blockSize + n]; }
}

#endif /* ARM_MATH_H_SUCEDANEO */
