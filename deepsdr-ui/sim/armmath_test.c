/*
 * EL BANCO DEL SUCEDANEO DEL CMSIS. 10/10/2026.
 *
 * hostinc/arm_math.h no es el CMSIS: es el mismo calculo escrito a mano
 * para que el simulador pueda compilar codigo del firmware sin arrastrar
 * medio Cortex-M. Eso esta dicho desde el primer dia en su cabecera.
 *
 * LO QUE NO ESTABA DICHO ES QUE NADIE LO COMPROBABA.
 *
 * Y hoy se ha visto lo que cuesta: su arm_fir_f32 aplicaba los coeficientes
 * AL REVES que el CMSIS, y lo decia en un comentario convencido de lo
 * contrario. No habia saltado porque su unico usuario -el filtro adaptado
 * de HFDL- tiene 19 coeficientes SIMETRICOS, y a un filtro simetrico le da
 * igual el orden. El siguiente usuario era el Hilbert de demod_am.c: 63
 * coeficientes ANTISIMETRICOS, donde dar la vuelta es multiplicar por -1,
 * o sea la banda lateral contraria.
 *
 * Un sucedaneo sin banco es una suposicion con sintaxis de C. Esto son las
 * cuatro familias que usa el firmware, cada una contra numeros calculados a
 * mano, y cada comprobacion elegida para que distinga el convenio BUENO del
 * convenio PLAUSIBLE: filtros asimetricos donde dar la vuelta se nota,
 * signos donde invertirlos se nota, y fases donde confundirlas se nota.
 *
 *   make armmath FIRMWARE=...
 */
#include <stdio.h>
#include <math.h>
#include "arm_math.h"

static int fallos;

static void igual(const char *que, float visto, float espera)
{
    /* 1e-5 relativo: estas cuentas son de cuatro o cinco operaciones, no
     * hay sitio para que se acumule nada. Un margen flojo aqui taparia
     * justo los fallos que esto busca. */
    float tol = 1e-5f * (fabsf(espera) > 1.0f ? fabsf(espera) : 1.0f);
    if (fabsf(visto - espera) <= tol) {
        printf("    %-46s ok\n", que);
    } else {
        printf("    %-46s *** MAL ***  visto %.6f, esperaba %.6f\n",
               que, (double)visto, (double)espera);
        fallos++;
    }
}

static void igual_i(const char *que, int visto, int espera)
{
    if (visto == espera) {
        printf("    %-46s ok\n", que);
    } else {
        printf("    %-46s *** MAL ***  visto %d, esperaba %d\n",
               que, visto, espera);
        fallos++;
    }
}

/* ------------------------------------------------------------------ */
/* 1. EL FIR, Y EL ORDEN DE LOS COEFICIENTES.                          */
/*                                                                     */
/* h = {1, 10, 100}, asimetrico a proposito: con el convenio del CMSIS  */
/* -h[0] contra la muestra mas vieja- la respuesta al impulso sale      */
/* {100, 10, 1, 0}. Con los coeficientes al reves sale {1, 10, 100, 0}: */
/* los mismos numeros en orden contrario, que es como se disfraza este  */
/* fallo de "funciona".                                                 */
/* ------------------------------------------------------------------ */
static void prueba_fir(void)
{
    static const float32_t h[3] = { 1.0f, 10.0f, 100.0f };
    float32_t estado[3 + 4 - 1];
    float32_t x[4] = { 1.0f, 0.0f, 0.0f, 0.0f };
    float32_t y[4];
    arm_fir_instance_f32 S;

    printf("\n  FIR: a que muestra multiplica pCoeffs[0]\n");
    arm_fir_init_f32(&S, 3, h, estado, 4);
    arm_fir_f32(&S, x, y, 4);
    igual("impulso, muestra 0 (el coeficiente ULTIMO)", y[0], 100.0f);
    igual("impulso, muestra 1", y[1], 10.0f);
    igual("impulso, muestra 2 (el coeficiente PRIMERO)", y[2], 1.0f);
    igual("impulso, muestra 3", y[3], 0.0f);

    /* Y QUE EL ESTADO CRUCE DE UN BLOQUE A OTRO. Un FIR que olvida entre
     * bloques tambien da bien la respuesta al impulso de arriba si cabe
     * entera en el primer bloque; esto es lo que lo distingue. */
    {
        float32_t x1[2] = { 1.0f, 0.0f };
        float32_t x2[2] = { 0.0f, 0.0f };
        float32_t y1[2], y2[2];
        arm_fir_init_f32(&S, 3, h, estado, 2);
        arm_fir_f32(&S, x1, y1, 2);
        arm_fir_f32(&S, x2, y2, 2);
        igual("partido en dos bloques, muestra 0", y1[0], 100.0f);
        igual("partido en dos bloques, muestra 1", y1[1], 10.0f);
        igual("la cola cae en el bloque SIGUIENTE", y2[0], 1.0f);
        igual("y luego nada", y2[1], 0.0f);
    }

    /* EL CASO QUE IBA A DOLER: un filtro ANTISIMETRICO, como el Hilbert de
     * demod_am.c. h = {1, 0, -1}. Con el orden bueno la salida de un
     * impulso es {-1, 0, 1}; con el orden del reves, {1, 0, -1}. Cambiado
     * de signo, que en banda lateral unica es la banda contraria. */
    {
        static const float32_t ha[3] = { 1.0f, 0.0f, -1.0f };
        float32_t est2[3 + 4 - 1];
        float32_t xi[4] = { 1.0f, 0.0f, 0.0f, 0.0f };
        float32_t yo[4];
        arm_fir_init_f32(&S, 3, ha, est2, 4);
        arm_fir_f32(&S, xi, yo, 4);
        igual("antisimetrico: el signo NO esta invertido", yo[0], -1.0f);
        igual("antisimetrico: centro", yo[1], 0.0f);
        igual("antisimetrico: cola", yo[2], 1.0f);
    }
}

/* ------------------------------------------------------------------ */
/* 2. EL BIQUAD, Y EL SIGNO DE a1/a2.                                  */
/*                                                                     */
/* El CMSIS quiere a1 y a2 YA cambiados de signo, o sea y[n] = ... +    */
/* a1*y[n-1] + a2*y[n-2] con un MAS. Con b={1,0,0}, a1=0.5, a2=0 la     */
/* respuesta al impulso es 0.5^n: {1, 0.5, 0.25, 0.125}. Si el signo    */
/* estuviera al reves saldria {1, -0.5, 0.25, -0.125}: alterna, que es  */
/* la firma de haberse equivocado aqui.                                 */
/* ------------------------------------------------------------------ */
static void prueba_biquad(void)
{
    static const float32_t c1[5] = { 1.0f, 0.0f, 0.0f, 0.5f, 0.0f };
    float32_t estado[4];
    float32_t x[4] = { 1.0f, 0.0f, 0.0f, 0.0f };
    float32_t y[4];
    arm_biquad_casd_df1_inst_f32 S;

    printf("\n  biquad: el signo de a1/a2 y la cascada\n");
    arm_biquad_cascade_df1_init_f32(&S, 1, c1, estado);
    arm_biquad_cascade_df1_f32(&S, x, y, 4);
    igual("un polo a 0,5: muestra 0", y[0], 1.0f);
    igual("un polo a 0,5: muestra 1 (positiva)", y[1], 0.5f);
    igual("un polo a 0,5: muestra 2", y[2], 0.25f);
    igual("un polo a 0,5: muestra 3", y[3], 0.125f);

    /* El estado tiene que cruzar entre llamadas, igual que en la radio:
     * alli cada bloque es una llamada. */
    {
        float32_t x1[2] = { 1.0f, 0.0f }, x2[2] = { 0.0f, 0.0f };
        float32_t y1[2], y2[2];
        arm_biquad_cascade_df1_init_f32(&S, 1, c1, estado);
        arm_biquad_cascade_df1_f32(&S, x1, y1, 2);
        arm_biquad_cascade_df1_f32(&S, x2, y2, 2);
        igual("el estado cruza de bloque: y[2]", y2[0], 0.25f);
        igual("el estado cruza de bloque: y[3]", y2[1], 0.125f);
    }

    /* DOS ETAPAS. Dos polos iguales a 0,5 en cascada dan (n+1)*0.5^n:
     * {1, 1, 0.75, 0.5}. Esto cae si la segunda etapa lee la ENTRADA en
     * vez de la salida de la primera, o si el estado de la segunda pisa
     * al de la primera (de ahi los cuatro huecos por etapa). */
    {
        static const float32_t c2[10] = { 1.0f, 0.0f, 0.0f, 0.5f, 0.0f,
                                          1.0f, 0.0f, 0.0f, 0.5f, 0.0f };
        float32_t est2[8];
        float32_t xi[4] = { 1.0f, 0.0f, 0.0f, 0.0f };
        float32_t yo[4];
        arm_biquad_cascade_df1_init_f32(&S, 2, c2, est2);
        arm_biquad_cascade_df1_f32(&S, xi, yo, 4);
        igual("dos etapas: muestra 0", yo[0], 1.0f);
        igual("dos etapas: muestra 1", yo[1], 1.0f);
        igual("dos etapas: muestra 2", yo[2], 0.75f);
        igual("dos etapas: muestra 3", yo[3], 0.5f);
    }
}

/* ------------------------------------------------------------------ */
/* 3. EL DIEZMADOR.                                                     */
/*                                                                     */
/* M=2, h={1,10,100}, impulso: la ventana avanza de dos en dos, asi que */
/* salen {100, 1} y no {100, 10}. Y lo que devuelve init() con un       */
/* tamaño que no cuadra lo MIRA demod_am.c, asi que se comprueba.        */
/* ------------------------------------------------------------------ */
static void prueba_diezma(void)
{
    static const float32_t h[3] = { 1.0f, 10.0f, 100.0f };
    float32_t estado[3 + 4 - 1];
    float32_t x[4] = { 1.0f, 0.0f, 0.0f, 0.0f };
    float32_t y[2];
    arm_fir_decimate_instance_f32 S;
    arm_status st;

    printf("\n  diezmador: el salto de la ventana y el error de tamaño\n");
    st = arm_fir_decimate_init_f32(&S, 3, 2, h, estado, 4);
    igual_i("init con bloque multiplo de M", (int)st, (int)ARM_MATH_SUCCESS);
    arm_fir_decimate_f32(&S, x, y, 4);
    igual("salida 0", y[0], 100.0f);
    igual("salida 1 (saltandose la de 10)", y[1], 1.0f);

    st = arm_fir_decimate_init_f32(&S, 3, 2, h, estado, 3);
    igual_i("init con bloque IMPAR devuelve LENGTH_ERROR",
            (int)st, (int)ARM_MATH_LENGTH_ERROR);
}

/* ------------------------------------------------------------------ */
/* 4. EL INTERPOLADOR, Y EL ORDEN DE LAS FASES.                         */
/*                                                                     */
/* L=2, h={1,2,3,4}, phaseLength=2, x={1,0}. El CMSIS empieza por la    */
/* fase L-1 y baja, asi que sale {4, 3, 2, 1}. Si se empezara por la    */
/* fase 0 saldria {3, 4, 1, 2}: los mismos numeros, por parejas         */
/* intercambiadas. No rompe nada audible -es medio periodo de retardo-  */
/* y por eso hay que medirlo y no mirarlo.                              */
/* ------------------------------------------------------------------ */
static void prueba_interpola(void)
{
    static const float32_t h[4] = { 1.0f, 2.0f, 3.0f, 4.0f };
    float32_t estado[2 + 2 - 1];
    float32_t x[2] = { 1.0f, 0.0f };
    float32_t y[4];
    arm_fir_interpolate_instance_f32 S;
    arm_status st;

    printf("\n  interpolador: que fase sale primero\n");
    st = arm_fir_interpolate_init_f32(&S, 2, 4, h, estado, 2);
    igual_i("init con numTaps multiplo de L", (int)st, (int)ARM_MATH_SUCCESS);
    igual_i("phaseLength = numTaps / L", (int)S.phaseLength, 2);
    arm_fir_interpolate_f32(&S, x, y, 2);
    igual("salida 0 (fase L-1)", y[0], 4.0f);
    igual("salida 1 (fase L-2)", y[1], 3.0f);
    igual("salida 2", y[2], 2.0f);
    igual("salida 3", y[3], 1.0f);

    st = arm_fir_interpolate_init_f32(&S, 3, 4, h, estado, 2);
    igual_i("numTaps no multiplo de L devuelve LENGTH_ERROR",
            (int)st, (int)ARM_MATH_LENGTH_ERROR);
}

int main(void)
{
    printf("\nEL SUCEDANEO DEL CMSIS, CONTRA NUMEROS HECHOS A MANO\n");
    prueba_fir();
    prueba_biquad();
    prueba_diezma();
    prueba_interpola();

    printf("\n");
    if (fallos) {
        printf("HAY FALLOS (%d)\n\n", fallos);
        return 1;
    }
    printf("TODO CORRECTO (0 fallos)\n\n");
    return 0;
}
