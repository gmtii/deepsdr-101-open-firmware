/*
 * EL DEMODULADOR DE LA RADIO CONTRA UNA GRABACION DE VERDAD.
 *
 * Este banco no prueba un algoritmo: prueba EL CODIGO QUE VA EN LA
 * RADIO, User/stanag_rx.c, compilado tal cual. Y lo que le exige no es
 * una metrica interna sino una frase:
 *
 *     "This is a Stanag4481 test message"
 *
 * que es lo que lleva dentro sim/muestras/stanag4481_psk.mp3, un STANAG
 * 4285 a 300 bps con entrelazado largo grabado del aire. Si de ahi no
 * sale esa frase, el banco esta rojo, y da igual lo bonitos que sean los
 * numeros intermedios.
 *
 * Se eligio ese texto como prueba y no un porcentaje porque un Viterbi
 * SIEMPRE devuelve algo: con ruido devuelve bits con buena cara. Lo
 * unico que de verdad distingue "ha enganchado" de "se lo ha inventado"
 * es que salga ingles.
 */
#include "stanag_rx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fallos = 0;
static void di(const char *que, int ok)
{
    printf("  %-56s %s\n", que, ok ? "ok" : "MAL");
    if (!ok) { fallos++; }
}

static uint32_t le32(const uint8_t *p)
{ return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint16_t le16(const uint8_t *p)
{ return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8)); }

static float *lee_wav(const char *ruta, uint32_t *n_out, uint32_t *hz_out, uint16_t *can_out)
{
    FILE *f = fopen(ruta, "rb");
    uint8_t cab[12];
    uint16_t canales = 1U, bits = 16U;
    uint32_t hz = 0UL;
    float *x = 0;

    if (!f) { printf("  no puedo abrir %s\n", ruta); return 0; }
    if (fread(cab, 1, 12, f) != 12 || memcmp(cab, "RIFF", 4) != 0) { fclose(f); return 0; }
    for (;;) {
        uint8_t ch[8]; uint32_t tam;
        if (fread(ch, 1, 8, f) != 8) { break; }
        tam = le32(&ch[4]);
        if (memcmp(ch, "fmt ", 4) == 0) {
            uint8_t fmt[16];
            if (tam < 16U || fread(fmt, 1, 16, f) != 16) { break; }
            canales = le16(&fmt[2]); hz = le32(&fmt[4]); bits = le16(&fmt[14]);
            if (tam > 16U) { fseek(f, (long)(tam - 16U), SEEK_CUR); }
        } else if (memcmp(ch, "data", 4) == 0) {
            uint32_t n = tam / (bits / 8U), i;
            x = (float *)malloc(sizeof(float) * (size_t)n);
            if (!x) { fclose(f); return 0; }
            for (i = 0U; i < n; i++) {
                int16_t s;
                if (fread(&s, 2, 1, f) != 1) { n = i; break; }
                x[i] = (float)s / 32768.0f;
            }
            *n_out = n / canales; *hz_out = hz; *can_out = canales;
            fclose(f); return x;
        } else {
            fseek(f, (long)tam + (long)(tam & 1U), SEEK_CUR);
        }
    }
    fclose(f); return 0;
}

int main(int argc, char **argv)
{
    static float trabajo[STANAG_RX_FLOATS];
    const char *ruta = (argc > 1) ? argv[1] : 0;
    const char *busca = (argc > 2) ? argv[2] : "This is a Stanag4481 test message";
    float *x = 0, *y = 0;
    uint32_t n = 0UL, hz = 0UL, m, i;
    uint16_t canales = 1U;
    stanag_rx_est_t e;
    char todo[STANAG_RX_LINEAS * STANAG_RX_LARGO + 8];

    if (!ruta) { printf("uso: stanagrx <fichero.wav> [frase]\n"); return 1; }
    printf("\n=== El demodulador de la radio contra una grabacion real ===\n\n");

    x = lee_wav(ruta, &n, &hz, &canales);
    if (!x) { printf("  no hay WAV\n"); return 1; }
    printf("  %s: %u marcos, %u Hz, %u canal(es) = %.1f s\n",
           ruta, (unsigned)n, (unsigned)hz, (unsigned)canales, (double)n / (double)hz);

    /* a mono y a 12 kHz, que es lo que le llega en la radio */
    m = (uint32_t)(((double)n * 12000.0) / (double)hz);
    y = (float *)malloc(sizeof(float) * (size_t)m);
    if (!y) { return 1; }
    for (i = 0U; i < m; i++) {
        double t = ((double)i * (double)hz) / 12000.0;
        uint32_t k = (uint32_t)t;
        double fr = t - (double)k;
        if ((k + 1U) >= n) { k = n - 2U; fr = 1.0; }
        y[i] = (float)((1.0 - fr) * (double)x[k * canales] + fr * (double)x[(k + 1U) * canales]);
    }

    stanag_rx_largo_pon(1U);
    stanag_rx_velocidad_pon(STANAG_RX_300);
    di("coge su memoria prestada", stanag_rx_init(trabajo, STANAG_RX_FLOATS) == 1U);

    for (i = 0U; i + 64U <= m; i += 64U) {
        stanag_rx_mete(&y[i], 64U);
        (void)stanag_rx_paso();
    }

    stanag_rx_estado(&e);
    printf("\n  engancha %u   tramas %u   sondeos %u%%   desvio %+.1f Hz"
           "   grupos %u   BER %u%%\n",
           (unsigned)e.engancha, (unsigned)e.tramas, (unsigned)e.sondeos,
           (double)e.desvio_dhz / 10.0, (unsigned)e.grupos, (unsigned)e.ber);

    todo[0] = '\0';
    printf("\n  --- lo que ha sacado ---\n");
    for (i = 0U; i < stanag_rx_nlineas(); i++) {
        printf("    %s\n", stanag_rx_linea((uint8_t)i));
        strcat(todo, stanag_rx_linea((uint8_t)i));
    }

    printf("\n");
    printf("      trama %u, preambulo %u sobre fondo %u\n",
           (unsigned)e.trama, (unsigned)e.corr_max, (unsigned)e.fondo);
    di("la trama de 106,67 ms se ve clarisima", e.trama >= 70U);
    di("sincroniza con el preambulo", e.engancha == 1U);
    di("ve las tramas que tiene la grabacion", e.tramas > 200U);
    di("los simbolos de sondeo caen en su sitio", e.sondeos >= 40U);
    di("el entrelazador largo llega a soltar datos", e.grupos > 400U);
    {
        char que[96];
        snprintf(que, sizeof que, "y sale el texto: \"%s\"", busca);
        di(que, strstr(todo, busca) != 0);
    }

    /*
     * EL BOTON BORRAR.
     *
     * Pedido por el usuario, 02/10/2026.
     *
     * Borraba los renglones del identificador, que es lo que el panel
     * pinta cuando NO demodula. Demodulando pinta los de stanag_rx y
     * esos no los tocaba nadie. Aqui se comprueba lo que tiene que
     * hacer y, igual de importante, lo que NO tiene que hacer: el
     * enganche y el relleno del desentrelazador se quedan, porque si
     * no, borrar la pantalla cuesta otros catorce segundos.
     */
    {
        stanag_rx_est_t eb;
        stanag_rx_borra();
        stanag_rx_estado(&eb);
        di("borrar deja la pantalla vacia", stanag_rx_nlineas() == 0U);
        di("y el renglon de verdad vacio", stanag_rx_linea(0U)[0] == '\0');
        di("pero no pierde el enganche", eb.engancha == e.engancha);
        di("ni tira el relleno del entrelazador", eb.grupos >= e.grupos);
        di("ni se olvida del desvio de portadora", eb.desvio_dhz == e.desvio_dhz);
    }

    /*
     * CON RUIDO NO PUEDE SALIR NI UNA LETRA.
     *
     * Pedido por el usuario, 02/10/2026.
     *
     * Es el peor fallo posible en un decodificador y el mas facil de no
     * ver: que dos señales distintas den la misma salida quiere decir
     * que la salida no viene de la señal. Venia del relleno del
     * entrelazador -ceros, que para el Viterbi son "no se", y con "no
     * se" elige igualmente un camino, siempre el mismo-.
     *
     * Este banco no comprueba que decodifique: comprueba que CALLE. Se
     * le mete ruido y se exige que no suelte un solo caracter.
     */
    {
        /*
         * Una RAFAGA CORTA de la señal buena: cuatro segundos, de sobra
         * para enganchar el preambulo y ni de lejos los diez que el
         * entrelazado largo necesita para soltar el primer dato. Es
         * exactamente el caso del usuario en 4277 y 4338 -una costera que
         * emite a rafagas- y lo que tiene que salir es NADA.
         *
         * Con ruido puro esto no valdria de prueba: como no engancha, no
         * procesa tramas y callaria de todas formas.
         */
        uint32_t tope = 12000U * 4U;
        stanag_rx_velocidad_pon(STANAG_RX_300);
        stanag_rx_largo_pon(1U);
        if (tope > m) { tope = m; }
        if (stanag_rx_init(trabajo, STANAG_RX_FLOATS)) {
            stanag_rx_est_t ec;
            for (i = 0U; i + 64U <= tope; i += 64U) {
                stanag_rx_mete(&y[i], 64U);
                (void)stanag_rx_paso();
            }
            stanag_rx_estado(&ec);
            printf("      rafaga de 4 s: engancha %u, %u/%u grupos -> %u renglones\n",
                   (unsigned)ec.engancha, (unsigned)ec.grupos_hechos,
                   (unsigned)ec.grupos_hacen, (unsigned)stanag_rx_nlineas());
            di("engancha con la rafaga", ec.engancha == 1U);
            di("pero no suelta un solo caracter hasta llenar",
               stanag_rx_nlineas() == 0U);
        }
    }

    /*
     * CON EL UMBRAL BAJO, EL RUIDO TIENE QUE SEGUIR MUDO.
     *
     * El umbral de enganche bajo a 0,30 porque una señal real de S8 con
     * interferencia al lado no llegaba a 0,45. El precio es que el ruido
     * ya engancha de vez en cuando -mide 26 de 45 por su cuenta- y la
     * red se ha movido a los simbolos de SONDEO: 48 por trama de valor
     * conocido, y la trama que no los clava no cuenta para el relleno.
     *
     * Esto comprueba que esa red aguanta: un minuto de ruido, que da
     * unos cuantos enganches falsos, y ni un renglon ni un grupo de
     * relleno.
     */
    {
        uint32_t sem = 999U, j5;
        stanag_rx_velocidad_pon(STANAG_RX_600);
        stanag_rx_largo_pon(1U);
        if (stanag_rx_init(trabajo, STANAG_RX_FLOATS)) {
            for (i = 0U; i < 12000U * 60U; i += 64U) {
                float tr2[64];
                for (j5 = 0U; j5 < 64U; j5++) {
                    sem = (sem * 1103515245U) + 12345U;
                    tr2[j5] = (float)((int32_t)(sem >> 16) - 32768) / 32768.0f;
                }
                stanag_rx_mete(tr2, 64U);
                (void)stanag_rx_paso();
            }
            stanag_rx_estado(&e);
            printf("      60 s de ruido: %u enganches falsos, correlacion %u,"
                   " relleno %u\n",
                   (unsigned)e.tramas, (unsigned)e.corr,
                   (unsigned)e.grupos_hechos);
            di("el ruido no pasa de los sondeos: cero relleno",
               e.grupos_hechos == 0U);
            di("y ni un renglon", stanag_rx_nlineas() == 0U);
            /*
             * Y LA MEDIDA DE TRAMA TIENE QUE SABER DISTINGUIRLO.
             *
             * Es la autocorrelacion a un retardo de trama exacta, y es
             * lo que contesta "¿hay un 4285 en esta frecuencia?" sin
             * cambiar al identificador. Medido: 97 con la grabacion
             * buena, 64 con la del movil del usuario, 12 con ruido.
             */
            printf("      y la trama mide %u con ruido\n", (unsigned)e.trama);
            di("la trama no se confunde con el ruido", e.trama < 25U);
        }
    }

    /*
     * EL PEOR CASO NO PUEDE COSTAR LO QUE QUIERA.
     *
     * Pedido por el usuario, 02/10/2026. Buscar la portadora es lo unico caro de este modulo, y
     * se hacia en CADA trama mientras no enganchara: 26 segundos de CPU
     * para 20 de ruido, medido en el PC. En la radio eso va en el bucle
     * principal y se nota en todo lo demas.
     *
     * No hacia falta: el preambulo se repite cada trama, asi que barrer
     * una trama entera de posiciones una vez por segundo tiene la misma
     * punteria que hacerlo nueve veces. Esto lo vigila contando las
     * busquedas, que es una medida exacta y no depende de lo rapido que
     * sea el ordenador que corra el banco.
     */
    {
        uint32_t seg = m / 12000U;
        stanag_rx_velocidad_pon(STANAG_RX_600);
        stanag_rx_largo_pon(1U);
        if (stanag_rx_init(trabajo, STANAG_RX_FLOATS)) {
            /* audio dado la vuelta: tiene trama pero no engancha, asi que
             * busca sin parar - que es el caso que se quiere acotar */
            for (i = 0U; i + 64U <= m; i += 64U) {
                float trozo[64];
                uint32_t j3;
                for (j3 = 0U; j3 < 64U; j3++) {
                    trozo[j3] = y[m - 1U - (i + j3)] * 0.02f;
                }
                stanag_rx_mete(trozo, 64U);
                (void)stanag_rx_paso();
            }
            stanag_rx_estado(&e);
            printf("      %u s de audio sin enganchar -> %u busquedas\n",
                   (unsigned)seg, (unsigned)e.busquedas);
            di("no barre la portadora mas de dos veces por segundo",
               e.busquedas <= (uint16_t)((seg * 2U) + 4U));
        }
    }

    /*
     * QUE EL INDICADOR DE LA CHAPA SIRVA PARA ELEGIR LOS AJUSTES.
     *
     * Al usuario se le dice "mira la e: si esta en 0-5 lo que sale vale, si
     * esta en 13 cambia la velocidad". Eso solo es cierto si el numero
     * DISTINGUE, asi que aqui se comprueba: de las doce combinaciones, la
     * buena -300 bps con entrelazado largo, que es lo que lleva esta
     * grabacion- tiene que dar el error MAS BAJO, y con margen.
     *
     * Y hay una trampa que ya me comio dos veces: la palabra todo ceros
     * es valida en cualquier codigo convolucional, asi que una emision en
     * reposo da 0 % sin haber enganchado nada. Por eso stanag_rx marca
     * cuando el numero no mide, y aqui se exige que la buena SI mida.
     */
    {
        uint8_t v, l, mejor_v = 255U, mejor_l = 255U;
        uint32_t mejor = 1000U, segundo = 1000U;
        for (l = 0U; l < 2U; l++) {
            for (v = 0U; v < STANAG_RX_VELOCIDADES; v++) {
                uint32_t b2;
                stanag_rx_velocidad_pon(v); stanag_rx_largo_pon(l);
                if (!stanag_rx_init(trabajo, STANAG_RX_FLOATS)) { continue; }
                for (i = 0U; i + 64U <= m; i += 64U) {
                    stanag_rx_mete(&y[i], 64U);
                    (void)stanag_rx_paso();
                }
                stanag_rx_estado(&e);
                b2 = e.ber_vale ? (uint32_t)e.ber : 999UL;
                if (b2 < mejor) { segundo = mejor; mejor = b2; mejor_v = v; mejor_l = l; }
                else if (b2 < segundo) { segundo = b2; }
            }
        }
        printf("      el mas bajo: %s bps %s con %u%%   el siguiente: %u%%\n",
               (mejor_v < STANAG_RX_VELOCIDADES) ? stanag_rx_velocidad_txt(mejor_v) : "?",
               (mejor_l == 1U) ? "largo" : "corto", (unsigned)mejor, (unsigned)segundo);
        di("el error mas bajo es el de los ajustes buenos",
           mejor_v == (uint8_t)STANAG_RX_300 && mejor_l == 1U);
        di("y le saca margen al siguiente", (mejor + 5UL) < segundo);
    }

    /*
     * EL MISMO AUDIO A TRES ESCALAS, Y LAS TRES TIENEN QUE DAR EL TEXTO.
     *
     * Pedido por el usuario, 02/10/2026.
     *
     * Era que el audio de banda lateral de la radio no viene normalizado
     * a la unidad y yo lo metia en un anillo de enteros con escala fija:
     * saturaba. Este banco pasaba en verde porque el WAV se lee dividido
     * por 32768 y queda justo en el rango bueno - o sea que probaba UNA
     * escala, la afortunada.
     *
     * Ahora se prueban tres que se diferencian en siete ordenes de
     * magnitud. Si alguien vuelve a meter una escala fija, esto salta.
     */
    {
        static const float k_esc[3] = { 30000.0f, 1.0f, 0.001f };
        uint32_t e2;
        int bien = 1;
        for (e2 = 0U; e2 < 3U; e2++) {
            char dentro[STANAG_RX_LINEAS * STANAG_RX_LARGO + 8];
            uint32_t j2;
            stanag_rx_largo_pon(1U);
            stanag_rx_velocidad_pon(STANAG_RX_300);
            if (!stanag_rx_init(trabajo, STANAG_RX_FLOATS)) { bien = 0; continue; }
            for (i = 0U; i + 64U <= m; i += 64U) {
                float trozo[64];
                for (j2 = 0U; j2 < 64U; j2++) { trozo[j2] = y[i + j2] * k_esc[e2]; }
                stanag_rx_mete(trozo, 64U);
                (void)stanag_rx_paso();
            }
            dentro[0] = '\0';
            for (j2 = 0U; j2 < stanag_rx_nlineas(); j2++) {
                strcat(dentro, stanag_rx_linea((uint8_t)j2));
            }
            stanag_rx_estado(&e);
            printf("      x%-9g sondeos %3u%%  BER %2u%%  %s\n",
                   (double)k_esc[e2], (unsigned)e.sondeos, (unsigned)e.ber,
                   (strstr(dentro, busca) != 0) ? "y sale el texto" : "SIN TEXTO");
            if (strstr(dentro, busca) == 0) { bien = 0; }
        }
        di("decodifica con el audio a cualquier escala", bien);
    }

    /*
     * Y lo que NO se puede probar con esta grabacion pero si se puede
     * comprobar que esta bien dicho: que las seis velocidades y los tres
     * formatos tienen rotulo, y que el unico reparto de memoria que no
     * cabe -2400 bps con entrelazado largo- se niega a arrancar diciendo
     * por que, en vez de arrancar y sacar basura.
     */
    {
        int bien = 1;
        uint8_t v, f;
        for (v = 0U; v < STANAG_RX_VELOCIDADES; v++) {
            if (stanag_rx_velocidad_txt(v)[0] == '\0'
                || stanag_rx_velocidad_txt(v)[0] == '?') { bien = 0; }
        }
        di("las seis velocidades tienen rotulo", bien);
        bien = 1;
        for (f = 0U; f < STANAG_RX_FORMATOS; f++) {
            if (stanag_rx_formato_txt(f)[0] == '\0'
                || stanag_rx_formato_txt(f)[0] == '?') { bien = 0; }
        }
        di("los tres formatos tienen rotulo", bien);
    }
    /*
     * EL REPARTO DE MEMORIA, EN LAS DOCE COMBINACIONES.
     *
     * Pedido por el usuario, 02/10/2026.
     *
     * Era que s_salida mide 41 bytes y dejaba las metricas y las
     * decisiones del Viterbi a un byte de su sitio. En el PC eso
     * funciona y este banco pasaba en verde; en el Cortex-M4 es
     * HardFault. Ahora stanag_rx_init() comprueba la alineacion y se
     * niega a arrancar si esta torcida, asi que basta con pedirle que
     * arranque en todas las velocidades para que no vuelva a colarse.
     */
    {
        uint8_t v, l;
        int bien = 1;
        for (l = 0U; l < 2U; l++) {
            for (v = 0U; v < STANAG_RX_VELOCIDADES; v++) {
                uint8_t espera;
                stanag_rx_velocidad_pon(v);
                stanag_rx_largo_pon(l);
                /* el unico que NO tiene que caber es 2400 con largo */
                espera = (uint8_t)((l == 1U && v == STANAG_RX_2400) ? 0U : 1U);
                if (stanag_rx_init(trabajo, STANAG_RX_FLOATS) != espera) {
                    printf("      %s bps %s: esperaba %s\n",
                           stanag_rx_velocidad_txt(v), l ? "largo" : "corto",
                           espera ? "que arrancara" : "que se negara");
                    bien = 0;
                }
            }
        }
        di("el reparto cuadra en las doce combinaciones", bien);
        /*
         * Y LA COLA DE AUDIO TIENE QUE DAR MARGEN DE SOBRA.
         *
         * Pedido por el usuario, 02/10/2026. Con 85 ms de cola, el propio barrido de
         * portadora bloqueaba el bucle mas de lo que la cola aguantaba:
         * el demodulador se comia su propia entrada. Ahora la cola se
         * queda con TODO lo que sobre del reparto, y eso depende de la
         * velocidad -el desentrelazador de 1200 pide 6 kB mas que el de
         * 600-. Se exige un cuarto de segundo en el caso peor.
         */
        {
            uint8_t v2, l2;
            uint16_t peor = 60000U;
            for (l2 = 0U; l2 < 2U; l2++) {
                for (v2 = 0U; v2 < STANAG_RX_VELOCIDADES; v2++) {
                    stanag_rx_velocidad_pon(v2); stanag_rx_largo_pon(l2);
                    if (!stanag_rx_init(trabajo, STANAG_RX_FLOATS)) { continue; }
                    stanag_rx_estado(&e);
                    if (e.cola_ms < peor) { peor = e.cola_ms; }
                }
            }
            printf("      la cola mas corta de las once que arrancan: %u ms\n",
                   (unsigned)peor);
            di("la cola de audio da al menos un cuarto de segundo", peor >= 250U);
        }
    }
    {
        uint8_t ok;
        stanag_rx_velocidad_pon(STANAG_RX_2400);
        stanag_rx_largo_pon(1U);
        ok = stanag_rx_init(trabajo, STANAG_RX_FLOATS);
        di("2400 bps con entrelazado largo NO arranca", ok == 0U);
        printf("      y dice: %s\n", stanag_rx_porque());
        di("y lo explica en vez de callarse", stanag_rx_porque()[0] != '\0');

        stanag_rx_largo_pon(0U);
        ok = stanag_rx_init(trabajo, STANAG_RX_FLOATS);
        di("pero con entrelazado corto si", ok == 1U);

        stanag_rx_velocidad_pon(STANAG_RX_1200);
        stanag_rx_largo_pon(1U);
        ok = stanag_rx_init(trabajo, STANAG_RX_FLOATS);
        di("y 1200 bps con largo tambien, que es el que casi no cabia", ok == 1U);
    }

    free(x); free(y);
    printf("\n  %s\n\n", fallos ? "HAY FALLOS" : "TODO CORRECTO");
    return fallos ? 1 : 0;
}
