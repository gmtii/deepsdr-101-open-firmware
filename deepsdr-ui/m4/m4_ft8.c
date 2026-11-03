/*
 * LO QUE TARDA DE VERDAD EN DECODIFICAR UNA RANURA DE FT8.
 *
 * POR QUE HACE FALTA EL NUMERO, y no una estimacion: mientras se
 * decodifica una ranura, la SIGUIENTE se esta capturando, y lo que se
 * captura va a parar al desvio -FT8_ADAPTER_LADO_BLOQUES bloques de 160
 * ms-. Lo que no quepa ahi se pierde, y lo cuenta
 * ft8_waterfall_desbordes().
 *
 * Ese desvio son hoy FT8_ADAPTER_LADO_BLOQUES bloques, y cuesta RAM: con
 * submuestreo en frecuencia cada bloque vale el doble. Antes de recortarlo
 * hay que saber cuanto se tarda EN LA CPU DE LA RADIO, no en el anfitrion
 * -donde la misma ranura se decodifica en 7 ms y no dice nada-.
 *
 * Esto corre el camino entero del firmware sobre una ranura fabricada
 * aqui mismo con el codificador de sim/ft8_gen.h: diezmador, cascada,
 * FFT, buscador de candidatos y Ft8Lib.
 *
 * ======================================================================
 * *** LO QUE ESTE NUMERO NO ES - 02/11/2026, noche. ***
 * ======================================================================
 *
 * Son INSTRUCCIONES divididas por 200 MHz, no milisegundos de reloj. QEMU
 * con -icount cuenta instrucciones y no modela ni la espera de la flash ni
 * el bus. En el chip de verdad, medido por el cable con tools/cat_vuelta.ps1,
 * UN candidato cuesta de 46 a 54 ms; aqui los veinte mensajes enteros salen
 * por 710.
 *
 * Y HAY UNA SEGUNDA TRAMPA, que es peor: esta ranura lleva VEINTE mensajes
 * y la banda de verdad da sesenta y dos candidatos a las nueve de la noche
 * en 40 m. El coste va con los candidatos, no con los mensajes.
 *
 * Asi que esta cifra sirve para COMPARAR versiones -es repetible al
 * instante- y no para decidir si la decodificacion cabe en el desvio. Para
 * eso esta el tope de reloj de ft8_modo_poll(), que no necesita saberlo:
 * mira los bloques que lleva gastados el desvio y corta.
 */
#include "semi.h"
void pila_di(unsigned long maximo);

/*
 * El contador del gancho: cuantas veces respira la busqueda de
 * candidatos. Con eso y lo que tarda entera sale el hueco entre respiros,
 * que es lo que de verdad decide si al remuestreador le da tiempo.
 *
 * SE MIDE LA MEDIA Y NO EL PEOR, y aqui si vale: el gancho va al final de
 * cada desplazamiento de tiempo y todos hacen EXACTAMENTE el mismo
 * recorrido -el bucle de freq_offset no depende del desplazamiento-, asi
 * que los huecos son todos iguales salvo ruido de medida.
 */
static unsigned long g_resp_n;
static void respira_cuenta(void) { g_resp_n++; }
#define SCB_CCR    (*(volatile uint32_t*)0xE000ED14UL)

#include <string.h>
#include <stdint.h>
#include "ft8_decimator.h"
#include "ft8_waterfall_adapter.h"
#include "ft8_fft.h"
#include "ft8_decoder.h"
#include "ft8_shared_ram.h"
#include "ft8/constants.h"
#include "ft8/crc.h"
#include "ft8/message.h"
#include "rtc_hw.h"

int g_fallos;

/* Aqui no hay NVIC: el diezmador cierra la interrupcion para cambiar de
 * buffer y en un banco de un solo hilo eso no hace falta. */
void __disable_irq(void) { }
void __enable_irq(void)  { }

/* Tapones: aqui no hay reloj ni base de paises. */
void rtc_hw_get(rtc_hw_datetime_t *o) { if (o) { memset(o, 0, sizeof *o); } }
const char *dxcc_pais(const char *ind) { (void)ind; return 0; }

ft8_shared_ram_t g_ft8_shared_ram;

#include "ft8_gen.h"

/* Veinte emisiones repartidas por la banda, y VEINTE DISTINTAS: el
 * decodificador junta las repetidas por el CRC, asi que poner seis
 * mensajes cinco veces cada uno mide otra cosa. El tiempo sale del numero
 * de CANDIDATOS, y una banda llena de verdad saca muchos mas que una con
 * cuatro gatos. */
#define N_EMI  20

static const char *const k_txt[N_EMI] = {
    "CQ EA4XYZ IN80", "CQ DL1ABC JO31", "CQ F5TGY JN38",
    "CQ I2ABC JN45",  "CQ G0XYZ IO91",  "CQ SM5ABC JO89",
    "CQ OH2BBB KP20", "CQ LA1ABC JP50", "CQ PA3XYZ JO22",
    "CQ ON4ABC JO21", "CQ OK1XYZ JO70", "CQ S51ABC JN76",
    "CQ YO3XYZ KN34", "CQ LZ1ABC KN12", "CQ SV1XYZ KM18",
    "CQ 9A1ABC JN85", "CQ HA5XYZ JN97", "CQ SP9ABC JO90",
    "CQ UR5XYZ KN66", "CQ EW1ABC KO33"
};

int main(void)
{
    unsigned long a, b, v;
    int i, n;
    uint8_t tonos[FT8_NN];

    /*
     * SIN LA TRAMPA DE ACCESOS DESALINEADOS, Y HAY QUE DECIR POR QUE.
     *
     * start.c pone UNALIGN_TRP para cazar nuestros fallos, y aqui salta
     * en el memcpy de newlib que usa ftx_message_encode(): la rutina de
     * la biblioteca hace lecturas de palabra sin alinear A PROPOSITO,
     * porque el Cortex-M4 las permite. Es legal y no es un fallo nuestro.
     *
     * La radio NO pone ese bit -lo pone este banco-, asi que apagarlo
     * aqui no tapa nada que pase en la radio. Los demas bancos del M4 lo
     * siguen teniendo puesto.
     */
    SCB_CCR &= ~(1UL << 3);

    reloj_on();
    pr("\nDECODIFICAR UNA RANURA DE FT8, EN UN CORTEX-M4 DE VERDAD\n\n");

    /* Una banda con seis emisiones repartidas, que es lo que de verdad
     * cuesta: un candidato por emision mas los que salen del ruido. */
    ranura_vacia(4242u);
    for (i = 0; i < N_EMI; i++) {
        (void)ft8_tonos(k_txt[i], tonos);
        mete_emision(300.0f + (float)i * 115.0f, 0.5f,
                     amplitud_para(-8.0f - (float)(i % 5)), tonos);
    }

    ft8_fft_init();
    ft8_decimator_reset();
    ft8_waterfall_reset();
    ft8_decoder_init();

    a = tic();
    for (n = 0; n < RANURA_N && !ft8_waterfall_is_full(); n += 256) {
        int16_t trozo[256];
        int j;
        for (j = 0; j < 256 && (n + j) < RANURA_N; j++) {
            float x = g_audio[n + j];
            if (x >  32767.0f) { x =  32767.0f; }
            if (x < -32768.0f) { x = -32768.0f; }
            trozo[j] = (int16_t)x;
        }
        ft8_decimator_feed_block(trozo, (uint16_t)j);
        ft8_decimator_poll();
    }
    b = tic();
    v = mide("capturar la ranura (15 s de audio)", a, b, 1);
    pr("      o sea ");
    prn((v / 200UL) / 1000UL);
    pr(" ms repartidos en los 15 s: no estorba\n");

    /*
     * LA BUSQUEDA DE CANDIDATOS, APARTE - 02/11/2026, noche.
     *
     * ft8_decoder_slot_empieza() es el unico trozo del camino que NO se
     * puede partir: corre entero, de una sentada, y mientras corre nadie
     * vacia el remuestreador -que aguanta 320 ms-. El tope de reloj de
     * ft8_modo_poll() no la cubre, porque el tope mira ENTRE candidatos.
     *
     * Y acaba de doblar de tamano: con FREQ_OSR=2 el buscador recorre el
     * doble de bins. O sea que es justo el sitio donde hay que mirar
     * antes de decir que la captura esta a salvo.
     */
    /* El gancho de verdad, contando. Ver ftx_find_candidates_respira(). */
    ft8_decoder_set_respira(respira_cuenta);
    g_resp_n = 0UL;

    a = tic();
    ft8_decoder_slot_empieza();
    b = tic();
    v = mide("  buscar los candidatos            ", a, b, 1);
    {
        unsigned long ms  = (v / 200UL) / 1000UL;
        unsigned long hue = (g_resp_n > 0UL) ? (ms / g_resp_n) : ms;

        pr("      "); prn(ms); pr(" ms de un tiron si nadie respira\n");
        pr("      "); prn(g_resp_n); pr(" respiros, o sea un hueco de ");
        prn(hue); pr(" ms contra los 320 de colchon\n");
        /*
         * EL VEREDICTO NO ES "QUE TARDE POCO" SINO "QUE NO DEJE AL
         * REMUESTREADOR SIN VACIAR". Son dos cosas distintas y hasta el
         * 02/11/2026 aqui se miraba la primera, que es la que no se puede
         * cumplir: buscar candidatos cuesta lo que cuesta.
         */
        if (g_resp_n == 0UL) {
            pr("      *** NADIE RESPIRA: el gancho no se llama ***\n");
            g_fallos++;
        } else if (hue * 2UL > 320UL) {
            pr("      *** UN HUECO SE PASA DE MEDIO COLCHON ***\n");
            g_fallos++;
        }
    }

    {
        unsigned long busca = v;
        unsigned long n = 0UL;

        a = tic();
        while (ft8_decoder_slot_paso()) { n++; }
        b = tic();
        v = mide("  los candidatos, uno a uno        ", a, b, 1);
        if (n > 0UL) {
            pr("      "); prn(n); pr(" candidatos, ");
            prn(((v / 200UL) / 1000UL) / n);
            pr(" ms cada uno\n");
        }
        v += busca;
        pr("    DECODIFICAR la ranura, las dos cosas\n");
    }
    {
        unsigned long ms = (v / 200UL) / 1000UL;
        unsigned long bloques = (ms + 159UL) / 160UL;

        pr("      son "); prn(ms); pr(" ms, o sea ");
        prn(bloques); pr(" bloques de 160 ms\n");
        pr("      el desvio aguanta "); prn((unsigned long)FT8_ADAPTER_LADO_BLOQUES);
        pr(" bloques\n");
        if (bloques * 2UL > (unsigned long)FT8_ADAPTER_LADO_BLOQUES) {
            pr("      *** MENOS DEL DOBLE DE MARGEN ***\n");
            g_fallos++;
        }
    }

    {
        ft8_decoded_msg_t m;
        int cuantos = 0;
        while (ft8_decoder_get_message(&m)) { cuantos++; }
        pr("\n    mensajes decodificados "); prn((unsigned long)cuantos);
        pr(" de "); prn((unsigned long)N_EMI); pr(" puestos\n");
        if (cuantos < (N_EMI * 3) / 4) {
            pr("    *** DECODIFICA MENOS DE TRES CUARTAS PARTES ***\n");
            g_fallos++;
        }
    }

    pr("\n");
    pila_di(8192UL);
    pr("\n");
    if (g_fallos) { pr("  HAY FALLOS\n\n"); } else { pr("  TODO CORRECTO\n\n"); }
    salir();
    return 0;
}
