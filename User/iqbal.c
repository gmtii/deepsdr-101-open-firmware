/* Ver iqbal.h para el porque. Aqui solo esta el como. */
#include "iqbal.h"
/*
 * Para que la prueba A/B apague y encienda LOS DOS correctores. Si solo
 * tocara el plano, la prueba compararia "sin plano pero con el de banda
 * ancha" contra "con los dos", que no es la pregunta que se esta haciendo y
 * ademas daria una diferencia pequeña siempre.
 */
/*
 * ESO ERA VERDAD HASTA LA V3.95 - revision a fondo del 08/10/2026.
 *
 * Desde que el corrector de banda ancha salio de la interrupcion del ADC
 * -costaba nueve veces y media lo que el plano y la radio perdia
 * pulsaciones; ver el comentario largo de sdr_rx.c, el del 08/10/2026-,
 * iqa_bloque() NO LO LLAMA NADIE. El codigo de iqancho.c sigue compilado y
 * su banco sigue pasando, pero ninguna muestra pasa ya por el.
 *
 * O sea que los iqa_set_on() de aqui abajo no cambian nada en la señal: lo
 * que encienden y apagan es un corrector que no esta en el camino. Se dejan
 * puestos A PROPOSITO, no por descuido: el dia que el ancho vuelva al camino
 * -a mano, o cuando haya sitio- la prueba A/B tiene que seguir comparando
 * los dos correctores contra ninguno, y descubrir entonces que la prueba se
 * habia quedado midiendo solo medio sistema seria una averia mucho mas cara
 * de encontrar que estas cuatro llamadas que hoy no hacen nada.
 *
 * Lo que hoy mide la prueba A/B, por tanto, es el corrector plano y solo el.
 * Que es ademas el que arregla el espejo de verdad.
 */
#include "iqancho.h"
#include <math.h>

/*
 * Constante de tiempo del promedio. A 96 kHz con bloques de 256 muestras son
 * 375 bloques por segundo, pero SOLO SE MIDE UNO DE CADA OCHO, asi que con
 * 1/512 la constante son 512 * 8 * 2,667 ms = **10,9 segundos** (21,8 a 48
 * kHz). Aqui ponia "algo mas de un segundo", que es lo que saldria midiendo
 * todos los bloques; el diezmado por ocho lo multiplica por ocho y eso es
 * justo lo que tarda en reabsorberse lo aprendido en otra banda. Corregido
 * el 27/10/2026; doscientas lineas mas abajo ya estaba bien.
 *
 * Lenta a proposito. El desequilibrio del hardware no cambia con el tiempo,
 * cambia con la FRECUENCIA -el reparto del clon de reloj no es el mismo en
 * 3,3 MHz que en 14-, o sea que solo se mueve cuando el usuario mueve el dial.
 * Lo unico rapido que hay aqui es la señal, y la señal es justo de lo que
 * esto NO se tiene que enterar.
 */
#define IQB_ALFA   (1.0f / 512.0f)

/* Debajo de esto el promedio es ruido de cuantizacion del ADC y no hay nada
 * que medir: con la entrada muerta, E[I*Q] vale cero por casualidad y theta
 * saldria de dividir dos ceros. */
#define IQB_POT_MIN   100.0f

/*
 * EL ESTADO VIVE EN EL TCM, NO EN LA SRAM - 07/10/2026.
 *
 * Son veintiocho bytes y la SRAM estaba llena: al meter este fichero, el
 * enlazado se quedo corto por exactamente esa cantidad. El TCM tiene sitio,
 * es la RAM rapida del nucleo -que viene bien, porque de estas variables se
 * lee una vez por muestra dentro de la interrupcion del DMA- y su unica
 * limitacion es que el DMA no llega ahi. Aqui no hace falta que llegue: lo
 * que el DMA escribe es el buffer de muestras, que sigue donde estaba.
 *
 * Ver el comentario de la seccion .tcmram en GD32F450VE_FLASH.ld.
 */
#ifdef __GNUC__
#define TCMRAM_BSS __attribute__((section(".tcmram")))
#else
#define TCMRAM_BSS
#endif

/*
 * s_on ES VOLATIL - revision a fondo del 08/10/2026. Ver el bloque de las
 * s_ab_* de mas abajo: la prueba A/B lo escribe desde el bucle principal
 * (iqbal_ab_lanza(), iqbal_ab_paso()) y TAMBIEN desde la interrupcion del
 * DMA (iqbal_bloque() -> ab_aborta() cuando vence el plazo), y ademas lo
 * lee la propia interrupcion una vez por bloque para decidir si corrige.
 * Sin volatil el compilador puede guardarselo en un registro a los dos
 * lados y perderse lo que escribio el otro.
 */
static volatile uint8_t s_on  TCMRAM_BSS;
/* Si el estimador puede aprender de lo que entra. Ver iqbal_aprende(): en
 * FM ancha no puede, porque la portadora vive en continua. Volatil por lo
 * mismo que s_on: lo escribe el bucle principal y lo lee la interrupcion. */
static volatile uint8_t s_aprende TCMRAM_BSS;
/*
 * VOLATILES DESDE EL 29/10/2026, revision a fondo de HFDL.
 *
 * Las dos las escribe el BUCLE PRINCIPAL -iqbal_fir_olvida(),
 * iqbal_plano_olvida(), iqbal_resuelve()- y las lee la INTERRUPCION del
 * DMA una vez por bloque. Este fichero establece esa regla en su cabecera
 * y pone volatile en s_on, en s_aprende y en las once variables de la
 * prueba A/B, porque sin volatil el compilador puede guardarselo en un
 * registro a los dos lados. Estas dos se quedaron fuera.
 *
 * Y hay un arreglo que DEPENDE de ellas: el del 26/10 apaga la bandera la
 * PRIMERA y borra las tomas despues, para que la interrupcion vea que no
 * hay FIR desde la primera instruccion. Eso es una garantia de ORDEN entre
 * escrituras a objetos distintos, y sin volatile el compilador no tiene
 * ninguna obligacion de respetarla.
 *
 * Mirado el ensamblador del binario de hoy: el orden se respeta, asi que
 * no es un fallo observable ahora mismo. Es un contrato que no estaba
 * escrito, y la clase de cosa que se convierte en fallo el dia que alguien
 * toque el nivel de optimizacion. Si el fichero dice que lo que cruza va
 * volatile, va volatile.
 */
static volatile uint8_t s_hay TCMRAM_BSS;  /* ya hay una estimacion creible */
static float    s_ii TCMRAM_BSS, s_qq TCMRAM_BSS, s_iq TCMRAM_BSS;
static float    s_theta TCMRAM_BSS, s_g TCMRAM_BSS;
static uint32_t s_bloques     TCMRAM_BSS;
/* Bloques que la guarda de la pareja simetrica ha dejado fuera del
 * promedio. Ver IQB_PAR_MAX: en una banda llena esto sube y el estimador
 * se queda quieto, que es lo correcto. */
static uint32_t s_pares       TCMRAM_BSS;
static uint8_t  s_turno       TCMRAM_BSS;
static float    s_esp_db      TCMRAM_BSS;
static int16_t  s_esp_bin     TCMRAM_BSS;

#define IQB_AB_ESPERA  20U
#define IQB_AB_MIDE    25U

#define AB_PARADA      0U
#define AB_OFF_ESPERA  1U
#define AB_OFF_MIDE    2U
#define AB_ON_ESPERA   3U
#define AB_ON_MIDE     4U

/*
 * TODAS VOLATILES - revision a fondo del 08/10/2026.
 *
 * Este puñado de variables lo escriben DOS SITIOS A LA VEZ: el bucle
 * principal, desde iqbal_ab_lanza() e iqbal_ab_paso(), y la interrupcion
 * del DMA, que desde iqbal_bloque() llama a ab_aborta() cuando vence el
 * plazo. Hasta hoy ninguna era volatil, o sea que el compilador tenia
 * permiso para leerlas una vez, trabajar en registros y escribirlas al
 * final: cualquier cosa que hubiese hecho la interrupcion por el camino
 * se perdia sin dejar rastro, y la que mas duele es s_on, que es la que
 * decide si el corrector de espejo esta puesto.
 *
 * Volatil no es un cerrojo y no pretende serlo -para eso haria falta
 * apagar la interrupcion, y este fichero se compila tal cual en el banco
 * del ordenador, donde no hay interrupciones que apagar-. Lo que hace es
 * obligar a que cada lectura y cada escritura vaya de verdad a memoria,
 * que es lo que convierte "el bucle principal no se entera nunca" en "el
 * bucle principal se entera salvo en una ventana de dos instrucciones".
 */
static volatile uint8_t  s_ab_est     TCMRAM_BSS;
static volatile uint8_t  s_ab_n       TCMRAM_BSS;
static volatile uint8_t  s_ab_antes   TCMRAM_BSS;  /* como estaba el corrector */
static volatile uint8_t  s_ab_hay     TCMRAM_BSS;
static volatile uint8_t  s_ab_c_off   TCMRAM_BSS;
static volatile uint8_t  s_ab_c_on    TCMRAM_BSS;
static volatile float    s_ab_s_off   TCMRAM_BSS;
static volatile float    s_ab_s_on    TCMRAM_BSS;
static volatile float    s_ab_off     TCMRAM_BSS;
static volatile float    s_ab_on      TCMRAM_BSS;
/*
 * El plazo, en bloques de audio. 375 por segundo, asi que 2.500 son algo mas
 * de seis segundos y medio: la prueba entera son noventa cuadros, unos tres,
 * o sea el doble de margen. Se cuenta en la interrupcion del DMA, que es lo
 * unico de esta radio que no se salta nunca. Ver iqbal.h.
 */
#define IQB_AB_PLAZO  2500U
/* Volatil por lo mismo que las de arriba: la cuenta la lleva la
 * interrupcion y el bucle principal la pone a cero. */
static volatile uint16_t s_ab_plazo   TCMRAM_BSS;
/*
 * CUANTAS VECES SE HA ABORTADO. 26/10/2026, revision a fondo de espejos y
 * armonicos. Lo unico que hace falta para que el bucle principal se entere
 * de que la interrupcion aborto MIENTRAS el estaba a mitad de camino: no
 * se puede mirar el estado -la interrupcion lo deja en AB_PARADA y el
 * bucle principal esta a punto de escribirlo- ni el plazo -que vale cero
 * tanto despues de abortar como recien lanzada-. Ver iqbal_ab_paso().
 */
static volatile uint16_t s_ab_gen     TCMRAM_BSS;

static void ab_aborta(void);


void iqbal_init(void)
{
    /*
     * SIN_IQ: LA COMPILACION DE DIAGNOSTICO, QUE HASTA HOY NO HACIA NADA.
     * 26/10/2026, revision a fondo de espejos y armonicos.
     *
     * avisos.mk ofrece `make SINIQ=1` y dice que "lo deja fuera; si con eso
     * la radio vuelve a ir fina, ya sabemos donde mirar". Pero -DSIN_IQ no
     * lo miraba NI UN FICHERO del arbol: el firmware salia identico y la
     * respuesta era "no era eso" pasara lo que pasara, que es peor que no
     * tener el interruptor.
     *
     * Y NO BASTA CON ARRANCAR APAGADO, que fue el primer intento: a
     * s_aprende lo vuelve a encender apply_demod_mode() con
     * iqbal_aprende(!wfm) en cuanto se aplica cualquier modo que no sea FM
     * ancha, o sea siempre. Con s_on = 0 y s_aprende = 1 se quedaban
     * muertos la correccion, iqbal_calibra() y iqbal_resuelve() -su puerta
     * mira los dos-, pero el ESTIMADOR seguia corriendo DENTRO DE LA
     * INTERRUPCION DEL DMA, que es lo primero que avisos.mk nombra. Asi
     * que las puertas de verdad van en las propias funciones, abajo.
     *
     * (Esa compilacion avisa de que ab_aborta() y fir_aplica() se quedan
     * sin llamar. Es correcto y es justo lo que se busca: con SIN_IQ no
     * hay nada que abortar ni ningun FIR que aplicar.)
     */
#ifdef SIN_IQ
    s_on      = 0U;
    s_aprende = 0U;
#else
    s_on      = 1U;
    s_aprende = 1U;
#endif
    s_hay     = 0U;
    s_ii      = 0.0f;
    s_qq      = 0.0f;
    s_iq      = 0.0f;
    s_theta   = 0.0f;
    s_g       = 1.0f;
    s_bloques = 0U;
    s_pares   = 0U;   /* va en pareja con s_bloques: ver iqbal_pares() */
    s_turno   = 0U;
    s_esp_db  = 0.0f;
    s_esp_bin = 0;
    s_ab_est  = AB_PARADA;
    s_ab_gen  = 0U;
    s_ab_hay  = 0U;
    s_ab_off  = 0.0f;
    s_ab_on   = 0.0f;
}

void iqbal_set_on(uint8_t on) { s_on = on ? 1U : 0U; }
uint8_t iqbal_get_on(void)    { return s_on; }

/*
 * ======================================================================
 * APRENDER Y CORREGIR SON DOS COSAS, Y EN FM ANCHA SOLO VALE LA SEGUNDA
 * 26/10/2026, decimocuarta vuelta de la revision a fondo.
 * ======================================================================
 *
 * POR QUE. Todo el estimador plano se apoya en que E[x^2] tiende a cero
 * con x = I + jQ. Eso es cierto cuando lo que se recibe esta DESPLAZADO
 * del oscilador, porque entonces x^2 gira y se promedia solo. En FM ancha
 * NO HAY MEZCLADOR: la portadora vive en continua, y entonces
 *
 *     E[x^2] = A^2 * E[e^{j2*fase}]
 *
 * que no es cero: lo fija el indice de modulacion. La guarda de la pareja
 * simetrica (IQB_PAR_MAX) caza los casos extremos -rechaza todo por debajo
 * de +-40 kHz de desviacion, comprobado 750 bloques de 750- pero deja
 * pasar justo hasta su propio limite de diseño.
 *
 * Y entonces el corrector no corrige: SE INVENTA UN ESPEJO QUE NO EXISTE.
 * Medido con hardware perfecto (eg = 1,00, psi = 0 grados), portadora de
 * 8.000 cuentas, 16 s por fila. La ultima columna es el rechazo de imagen
 * que el propio corrector FABRICA sobre un tono limpio:
 *
 *     programa                theta      g       espejo fabricado
 *     musica fuerte +-75 kHz  -0,0069   1,0139      42,3 dB
 *     pasaje normal +-30 kHz  -0,1257   0,9617      23,8 dB
 *     voz floja     +-10 kHz  -0,0348   0,9480      30,0 dB
 *     voz muy floja  +-3 kHz  +0,0220   1,0742      28,5 dB
 *
 * Con un desequilibrio DE VERDAD de 25,3 dB, en el pasaje mas comun deja
 * 23,8 dB: PEOR que no tocar nada.
 *
 * Y LO QUE DE VERDAD DUELE ES LO QUE SE LLEVA A ONDA CORTA. Las medias
 * s_ii/s_qq/s_iq se actualizan SIEMPRE -la guarda solo decide si el numero
 * se acepta- y su constante de tiempo son 10,9 segundos, asi que el hueco
 * de programa las envenena y lo aprendido viaja. Medido, radio con espejo
 * nativo de 25,3 dB:
 *
 *     60 s solo en onda corta ....................  86,7 dB
 *     15 s de musica en FM ancha .................  44,5 dB
 *     + 7,5 s de hueco de programa ...............  32,0 dB
 *     de vuelta en onda corta: 0 s -> 32,0   20 s -> 51,7   40 s -> 67,6
 *
 * O sea que UN PASO POR LA FM COMERCIAL CUESTA DE 42 A 55 dB DE RECHAZO DE
 * IMAGEN EN ONDA CORTA, y cerca de un minuto en recuperarse. Que es,
 * palabra por palabra, el sintoma para el que se escribio este modulo.
 *
 * EL ARREGLO. No apagar el corrector -lo ya aprendido en onda corta sigue
 * siendo bueno y la pantalla lo agradece-, sino APAGAR EL APRENDIZAJE
 * mientras la señal no este desplazada. Asi la calibracion de onda corta
 * sobrevive intacta a un paseo por la FM.
 *
 * POR QUE NO LO MEDIA NADIE: sim/wfm_espejo.c mide el espejo del HARDWARE
 * sobre el audio de WFM y nunca llama a iqbal_bloque(); sim/iqbaltest.c no
 * tiene ni un caso a 192 kHz ni de FM ancha.
 */
void iqbal_aprende(uint8_t si)
{
#ifdef SIN_IQ
    (void)si;
    s_aprende = 0U;   /* ver iqbal_init(): con SIN_IQ no se aprende nunca */
#else
    s_aprende = si ? 1U : 0U;
#endif
}
uint8_t iqbal_get_aprende(void) { return s_aprende; }

/*
 * ======================================================================
 * LA PAREJA SIMETRICA, QUE ES LO QUE DE VERDAD ROMPIA ESTO - 24/10/2026.
 * ======================================================================
 *
 * *** Avisado por el usuario: le salen señales espejo, de esas que van al
 * contrario de la marcha, en 40 metros. Y en las dos tasas. ***
 *
 * Y NO ERA EL ESPEJO DEL APARATO: ERA ESTE FICHERO METIENDOLO. Medido en
 * sim/iqbaltest.c, seccion "la banda poblada", con veinte emisoras y tres
 * parejas puestas una enfrente de otra:
 *
 *     corrector apagado     26,8 dB de rechazo
 *     corrector encendido   12,3 dB     <- CATORCE dB PEOR
 *
 * DE DONDE SALE. El estimador plano se apoya en dos numeros:
 *
 *     E[I*Q]   tiene que ser cero si I y Q estan en cuadratura
 *     E[I*I] - E[Q*Q]   tiene que ser cero si las dos ramas pesan igual
 *
 * Los dos son la parte real y la imaginaria de E[x^2] con x = I + jQ. Para
 * UNA portadora en +f, x^2 gira al doble de velocidad y promedia cero: por
 * eso el metodo funciona. Pero con DOS portadoras, una en +f y otra en -f,
 * el producto cruzado NO GIRA -las frecuencias se cancelan- y deja un
 * termino constante 2*a*b en E[x^2].
 *
 * O sea que dos emisoras de verdad colocadas simetricamente respecto al
 * oscilador producen exactamente la misma firma que un desequilibrio. El
 * estimador no puede distinguirlas: se cree el 2*a*b, calcula una ganancia
 * y una fase que no existen, y al aplicarlas CREA la imagen que venia a
 * quitar. Y en los 40 metros eso no es raro, es lo normal: banda de
 * aficionados llena de un lado, radiodifusion del otro, decenas de señales
 * dentro de la pantalla y el oscilador en medio.
 *
 * LA GUARDA, Y POR QUE ES UN TOPE ABSOLUTO Y NO UN FILTRO.
 *
 * Las dos cosas -el desequilibrio y la pareja- dejan la misma huella en
 * E[x^2], y por la magnitud sola NO se pueden separar: lo unico que las
 * distingue es que una es coherente con la frecuencia y la otra no, y eso
 * se mide en el espectro, no aqui. Lo que si se puede hacer aqui es
 * reconocer un numero IMPOSIBLE.
 *
 * |E[x^2]| / E[|x|^2] vale aproximadamente 2*epsilon, con epsilon la
 * relacion imagen/señal. O sea:
 *
 *     rechazo nativo   epsilon   la razon
 *     ---------------- --------- ---------
 *     22 dB (el peor      0,079    0,158     <- lo peor que ha enseñado
 *       visto en la                             esta radio
 *       radio)
 *     25 dB (el caso      0,056    0,112     <- el que se arreglo de 25
 *       de la cabecera)                         a 86 dB
 *     20,8 dB             0,091    0,180     <- el tope
 *
 * Por encima de 0,18 lo que se esta midiendo no puede ser el desequilibrio
 * de un receptor en cuadratura: es la escena. Medido en sim/iqbaltest.c,
 * la banda poblada da 0,24.
 *
 * LO QUE ESTO CUESTA, dicho claro: si algun dia un ejemplar viene peor de
 * 21 dB, el corrector plano no lo tocara y se quedara como esta. Eso es
 * exactamente lo que tiene que pasar -la alternativa es creerse una medida
 * que puede ser de la escena y DOBLAR el espejo, que es lo que estaba
 * haciendo- y ademas el FIR del espectro sigue pudiendo con ello, porque
 * aquel si sabe mirar bin a bin.
 *
 * Lo que pasa en una banda llena, con esto puesto, es que el estimador se
 * QUEDA QUIETO con lo ultimo bueno que midio. Que es justo lo que dice la
 * cabecera que tiene que hacer: lo peor que puede hacer cuando se equivoca
 * es no hacer nada.
 */
#define IQB_PAR_MAX      0.18f

/*
 * ======================================================================
 * RESOLVER LAS MEDIAS: UNA SOLA ESCRITURA - 24/10/2026.
 * ======================================================================
 *
 * Encontrado en la revision a fondo pedida por el usuario, y es el fallo
 * que agc.h describe en su cabecera con todas las letras: dos copias del
 * mismo lazo se separan solas.
 *
 * Habia DOS: la de iqbal_mide() -que usan los bancos del PC- y la de
 * iqbal_bloque() -la que corre en la radio-. Hacian la misma cuenta: de las
 * tres medias saca theta, de theta saca E[Q1*Q1], de ahi la ganancia, y
 * acepta solo si los tres numeros caen dentro de sus topes.
 *
 * Y YA SE HABIAN SEPARADO. El 24/10/2026, por la mañana, se añadio la
 * guarda de la pareja simetrica -IQB_PAR_MAX, la que impide que dos
 * emisoras enfrentadas respecto al oscilador se tomen por desequilibrio del
 * hardware- y se escribio SOLO en iqbal_bloque(). O sea que el banco que
 * respalda las cifras de rechazo de este modulo estaba midiendo un
 * estimador que ya no era el de la radio, y encima la copia que se quedo
 * atras es la que valida a la otra.
 *
 * Ahora la cuenta esta aqui y las dos entran por la misma puerta.
 *
 * Devuelve 1 si la estimacion se ha aceptado y 0 si no, y en el segundo
 * caso dice por `pareja` si el motivo fue el tope de la pareja simetrica,
 * que es el unico que se cuenta aparte (ver iqbal_pares()).
 */
static uint8_t resuelve(uint8_t *pareja)
{
    float pot = s_ii + s_qq;
    float re  = s_ii - s_qq;
    float im  = 2.0f * s_iq;
    float th, q1q1, g;

    *pareja = 0U;

    /* Sin potencia no hay nada que medir, y eso NO es una pareja simetrica:
     * es que no entra nada -el I2S parado, o un bloque de muestras todas
     * iguales-. Contarlo como pareja ensuciaria la fila que existe para
     * decir si la banda esta poblada. 24/10/2026. */
    if (pot <= 0.0f) { return 0U; }
    /*
     * Y EL UMBRAL DE POTENCIA, TAMBIEN ANTES - 26/10/2026.
     *
     * Estaba DESPUES de la prueba de pareja simetrica, y entonces el
     * comentario de arriba solo era cierto para pot EXACTAMENTE cero. Con
     * un canal del codec vivo y el otro mudo -s_ii = 50, s_qq ~ 0- sale
     * re/pot ~ 1, que pasa de largo cualquier IQB_PAR_MAX, y el bloque se
     * contaba como pareja. O sea que media cadena de recepcion caida se
     * enseñaba en la fila de Informacion como "la banda esta poblada",
     * que es justo lo contrario de lo que pasa.
     */
    if (s_ii < IQB_POT_MIN || s_qq < IQB_POT_MIN) { return 0U; }
    if ((re * re + im * im) > (IQB_PAR_MAX * IQB_PAR_MAX) * pot * pot) {
        *pareja = 1U;
        return 0U;
    }

    th = s_iq / s_ii;
    if (th > IQB_THETA_MAX || th < -IQB_THETA_MAX) { return 0U; }

    /* E[Q1*Q1] = E[QQ] - 2*th*E[IQ] + th*th*E[II], sin tener que volver a
     * recorrer el bloque: sale de las tres medias que ya hay. */
    q1q1 = s_qq - 2.0f * th * s_iq + th * th * s_ii;
    if (q1q1 < IQB_POT_MIN) { return 0U; }

    g = sqrtf(s_ii / q1q1);
    if (g > IQB_G_MAX || g < IQB_G_MIN) { return 0U; }

    s_theta = th;
    s_g     = g;
    s_hay   = 1U;
    return 1U;
}

void iqbal_mide(const int16_t *i, const int16_t *q, uint32_t n)
{
    float ii = 0.0f, qq = 0.0f, iq = 0.0f;
    float inv;
    uint32_t k;

    if (n == 0U) { return; }

    /*
     * LA CONTINUA SE QUITA ANTES DE MEDIR, y no es un refinamiento: es la
     * diferencia entre medir y no medir. Con una componente continua en I y
     * otra en Q -que las hay siempre, el desplazamiento del ADC- las tres
     * medias se llenan con el producto de las dos continuas, que no tiene
     * nada que ver con el desequilibrio. El banco lo pilla con la portadora
     * puesta en el centro exacto: sin esto, la ganancia se iba al tope.
     */
    {
        float mi = 0.0f, mq = 0.0f;
        for (k = 0U; k < n; k++) {
            mi += (float)i[k];
            mq += (float)q[k];
        }
        inv = 1.0f / (float)n;
        mi *= inv; mq *= inv;
        for (k = 0U; k < n; k++) {
            float a = (float)i[k] - mi;
            float b = (float)q[k] - mq;
            ii += a * a;
            qq += b * b;
            iq += a * b;
        }
    }
    ii *= inv; qq *= inv; iq *= inv;

    s_ii += IQB_ALFA * (ii - s_ii);
    s_qq += IQB_ALFA * (qq - s_qq);
    s_iq += IQB_ALFA * (iq - s_iq);
    s_bloques++;

    /*
     * Sin señal, y fuera de los topes, no se toca lo que ya habia: poner
     * theta a cero seria tirar la estimacion cada vez que el usuario baja la
     * ganancia de antena o cambia de banda a un hueco vacio. Y una medida
     * fuera de rango solo puede querer decir que lo que se esta midiendo no
     * es el desequilibrio del hardware -una continua grande, un bloque
     * recortado, una portadora justo en el centro, o dos emisoras
     * enfrentadas respecto al oscilador-, y ante eso lo correcto es no tocar
     * nada y esperar al bloque siguiente.
     *
     * Las dos cosas las decide resuelve(), que es la MISMA que usa el camino
     * de la radio. Ver su comentario. */
    {
        uint8_t pareja = 0U;

        if (!resuelve(&pareja) && pareja && s_pares < 0xFFFFFFFFUL) {
            s_pares++;
        }
    }
}

void iqbal_aplica(int16_t *i, int16_t *q, uint32_t n)
{
    uint32_t k;
    float th, g;

    if (!s_on || !s_hay) { return; }
    th = s_theta;
    g  = s_g;

    for (k = 0U; k < n; k++) {
        float v = g * ((float)q[k] - th * (float)i[k]);
        /* El entero de 16 bits se satura, no se envuelve. Envolver convierte
         * un pico en un salto de escala entera, que en la FFT es un suelo de
         * ruido nuevo repartido por toda la pantalla - mucho peor que el
         * espejo que estamos quitando. */
        if (v >  32767.0f) { v =  32767.0f; }
        if (v < -32768.0f) { v = -32768.0f; }
        q[k] = (int16_t)v;
    }
}

/* ======================================================================
 * CALIBRACION POR PORTADORAS IDENTIFICADAS, Y EL FIR QUE SALE DE ELLA
 * ======================================================================
 * El porque entero esta en iqbal.h. Aqui solo el como.
 */

/* Cuanto tiene que sobresalir un bin sobre la potencia media para que se le
 * considere una portadora: 20 dB. Mas bajo entran picos de ruido, y un pico
 * de ruido comparado con otro pico de ruido da un numero perfectamente
 * formado que no significa nada. */
#define IQB_PICO_SOBRE   100.0f

/* Bins pegados a la continua que se saltan: ahi viven el desplazamiento del
 * ADC y la fuga del propio LO, que no son señal y cuyo "espejo" son ellos
 * mismos. Es el mismo numero que usa iqbal_mira_espectro() mas abajo, y por
 * la misma razon; se escribe aparte porque aquel vive en el array YA
 * desplazado (centro en n/2) y este en el orden natural de la FFT. */
#define IQB_CAL_GUARDA   4U

/*
 * Y EL BIN DE ENFRENTE TIENE QUE ESTAR BIEN POR DEBAJO. Si no, lo que hay
 * ahi no es una imagen: son dos emisoras, y medirlas contamina el ajuste.
 * Es uno de los dos fallos que el banco encontro en el estimador.
 *
 * ERAN 6 dB HASTA EL 24/10/2026, Y SEIS NO SON NADA. Un receptor con un
 * espejo a 6 dB no es un receptor: el peor que ha enseñado esta radio son
 * 22 dB, y la cabecera de iqbal.h dice que tres grados de error dejan 30.
 * O sea que todo lo que entraba entre 6 y 20 dB NO PODIA ser una imagen de
 * este aparato, y sin embargo se tomaba por buena.
 *
 * 0,02 son 17 dB: cinco por debajo del peor caso real, que es margen de
 * sobra, y deja fuera las parejas de emisoras de verdad.
 */
#define IQB_ESPEJO_BAJO  0.02f

/* Medidas minimas en una ranura para fiarse de ella, y ranuras minimas para
 * poder resolver. Cuatro incognitas piden al menos cuatro puntos; se exige
 * UNA MAS -cinco- para que el ajuste no pase exactamente por todos y copie
 * su ruido. (Aqui ponia "seis" y el numero de abajo es cinco desde que
 * existe; corregido el 26/10/2026 al revisar espejos y armonicos.) */
#define IQB_CAL_MIN_N    4U
#define IQB_RANURAS_MIN  5U


/*
 * LAS RANURAS VIVEN EN LA SRAM Y EL FIR EN EL TCM, y no es indiferente: de
 * las ranuras se escribe una vez por FFT -en el bucle de pantalla- y del FIR
 * se lee UNA VEZ POR MUESTRA dentro de la interrupcion del DMA. El TCM es la
 * RAM rapida del nucleo y es donde tiene que estar lo segundo. Son 288 bytes
 * de SRAM y 72 de TCM; ver el comentario de arriba sobre por que no se
 * acumula un numero por bin.
 */
static float    s_cal_nre[IQB_RANURAS];
static float    s_cal_nim[IQB_RANURAS];
static float    s_cal_den[IQB_RANURAS];
static float    s_cal_fs[IQB_RANURAS];   /* suma de frecuencias, ciclos/muestra */
static uint16_t s_cal_n[IQB_RANURAS];

static float   s_fir_re[IQB_FIR_TOMAS] TCMRAM_BSS;
static float   s_fir_im[IQB_FIR_TOMAS] TCMRAM_BSS;
static float   s_hre[IQB_FIR_TOMAS]    TCMRAM_BSS;  /* historia entre bloques */
static float   s_him[IQB_FIR_TOMAS]    TCMRAM_BSS;
/* Volatil por lo mismo que s_hay: ver su comentario. */
static volatile uint8_t s_fir_hay      TCMRAM_BSS;

uint8_t iqbal_fir_hay(void) { return s_fir_hay; }

/* Ver iqbal.h para cuando hace falta y por que. */
void iqbal_plano_olvida(void)
{
    s_hay   = 0U;
    s_theta = 0.0f;
    s_g     = 1.0f;
    s_ii    = 0.0f;
    s_qq    = 0.0f;
    s_iq    = 0.0f;
}

void iqbal_fir_olvida(void)
{
    uint8_t m;

    /*
     * LA BANDERA SE APAGA LA PRIMERA, Y NO LA ULTIMA - 26/10/2026.
     *
     * Estaba al final, detras de borrar las 16 ranuras y las 16 tomas mas
     * la historia. Esto se llama desde el bucle principal y fir_aplica()
     * corre en la interrupcion del DMA mirando s_fir_hay: con la bandera
     * todavia a uno, un bloque de audio podia entrar y correr con MEDIO
     * FIR a cero. Apagandola primero, la interrupcion ve "no hay FIR" ya
     * desde la primera instruccion y se va por la rama plana.
     *
     * Es el mismo orden que iqbal_ab_lanza() ya se cuida de respetar con
     * s_ab_est; aqui faltaba.
     */
    s_fir_hay = 0U;
    for (m = 0U; m < (uint8_t)IQB_RANURAS; m++) {
        s_cal_nre[m] = 0.0f; s_cal_nim[m] = 0.0f;
        s_cal_den[m] = 0.0f; s_cal_fs[m] = 0.0f; s_cal_n[m] = 0U;
    }
    for (m = 0U; m < (uint8_t)IQB_FIR_TOMAS; m++) {
        s_fir_re[m] = 0.0f; s_fir_im[m] = 0.0f;
        s_hre[m] = 0.0f;    s_him[m] = 0.0f;
    }
}

uint8_t iqbal_cal_ranuras(void)
{
    uint8_t m, c = 0U;
    for (m = 0U; m < (uint8_t)IQB_RANURAS; m++) {
        if (s_cal_n[m] >= (uint16_t)IQB_CAL_MIN_N) { c++; }
    }
    return c;
}

void iqbal_calibra(const float *re, const float *im, uint32_t n)
{
    uint32_t k, mitad;
    float media = 0.0f, umbral;

    if (re == 0 || im == 0 || n < 64U) { return; }
    /*
     * ==================================================================
     * *** Y SOLO CON EL CORRECTOR ENCENDIDO Y APRENDIENDO - 26/10/2026,
     * revision a fondo de espejos y armonicos. ***
     * ==================================================================
     *
     * Esto son dos cosas a la vez, y las dos importan.
     *
     * 1. EL LAZO SE ABRIA. iqbal_resuelve() SUMA al FIR porque da por
     *    hecho que lo que el panadaptador mide es el RESIDUO, o sea lo que
     *    queda DESPUES de corregir. Eso solo es cierto si el corrector
     *    esta puesto. Con s_on = 0 -la fase apagada de la prueba A/B-,
     *    aqui entraba el desequilibrio ENTERO, y la vuelta siguiente le
     *    sumaba otro medio desequilibrio completo a un FIR que ya estaba
     *    bien. El tope de iqbal_resuelve() no lo paraba: vigila el
     *    INCREMENTO, no lo acumulado. Con el corrector apagado a mano, el
     *    coeficiente crece lineal y sin freno hasta que el "corrector"
     *    deja de quitar espejo y lo FABRICA a escala completa.
     *
     *    Hoy en la radio s_on solo se apaga 1,5 s por prueba A/B, asi que
     *    el daño era un escalon y la contaminacion de las ranuras con
     *    medidas del desequilibrio completo. Pero el mecanismo estaba
     *    entero y nada en el modulo lo acotaba.
     *
     * 2. Y ERA LO MAS CARO QUE CORRIA SIN NINGUNA PUERTA. Esto lo llama
     *    fft.c en CADA FFT cruda del panadaptador, hasta seis por cuadro,
     *    unas 180 por segundo: del orden de 276.000 multiplicaciones por
     *    segundo en el bucle de pantalla, se estuviera usando o no. No
     *    miraba s_on, ni s_aprende, ni s_hay, ni el modo.
     *
     * s_aprende ademas es la puerta que ya existe para FM ancha
     * -iqbal_aprende(!wfm) en main.c-, asi que con esto el FIR deja de
     * intentar aprender alli tambien.
     */
    if (!s_on || !s_aprende) { return; }
    mitad = n / 2U;

    /*
     * EL SUELO, CON LA MEDIA RECORTADA - y no con la media a secas, que fue
     * el primer intento y no encontraba NI UNA portadora.
     *
     * Con cinco emisoras fuertes dentro de 256 bins, las propias emisoras
     * levantan la media hasta que ninguna la supera por 20 dB: en el banco
     * salian cero ranuras y el FIR no llegaba a existir. iqbal_mira_espectro()
     * ya tropezo con esto en su dia y lo resolvio con una mediana aproximada
     * por biseccion; aqui sale mas barato -esto corre en CADA FFT, aquello
     * una vez por cuadro- con dos pasadas: la media de todo, y despues la
     * media de lo que no pase de diez veces la primera. Las portadoras se
     * caen de la segunda y lo que queda es el suelo de ruido.
     */
    for (k = 0U; k < n; k++) {
        media += re[k] * re[k] + im[k] * im[k];
    }
    media /= (float)n;
    if (media <= 0.0f) { return; }
    {
        float tope = media * 10.0f, suma = 0.0f;
        uint32_t cuenta = 0U;
        for (k = 0U; k < n; k++) {
            float p = re[k] * re[k] + im[k] * im[k];
            if (p <= tope) { suma += p; cuenta++; }
        }
        if (cuenta >= (n / 2U)) { media = suma / (float)cuenta; }
    }
    umbral = media * IQB_PICO_SOBRE;

    /* Se empieza en 2 y se acaba en n-2 para poder mirar los vecinos sin
     * salirse; los bins de la continua se saltan aparte, mas abajo. */
    for (k = 2U; k + 2U < n; k++) {
        float pk = re[k] * re[k] + im[k] * im[k];
        float pa, pb, pe, dr, di, fq;
        uint32_t ki, r;

        if (pk < umbral) { continue; }

        /* SOLO EL PICO. Las faldas de la ventana a los lados de una
         * portadora pasan el umbral de sobra, y lo que miden es basura. */
        pa = re[k - 1U] * re[k - 1U] + im[k - 1U] * im[k - 1U];
        pb = re[k + 1U] * re[k + 1U] + im[k + 1U] * im[k + 1U];
        if (pk < pa || pk < pb) { continue; }

        ki = (n - k) % n;                 /* el bin espejo */
        /* La continua y sus vecinos no son señal, y su "espejo" son ellos
         * mismos. */
        if (k < IQB_CAL_GUARDA || (n - k) < IQB_CAL_GUARDA) { continue; }
        if (ki < 2U || ki + 2U >= n) { continue; }

        pe = re[ki] * re[ki] + im[ki] * im[ki];
        if (pe > pk * IQB_ESPEJO_BAJO) { continue; }   /* enfrente hay señal */

        /*
         * W(-k) = -Z(-k)/conj(Z(k)) = -Z(-k)*Z(k)/|Z(k)|^2.
         * Se acumulan numerador y denominador por separado para que el
         * promedio entre medidas sea COHERENTE: promediar cocientes le da
         * el mismo peso a una portadora fuerte y a una que apenas pasa el
         * umbral, y la segunda trae mucho mas ruido.
         */
        dr = -(re[ki] * re[k] - im[ki] * im[k]);
        di = -(re[ki] * im[k] + im[ki] * re[k]);

        r = (ki * (uint32_t)IQB_RANURAS) / n;
        if (r >= (uint32_t)IQB_RANURAS) { r = (uint32_t)IQB_RANURAS - 1U; }

        /* La frecuencia del bin espejo, en ciclos por muestra y con signo. */
        fq = (ki < mitad) ? ((float)ki / (float)n)
                          : (((float)ki - (float)n) / (float)n);

        /*
         * *** SE PARTEN POR LA MITAD AL LLENARSE, Y NO SE DEJA DE CONTAR.
         * 26/10/2026. ***
         *
         * Aqui el contador se paraba en 60.000 y los CUATRO acumuladores
         * seguian sumando. Dos consecuencias, y la primera es grave:
         *
         *   - fq = s_cal_fs[r] / s_cal_n[r] (ver mas abajo) se divide por
         *     el contador parado, asi que a partir de la medida 60.000 la
         *     frecuencia que se le atribuye a la ranura CRECE SIN LIMITE.
         *     Y esa frecuencia es la que arma la matriz: el ajuste pasa a
         *     colocar la correccion en una frecuencia que no existe. Se
         *     llega en cinco minutos y medio de escuchar una emisora fija,
         *     porque las ranuras solo se borran cuando iqbal_resuelve()
         *     consigue resolver, y con una sola emisora nunca junta las
         *     cinco que necesita.
         *
         *   - y el peso de la ranura tambien se congela mientras sus
         *     sumas siguen creciendo, asi que deja de decir cuantas
         *     medidas tiene.
         *
         * Partiendo los cuatro y el contador a la vez, TODAS las medias
         * salen iguales -es la misma division con numerador y denominador
         * divididos por dos- y ninguna suma puede crecer sin fin. De paso
         * se va el otro problema que esto tenia: con s_cal_den subiendo
         * sin tope, pasadas unas 26 horas el float de 32 bits ya no podia
         * representar las sumas nuevas y la ranura se quedaba clavada en
         * lo que midio el primer dia.
         */
        if (s_cal_n[r] >= 60000U) {
            s_cal_nre[r] *= 0.5f;
            s_cal_nim[r] *= 0.5f;
            s_cal_den[r] *= 0.5f;
            s_cal_fs[r]  *= 0.5f;
            s_cal_n[r]    = (uint16_t)(s_cal_n[r] / 2U);
        }
        s_cal_nre[r] += dr;
        s_cal_nim[r] += di;
        s_cal_den[r] += pk;
        s_cal_fs[r]  += fq;
        s_cal_n[r]++;
    }
}

/*
 * El sistema de IQB_FIR_TOMAS incognitas complejas, por eliminacion de Gauss
 * con pivoteo parcial. Cuatro por cuatro: no merece una biblioteca, y una
 * biblioteca de algebra lineal en un firmware de 475 kB tampoco.
 */
static uint8_t resuelve_sistema(float ar[IQB_FIR_TOMAS][IQB_FIR_TOMAS + 1U],
                                float ai[IQB_FIR_TOMAS][IQB_FIR_TOMAS + 1U],
                                float *xr, float *xi)
{
    const uint8_t L = (uint8_t)IQB_FIR_TOMAS;
    uint8_t c, f, p;
    float lim;

    /*
     * LA ESCALA DE LA MATRIZ, PARA PODER MIRAR EL PIVOTE EN RELATIVO.
     * 26/10/2026, revision a fondo de espejos y armonicos.
     *
     * Aqui abajo habia un umbral ABSOLUTO -1e-20- contra una matriz cuya
     * escala la fija la suma de los pesos de las ranuras, o sea entre unos
     * cientos y 1e12. El umbral quedaba entre veinticuatro y treinta y dos
     * ordenes de magnitud por debajo de lo que queria vigilar: NO SALTABA
     * NUNCA. Un sistema casi singular -que es justo el caso que el
     * comentario dice querer cazar, "todas las portadoras en el mismo
     * sitio", lo normal cuando no se ha recorrido la banda- pasaba el
     * pivoteo, amplificaba el ruido de la medida y lo unico que lo frenaba
     * era el tope de |x| < 0,5 de mas abajo.
     *
     * Con la escala delante, el umbral puede ser lo que tenia que ser: una
     * fraccion de la propia matriz. 1e-12 sobre el mayor |a|^2 de la
     * diagonal son seis ordenes de magnitud en amplitud, de sobra para
     * pasar un sistema sano y suficiente para parar uno degenerado.
     */
    {
        float esc = 0.0f;

        for (c = 0U; c < L; c++) {
            float v = ar[c][c] * ar[c][c] + ai[c][c] * ai[c][c];
            if (v > esc) { esc = v; }
        }
        if (!(esc > 0.0f)) { return 0U; }
        lim = esc * 1e-12f;
    }

    for (c = 0U; c < L; c++) {
        float mejor = -1.0f;
        p = c;
        for (f = c; f < L; f++) {
            float v = ar[f][c] * ar[f][c] + ai[f][c] * ai[f][c];
            if (v > mejor) { mejor = v; p = f; }
        }
        /* Un pivote a cero quiere decir que las medidas no dan para tantas
         * incognitas -por ejemplo, todas las portadanas en el mismo sitio-.
         * No se inventa una solucion: se dice que no. */
        if (!(mejor > lim)) { return 0U; }
        if (p != c) {
            for (f = 0U; f <= L; f++) {
                float t = ar[c][f]; ar[c][f] = ar[p][f]; ar[p][f] = t;
                t = ai[c][f]; ai[c][f] = ai[p][f]; ai[p][f] = t;
            }
        }
        {
            float dr = ar[c][c], di = ai[c][c];
            float inv = 1.0f / (dr * dr + di * di);
            for (f = c; f <= L; f++) {
                float zr = ar[c][f], zi = ai[c][f];
                ar[c][f] = (zr * dr + zi * di) * inv;
                ai[c][f] = (zi * dr - zr * di) * inv;
            }
        }
        for (f = 0U; f < L; f++) {
            float mr, mi;
            if (f == c) { continue; }
            mr = ar[f][c]; mi = ai[f][c];
            if (mr == 0.0f && mi == 0.0f) { continue; }
            {
                uint8_t j;
                for (j = c; j <= L; j++) {
                    ar[f][j] -= mr * ar[c][j] - mi * ai[c][j];
                    ai[f][j] -= mr * ai[c][j] + mi * ar[c][j];
                }
            }
        }
    }
    for (c = 0U; c < L; c++) { xr[c] = ar[c][L]; xi[c] = ai[c][L]; }
    return 1U;
}

uint8_t iqbal_resuelve(void)
{
    const uint8_t L = (uint8_t)IQB_FIR_TOMAS;
    float ar[IQB_FIR_TOMAS][IQB_FIR_TOMAS + 1U];
    float ai[IQB_FIR_TOMAS][IQB_FIR_TOMAS + 1U];
    float xr[IQB_FIR_TOMAS], xi[IQB_FIR_TOMAS];
    uint8_t r, m, j, usadas = 0U;

    /*
     * LA MISMA PUERTA QUE iqbal_calibra() - 26/10/2026. Con el corrector
     * apagado las ranuras no traen el residuo sino el desequilibrio
     * entero, y sumar eso al FIR es abrir el lazo. Ver alli.
     */
    if (!s_on || !s_aprende) { return 0U; }

    /*
     * Y SE CUENTAN LAS RANURAS ANTES DE ARMAR NADA - 26/10/2026.
     *
     * Esta cuenta estaba DESPUES del bucle que monta la matriz, o sea que
     * en la mayoria de las vueltas -las que no juntan cinco ranuras, que
     * son casi todas: con una sola emisora en la banda no se juntan
     * nunca- se pagaban hasta 640 llamadas a cosf/sinf en el bucle de
     * pantalla para tirarlo todo a la basura dos lineas despues.
     */
    for (r = 0U; r < (uint8_t)IQB_RANURAS; r++) {
        if (s_cal_n[r] < (uint16_t)IQB_CAL_MIN_N) { continue; }
        if (s_cal_den[r] <= 0.0f) { continue; }
        usadas++;
    }
    if (usadas < (uint8_t)IQB_RANURAS_MIN) { return 0U; }

    for (m = 0U; m < L; m++) {
        for (j = 0U; j <= L; j++) { ar[m][j] = 0.0f; ai[m][j] = 0.0f; }
    }

    for (r = 0U; r < (uint8_t)IQB_RANURAS; r++) {
        float wr, wi, fq, peso;
        float cr[IQB_FIR_TOMAS], ci[IQB_FIR_TOMAS];
        if (s_cal_n[r] < (uint16_t)IQB_CAL_MIN_N) { continue; }
        if (s_cal_den[r] <= 0.0f) { continue; }

        wr = s_cal_nre[r] / s_cal_den[r];
        wi = s_cal_nim[r] / s_cal_den[r];
        fq = s_cal_fs[r] / (float)s_cal_n[r];
        peso = (float)s_cal_n[r];

        /*
         * Fila de las ecuaciones normales: A^H W A h = A^H W d, con
         * A[m] = exp(-j 2 pi f m) (CAUSAL; ver iqbal.h para por que no
         * centrado). Se acumula directamente, sin montar A entera.
         */
        /*
         * LOS CUATRO ANGULOS, UNA VEZ - 26/10/2026.
         *
         * aj_r/aj_i se calculaban DENTRO del bucle de m aunque solo
         * dependen de j: por ranura salian 40 llamadas a cosf/sinf donde
         * hacen falta 8. Cinco veces mas trigonometria de la necesaria, en
         * el bucle de pantalla. El resultado es identico: es el mismo
         * coseno del mismo angulo.
         */
        for (m = 0U; m < L; m++) {
            cr[m] = cosf(-6.2831853f * fq * (float)m);
            ci[m] = sinf(-6.2831853f * fq * (float)m);
        }

        for (m = 0U; m < L; m++) {
            float am_r = cr[m];
            float am_i = ci[m];
            for (j = 0U; j < L; j++) {
                float aj_r = cr[j];
                float aj_i = ci[j];
                /* conj(A[m]) * A[j] * peso */
                ar[m][j] += peso * (am_r * aj_r + am_i * aj_i);
                ai[m][j] += peso * (am_r * aj_i - am_i * aj_r);
            }
            /* conj(A[m]) * d * peso */
            ar[m][L] += peso * (am_r * wr + am_i * wi);
            ai[m][L] += peso * (am_r * wi - am_i * wr);
        }
    }

    /*
     * SI YA NO HAY ESPEJO, NO SE TOCA NADA - y esto no es prudencia, es un
     * fallo que el banco encontro.
     *
     * Con el desequilibrio PLANO, el corrector de un coeficiente deja el
     * espejo en 78 dB. El FIR, ajustado sobre cinco puntos ruidosos, metia
     * SU PROPIO ruido y lo dejaba en 59: diecinueve dB peor que no hacer
     * nada. El estimador tiene un techo de precision y, por encima de el,
     * corregir es estropear.
     *
     * Asi que primero se mira CUANTO espejo queda: |W| es la relacion
     * imagen/señal medida.
     *
     * EL UMBRAL ES -42 dB, y conviene saber que NO basta. El estimador
     * tiene su propio suelo -FFT de 256 puntos y señal cuantizada a 16
     * bits-, y en el caso plano, con el espejo real en 78 dB, sigue
     * midiendo residuo por encima de esto. O sea que la guarda no llega a
     * dispararse ahi y el FIR mete algo de ruido igualmente: 78 -> 74,6 dB
     * medido en el banco.
     *
     * Se deja asi a proposito, con el numero delante: 74 dB de rechazo
     * siguen estando muy por encima de lo que se ve en pantalla (45-50), y
     * a cambio los casos que de verdad duelen -22 dB- suben a 38. Subir el
     * umbral a -35 arregla menos de lo que estropea: el lazo se para antes
     * y el caso con pendiente se queda en 34,5 en vez de 38,5. Medido
     * tambien.
     */
    {
        float suma = 0.0f, pesos = 0.0f;
        for (r = 0U; r < (uint8_t)IQB_RANURAS; r++) {
            float wr, wi, pw;
            if (s_cal_n[r] < (uint16_t)IQB_CAL_MIN_N) { continue; }
            if (s_cal_den[r] <= 0.0f) { continue; }
            wr = s_cal_nre[r] / s_cal_den[r];
            wi = s_cal_nim[r] / s_cal_den[r];
            pw = (float)s_cal_n[r];
            suma  += pw * (wr * wr + wi * wi);
            pesos += pw;
        }
        /* (10^(-42/20))^2 = 6,3e-5 */
        if (pesos <= 0.0f || (suma / pesos) < 6.3e-5f) { return 0U; }
    }

    if (!resuelve_sistema(ar, ai, xr, xi)) { return 0U; }

    /*
     * EL TOPE. Lo mismo que hacen theta y g mas arriba, y por lo mismo: un
     * coeficiente disparatado solo puede querer decir que lo que se ha
     * medido no es el desequilibrio. Una imagen a 0 dB de la señal seria
     * |w| = 1; por encima de 0,5 ya no hay nada que corregir, hay otra cosa.
     */
    for (m = 0U; m < L; m++) {
        float mag = xr[m] * xr[m] + xi[m] * xi[m];
        if (!(mag < 0.25f)) { return 0U; }
    }

    /*
     * SE SUMA, no se sustituye: el panadaptador mide sobre lo ya corregido,
     * asi que esto es el residuo. Ver iqbal.h.
     *
     * Y SE SUMA A MEDIAS. El paso completo mete de golpe todo el ruido de
     * una sola estimacion; con medio paso, el lazo tarda una vuelta mas en
     * llegar -son dos segundos- y el ruido de cada estimacion se promedia
     * con el de las siguientes en vez de acumularse. Es amortiguacion de
     * toda la vida, y aqui se nota porque el lazo corre muchas veces y la
     * prisa no sirve de nada.
     */
    /*
     * *** Y EL TOPE TAMBIEN SOBRE LO ACUMULADO - 26/10/2026. ***
     *
     * El tope de arriba mira el INCREMENTO. Eso basta mientras el lazo
     * este cerrado -cada vuelta mide el residuo y el residuo se encoge-,
     * pero no es una garantia: si por lo que sea entraran medidas que no
     * son el residuo, el acumulado podria crecer sin que nada lo mirara, y
     * un coeficiente grande no corrige espejo, lo FABRICA.
     *
     * Ahora mismo la puerta de arriba cierra el unico camino conocido,
     * asi que esto no deberia saltar jamas. Esta precisamente por eso: es
     * barato -cuatro modulos- y convierte una suposicion en una
     * comprobacion. Y ademas hace verdadero el comentario del paso por
     * int32 de fir_aplica(), que afirmaba que las tomas valen menos de
     * 0,5 apoyandose en un tope que no existia.
     *
     * Si salta, no se aplica NADA de esta vuelta: se deja el FIR como
     * estaba, que es lo ultimo que se sabe bueno.
     */
    for (m = 0U; m < L; m++) {
        float nr = s_fir_re[m] + 0.5f * xr[m];
        float ni = s_fir_im[m] + 0.5f * xi[m];

        if (!((nr * nr + ni * ni) < 0.25f)) { return 0U; }
    }

    for (m = 0U; m < L; m++) {
        s_fir_re[m] += 0.5f * xr[m];
        s_fir_im[m] += 0.5f * xi[m];
    }
    s_fir_hay = 1U;

    /* Y las ranuras a cero: la proxima vuelta tiene que medir el residuo
     * nuevo, no volver a sumar el de antes. */
    for (r = 0U; r < (uint8_t)IQB_RANURAS; r++) {
        s_cal_nre[r] = 0.0f; s_cal_nim[r] = 0.0f;
        s_cal_den[r] = 0.0f; s_cal_fs[r] = 0.0f; s_cal_n[r] = 0U;
    }
    return 1U;
}

/*
 * El FIR sobre el conjugado, en el sitio y sobre el bloque entrelazado.
 *
 *     y(n) = z(n) + suma_m w[m] * conj(z(n-m))
 *
 * La historia se guarda entre bloques: sin eso, las IQB_FIR_TOMAS-1 primeras
 * muestras de cada bloque se corregirian con ceros y el resultado seria un
 * chasquido cada 2,7 ms, que es justo la clase de fallo que se oye y no se
 * ve en ninguna medida de rechazo.
 */
/*
 * EL FIR DEL ESPEJO, FUNDIDO CON EL CORRECTOR PLANO.
 *
 * Una sola pasada sobre el bloque. La primera version hacia dos -el plano
 * escribia el bloque entero de vuelta en int16 y el FIR lo volvia a leer- y
 * ademas corria el historial en memoria para cada muestra. Las dos cosas se
 * pagan dentro de la interrupcion del ADC, que es por donde pasan TODAS las
 * muestras de TODOS los modos. Medido con sim/iq_coste.c: asi costaba 5,1
 * veces el corrector plano solo, por encima del tope de ese banco.
 *
 * El historial vive en registros mientras dura el bloque y solo se guarda al
 * salir. Por eso va desenrollado y por eso hay un _Static_assert: si alguien
 * cambia el numero de tomas, esto tiene que cambiar con el.
 *
 * El corrector plano entra aqui como th y g en vez de como una pasada
 * aparte; cuando no hay medida plana se llama con th=0 y g=1, que es la
 * identidad, y asi no hace falta un if por muestra. De paso el FIR ve la Q
 * corregida SIN recortar a int16 en medio, que es un redondeo menos.
 */
/*
 * RECORTE A 16 BITS.
 *
 * En el Cortex-M4 es UNA instruccion, SSAT; escrita a mano en vez de traer
 * core_cm4.h porque ese arrastra medio CMSIS hasta el simulador y alli se
 * pelea con las intrinsecas del anfitrion (ver hostinc/arm_math.h). En el
 * PC son dos comparaciones y el resultado es el mismo numero.
 *
 * El paso por int32 es seguro y no hace falta comprobarlo en caliente: la
 * entrada es int16, el corrector plano la multiplica por g <= IQB_G_MAX con
 * un sesgo de th <= IQB_THETA_MAX, y el FIR suma cuatro tomas de modulo
 * menor que 0,5 (iqbal_resuelve() rechaza las que no lo cumplan). El mayor
 * valor posible es 32768 * 2 * 1,5 * 3 = 294.912, que cabe de sobra.
 */
/*
 * Y LA FUNCION ESTA EN sat16.h, QUE EXISTE PARA ESTO - 24/10/2026.
 *
 * Aqui habia una copia byte a byte de la de aquel fichero, incluido el
 * ensamblador y el respaldo del PC. La cabecera compartida se escribio el
 * 11/10/2026 justo porque esta operacion estaba repetida y una de las copias
 * se habia quedado sin el SSAT -17 instrucciones por muestra en vez de 13,
 * medido-; agc.c la incluye desde entonces y este fichero se quedo con la
 * suya. Encontrado en la revision a fondo pedida por el usuario.
 */
#include "sat16.h"

_Static_assert(IQB_FIR_TOMAS == 4U, "fir_aplica() va desenrollada a cuatro tomas");

static void fir_aplica(int16_t *crudo, uint32_t n, float th, float g)
{
    const float w0r = s_fir_re[0], w0i = s_fir_im[0];
    const float w1r = s_fir_re[1], w1i = s_fir_im[1];
    const float w2r = s_fir_re[2], w2i = s_fir_im[2];
    const float w3r = s_fir_re[3], w3i = s_fir_im[3];
    float h0r = s_hre[0], h0i = s_him[0];
    float h1r = s_hre[1], h1i = s_him[1];
    float h2r = s_hre[2], h2i = s_him[2];
    float h3r = s_hre[3], h3i = s_him[3];
    uint32_t k;

    for (k = 0U; k < n; k++) {
        float zr = (float)crudo[2U * k];
        float zi = g * ((float)crudo[2U * k + 1U] - th * zr);
        float yr, yi;

        /* La mas nueva entra la primera; las demas se corren. */
        h3r = h2r; h3i = h2i;
        h2r = h1r; h2i = h1i;
        h1r = h0r; h1i = h0i;
        h0r = zr;  h0i = zi;

        /* y = z + suma de w[m] * conj(h[m]) */
        yr = zr + w0r * h0r + w0i * h0i
                + w1r * h1r + w1i * h1i
                + w2r * h2r + w2i * h2i
                + w3r * h3r + w3i * h3i;
        yi = zi + w0i * h0r - w0r * h0i
                + w1i * h1r - w1r * h1i
                + w2i * h2r - w2r * h2i
                + w3i * h3r - w3r * h3i;

        crudo[2U * k]      = sat16((int32_t)yr);
        crudo[2U * k + 1U] = sat16((int32_t)yi);
    }

    s_hre[0] = h0r; s_him[0] = h0i;
    s_hre[1] = h1r; s_him[1] = h1i;
    s_hre[2] = h2r; s_him[2] = h2i;
    s_hre[3] = h3r; s_him[3] = h3i;
}

/* Uno de cada ocho. Ver iqbal.h. */
#define IQB_UNO_DE   8U

void iqbal_bloque(int16_t *crudo, uint32_t n)
{
#ifdef SIN_IQ
    /* El interruptor de diagnostico: esto es lo que corre DENTRO de la
     * interrupcion del DMA, o sea lo primero que avisos.mk dice que
     * SINIQ=1 quita. Ver iqbal_init(). */
    (void)crudo; (void)n;
    return;
#else
    uint32_t k;
    float th, g;

    if (n == 0U) { return; }

    /* El plazo de la prueba A/B. Aqui y no en iqbal_ab_paso(), porque el
     * fallo que lo trajo era justo que a iqbal_ab_paso() no la llamaba
     * nadie. Ver iqbal.h. */
    if (s_ab_est != AB_PARADA) {
        s_ab_plazo++;
        if (s_ab_plazo >= IQB_AB_PLAZO) { ab_aborta(); }
    }

    if (s_turno == 0U && s_aprende) {
        /*
         * La medida sobre el entrelazado. Es la misma cuenta que
         * iqbal_mide() -incluido quitar la continua- pero recorriendo de dos
         * en dos, para no tener que copiar el bloque a dos arrays sueltos
         * dentro de la interrupcion.
         */
        float mi = 0.0f, mq = 0.0f, ii = 0.0f, qq = 0.0f, iq = 0.0f;
        float inv = 1.0f / (float)n;

        for (k = 0U; k < n; k++) {
            mi += (float)crudo[2U * k];
            mq += (float)crudo[2U * k + 1U];
        }
        mi *= inv; mq *= inv;
        for (k = 0U; k < n; k++) {
            float a = (float)crudo[2U * k]      - mi;
            float b = (float)crudo[2U * k + 1U] - mq;
            ii += a * a; qq += b * b; iq += a * b;
        }
        ii *= inv; qq *= inv; iq *= inv;

        s_ii += IQB_ALFA * (ii - s_ii);
        s_qq += IQB_ALFA * (qq - s_qq);
        s_iq += IQB_ALFA * (iq - s_iq);
        s_bloques++;

        /*
         * LA GUARDA DE LA PAREJA SIMETRICA, SOBRE EL PROMEDIO Y NO SOBRE EL
         * BLOQUE. Ver IQB_PAR_MAX.
         *
         * Y SOBRE EL PROMEDIO NO ES UN DETALLE: la primera version miraba el
         * bloque suelto y DEJABA PASAR. Con 256 muestras los productos
         * cruzados de las emisoras que NO forman pareja no promedian a cero
         * del todo, asi que la razon baila de bloque a bloque alrededor de su
         * valor medio; un tope duro sobre un numero que baila deja colarse
         * justo los bloques que caen por debajo, y esos estan sesgados todos
         * hacia el mismo lado. Medido: de 2.580 bloques se colaban 246, y con
         * esos 246 el estimador salia con g = 1,175 cuando lo cierto era
         * 0,912. El promedio no baila, y ahi el tope hace lo que dice.
         *
         * Las medias se siguen actualizando SIEMPRE -son la mejor estimacion
         * que hay de lo que entra-; lo que la guarda decide es si ese numero
         * se puede creer o no.
         */
        /* Y la cuenta, en resuelve(), que es la MISMA que usa iqbal_mide().
         * Estaban escritas dos veces y ya se habian separado una vez; ver
         * el comentario de aquella funcion. */
        {
            uint8_t pareja = 0U;

            if (!resuelve(&pareja) && pareja && s_pares < 0xFFFFFFFFUL) {
                s_pares++;
            }
        }
    }
    /*
     * ==================================================================
     * EL TURNO SE QUEDA CLAVADO, Y ESO SE DECIDIO MIDIENDO - 26/10/2026.
     * ==================================================================
     *
     * EL RAZONAMIENTO QUE LLEVO A TOCARLO, que sigue siendo cierto: esto
     * corre DENTRO de la interrupcion de audio, y la medida de arriba
     * cuesta el triple que la correccion -13 instrucciones por muestra
     * todos los bloques mas 24 por muestra uno de cada ocho-. En FM ancha
     * son 6.656 ciclos de base y 13.512 de mas en el bloque que toca, y
     * uno de cada ocho clavados son 46,875 Hz sin vaiven ninguno. Este
     * proyecto tiene escrito -busca "EL VAIVEN DEL FOTOGRAMA" en main.c-
     * que una perturbacion periodica enganchada a un numero entero de
     * bloques apila su energia en unas pocas rayas y el oido las junta en
     * un tono.
     *
     * POR QUE NO SE TOCA, DE TODAS FORMAS. Se sorteo el turno entre 5 y
     * 11 bloques, con la misma media de ocho, y pasaron dos cosas:
     *
     *   1. La bateria se puso ROJA. sim/iqanchotest.c compara el
     *      corrector plano con el de banda ancha y exige que el ancho no
     *      quede mas de 1 dB por debajo. Con el turno sorteado los
     *      bloques que miden caen sobre otros trozos de la señal, el
     *      PLANO converge mejor -de 34,4 a 35,2 dB en 5.000 Hz- y el
     *      hueco entre los dos se abre a 1,3 dB. No es que el receptor
     *      empeore: es que la comparacion era fragil y el sorteo la
     *      cruza.
     *
     *   2. Y sobre todo: el presupuesto dice que ese escalon NO PUEDE
     *      hacer daño. Contadas una a una las instrucciones del
     *      desensamblado, la interrupcion de WFM gasta del 17 al 21 % de
     *      sus 533.333 ciclos en regimen, y del 45 % en el peor caso con
     *      todo encendido. sim/pingpong.c midio que el transporte de
     *      audio no falla ni una vez en cien mil bloques por debajo del
     *      85 % del presupuesto. Un escalon del 2,5 al 3,3 % que va y
     *      viene sobre un 21 % no se acerca a ninguna frontera.
     *
     * O sea: la regla del vaiven es buena, pero se aplica donde el
     * enganche puede morder. Aqui no muerde, y aplicarla costaba una
     * comprobacion en verde. Primero se mide.
     */
    s_turno = (uint8_t)((s_turno + 1U) % IQB_UNO_DE);

    if (!s_on) { return; }

    th = s_hay ? s_theta : 0.0f;
    g  = s_hay ? s_g     : 1.0f;

    /*
     * EL CORRECTOR PLANO Y EL FIR, QUE SON UNO SOLO.
     *
     * El plano quita la parte del desequilibrio que es igual en toda la
     * banda -que es la mayor- y el FIR se ocupa de lo que varia con la
     * frecuencia, que es lo que aquel no puede. Por eso el FIR es un
     * añadido y no un reemplazo, y por eso va despues: trabaja sobre lo que
     * el plano ha dejado.
     *
     * Van fundidos en una sola pasada porque esto corre dentro de la
     * interrupcion del ADC. Mientras no haya calibracion, s_fir_hay vale
     * cero, se toma la rama de abajo y la radio se comporta EXACTAMENTE
     * como antes de que el FIR existiera.
     */
    if (s_fir_hay) {
        fir_aplica(crudo, n, th, g);
        return;
    }

    if (s_hay) {
        for (k = 0U; k < n; k++) {
            float v = g * ((float)crudo[2U * k + 1U] - th * (float)crudo[2U * k]);
            crudo[2U * k + 1U] = sat16((int32_t)v);
        }
    }
#endif
}

/* --- el espejo leido del espectro (ver iqbal.h) ------------------------- */

/* Bins pegados al centro que se saltan: ahi viven la continua y la fuga del
 * propio LO, que no son señal y cuyo "espejo" son ellos mismos. */
#define IQB_GUARDA   4

/* Cuanto tiene que sobresalir el pico sobre el suelo para que la medida
 * signifique algo. Comparar un pico de ruido con otro pico de ruido da un
 * numero perfectamente formado que no dice nada. */
#define IQB_SOBRE_DB 12.0f

void iqbal_mira_espectro(const float *db, uint16_t n)
{
#ifdef SIN_IQ
    (void)db; (void)n;   /* ver iqbal_init() */
    return;
#else
    uint16_t c, k, mejor = 0U;
    float pico = -1e30f, suelo, dif;

    if (db == 0 || n < 32U) { return; }
    c = (uint16_t)(n / 2U);

    /* El pico mas alto que no sea el centro. */
    for (k = 0U; k < n; k++) {
        int32_t d = (int32_t)k - (int32_t)c;
        if (d < 0) { d = -d; }
        if (d < IQB_GUARDA) { continue; }
        /* y que su espejo este dentro del array */
        if ((uint16_t)(2U * c) < k || (2U * c - k) >= n) { continue; }
        if (db[k] > pico) { pico = db[k]; mejor = k; }
    }
    if (pico < -1e29f) { return; }

    /*
     * El suelo, por MEDIANA aproximada y no por media: con una portadora
     * fuerte dentro, la media del espectro la arrastra y el "sobresale 12 dB"
     * deja de medir lo que dice. Se hace contando cuantos bins quedan por
     * debajo de un valor que se va ajustando, que sale mas barato que ordenar
     * doscientos cincuenta y seis numeros en el bucle de pantalla.
     */
    {
        float lo = 1e30f, hi = -1e30f, m;
        uint16_t it;
        for (k = 0U; k < n; k++) {
            if (db[k] < lo) { lo = db[k]; }
            if (db[k] > hi) { hi = db[k]; }
        }
        for (it = 0U; it < 12U; it++) {
            uint16_t bajo = 0U;
            m = 0.5f * (lo + hi);
            for (k = 0U; k < n; k++) { if (db[k] < m) { bajo++; } }
            if (bajo * 2U < n) { lo = m; } else { hi = m; }
        }
        suelo = 0.5f * (lo + hi);
    }

    if (pico < suelo + IQB_SOBRE_DB) {
        /*
         * EL COMENTARIO MENTIA Y EL BIN SE QUEDABA COLGADO - revision a
         * fondo del 08/10/2026.
         *
         * Aqui ponia "se deja el anterior y se marca que no hay", y el
         * codigo no deja el anterior: lo pisa con cero. El codigo es el
         * que tiene razon -el cero ES la marca de "no hay", y la prueba
         * A/B se apoya en ella con el `if (s_esp_db > 0.0f)` de
         * iqbal_ab_paso() para no promediar cuadros sin señal-, asi que lo
         * que se arregla es el texto, no la cuenta.
         *
         * Y faltaba la otra mitad: s_esp_bin no se tocaba. La pagina de
         * Informacion enseña los dos numeros JUNTOS, asi que salia "0 dB
         * de rechazo" al lado del bin de un pico de hace minutos y de otra
         * frecuencia, que es una pareja que no ha existido nunca. Un cero
         * en los dos dice lo que pasa: no hay medida.
         */
        s_esp_db  = 0.0f;
        s_esp_bin = 0;
        return;
    }

    /*
     * Y el espejo: el bin simetrico respecto al centro. Se cogen tres y se
     * queda el mas alto, porque el pico de la señal y el de su imagen no
     * caen siempre en el mismo bin exacto -el promediado de pantalla los
     * desplaza medio bin- y quedarse corto aqui inventaria rechazo de mas.
     */
    {
        uint16_t e = (uint16_t)(2U * c - mejor);
        float esp = -1e30f;
        int16_t j;
        for (j = -1; j <= 1; j++) {
            int32_t x = (int32_t)e + j;
            if (x < 0 || x >= (int32_t)n) { continue; }
            if (db[x] > esp) { esp = db[x]; }
        }
        /* Si el espejo esta en el suelo, lo que se mide es el suelo: el
         * rechazo es "por lo menos esto", y se dice con ese valor. */
        if (esp < suelo) { esp = suelo; }
        dif = pico - esp;
    }

    if (dif < 0.0f) { dif = 0.0f; }
    s_esp_db  = dif;
    s_esp_bin = (int16_t)((int32_t)mejor - (int32_t)c);
#endif
}

/* --- la prueba A/B (ver iqbal.h) ---------------------------------------- */

/*
 * Cuadros que se esperan antes de empezar a medir en cada fase. La pantalla
 * promedia dentro del cuadro Y ADEMAS mezcla cada cuadro con el anterior, asi
 * que la medida de justo despues de tocar el corrector todavia lleva dentro
 * lo de antes. Veinte cuadros son dos tercios de segundo: de sobra para que
 * no quede nada de la fase anterior.
 */
void iqbal_ab_lanza(void)
{
#ifdef SIN_IQ
    return;   /* sin corrector no hay nada que comparar; ver iqbal_init() */
#else
    if (s_ab_est != AB_PARADA) { return; }   /* ya hay una en marcha */
    s_ab_antes = s_on;
    s_ab_n     = 0U;
    s_ab_c_off = 0U; s_ab_c_on = 0U;
    s_ab_s_off = 0.0f; s_ab_s_on = 0.0f;
    s_ab_hay   = 0U;
    s_ab_plazo = 0U;
    s_on       = 0U;
    iqa_set_on(0U);
    /*
     * Y EL ESTADO, EL ULTIMO - revision a fondo del 08/10/2026.
     *
     * Aqui s_ab_est se ponia el PRIMERO, y eso abria la prueba en canal
     * antes de que estuviera preparada. La interrupcion del DMA mira
     * exactamente esa variable para decidir si cuenta el plazo, asi que
     * entre esa linea y el `s_ab_plazo = 0U` de abajo habia unos cuantos
     * bloques de audio de ventana en los que la interrupcion contaba el
     * plazo CON EL CONTADOR DE LA PRUEBA ANTERIOR. Si aquella habia
     * vencido por plazo, el contador valia ya 2.500 y el primer bloque que
     * entrase abortaba esta nada mas lanzarla.
     *
     * Y abortar entonces no es solo perder la medida: ab_aborta() devuelve
     * s_on a como estaba, y acto seguido este codigo sigue por donde iba y
     * escribe s_on = 0 y iqa_set_on(0) para empezar la fase de "apagado"
     * de una prueba que ya no existe. Resultado: EL CORRECTOR DE ESPEJO
     * APAGADO PARA SIEMPRE, sin nadie que lo vuelva a encender y sin que
     * la pantalla diga nada, porque iqbal_ab_corriendo() contesta que no
     * hay prueba. Lo que se veia como un rechazo que desaparecia
     * era esto.
     *
     * Poniendo el estado al final, la interrupcion no puede ver la prueba
     * en marcha hasta que todo lo demas -plazo incluido- ya esta puesto.
     */
    s_ab_est   = AB_OFF_ESPERA;
#endif
}

/* Aborta la prueba y devuelve el corrector a como estaba. Se llama sola
 * cuando vence el plazo; no deja resultado a proposito, porque una prueba
 * que no ha terminado no ha medido nada. */
static void ab_aborta(void)
{
    s_ab_gen++;
    s_on     = s_ab_antes;
    iqa_set_on(s_ab_antes);
    s_ab_est = AB_PARADA;
    s_ab_n   = 0U;
    s_ab_hay = 0U;
    /*
     * Y EL PLAZO A CERO - revision a fondo del 08/10/2026.
     *
     * Faltaba, y era la mitad de la averia que cuenta iqbal_ab_lanza():
     * una prueba que vencia por plazo dejaba el contador clavado en 2.500,
     * y como solo lo ponia a cero iqbal_ab_lanza() -y encima despues de
     * abrir la prueba-, la siguiente se abortaba sola al primer bloque.
     * Dejarlo limpio aqui, en el mismo sitio que apaga la prueba, es lo
     * que hace que esto no dependa del orden en que escriba el otro lado.
     */
    s_ab_plazo = 0U;
}

uint8_t iqbal_ab_corriendo(void)
{
    if (s_ab_est == AB_OFF_ESPERA || s_ab_est == AB_OFF_MIDE) { return 1U; }
    if (s_ab_est == AB_ON_ESPERA  || s_ab_est == AB_ON_MIDE)  { return 2U; }
    return 0U;
}

uint8_t iqbal_ab_hay(void) { return s_ab_hay; }
float   iqbal_ab_off(void) { return s_ab_off; }
float   iqbal_ab_on(void)  { return s_ab_on;  }

void iqbal_ab_paso(void)
{
#ifdef SIN_IQ
    return;   /* ver iqbal_init() */
#else
    uint16_t gen;

    if (s_ab_est == AB_PARADA) { return; }
    /*
     * La generacion CON LA QUE SE ENTRA. Si al salir no es la misma, la
     * interrupcion aborto por el medio y lo que se ha escrito aqui hay que
     * deshacerlo: ver el guardian del final.
     */
    gen = s_ab_gen;
    s_ab_n++;

    switch (s_ab_est) {
    case AB_OFF_ESPERA:
        if (s_ab_n >= IQB_AB_ESPERA) { s_ab_est = AB_OFF_MIDE; s_ab_n = 0U; }
        break;

    case AB_OFF_MIDE:
        /* Solo cuentan los cuadros en los que habia una señal clara: ver
         * iqbal_mira_espectro(), que devuelve 0 cuando no la hay. Promediar
         * ceros rebajaria el resultado por no haber nada que medir, que es
         * la forma mas facil de hacer creer que el corrector gana. */
        if (s_esp_db > 0.0f) { s_ab_s_off += s_esp_db; s_ab_c_off++; }
        if (s_ab_n >= IQB_AB_MIDE) {
            s_ab_est = AB_ON_ESPERA; s_ab_n = 0U;
            s_on = 1U; iqa_set_on(1U);
        }
        break;

    case AB_ON_ESPERA:
        if (s_ab_n >= IQB_AB_ESPERA) { s_ab_est = AB_ON_MIDE; s_ab_n = 0U; }
        break;

    case AB_ON_MIDE:
        if (s_esp_db > 0.0f) { s_ab_s_on += s_esp_db; s_ab_c_on++; }
        if (s_ab_n >= IQB_AB_MIDE) {
            /*
             * Y el corrector vuelve a como estaba, pase lo que pase. Dejarlo
             * donde lo dejo la prueba seria cambiarle la radio al usuario por
             * haber pedido una medida.
             */
            s_on     = s_ab_antes;
            iqa_set_on(s_ab_antes);
            s_ab_est = AB_PARADA;
            s_ab_n   = 0U;
            /* Sin cuadros validos en alguna de las dos fases no hay
             * resultado: media de nada es cero, y un cero aqui se leeria
             * como "cero dB de rechazo". */
            if (s_ab_c_off >= 3U && s_ab_c_on >= 3U) {
                s_ab_off = s_ab_s_off / (float)s_ab_c_off;
                s_ab_on  = s_ab_s_on  / (float)s_ab_c_on;
                s_ab_hay = 1U;
            } else {
                s_ab_hay = 0U;
            }
        }
        break;

    default:
        s_ab_est = AB_PARADA;
        break;
    }

    /*
     * ==================================================================
     * Y SI LA INTERRUPCION HA ABORTADO MIENTRAS SE CORRIA ESTO, SE
     * RESPETA. 26/10/2026, revision a fondo de espejos y armonicos.
     * ==================================================================
     *
     * ab_aborta() corre en la interrupcion del DMA y puede caer entre el
     * `switch (s_ab_est)` de arriba y la escritura de la rama que se
     * eligio. Cuando pasa, esto escribe DESPUES de que ab_aborta() haya
     * dejado el corrector como estaba, y resucita una prueba ya abortada:
     * vuelve a poner s_ab_est a una fase y s_on a uno, con el plazo ya a
     * cero, y iqbal_ab_corriendo() dice que hay prueba otros seis segundos
     * largos hasta que se aborta otra vez, sin resultado.
     *
     * Hoy el daño queda ahi porque s_ab_antes vale siempre 1 -nadie apaga
     * el corrector desde la interfaz-, asi que lo que se resucita deja el
     * corrector encendido igual. Pero `volatile` no es un cerrojo y la
     * ventana no son dos instrucciones como decia la cabecera: es la
     * maquina de estados entera.
     *
     * No hace falta un cerrojo: basta con mirar DESPUES si la cuenta de
     * abortos ha cambiado. El plazo NO vale para esto -vale cero tanto
     * despues de abortar como recien lanzada la prueba-, y el estado
     * tampoco, porque es precisamente lo que esta funcion acaba de
     * escribir. Por eso s_ab_gen.
     */
    if (gen != s_ab_gen) {
        s_ab_est = AB_PARADA;
        s_ab_n   = 0U;
        s_ab_hay = 0U;
        s_on     = s_ab_antes;
        iqa_set_on(s_ab_antes);
    }
#endif
}

float   iqbal_espejo_db(void)  { return s_esp_db; }
int16_t iqbal_espejo_bin(void) { return s_esp_bin; }

float iqbal_theta(void) { return s_theta; }
float iqbal_g(void)     { return s_g; }

/*
 * DE LOS DOS COEFICIENTES A LOS DOS NUMEROS QUE SE ENTIENDEN.
 *
 * Lo que entra por la antena, con el convenio de siempre -toda la culpa en
 * Q-, es I = cos(wt) y Q = a*sin(wt+phi). Entonces
 *
 *     theta = E[IQ]/E[II]       = a*sin(phi)
 *     Q1    = Q - theta*I       = a*cos(phi)*sin(wt)
 *     g     = raiz(E[II]/E[Q1Q1]) = 1 / (a*cos(phi))
 *
 * y de ahi salen los dos de vuelta, EXACTOS y no aproximados:
 *
 *     tan(phi) = theta * g          -> phi
 *     a        = raiz(theta^2 + 1/g^2)
 *
 * Esto no es un detalle de presentacion. La primera version devolvia
 * atan(theta) a secas, que es a*sin(phi) y no sin(phi): con 3,00 grados de
 * verdad y 0,8 dB de ganancia, enseñaba 3,91. El numero que sale en la
 * pantalla de Informacion tiene que ser el que uno pueda comparar con la
 * hoja de caracteristicas de un mezclador, no uno parecido.
 */
float iqbal_fase_grados(void)
{
    return atanf(s_theta * s_g) * (180.0f / 3.14159265358979f);
}

float iqbal_gan_db(void)
{
    float a;
    if (s_g <= 0.0f) { return 0.0f; }
    a = sqrtf(s_theta * s_theta + 1.0f / (s_g * s_g));
    if (a <= 0.0f) { return 0.0f; }
    return 20.0f * log10f(a);
}

float iqbal_rechazo_db(void)
{
    /*
     * Con un error de ganancia e (lineal, g = 1+e) y uno de fase phi, el
     * espejo queda en
     *
     *     IRR = (e^2 + phi^2) / 4        (en potencia, para errores pequeños)
     *
     * que es la aproximacion clasica y vale de sobra en el margen que nos
     * importa -por debajo de cinco grados y un dB-. Se devuelve en dB y
     * positivo: cuanto mas alto, menos espejo.
     */
    float e   = sqrtf(s_theta * s_theta + 1.0f / (s_g * s_g)) - 1.0f;
    float phi = atanf(s_theta * s_g);
    float p   = (e * e + phi * phi) * 0.25f;

    if (!s_hay) { return 0.0f; }
    if (p < 1e-12f) { p = 1e-12f; }
    return -10.0f * log10f(p);
}

uint32_t iqbal_bloques(void) { return s_bloques; }
uint32_t iqbal_pares(void)    { return s_pares; }
