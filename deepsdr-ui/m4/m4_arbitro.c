/*
 * EL ARBITRO: CUANTO CUESTA DE VERDAD EL DETECTOR DE STANAG, POR SEGUNDO.
 *
 * POR QUE EXISTE - 08/11/2026.
 *
 * Al medir la radio entera en el M4 salieron DOS numeros para la misma
 * funcion, stanag_det_paso(), y no se parecian:
 *
 *     por el lado de IDENT      2.726.360 instrucciones por ventana
 *     por el lado de STANAG     7.826.538 instrucciones por ventana
 *
 * Casi el triple. Y no es un detalle: es el numero mas gordo de todo el
 * estudio -el que decide si el identificador se come el 16 % o el 45 % de
 * cada segundo- asi que no vale quedarse con el que mas guste ni promediar.
 *
 * Este banco no mide piezas: mide LO UNICO que no admite interpretacion.
 * Le da UN SEGUNDO EXACTO de audio a 12 kHz, en bloques de 32 como hace
 * demod_am.c, llamando a stanag_det_paso() despues de cada bloque igual que
 * hace el bucle principal, y cuenta las instrucciones de la tanda entera.
 * De ahi sale el porcentaje del segundo sin tener que suponer cada cuanto
 * se completa una ventana ni cuantas llamadas vuelven por la puerta.
 *
 * Y de paso cuenta las ventanas que salen, que es el otro numero que hacia
 * falta: la cuenta dice que una ventana se cierra cada CADA x DIEZMA = 1024
 * muestras, o sea 11,7 por segundo. Si lo medido no da eso, es que la
 * cuenta estaba mal.
 *
 * Se mide con ruido Y con una FSK, porque la sospecha era justamente que el
 * detector trabaja mas cuando encuentra estructura.
 */
#include <stdint.h>
#include "semi.h"
#include "stanag_det.h"

int g_fallos;
void pila_di(unsigned long maximo);   /* vive en start.c */

/* El bit de accesos desalineados se quita, como en los demas bancos del M4:
 * el arranque de la radio no lo pone (no aparece ni en boot/ ni en User/),
 * o sea que ahi esas lecturas son legales y corren. Lo de dividir entre
 * cero y los manejadores con nombre se quedan. */
#define SCB_CCR (*(volatile uint32_t*)0xE000ED14UL)
static void desalineado_vale(void) { SCB_CCR &= ~(1UL << 3); }

#define FS        12000U            /* lo que le entra, muestras por segundo */
#define BLOQUE    32U               /* como se lo da demod_am.c */
#define FLOATS    STANAG_DET_FLOATS

static float s_trabajo[FLOATS];
static float s_audio[FS];

static uint32_t s_rnd = 1234567u;
static float ruido(void)
{
    s_rnd = s_rnd * 1664525u + 1013904223u;
    return (float)((int32_t)((s_rnd >> 9) & 0x7FFFF) - 262144) * (1.0f / 262144.0f);
}

/* Una FSK de 850 Hz de salto a 50 baudios, que es una firma que el detector
 * reconoce. Tabulada: un seno por muestra dentro de QEMU con icount cuesta
 * mas que lo que se quiere medir. */
static void llena_fsk(void)
{
    /* 12.000 / 50 = 240 muestras por simbolo. Las dos portadoras caen en
     * 1275 y 2125 Hz, o sea 12000/1275 y 12000/2125: no son enteras, asi
     * que se avanza la fase a mano y se lee de una tabla de 64 de seno. */
    static const float sen64[64] = {
        0.0000f, 0.0980f, 0.1951f, 0.2903f, 0.3827f, 0.4714f, 0.5556f, 0.6344f,
        0.7071f, 0.7730f, 0.8315f, 0.8819f, 0.9239f, 0.9569f, 0.9808f, 0.9952f,
        1.0000f, 0.9952f, 0.9808f, 0.9569f, 0.9239f, 0.8819f, 0.8315f, 0.7730f,
        0.7071f, 0.6344f, 0.5556f, 0.4714f, 0.3827f, 0.2903f, 0.1951f, 0.0980f,
        0.0000f,-0.0980f,-0.1951f,-0.2903f,-0.3827f,-0.4714f,-0.5556f,-0.6344f,
       -0.7071f,-0.7730f,-0.8315f,-0.8819f,-0.9239f,-0.9569f,-0.9808f,-0.9952f,
       -1.0000f,-0.9952f,-0.9808f,-0.9569f,-0.9239f,-0.8819f,-0.8315f,-0.7730f,
       -0.7071f,-0.6344f,-0.5556f,-0.4714f,-0.3827f,-0.2903f,-0.1951f,-0.0980f
    };
    uint32_t i, fase = 0u, bits = 0xB5A3C719u;
    for (i = 0U; i < FS; i++) {
        uint32_t sim = i / 240U;
        uint32_t uno = (bits >> (sim & 31U)) & 1U;
        /* paso de fase en 1/64 de vuelta por muestra, en coma fija de 16 */
        uint32_t paso = uno ? (uint32_t)((2125.0f / 12000.0f) * 64.0f * 65536.0f)
                            : (uint32_t)((1275.0f / 12000.0f) * 64.0f * 65536.0f);
        fase += paso;
        s_audio[i] = sen64[(fase >> 16) & 63U] * 0.5f + ruido() * 0.05f;
    }
}

/* La semilla se repone en cada llenado: si no, la segunda tanda de ruido
 * lleva OTRO ruido y los dos numeros no se pueden comparar. Eso fue lo
 * primero que salio mal aqui. */
static void llena_ruido(void)
{
    uint32_t i;
    s_rnd = 1234567u;
    for (i = 0U; i < FS; i++) { s_audio[i] = ruido() * 0.3f; }
}

/*
 * Un segundo exacto, bloque a bloque, con el paso detras de cada bloque.
 * Devuelve las instrucciones de la tanda entera y deja fuera cuantas
 * ventanas se cerraron.
 */
static unsigned long un_segundo(unsigned *ventanas_fuera)
{
    stanag_det_t r;
    unsigned long a, b;
    uint32_t i;
    unsigned v = 0U;

    a = tic();
    for (i = 0U; i + BLOQUE <= FS; i += BLOQUE) {
        stanag_det_mete(&s_audio[i], BLOQUE);
        if (stanag_det_paso(&r)) { v++; }
    }
    b = tic();
    *ventanas_fuera = v;
    return ((a - b) & 0xFFFFFFUL) * 40UL;
}

static void cuenta(const char *que, unsigned long ins, unsigned ventanas)
{
    pr("    "); pr(que); pr("\n");
    pr("      la tanda entera      "); prn(ins); pr(" instrucciones\n");
    pr("      ventanas cerradas    "); prn(ventanas); pr("\n");
    if (ventanas != 0U) {
        pr("      por ventana         "); prn(ins / ventanas); pr("\n");
    }
    /*
     * EL PORCENTAJE, SIN DESBORDAR. La primera version hacia
     * "ins * 100 / 200000000" y con 100 millones de instrucciones eso son
     * 10.000 millones, que no caben en 32 bits: salian porcentajes de un
     * digito para tandas que se comen media CPU. Se divide ANTES de
     * multiplicar: el segundo son 200.000.000 instrucciones, asi que las
     * decimas de porcentaje son ins/200.000 y punto.
     */
    {
        unsigned long d = ins / 200000UL;          /* decimas de % */
        pr("      % de cada segundo    ");
        prn(d / 10UL); pr(","); prn(d % 10UL); pr(" %\n");
        pr("      y en la radio (x1,7) ");
        prn((d * 17UL) / 100UL); pr(",");
        prn(((d * 17UL) / 10UL) % 10UL); pr(" %\n\n");
    }
}

int main(void)
{
    unsigned long ins;
    unsigned ven;

    desalineado_vale();
    reloj_on();

    pr("\nEL DETECTOR DE STANAG: UN SEGUNDO EXACTO DE AUDIO\n");
    pr("=================================================\n\n");
    pr("  12.000 muestras, en bloques de 32, con stanag_det_paso()\n");
    pr("  detras de cada bloque. La cuenta dice que una ventana se cierra\n");
    pr("  cada 1024 muestras, o sea 11 o 12 por segundo.\n\n");

    if (!stanag_det_init(s_trabajo, FLOATS)) {
        pr("  *** el detector no arranca: no mido nada ***\n");
        return 1;
    }

    /* Una tanda de calentamiento: la primera ventana no puede salir hasta
     * que el anillo se llena, y medir eso seria medir el arranque. */
    llena_ruido();
    (void)un_segundo(&ven);

    llena_ruido();
    ins = un_segundo(&ven);
    cuenta("SOLO RUIDO", ins, ven);

    llena_fsk();
    ins = un_segundo(&ven);
    cuenta("CON UNA FSK DE 850 Hz A 50 BAUDIOS", ins, ven);

    llena_ruido();
    ins = un_segundo(&ven);
    cuenta("RUIDO OTRA VEZ (que el numero se repita)", ins, ven);

    /*
     * Y CUATRO SEGUNDOS SEGUIDOS DE RUIDO.
     *
     * Hace falta porque el detector arrastra estado de una ventana a la
     * siguiente -el anillo, las estimaciones de antes- y las dos primeras
     * tandas salieron a 8,35 y 6,68 millones por ventana segun lo que
     * hubiera corrido justo antes. Lo que importa de verdad es donde se
     * queda cuando lleva un rato con la banda vacia, que es el estado en
     * el que esta la radio casi todo el tiempo.
     */
    pr("  cuatro segundos seguidos con la banda vacia:\n\n");
    {
        int k;
        for (k = 1; k <= 4; k++) {
            llena_ruido();
            ins = un_segundo(&ven);
            pr("    ruido, segundo ");
            prn((unsigned long)k);
            pr("            ");
            prn(ins);
            pr(" ins   ");
            prn(ins / 200000UL / 10UL); pr(","); prn((ins / 200000UL) % 10UL);
            pr(" %   ventanas "); prn(ven); pr("\n");
        }
        pr("\n");
    }

    pila_di(0);
    pr("ARBITRO: hecho\n");
    salir();
    return g_fallos ? 1 : 0;
}
