/*
 * EL CUADRO ENTERO, Y POR QUE LA CASCADA VA A LA MITAD DE LO QUE PONE.
 *
 * EL PORQUE DE ESTE BANCO. Los otros miden piezas: la traza, la cabecera,
 * un byte del I2C. Y con las piezas sueltas no sale la cuenta, porque el
 * cuadro NO se pinta en el tiempo que sobra: se pinta en lo que queda
 * DESPUES del audio, que tiene prioridad y nunca se queda sin servir -la
 * radio dice "0 vac", ni un solo vaciado del buffer de salida-.
 *
 * O sea:
 *
 *     periodo del cuadro = trabajo de dibujo / (1 - carga de audio)
 *
 * Y ese divisor es el multiplicador que faltaba. Con la carga de audio al
 * 54 %, UN milisegundo de dibujo de mas cuesta 2,2 ms de reloj de pared.
 * Por eso repintar la cabecera entera en cada detente -34 ms- se veia como
 * se veia, y por eso el reductor de ruido frena la cascada sin tocar ni un
 * pixel: no le quita tiempo al dibujo, le quita SITIO.
 *
 * Las piezas de aqui abajo salen MEDIDAS de los otros bancos del M4
 * (make m4) y de la propia radio. Esto no las vuelve a medir: las suma y
 * dice que cuadro sale. Sirve para contestar "¿y si quito esto?" sin
 * grabar un binario y sin pedir una foto.
 */
#include "semi.h"

int g_fallos;

/* Lo que se lleva el audio de cada segundo, en milesimas. Sale de la fila
 * "Reparto del audio" de la radio (rf/ex/au/ag) y del banco m4_audio. */
#define A_RF      210U   /* cabecera de RF: desentrelazar, mezclar, filtrar */
#define A_EX      110U   /* sacar la modulacion: AM/SSB/FM */
#define A_AU       30U   /* bloqueo de continua y filtro de audio */
#define A_AG       60U   /* el AGC y escribir al I2S */
#define A_NR       82U   /* el reductor de ruido, medido en m4_audio */

/* Y lo que cuesta dibujar un cuadro, en microsegundos DE LA RADIO. Los
 * bancos del M4 miden composicion; la radio tarda 1,7 veces eso (calibrado
 * contra la propia radio: 7.522 us de banco = 13 ms de traza medidos). */
#define R_FFT    2000U
/*
 * LA TRAZA, DESPUES DE LAS LONJAS - 02/11/2026 (V6.41).
 *
 * Eran 13.000: los 13 ms que medía la radio con 7.724 us de banco. El
 * banco da ahora 4.350 para el mismo caso -misma traza, mismos ajustes,
 * ni un pixel distinto-, o sea el 56,3 % de lo que daba. El mismo factor
 * contra la radio deja la traza en 7.320 us.
 *
 * Este numero es DERIVADO, no medido en la radio: el factor 1,7 se
 * calibro una vez y no se ha vuelto a comprobar con una radio delante.
 */
#define R_TRAZA  7320U
#define R_CASC   7000U
#define R_MARCO      0U

/* Y los repintados que se suman SOLO mientras el mando gira, por detente.
 * Banco de la cabecera, pasados a la radio con el mismo 1,7. */
#define D_DIGITO   1060U   /*   623 us de banco: la caja de un digito     */
#define D_PASTILLA 3715U   /* 2.185 us: la pastilla del mando (V6.39)     */
#define D_PAST_38 49106U   /* 28.886 us: la cabecera entera (V6.38)       */
#define D_CHIPS    7478U   /* 4.399 us: solo los chips (V6.39)            */

/* Y lo que NO es dibujo pero tambien se come la vuelta: resintonizar el
 * oscilador por el bus I2C, con la CPU parada mientras dura. 1.763 us de
 * banco por los mismos 1,7. Solo pasa SIN el NCO: con el NCO puesto el
 * oscilador se queda aparcado y no se toca el bus. */
#define D_I2C      2997U

static void fila(const char *que, unsigned long dib_us, unsigned long carga_mil)
{
    /* periodo = dibujo / (1 - carga). En enteros: dib*1000/(1000-carga). */
    unsigned long per, fps;

    if (carga_mil >= 1000UL) { carga_mil = 999UL; }
    per = (dib_us * 1000UL) / (1000UL - carga_mil);
    fps = (per != 0UL) ? (1000000UL / per) : 0UL;

    pr("    "); pr(que);
    pr("   carga "); prn(carga_mil / 10UL); pr(" %");
    pr("   dibujo "); prn(dib_us / 1000UL); pr(" ms");
    pr("   ->  cuadro "); prn(per / 1000UL); pr(" ms, ");
    prn(fps); pr(" por segundo\n");
}

int main(void)
{
    const unsigned long base  = R_FFT + R_TRAZA + R_CASC + R_MARCO;
    const unsigned long audio = A_RF + A_EX + A_AU + A_AG;
    /* Un detente cada 50 ms es un giro comodo: 20 por segundo. En un
     * cuadro de ~48 ms entra aproximadamente uno. */
    reloj_on();
    pr("\nEL CUADRO ENTERO: QUE SALE DE SUMARLO TODO\n\n");

    pr("  QUIETO\n");
    fila("sin reductor de ruido      ", base, audio);
    fila("con reductor de ruido      ", base, audio + A_NR);

    pr("\n  GIRANDO LA SINTONIA CON EL NCO (no se toca el bus I2C)\n");
    fila("1 detente por cuadro       ", base + 1UL * D_DIGITO, audio + A_NR);
    fila("2 detentes por cuadro      ", base + 2UL * D_DIGITO, audio + A_NR);
    fila("4 detentes por cuadro      ", base + 4UL * D_DIGITO, audio + A_NR);

    pr("\n  GIRANDO LA SINTONIA SIN NCO (cada detente resintoniza por I2C)\n");
    fila("1 detente por cuadro       ", base + 1UL * (D_DIGITO + D_I2C), audio + A_NR);
    fila("2 detentes por cuadro      ", base + 2UL * (D_DIGITO + D_I2C), audio + A_NR);
    fila("4 detentes por cuadro      ", base + 4UL * (D_DIGITO + D_I2C), audio + A_NR);

    pr("\n  GIRANDO EL MANDO, un ajuste anclado con Func\n");
    fila("V6.38, la cabecera entera  ", base + D_PAST_38, audio + A_NR);
    fila("V6.39, solo la pastilla    ", base + D_PASTILLA, audio + A_NR);

    pr("\n  Y SI SE BAJARA LA CARGA DE AUDIO\n");
    fila("10 puntos menos de audio   ", base, audio + A_NR - 100U);
    fila("20 puntos menos de audio   ", base, audio + A_NR - 200U);

    pr("\n  Y SI SE ABARATARA EL DIBUJO\n");
    fila("la traza a la mitad        ", base - (R_TRAZA / 2U), audio + A_NR);
    fila("la cascada a la mitad      ", base - (R_CASC / 2U), audio + A_NR);

    /*
     * Y LO QUE ESTE MODELO NO EXPLICA, DICHO AQUI Y NO EN UNA CONVERSACION.
     *
     * Las filas de arriba dicen que girar con el NCO puesto cuesta entre
     * un 10 % y un 25 % mas de cuadro. Lo que se ve en la radio es mas que
     * eso, varias versiones seguidas. O el modelo se deja algo, o hay un
     * tramo del bucle que ninguno de estos bancos mide.
     *
     * Por eso existe la orden "DG;" del CAT desde la V6.42: la vuelta a
     * vuelta de la radio de verdad, con sus quince tramos, que es lo unico
     * que puede decidirlo. Ver tools/cat_vuelta.ps1.
     *
     * Esto se queda escrito: un modelo que no cuadra y lo dice vale; uno
     * que no cuadra y se calla es como no tenerlo.
     */
    pr("\n  LO QUE ESTE MODELO NO EXPLICA\n");
    pr("    girar con el NCO deberia costar de un 10 a un 25 % mas de\n");
    pr("    cuadro, y en la radio se ve mas. Falta la vuelta a vuelta de\n");
    pr("    la radio: orden \"DG;\" del CAT, tools/cat_vuelta.ps1.\n");

    pr("\n");
    if (g_fallos) { pr("  HAY FALLOS\n\n"); } else { pr("  TODO CORRECTO\n\n"); }
    salir();
    return 0;
}
