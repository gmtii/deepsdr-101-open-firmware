#include "dcf77.h"
#include "hora_rbu.h"
#include "hora_wwv.h"
#include "hora_comun.h"
#include <math.h>

/* Ver dcf77.h para el formato y el porque de cada decision. Aqui el como. */

/* Ventanas de duracion, en milisegundos. Anchas a proposito: el ruido
 * desplaza los flancos unos pocos ms y el emisor tiene su propia tolerancia.
 * Lo que NO puede pasar es que las dos se solapen, porque entonces un bit
 * seria de los dos a la vez. */
#define BIT0_MIN_MS   50.0f
#define BIT0_MAX_MS  150.0f
#define BIT1_MIN_MS  150.0f
#define BIT1_MAX_MS  260.0f

/*
 * PUNTOS DE MUESTREO, en ms desde la bajada que abre el segundo.
 *
 * Hasta el 23/09/2026 el bit de DCF77 salia de MEDIR la duracion de la
 * marca. Al añadir MSF eso dejo de valer, porque MSF mete dos bits en el
 * mismo segundo y con A=0,B=1 hay dos apagones separados: no hay "una"
 * duracion que medir. La forma que funciona para las dos es MIRAR EL ESTADO
 * de la portadora en instantes fijos, que ademas es mas robusta -un flanco
 * de subida con ruido se mueve, pero mirar en mitad de una meseta no-.
 *
 * DCF77: a 150 ms. La marca corta (100 ms) ya ha terminado -> 0; la larga
 *        (200 ms) sigue -> 1.
 * MSF:   a 150 ms el bit A, a 250 ms el bit B, y a 400 ms se sabe si es la
 *        marca de minuto (500 ms).
 */
#define MUESTRA_A_MS   150.0f
#define MUESTRA_B_MS   250.0f
#define MUESTRA_MIN_MS 400.0f

/* Entre dos bajadas hay un segundo; entre la de antes del hueco y la de
 * despues, dos. */
#define SEG_MIN_MS   800.0f
#define SEG_MAX_MS  1200.0f
#define HUECO_MIN_MS 1800.0f
#define HUECO_MAX_MS 2200.0f

/* La portadora baja al ~15%. El umbral va al 50%, bien lejos de los dos
 * niveles, con histeresis para que el ruido no haga tiritar el flanco. */
#define UMBRAL_BAJA   0.50f
#define UMBRAL_SUBE   0.60f

/* Seguidor de pico del nivel de portadora. Sube al instante y baja despacio:
 * el nivel alto es la referencia y las bajadas son la informacion, asi que
 * el seguidor no debe perseguirlas. La constante es de varios segundos. */
#define PICO_CAIDA_S  8.0f

#define CONTRASTE_MIN 0.25f  /* por debajo de esto no hay DCF77, hay ruido */

/*
 * FILTRO DE LA ENVOLVENTE, y no es un adorno: es lo que decide cuanto ruido
 * aguanta esto.
 *
 * Toda la informacion del DCF77 esta en marcas de 100 y 200 ms, o sea por
 * debajo de unos 10 Hz. La envolvente que llega trae ademas el ruido de los
 * 96 kHz enteros de entrada. Umbralizarla en crudo -que es lo que hacia la
 * primera version- significa dejar que ese ruido cruce el umbral en los dos
 * sentidos y fabrique flancos que no existen: el banco pasaba de sincronizar
 * con sigma 60 a no sincronizar con sigma 120, y no por falta de señal sino
 * por flancos inventados.
 *
 * El corte NO esta elegido a ojo: sim/dcf77test.c lo barre midiendo hasta
 * cuanto ruido aguanta el decodificador. El resultado (ruido maximo que
 * todavia sincroniza, en % de la profundidad de la marca):
 *
 *      sin filtro   ~7%        25 Hz  ~18%
 *      50 Hz   ~7%             10 Hz  ~35% (ya con tramas descartadas)
 *      35 Hz  ~14%           3..8 Hz  ~35% (limpio, sin descartes)
 *
 * O sea que hay una MESETA entre 3 y 8 Hz donde da igual el valor exacto, y
 * por encima de 10 Hz empieza a caer. Se coge 5, en mitad de la meseta, que
 * es donde un valor aguanta que el mundo real no se parezca del todo al
 * banco. Por abajo tampoco conviene pasarse: a 3 Hz el tiempo de subida
 * (~110 ms) ya iguala a la marca mas corta, y la marca empezaria a perder
 * profundidad.
 *
 * El retardo que mete el filtro es el mismo para todos los flancos, asi que
 * no altera NINGUNA duracion medida: las duraciones salen de restar dos
 * flancos y el retardo se cancela.
 */
#ifndef LPF_HZ
#define LPF_HZ 5.0f   /* ver el barrido de arriba */
#endif

/* Febrero con 29 a proposito: aqui solo se usa para descartar una trama
 * imposible, y rechazar un 29 de febrero valido seria peor que aceptar un
 * 29 de febrero de un año que no es bisiesto. */
/* La tabla de dias por mes vive en hora_comun.c desde el 23/09/2026: la
 * necesitan tambien RBU y WWV, y tres copias de un calendario es como se
 * acaba teniendo febrero de 29 dias en dos sitios y de 28 en el tercero. */
#define k_dias_mes k_hora_dias_mes

static float          s_fs = 1.0f;
static uint8_t        s_activo = 0U;
static hora_emisora_t s_emisora = HORA_DCF77;

/* Lo que se ha leido en los puntos de muestreo del segundo en curso. */
static uint8_t s_bit_a, s_bit_b, s_marca_min;
static uint8_t s_hecho_a, s_hecho_b, s_hecho_min;

/* MSF: los dos bits de cada uno de los 60 segundos del minuto. */
static uint8_t s_msf_a[60], s_msf_b[60];
static uint8_t s_msf_seg;        /* segundo en curso, 0..59 */
static uint8_t s_msf_enganchado; /* ya se ha visto una marca de minuto */
static uint8_t s_msf_sospechosa; /* algo raro en este minuto: no aceptarlo */

static uint16_t s_ms_marca, s_ms_ciclo, s_marcas_raras;

/* ------------------------- ALS162: el frente de fase --------------------
 *
 * Lo que llega es la fase de la portadora, que trae DOS cosas encima del
 * elemento triangular que interesa:
 *
 *   1. UN GIRO CONSTANTE, porque el oscilador local nunca cae exactamente
 *      en la frecuencia de la emisora. A 162 kHz el oscilador lo genera el
 *      temporizador del micro con pasos de unos 190 Hz, asi que ese giro
 *      puede ser de cientos de vueltas por segundo. Es enorme comparado con
 *      el radian que hay que medir.
 *   2. Saltos de +-2*pi al desenrollar.
 *
 * Asi que primero se desenrolla, y luego se le quita la deriva con un paso
 * alto de constante larga: el giro del oscilador es practicamente constante
 * a lo largo de un segundo y el triangulo dura 100 ms, o sea que estan bien
 * separados en frecuencia y un solo polo basta para dejar el triangulo
 * limpio sobre cero.
 *
 * COMO SE QUITA LA DERIVA, que es donde estaba el fallo de la primera
 * version. Lo obvio es un paso alto sobre la FASE, y no vale: un error de
 * oscilador es una RAMPA en fase, y un paso alto de un polo deja delante de
 * una rampa un residuo constante igual a pendiente por constante de tiempo.
 * Con un error de solo 1 Hz ese residuo ya valia medio radian -mas que el
 * umbral- y el detector se quedaba pegado arriba para siempre. El banco lo
 * caza a la primera: sin deriva sincronizaba y con 1 Hz ya no.
 *
 * Lo que si vale es trabajar sobre la DERIVADA de la fase. Ahi el error del
 * oscilador es una CONSTANTE, y quitarle su media lo elimina exacto, no
 * aproximado. Y de paso la derivada del triangulo es una onda cuadrada
 * -sube, baja el doble, sube-, que se integra despues para recuperar el
 * triangulo ya sin rampa.
 *
 * El elemento se reconoce por su FORMA, no por su altura: sube por encima
 * del umbral y vuelve. Inmune a que la amplitud no sea exactamente 1 radian,
 * que no lo va a ser -la propagacion y la integracion con fuga se comen algo-.
 */
#define FASE_DERIVA_HZ  0.5f    /* seguimiento del error de oscilador */
#define FASE_FUGA_HZ    2.0f    /* fuga del integrador que rehace el triangulo */
/*
 * Corte del paso bajo del triangulo. Barrido con sim/alstest.c: por debajo
 * de 3 Hz se come el elemento y no sincroniza nunca; de 6 a 35 Hz el
 * resultado es identico (aguanta 0,15 rad de ruido de fase). Se coge 12, en
 * medio de esa meseta.
 *
 * Y ese 0,15 rad es el limite DURO de esta emisora, no un ajuste: con mas
 * ruido el propio atan2 empieza a dar vueltas de mas y el desenrollado
 * pierde ciclos. Es la diferencia entre leer una fase y leer una envolvente
 * - MSF aguanta la mitad de su marca en ruido, aqui no hay tanto margen. A
 * cambio, ALS162 emite con 800 kW contra los 17 de MSF.
 */
#ifndef TRI_LPF_HZ
#define TRI_LPF_HZ      12.0f
#endif
#define FASE_UMBRAL     0.35f   /* radianes; el elemento vale 1, sobra margen */
#define ELEM_VENTANA_MS 260.0f  /* dos elementos caben en 200; 260 da aire */

static float   s_fase_prev;      /* para desenrollar */
static float   s_deriva;         /* error de oscilador, en rad por muestra */
static float   s_tri;            /* el triangulo reconstruido, ya sin rampa */
static float   s_tri_lpf;        /* y filtrado: integrar ruido es acumularlo */
static float   s_a_tri;
/* Pico a pico del triangulo en el ultimo segundo. Es LA medida de si hay
 * señal de verdad en una emisora de fase: el nivel de portadora no sirve
 * -puede estar lleno de ruido de banda ancha-, y con el elemento de verdad
 * este numero se va a ~2 radianes. Sin el, las dos barras de la pantalla no
 * median NADA en ALS162 y no habia forma de distinguir "no hay señal" de
 * "hay un fallo": las dos se veian igual. */
static float   s_tri_max, s_tri_min, s_tri_pp;
static uint32_t s_tri_n;
/* Periodo refractario tras contar un elemento. El integrador con fuga
 * rebota despues del triangulo, y con ruido ese rebote cruza el umbral y se
 * cuenta como un elemento que no existe. Los dos elementos de un "1" estan
 * separados 100 ms, asi que 60 ms de silencio no pierden ninguno de verdad
 * y se comen el rebote. */
#define ELEM_REFRACTARIO_MS 60.0f
static uint32_t s_elem_ultimo_ms;
static float   s_a_deriva;       /* que rapido se sigue la deriva */
static float   s_a_fuga;         /* fuga del integrador */
static uint8_t s_fase_arriba;    /* 1 si la fase esta por encima del umbral */
static uint8_t s_elem_n;         /* elementos vistos en el segundo en curso */
static uint8_t  s_elem_cerrado;  /* el bit de este segundo ya se ha guardado */
static uint8_t s_fase_lista;     /* el paso alto ya se ha asentado */
static uint32_t s_fase_n;

/*
 * TODO lo que distingue a una emisora de otra, en UNA tabla y en UNA fila:
 * donde esta, como se llama, si se lee de la fase o de la envolvente, y a
 * que ritmo hay que alimentarla. Antes esto estaba repartido entre esta
 * tabla, un "e == HORA_ALS162" aqui y un "?4:1" en main.c, y cada emisora
 * nueva era tres sitios donde acordarse. Ahora es una fila.
 *
 * El _Static_assert de abajo es el que impide que se añada una emisora al
 * enum y se olvide la fila: sin el, el compilador rellenaria con ceros y la
 * emisora nueva se sintonizaria en 0 Hz sin decir nada.
 */
static const struct {
    uint32_t hz;
    const char *nombre;
    uint8_t fase;        /* 1 = se lee de la fase de la portadora */
    uint8_t sub;         /* muestras por bloque que hay que darle */
    uint8_t nivel_propio;/* 1 = su `nivel` ES el de la portadora */
} k_emisoras[] = {
    {    77500UL, "DCF77 - 77,5 kHz, Alemania",  0U, 1U, 1U },
    {    60000UL, "MSF - 60 kHz, Reino Unido",   0U, 1U, 1U },
    {   162000UL, "ALS162 - 162 kHz, Francia",   1U, 4U, 0U },
    /* 66 2/3 kHz de verdad son 66.666,67. Se redondea a 66.667 porque el
     * sintetizador va en hercios enteros; los 0,33 Hz que se pierden estan
     * cuarenta veces por debajo del paso del propio temporizador. */
    {    66667UL, "RBU - 66,66 kHz, Rusia",      1U, 8U, 0U },
    { 10000000UL, "WWV - 10 MHz, EEUU",          0U, 4U, 0U },
    { 15000000UL, "WWV - 15 MHz, EEUU",          0U, 4U, 0U },
    /* Misma frecuencia que WWV-10 a proposito: es otra emisora con otro
     * formato en el mismo sitio del dial, y la unica forma de probar las dos
     * es tenerlas las dos. BPM ademas emite las 24 horas. */
    { 10000000UL, "BPM - 10 MHz, China",         0U, 4U, 0U },
    /* Frecuencia 0 = "no retunees, quedate en la FM que estes escuchando".
     * Ver el comentario de HORA_RDS en dcf77.h: esta fila existe para que la
     * pantalla de hora pueda nombrarlo y listarlo, pero el RDS no pasa por
     * este fichero - lo decodifica User/rds.c. */
    {        0UL, "RDS - la FM que escuches",   0U, 1U, 0U }
};
_Static_assert((int)(sizeof(k_emisoras) / sizeof(k_emisoras[0])) == (int)HORA_N,
               "k_emisoras[] y hora_emisora_t se han desincronizado");
/* El `sub` de cada fila tiene que dividir al bloque de RX. Esa comprobacion
 * NO esta aqui sino en demod_am.c, que es quien conoce el tamaño del bloque:
 * este fichero no incluye nada del GD32 a proposito, para que el simulador
 * del PC lo pueda compilar tal cual. */

uint8_t dcf77_usa_fase(hora_emisora_t e)
{
    return (e < HORA_N) ? k_emisoras[e].fase : 0U;
}

uint8_t dcf77_submuestras(hora_emisora_t e)
{
    return (e < HORA_N) ? k_emisoras[e].sub : 1U;
}

uint8_t dcf77_nivel_propio(hora_emisora_t e)
{
    return (e < HORA_N) ? k_emisoras[e].nivel_propio : 0U;
}

hora_emisora_t dcf77_emisora(void)
{
    return s_emisora;
}

uint32_t dcf77_freq_hz(hora_emisora_t e)
{
    return (e < HORA_N) ? k_emisoras[e].hz : k_emisoras[0].hz;
}

const char *dcf77_nombre(hora_emisora_t e)
{
    return (e < HORA_N) ? k_emisoras[e].nombre : k_emisoras[0].nombre;
}

/* 1 si esa emisora la lleva otro fichero: RBU y WWV no comparten nada con
 * las tres de aqui salvo la interfaz. Se pregunta en tres sitios (arrancar,
 * alimentar y leer el estado) y por eso vive en una funcion. */
static uint8_t es_de_fuera(hora_emisora_t e)
{
    return (uint8_t)(e == HORA_RBU || e == HORA_WWV10
                  || e == HORA_WWV15 || e == HORA_BPM);
}

static float    s_lpf;           /* envolvente ya filtrada */
static float    s_lpf_a;         /* coeficiente del polo */
static float    s_pico;          /* nivel de portadora estimado */
static float    s_caida;         /* factor de caida por muestra */
static uint8_t  s_bajo;          /* 1 mientras la portadora esta caida */
static uint32_t s_n_bajo;        /* muestras que lleva caida */
static uint32_t s_n_desde_flanco;/* muestras desde la ultima BAJADA */
static uint8_t  s_hubo_flanco;   /* ya se ha visto al menos una bajada */
static float    s_min_bajo;      /* minimo alcanzado en la bajada en curso */
static float    s_contraste;

static uint8_t  s_bits[59];
static uint8_t  s_n_bits;

static dcf_estado_t s_estado;
static uint16_t s_ok, s_malas;

/* La trama anterior que paso todas las comprobaciones, para exigir que la
 * siguiente sea exactamente un minuto mayor. */
static uint8_t  s_prev_valida;
static uint8_t  s_prev_h, s_prev_m, s_prev_d, s_prev_mes, s_prev_a, s_prev_ds, s_prev_ver;

static uint8_t  s_h, s_m, s_d, s_mes, s_a, s_ds, s_ver;


void dcf77_start(hora_emisora_t emisora, float fs_hz)
{
    uint8_t k;

    s_emisora = emisora;
    s_fs = (fs_hz > 1.0f) ? fs_hz : 1.0f;
    /* exp(-1/(tau*fs)) aproximado por 1 - 1/(tau*fs): con tau de segundos y
     * fs de cientos de Hz el error es despreciable y ahorra una exp(). */
    s_caida = 1.0f - (1.0f / (PICO_CAIDA_S * s_fs));
    if (s_caida < 0.0f) { s_caida = 0.0f; }

    /* 1 - exp(-2*pi*fc/fs), linealizado: con fc muy por debajo de fs el
     * error no llega al 1% y se ahorra una exp() en el arranque. */
    s_lpf_a = 6.2831853f * LPF_HZ / s_fs;
    if (s_lpf_a > 1.0f) { s_lpf_a = 1.0f; }
    s_lpf = 0.0f;

    s_pico = 0.0f;
    s_bajo = 0U;
    s_n_bajo = 0U;
    s_n_desde_flanco = 0U;
    s_hubo_flanco = 0U;
    s_min_bajo = 0.0f;
    s_contraste = 0.0f;
    s_n_bits = 0U;
    s_estado = DCF_BUSCANDO;
    s_ok = 0U;
    s_malas = 0U;
    s_prev_valida = 0U;
    s_bit_a = 0U; s_bit_b = 0U; s_marca_min = 0U;
    s_hecho_a = 0U; s_hecho_b = 0U; s_hecho_min = 0U;
    for (k = 0U; k < 60U; k++) { s_msf_a[k] = 0U; s_msf_b[k] = 0U; }
    s_msf_seg = 0U;
    s_msf_enganchado = 0U;
    s_msf_sospechosa = 0U;
    s_ms_marca = 0U; s_ms_ciclo = 0U; s_marcas_raras = 0U;

    /* Estado del frente de fase (ALS162). Va aqui y no en una funcion
     * aparte: el 23/09/2026 estas lineas se quedaron FUERA por una
     * sustitucion que no encontro su texto y que no comprobe, y el efecto
     * fue que s_a_deriva y s_a_fuga valian cero -o sea, seguidor de deriva
     * parado e integrador sin fuga-. Con el oscilador exacto funcionaba y
     * con cualquier error se disparaba y no contaba ni un elemento. El banco
     * lo enseño como "sin deriva sincroniza, con 1 Hz ya no". */
    s_fase_prev = 0.0f;
    s_deriva    = 0.0f;
    s_tri       = 0.0f;
    s_fase_arriba = 0U;
    s_elem_n      = 0U;
    s_elem_cerrado = 0U;
    s_fase_lista  = 0U;
    s_fase_n      = 0U;
    s_tri_lpf = 0.0f;
    s_tri_max = 0.0f; s_tri_min = 0.0f; s_tri_pp = 0.0f; s_tri_n = 0U;
    s_elem_ultimo_ms = 0U;
    s_a_tri = 6.2831853f * TRI_LPF_HZ / s_fs;
    if (s_a_tri > 1.0f) { s_a_tri = 1.0f; }
    s_a_deriva = 6.2831853f * FASE_DERIVA_HZ / s_fs;
    s_a_fuga   = 6.2831853f * FASE_FUGA_HZ   / s_fs;
    if (s_a_deriva > 1.0f) { s_a_deriva = 1.0f; }
    if (s_a_fuga   > 1.0f) { s_a_fuga   = 1.0f; }

    /* Las de fuera se arrancan enteras aqui: asi quien llama solo conoce
     * dcf77_start() y no tiene que saber cuantos ficheros hay detras. */
    if (s_emisora == HORA_RBU) { rbu_start(s_fs); }
    else if (es_de_fuera(s_emisora)) { wwv_start(s_emisora, s_fs); }

    s_activo = 1U;
}

void dcf77_stop(void) { s_activo = 0U; }
uint8_t dcf77_activo(void) { return s_activo; }

/* BCD de un tramo de bits, del menos significativo al mas. */
static uint8_t bcd(const uint8_t *b, uint8_t ini, uint8_t n)
{
    static const uint8_t peso[8] = { 1U, 2U, 4U, 8U, 10U, 20U, 40U, 80U };
    uint8_t k, v = 0U;
    for (k = 0U; k < n; k++) { if (b[ini + k]) { v = (uint8_t)(v + peso[k]); } }
    return v;
}

/* Paridad PAR: el numero de unos de ini..fin (P incluido) debe ser par. */
static uint8_t paridad_par_ok(const uint8_t *b, uint8_t ini, uint8_t fin)
{
    uint8_t k, n = 0U;
    for (k = ini; k <= fin; k++) { n = (uint8_t)(n + b[k]); }
    return (uint8_t)((n & 1U) == 0U);
}



/*
 * Comprueba y decodifica los 59 bits. Devuelve 0 si algo no cuadra.
 *
 * Se comprueba TODO lo que el formato permite comprobar, no solo las
 * paridades: los dos bits fijos, que Z1 y Z2 sean opuestos (no puede ser
 * verano e invierno a la vez, ni ninguno de los dos), y que cada campo caiga
 * en su rango. Cada una de estas es gratis y cierra una puerta por la que
 * una trama rota podria colarse con las paridades cuadrando de casualidad.
 */
static uint8_t decodifica(const uint8_t *b)
{
    uint8_t h, m, d, mes, a, ds;

    if (b[0] != 0U) { return 0U; }   /* siempre 0 */
    if (b[20] != 1U) { return 0U; }  /* siempre 1 */
    if (b[17] == b[18]) { return 0U; }  /* Z1 y Z2 son opuestos por definicion */

    if (!paridad_par_ok(b, 21U, 28U)) { return 0U; }
    if (!paridad_par_ok(b, 29U, 35U)) { return 0U; }
    if (!paridad_par_ok(b, 36U, 58U)) { return 0U; }

    m   = bcd(b, 21U, 7U);
    h   = bcd(b, 29U, 6U);
    d   = bcd(b, 36U, 6U);
    ds  = bcd(b, 42U, 3U);
    mes = bcd(b, 45U, 5U);
    a   = bcd(b, 50U, 8U);

    if (m > 59U) { return 0U; }
    if (h > 23U) { return 0U; }
    if (mes < 1U || mes > 12U) { return 0U; }
    if (d < 1U || d > k_dias_mes[mes]) { return 0U; }
    if (ds < 1U || ds > 7U) { return 0U; }
    if (a > 99U) { return 0U; }

    s_h = h; s_m = m; s_d = d; s_mes = mes; s_a = a; s_ds = ds;
    s_ver = b[17];
    return 1U;
}

/* Un minuto mas que la trama anterior, contando el salto de hora y de dia.
 * No se comprueba el cambio de mes: una sincronizacion que caiga justo en la
 * medianoche del ultimo dia del mes simplemente pedira un minuto mas, que es
 * mejor que arrastrar aqui un calendario entero por un caso que pasa doce
 * veces al año. */
static uint8_t es_el_minuto_siguiente(void)
{
    uint8_t m = (uint8_t)(s_prev_m + 1U);
    uint8_t h = s_prev_h;

    if (m > 59U) { m = 0U; h = (uint8_t)(h + 1U); }
    if (h > 23U) { h = 0U; if (s_prev_d != s_d) { return (uint8_t)(s_m == m && s_h == h); } }
    return (uint8_t)(s_m == m && s_h == h && s_d == s_prev_d);
}

/* ----------------------------- MSF ------------------------------------- */

/* Paridad IMPAR (MSF), al reves que la de DCF77: el bit de paridad hace que
 * el total de unos del grupo MAS el propio bit sea impar. */
static uint8_t paridad_impar_ok(const uint8_t *a, uint8_t ini, uint8_t fin, uint8_t p)
{
    uint8_t k, n = p;
    for (k = ini; k <= fin; k++) { n = (uint8_t)(n + a[k]); }
    return (uint8_t)((n & 1U) == 1U);
}

/* BCD de un tramo de segundos de MSF. Aqui los bits van del MAS significativo
 * al menos -al reves que en DCF77-, asi que la tabla de pesos se recorre al
 * derecho. Esa diferencia es justo la clase de detalle que hace que dos
 * formatos parecidos no se puedan compartir a medias. */
static uint8_t bcd_msf(const uint8_t *a, uint8_t ini, uint8_t fin)
{
    static const uint8_t peso[8] = { 80U, 40U, 20U, 10U, 8U, 4U, 2U, 1U };
    uint8_t k, v = 0U;
    uint8_t n = (uint8_t)(fin - ini + 1U);
    uint8_t off = (uint8_t)(8U - n);
    for (k = 0U; k < n; k++) { if (a[ini + k]) { v = (uint8_t)(v + peso[off + k]); } }
    return v;
}

static uint8_t decodifica_msf(void)
{
    const uint8_t *a = s_msf_a;
    const uint8_t *b = s_msf_b;
    uint8_t h, m, d, mes, anno, ds, verano, k;

    /* El patron fijo 01111110 de los segundos 52..59. Es la comprobacion mas
     * fuerte que tiene MSF: ocho bits que solo pueden valer una cosa, y que
     * ademas estan repartidos por el final de la trama, justo donde un
     * desvanecimiento del final se notaria. */
    if (a[52] != 0U || a[59] != 0U) { return 0U; }
    for (k = 53U; k <= 58U; k++) { if (a[k] != 1U) { return 0U; } }

    if (!paridad_impar_ok(a, 17U, 24U, b[54])) { return 0U; }
    if (!paridad_impar_ok(a, 25U, 35U, b[55])) { return 0U; }
    if (!paridad_impar_ok(a, 36U, 38U, b[56])) { return 0U; }
    if (!paridad_impar_ok(a, 39U, 51U, b[57])) { return 0U; }

    anno = bcd_msf(a, 17U, 24U);
    mes  = bcd_msf(a, 25U, 29U);
    d    = bcd_msf(a, 30U, 35U);
    ds   = bcd_msf(a, 36U, 38U);
    h    = bcd_msf(a, 39U, 44U);
    m    = bcd_msf(a, 45U, 51U);
    verano = b[58];

    if (m > 59U) { return 0U; }
    if (h > 23U) { return 0U; }
    if (mes < 1U || mes > 12U) { return 0U; }
    if (d < 1U || d > k_dias_mes[mes]) { return 0U; }
    if (ds > 6U) { return 0U; }   /* 0=domingo .. 6=sabado */
    if (anno > 99U) { return 0U; }

    /*
     * MSF emite UTC. España peninsular va una hora por delante en invierno y
     * dos en verano, y el cambio de horario ocurre en el MISMO instante en
     * los dos paises (norma europea), asi que el bit de verano del Reino
     * Unido sirve tal cual para saber en cual estamos.
     *
     * La suma se hace aqui y no en quien llama: dejarla fuera seria repartir
     * "esta emisora da UTC y esta otra no" por el codigo, y ese es
     * exactamente el detalle que un dia se olvida y deja el reloj una hora
     * desviado sin que nada chirrie.
     */
    h = (uint8_t)((h + (verano ? 2U : 1U)) % 24U);
    /* Si el ajuste cruza la medianoche, el dia ya no es el que dice la
     * trama. No se corrige el calendario: se deja la fecha como venia y se
     * avisa en el comentario, porque la fecha aqui es informativa -lo que se
     * pone en el reloj es la hora- y arrastrar un calendario entero por una
     * hora al dia no compensa. */

    s_h = h; s_m = m; s_d = d; s_mes = mes; s_a = anno;
    /* MSF cuenta 0=domingo y DCF77 1=lunes: se pasa al convenio de DCF77
     * para que dcf_info_t signifique siempre lo mismo. */
    s_ds = (uint8_t)((ds == 0U) ? 7U : ds);
    s_ver = verano;
    return 1U;
}

/* Fin de trama: llega el hueco de dos segundos. */
static void cierra_trama(void)
{
    if (s_n_bits != 59U) {
        if (s_n_bits > 0U) { s_malas++; }
        s_n_bits = 0U;
        s_prev_valida = 0U;
        s_estado = DCF_LEYENDO;
        return;
    }

    if (!decodifica(s_bits)) {
        s_malas++;
        s_n_bits = 0U;
        s_prev_valida = 0U;   /* una mala rompe la cadena: hay que empezar de nuevo */
        s_estado = DCF_LEYENDO;
        return;
    }

    s_ok++;
    if (s_prev_valida && es_el_minuto_siguiente()) {
        s_estado = DCF_LISTO;
    } else {
        s_estado = DCF_CONFIRMANDO;
    }
    s_prev_valida = 1U;
    s_prev_h = s_h; s_prev_m = s_m; s_prev_d = s_d;
    s_prev_mes = s_mes; s_prev_a = s_a; s_prev_ds = s_ds; s_prev_ver = s_ver;
    s_n_bits = 0U;
}

/* Cierra el minuto de MSF: llega la marca de 500 ms. */
static void cierra_minuto_msf(void)
{
    if (!s_msf_enganchado) {
        /* Primera marca de minuto que se ve: a partir de aqui se sabe por
         * donde va la trama. Lo de antes no se puede usar. */
        s_msf_enganchado = 1U;
        s_msf_seg = 0U;
        s_msf_sospechosa = 0U;
        s_estado = DCF_LEYENDO;
        return;
    }

    /* Tienen que haber pasado exactamente 60 segundos desde la marca
     * anterior. Si no, se ha perdido algun segundo por el camino y los bits
     * estan corridos: la trama no vale aunque las paridades cuadraran. */
    if (s_msf_seg != 59U || s_msf_sospechosa) {
        s_malas++;
        s_prev_valida = 0U;
        s_msf_seg = 0U;
        s_msf_sospechosa = 0U;
        s_estado = DCF_LEYENDO;
        return;
    }

    if (!decodifica_msf()) {
        s_malas++;
        s_prev_valida = 0U;
        s_msf_seg = 0U;
        s_msf_sospechosa = 0U;
        s_estado = DCF_LEYENDO;
        return;
    }

    s_ok++;
    if (s_prev_valida && es_el_minuto_siguiente()) { s_estado = DCF_LISTO; }
    else                                          { s_estado = DCF_CONFIRMANDO; }
    s_prev_valida = 1U;
    s_prev_h = s_h; s_prev_m = s_m; s_prev_d = s_d;
    s_prev_mes = s_mes; s_prev_a = s_a; s_prev_ds = s_ds; s_prev_ver = s_ver;
    s_msf_seg = 0U;
    s_msf_sospechosa = 0U;
}

/* Guarda lo leido en los puntos de muestreo del segundo que acaba de
 * terminar, ya sabiendo si era o no la marca de minuto. */
static void cierra_segundo_msf(void)
{
    if (s_marca_min) { cierra_minuto_msf(); return; }
    if (!s_msf_enganchado) { return; }   /* aun sin saber por donde va */

    if (s_msf_seg < 59U) {
        s_msf_seg++;
        s_msf_a[s_msf_seg] = s_bit_a;
        s_msf_b[s_msf_seg] = s_bit_b;
    } else {
        /* Mas de 59 segundos sin marca de minuto: se ha perdido la marca.
         * Desengancharse es mejor que seguir guardando en posiciones que ya
         * no significan nada. */
        s_msf_enganchado = 0U;
        s_prev_valida = 0U;
        s_malas++;
        s_estado = DCF_PERDIDO;
    }
}

/*
 * ALS162. Recibe la fase cruda en radianes y lleva la maquina del minuto.
 *
 * Estructura calcada de DCF77 a proposito: 59 bits y un hueco de dos
 * segundos que cierra la trama. Lo unico distinto es de donde sale el bit
 * -de contar elementos, en vez de medir cuanto dura una bajada- y por eso
 * puede reusar cierra_trama() y decodifica() tal cual, que es todo el
 * contenido de hora y fecha.
 */
static void feed_fase(float fase)
{
    float d, alta;
    uint32_t ms;

    /* 1. Derivada: la diferencia entre esta muestra y la anterior,
     *    desenrollada. Que sea la DIFERENCIA y no la fase acumulada es lo
     *    que hace que esto funcione - ver el comentario de FASE_DERIVA_HZ.
     *
     *    OJO: esto exige que la fase no avance mas de pi entre muestras, o
     *    sea que el error de oscilador tiene que ser menor que la mitad del
     *    ritmo de muestreo. Por eso main.c alimenta esto CUATRO veces por
     *    bloque y no una: a 1500 muestras/s el limite son 750 Hz, contra
     *    los ~95 Hz que puede desviarse el temporizador a 162 kHz. Con una
     *    sola muestra por bloque el margen era de 187 Hz y el paso del
     *    temporizador ya vale 190: justo encima. */
    d = fase - s_fase_prev;
    while (d >  3.14159265f) { d -= 6.28318531f; }
    while (d < -3.14159265f) { d += 6.28318531f; }
    s_fase_prev = fase;

    /* 2. Quitarle su media, que ES el error del oscilador. Exacto, no
     *    aproximado: una constante menos su media es cero. */
    s_deriva += s_a_deriva * (d - s_deriva);

    /* 3. Integrar lo que queda para rehacer el triangulo, con una fuga lenta
     *    que impide que un residuo de nada se acumule sin fin. */
    s_tri += (d - s_deriva);
    s_tri -= s_a_fuga * s_tri;

    /* 4. Y un paso bajo sobre el triangulo. Hace falta porque integrar ruido
     *    blanco es ACUMULARLO -sale un paseo aleatorio, no ruido-, y sin
     *    esto el banco se caia ya con 0,15 rad. El elemento dura 100 ms, o
     *    sea que toda su forma vive por debajo de unos 20 Hz: filtrar por
     *    ahi no le quita nada y se lleva el resto. */
    s_tri_lpf += s_a_tri * (s_tri - s_tri_lpf);
    alta = s_tri_lpf;

    /* Pico a pico sobre ventanas de un segundo, para la barra de la
     * pantalla. Ver el comentario de s_tri_pp. */
    if (alta > s_tri_max) { s_tri_max = alta; }
    if (alta < s_tri_min) { s_tri_min = alta; }
    s_tri_n++;
    if ((float)s_tri_n >= s_fs) {
        s_tri_pp  = s_tri_max - s_tri_min;
        s_tri_max = 0.0f;
        s_tri_min = 0.0f;
        s_tri_n   = 0U;
    }

    /* Los dos primeros segundos no cuentan: el paso alto se esta asentando
     * y lo que sale de ahi es el transitorio, no la señal. */
    s_fase_n++;
    if (!s_fase_lista) {
        if ((float)s_fase_n > s_fs * 2.0f) { s_fase_lista = 1U; }
        return;
    }

    s_n_desde_flanco++;
    ms = (uint32_t)((float)s_n_desde_flanco * 1000.0f / s_fs);

    /* 3. Contar elementos: el triangulo sube por encima del umbral y baja.
     *    Se cuenta una vez por subida, con histeresis para que el ruido no
     *    cuente dos. Se mira la FORMA, no la altura exacta: el paso alto y
     *    la propagacion se comen parte del radian y da igual. */
    if (!s_fase_arriba) {
        /* El refractario: ver ELEM_REFRACTARIO_MS. */
        if (alta > FASE_UMBRAL
            && (s_elem_n == 0U
                || (ms - s_elem_ultimo_ms) >= (uint32_t)ELEM_REFRACTARIO_MS)) {
            s_fase_arriba = 1U;
            s_elem_ultimo_ms = ms;
            if (s_elem_n == 0U) {
                s_ms_ciclo = (uint16_t)ms;   /* aqui empieza el segundo */
                s_n_desde_flanco = 0U;
                s_elem_cerrado = 0U;
                ms = 0U;
            }
            if (s_elem_n < 255U) { s_elem_n++; }
            s_ms_marca = (uint16_t)(s_elem_n * 100U);
        }
    } else if (alta < FASE_UMBRAL * 0.5f) {
        s_fase_arriba = 0U;
    }

    /* 4. Pasada la ventana donde caben los dos elementos, lo contado ES el
     *    bit: uno = 0, dos = 1. */
    if (s_elem_n > 0U && !s_elem_cerrado && ms >= (uint32_t)ELEM_VENTANA_MS) {
        s_elem_cerrado = 1U;
        if (s_elem_n > 2U) {
            /* Mas de dos en un segundo es imposible: ruido. */
            s_marcas_raras++;
            if (s_n_bits > 0U) { s_malas++; }
            s_n_bits = 0U;
            s_prev_valida = 0U;
        } else if (s_n_bits < 59U) {
            s_bits[s_n_bits] = (uint8_t)(s_elem_n >= 2U ? 1U : 0U);
            s_n_bits++;
            if (s_estado == DCF_BUSCANDO || s_estado == DCF_PERDIDO) {
                s_estado = DCF_LEYENDO;
            }
        }
        s_elem_n = 0U;
    }

    /* 5. El hueco: ~2 segundos sin un solo elemento es el segundo 59, y con
     *    el se cierra la trama. Mismo criterio que DCF77. */
    if (s_elem_n == 0U && s_elem_cerrado && ms >= (uint32_t)HUECO_MIN_MS) {
        s_ms_ciclo = (uint16_t)ms;
        cierra_trama();
        s_n_desde_flanco = 0U;
        s_elem_cerrado = 0U;
    }
}

/* Las de envolvente: DCF77 y MSF. */
static void feed_envolvente(float env)
{
    float umbral_b, umbral_s;
    float ms_desde_flanco;

    /* Filtrar ANTES de cualquier otra cosa: el seguidor de pico y los
     * umbrales tienen que trabajar sobre la misma señal limpia, no cada uno
     * sobre la suya. Ver el comentario de LPF_HZ. */
    s_lpf += s_lpf_a * (env - s_lpf);
    env = s_lpf;

    /* Seguidor de pico: instantaneo al subir, lento al bajar. */
    s_pico *= s_caida;
    if (env > s_pico) { s_pico = env; }
    if (s_pico <= 0.0f) { return; }

    umbral_b = s_pico * UMBRAL_BAJA;
    umbral_s = s_pico * UMBRAL_SUBE;

    s_n_desde_flanco++;
    ms_desde_flanco = (float)s_n_desde_flanco * 1000.0f / s_fs;

    /* --- muestreo en instantes fijos, ver MUESTRA_*_MS ------------------ */
    if (s_hubo_flanco) {
        if (!s_hecho_a && ms_desde_flanco >= MUESTRA_A_MS) {
            s_bit_a = s_bajo; s_hecho_a = 1U;
        }
        if (s_emisora == HORA_MSF) {
            if (!s_hecho_b && ms_desde_flanco >= MUESTRA_B_MS) {
                s_bit_b = s_bajo; s_hecho_b = 1U;
            }
            if (!s_hecho_min && ms_desde_flanco >= MUESTRA_MIN_MS) {
                s_marca_min = s_bajo; s_hecho_min = 1U;
                /* La marca de minuto se reconoce AQUI, a los 400 ms, sin
                 * esperar a que termine: asi el segundo se cierra con la
                 * informacion completa en cuanto se sabe. */
                cierra_segundo_msf();
            }
        }
    }

    if (!s_bajo) {
        if (env < umbral_b) {
            float dt = ms_desde_flanco;

            /*
             * NO TODA BAJADA EMPIEZA UN SEGUNDO. 23/09/2026, reportado desde
             * la radio: MSF se quedaba eternamente en "leyendo la trama" con
             * señal buena.
             *
             * En MSF, un segundo con A=0 y B=1 apaga la portadora de 0 a 100
             * ms, la enciende de 100 a 200 y la vuelve a apagar de 200 a 300:
             * DOS bajadas dentro del mismo segundo. La primera version
             * trataba la segunda como el principio de un segundo nuevo, veia
             * que solo habian pasado 200 ms, decidia que se habia perdido el
             * ritmo y se desenganchaba. Con lo cual no pasaba de la primera
             * trama.
             *
             * Y el banco no podia cazarlo: generaba DUT1 a cero, o sea que
             * nunca producia un segundo con A=0 y B=1. En el aire DUT1 casi
             * nunca es cero, asi que fallaba siempre. Ahora el banco lo
             * genera.
             *
             * La regla es sencilla: una bajada que llega antes de que haya
             * pasado casi un segundo entero desde la anterior NO abre
             * segundo. Solo pone la portadora en "caida" para que los puntos
             * de muestreo la lean bien. Vale igual para DCF77, donde ademas
             * hace de filtro contra un flanco espurio del ruido.
             */
            if (s_hubo_flanco && dt < SEG_MIN_MS) {
                s_bajo = 1U;
                s_n_bajo = 0U;
                s_min_bajo = env;
                return;
            }

            s_ms_ciclo = (uint16_t)(dt + 0.5f);

            if (s_hubo_flanco) {
                if (s_emisora == HORA_DCF77) {
                    uint8_t normal = (uint8_t)(dt >= SEG_MIN_MS && dt <= SEG_MAX_MS);
                    uint8_t hueco  = (uint8_t)(dt >= HUECO_MIN_MS && dt <= HUECO_MAX_MS);

                    /*
                     * El bit del segundo que ACABA de terminar se guarda
                     * aqui, y hay que guardarlo TAMBIEN cuando lo que viene
                     * es el hueco de minuto.
                     *
                     * La primera version solo lo guardaba en el caso normal,
                     * y perdia sistematicamente el bit 58: en DCF77 el
                     * segundo 59 no tiene marca, asi que la bajada siguiente
                     * a la del segundo 58 es justo la que cierra la trama.
                     * Resultado, 58 bits en vez de 59 y TODAS las tramas
                     * rechazadas. Lo caza el banco a la primera.
                     */
                    if ((normal || hueco) && s_contraste >= CONTRASTE_MIN
                        && s_n_bits < 59U) {
                        s_bits[s_n_bits] = s_bit_a;
                        s_n_bits++;
                        if (s_estado == DCF_BUSCANDO || s_estado == DCF_PERDIDO) {
                            s_estado = DCF_LEYENDO;
                        }
                    }

                    if (hueco) {
                        cierra_trama();
                    } else if (normal) {
                        /* nada mas que hacer: el bit ya esta guardado */
                    } else {
                        /* Ni un segundo ni dos: se ha perdido el ritmo. */
                        if (s_n_bits > 0U) { s_malas++; }
                        s_n_bits = 0U;
                        s_prev_valida = 0U;
                        if (s_estado != DCF_BUSCANDO) { s_estado = DCF_PERDIDO; }
                    }
                } else {
                    /* MSF: si el segundo NO llego a los 400 ms de muestreo
                     * -porque la marca fue corta y la siguiente bajada
                     * llego antes- hay que cerrarlo ahora. */
                    if (!s_hecho_min) { s_marca_min = 0U; cierra_segundo_msf(); }
                    if (dt < SEG_MIN_MS || dt > SEG_MAX_MS) {
                        s_msf_enganchado = 0U;
                        s_prev_valida = 0U;
                        if (s_estado != DCF_BUSCANDO) { s_estado = DCF_PERDIDO; }
                    }
                }
            }
            s_hubo_flanco = 1U;
            s_n_desde_flanco = 0U;
            s_bajo = 1U;
            s_n_bajo = 0U;
            s_min_bajo = env;
            s_hecho_a = 0U; s_hecho_b = 0U; s_hecho_min = 0U;
            s_bit_a = 0U; s_bit_b = 0U; s_marca_min = 0U;
        }
    } else {
        s_n_bajo++;
        if (env < s_min_bajo) { s_min_bajo = env; }

        if (env > umbral_s) {
            /* SUBIDA: la marca ha terminado. La duracion ya no da el bit
             * -eso lo hace el muestreo- pero sigue valiendo como cordura:
             * una marca de duracion imposible es una trama que no vale. */
            float ms = (float)s_n_bajo * 1000.0f / s_fs;
            /* 700 y no 560: la marca de minuto de MSF son 500 ms nominales, pero el
             * filtro paso bajo y la histeresis la estiran unos 30 ms, y una
             * señal con desvanecimiento estira mas. 700 sigue rechazando un
             * corte de un segundo, que es lo que interesa cazar. */
            float tope = (s_emisora == HORA_MSF) ? 700.0f : BIT1_MAX_MS;

            s_contraste = (s_pico > 0.0f) ? ((s_pico - s_min_bajo) / s_pico) : 0.0f;
            s_bajo = 0U;

            s_ms_marca = (uint16_t)(ms + 0.5f);

            if (ms < BIT0_MIN_MS || ms > tope) {
                s_marcas_raras++;
                if (s_emisora == HORA_DCF77) {
                    if (s_n_bits > 0U) { s_malas++; }
                    s_n_bits = 0U;
                    s_prev_valida = 0U;
                } else {
                    /*
                     * En MSF una marca rara NO desengancha. 23/09/2026: antes
                     * si, y ademas lo hacia sin tocar s_estado, o sea que la
                     * pantalla seguia diciendo "leyendo la trama" mientras el
                     * decodificador estaba parado y el contador clavado en
                     * 00. Eso es lo peor que puede hacer una pantalla de
                     * diagnostico: mentir.
                     *
                     * Y desenganchar por UNA marca rara era demasiado bruto:
                     * un chasquido tira un minuto entero de trabajo. Ahora
                     * solo se marca el minuto como sospechoso -no se
                     * aceptara aunque cuadre- y se sigue contando segundos.
                     * La correccion de verdad ya la hacen el patron fijo, las
                     * cuatro paridades y el "tienen que ser exactamente 59
                     * segundos", que son mucho mas dificiles de enganar que
                     * una duracion.
                     */
                    s_msf_sospechosa = 1U;
                }
            }
        }
    }
}

/*
 * Una muestra por bloque. Lo que signifique depende de la emisora, y esa
 * decision se toma AQUI: demod_am.c solo pregunta dcf77_usa_fase() para
 * saber que medir, y no necesita saber nada mas de ninguna emisora.
 */
void dcf77_feed(float muestra)
{
    if (!s_activo) { return; }
    if (s_emisora == HORA_RBU)       { rbu_feed(muestra); }
    else if (es_de_fuera(s_emisora)) { wwv_feed(muestra); }
    else if (s_emisora == HORA_ALS162) { feed_fase(muestra); }
    else                             { feed_envolvente(muestra); }
}

void dcf77_info(dcf_info_t *out)
{
    if (out == 0) { return; }
    if (s_emisora == HORA_RBU)       { rbu_info(out); return; }
    if (es_de_fuera(s_emisora))      { wwv_info(out); return; }
    out->estado     = s_estado;
    out->hora       = s_h;
    out->minuto     = s_m;
    out->dia        = s_d;
    out->mes        = s_mes;
    out->anno       = s_a;
    out->dia_semana = s_ds;
    out->verano     = s_ver;
    /* Lo que va llenandose es distinto en cada emisora: DCF77 cuenta BITS
     * y MSF cuenta SEGUNDOS del minuto. Enseñar siempre el de DCF77 dejaba
     * MSF clavado en "00 de 59" aunque fuera bien, que es justo lo que
     * confunde al mirar la pantalla. */
    out->bits       = (s_emisora == HORA_MSF) ? s_msf_seg : s_n_bits;
    out->tramas_ok  = s_ok;
    out->tramas_malas = s_malas;
    out->nivel      = s_pico;
    /* En las de fase el "contraste" es el pico a pico del triangulo,
     * normalizado a los 2 radianes que vale un elemento entero. En las de
     * envolvente sigue siendo cuanto baja la portadora. Las dos cosas
     * responden a la misma pregunta -hay señal de verdad?- y por eso
     * comparten hueco en la pantalla. */
    out->contraste  = dcf77_usa_fase(s_emisora) ? (s_tri_pp * 0.5f) : s_contraste;
    out->ms_marca     = s_ms_marca;
    out->ms_ciclo     = s_ms_ciclo;
    out->marcas_raras = s_marcas_raras;
}
