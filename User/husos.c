/*
 * LA TABLA DE CIUDADES Y LAS REGLAS DE VERANO. Ver husos.h para el porque.
 */
#include "husos.h"
#include "hora_comun.h"
#include <math.h>

/*
 * LAS REGLAS, COMO DATOS Y NO COMO CODIGO.
 *
 * Las cinco dicen lo mismo con numeros distintos: "cambia tal domingo de tal
 * mes a tal hora". Escribirlas como cinco funciones habria sido cinco sitios
 * donde equivocarse con el "ultimo domingo".
 *
 * EL DOMINGO NO SE GUARDA COMO ORDINAL -"el segundo", "el ultimo"- SINO COMO
 * "EL PRIMER DOMINGO A PARTIR DEL DIA D". Son lo mismo para cuatro de las
 * cinco (el ultimo domingo de marzo es el primero a partir del 25, el
 * segundo de marzo el primero a partir del 8) y la quinta NO CABE en los
 * ordinales: Chile cambia el PRIMER SABADO de septiembre a las 24:00, o sea
 * el domingo siguiente, que en 2024 cayo el dia 8 -el SEGUNDO domingo- y en
 * 2025 el 7, que era el primero. No es ningun ordinal fijo; es "el primer
 * domingo a partir del 2". Esto tambien lo vio tools/gen_husos.py, y la
 * primera version de esta tabla estaba mal por ese motivo.
 */
typedef struct {
    uint8_t mes_ini, dia_ini, hora_ini;
    uint8_t mes_fin, dia_fin, hora_fin;
    uint8_t utc;        /* 1 = las horas de arriba ya son UTC */
} regla_t;

/*
 * EL `utc` NO ES UN ADORNO, Y LO DESTAPO EL BANCO.
 *
 * La primera version comparaba la hora UTC contra estas horas sin mirar de
 * donde salian. Para Europa da igual, porque la norma europea esta escrita
 * en UTC a proposito -es lo que permite que los veintisiete cambien a la vez-
 * y Chile publica la suya en UTC tambien. Pero Estados Unidos, Australia y
 * Nueva Zelanda la escriben en HORA LOCAL, y en Sidney las 02:00 locales son
 * las 16:00 UTC DEL DIA ANTERIOR: tomarlas por UTC metia catorce horas de
 * error dos dias al año. tools/gen_husos.py lo vio al primer intento,
 * comparando las fechas con zoneinfo ("AU 2012: la regla dice 01/04 y
 * zoneinfo 31/03"), y por eso las de local se convierten en vez de leerse.
 */
static const regla_t k_reglas[HUSO_DST_N] = {
/*    mes dia  h   mes dia  h  utc                                          */
    {   0,  0, 0,    0,  0, 0,  0 },  /* HUSO_DST_NO: no se usa             */
    {   3, 25, 1,   10, 25, 1,  1 },  /* EU  ultimo domingo, 01:00 UTC      */
    {   3,  8, 2,   11,  1, 2,  0 },  /* USA 2.o de marzo, 1.o de noviembre */
    {  10,  1, 2,    4,  1, 3,  0 },  /* AU  sur: octubre .. abril, LOCAL   */
    {   9, 24, 2,    4,  1, 3,  0 },  /* NZ  ultimo de septiembre, LOCAL    */
    {   9,  2, 4,    4,  2, 3,  1 }   /* CL  el sabado a las 24:00, en UTC  */
};

/*
 * CINCUENTA CIUDADES. El criterio no es el tamaño sino CUBRIR LOS
 * HUSOS: hay sitios pequeños aqui -Katmandu, Reikiavik, las Azores- porque
 * son el unico representante de su desplazamiento, y faltan ciudades
 * enormes que comparten huso con una que ya esta.
 *
 * Las coordenadas son en DECIMAS de grado y solo sirven para ORDENAR la
 * lista por cercania: una decima son 11 km, de sobra para eso y la mitad de
 * sitio que guardarlas finas.
 */
static const huso_ciudad_t k_ciudades[] = {
/*   nombre            base  regla         lat    lon  */
    { "Londres",          0, HUSO_DST_EU,   515,    -1 },
    { "Dublin",           0, HUSO_DST_EU,   533,   -63 },
    { "Lisboa",           0, HUSO_DST_EU,   387,   -91 },
    { "Las Azores",     -60, HUSO_DST_EU,   377,  -257 },
    { "Reikiavik",        0, HUSO_DST_NO,   641,  -219 },
    { "Madrid",          60, HUSO_DST_EU,   404,   -37 },
    { "Paris",           60, HUSO_DST_EU,   489,    24 },
    { "Berlin",          60, HUSO_DST_EU,   525,   134 },
    { "Roma",            60, HUSO_DST_EU,   419,   125 },
    { "Varsovia",        60, HUSO_DST_EU,   522,   210 },
    { "Oslo",            60, HUSO_DST_EU,   599,   108 },
    { "Budapest",        60, HUSO_DST_EU,   475,   190 },
    { "Atenas",         120, HUSO_DST_EU,   380,   237 },
    { "Helsinki",       120, HUSO_DST_EU,   602,   249 },
    { "Kiev",           120, HUSO_DST_EU,   505,   305 },
    { "Estambul",       180, HUSO_DST_NO,   410,   290 },
    { "Moscu",          180, HUSO_DST_NO,   558,   376 },
    { "Casablanca",      60, HUSO_DST_NO,   336,   -76 },
    { "Lagos",           60, HUSO_DST_NO,    65,    34 },
    { "Johannesburgo",  120, HUSO_DST_NO,  -262,   280 },
    { "Nairobi",        180, HUSO_DST_NO,   -13,   368 },
    { "Dubai",          240, HUSO_DST_NO,   252,   553 },
    { "Karachi",        300, HUSO_DST_NO,   249,   670 },
    { "Delhi",          330, HUSO_DST_NO,   286,   772 },
    { "Katmandu",       345, HUSO_DST_NO,   277,   853 },
    { "Bangkok",        420, HUSO_DST_NO,   138,  1005 },
    { "Yakarta",        420, HUSO_DST_NO,   -62,  1068 },
    { "Pekin",          480, HUSO_DST_NO,   399,  1164 },
    { "Hong Kong",      480, HUSO_DST_NO,   223,  1142 },
    { "Perth",          480, HUSO_DST_NO,  -320,  1159 },
    { "Tokio",          540, HUSO_DST_NO,   357,  1397 },
    { "Seul",           540, HUSO_DST_NO,   376,  1270 },
    { "Adelaida",       570, HUSO_DST_AU,  -349,  1386 },
    { "Brisbane",       600, HUSO_DST_NO,  -275,  1530 },
    { "Sidney",         600, HUSO_DST_AU,  -339,  1512 },
    { "Auckland",       720, HUSO_DST_NZ,  -368,  1748 },
    { "Anchorage",     -540, HUSO_DST_USA,  612, -1499 },
    { "Honolulu",      -600, HUSO_DST_NO,   213, -1579 },
    { "Los Angeles",   -480, HUSO_DST_USA,  341, -1182 },
    { "Vancouver",     -480, HUSO_DST_USA,  493, -1231 },
    { "Denver",        -420, HUSO_DST_USA,  397, -1050 },
    { "Chicago",       -360, HUSO_DST_USA,  419,  -876 },
    { "Mexico",        -360, HUSO_DST_NO,   194,  -991 },
    { "Nueva York",    -300, HUSO_DST_USA,  407,  -740 },
    { "Toronto",       -300, HUSO_DST_USA,  437,  -794 },
    { "Bogota",        -300, HUSO_DST_NO,    47,  -741 },
    { "Lima",          -300, HUSO_DST_NO,  -120,  -770 },
    { "Santiago",      -240, HUSO_DST_CL,  -335,  -707 },
    { "Sao Paulo",     -180, HUSO_DST_NO,  -235,  -466 },
    { "Buenos Aires",  -180, HUSO_DST_NO,  -346,  -584 }
};

#define N_CIUDADES ((uint8_t)(sizeof k_ciudades / sizeof k_ciudades[0]))

uint8_t              husos_n(void) { return N_CIUDADES; }
const huso_ciudad_t *husos_fila(uint8_t i)
{
    return (i < N_CIUDADES) ? &k_ciudades[i] : &k_ciudades[0];
}

/*
 * El primer domingo que cae en el dia `d0` de ese mes o despues.
 * hora_dia_semana() devuelve 1=lunes..7=domingo, el convenio de DCF77.
 */
static uint8_t domingo(uint8_t d0, uint8_t mes, uint8_t anno2)
{
    uint8_t dow = hora_dia_semana(d0, mes, anno2);

    return (uint8_t)(d0 + ((7U - dow) % 7U));
}

/* mes/dia/hora en un solo numero, para poder compararlos de una vez. */
static uint32_t sello(uint8_t mes, uint8_t dia, uint8_t hora)
{
    return (uint32_t)mes * 10000UL + (uint32_t)dia * 100UL + (uint32_t)hora;
}

/* Esa fecha y hora UTC, corridas `min` minutos, y de vuelta a un sello. */
static uint32_t sello_local(int16_t min, uint8_t dia, uint8_t mes,
                            uint8_t anno2, uint8_t hora_utc)
{
    uint8_t h = hora_utc, mi = 0U, d = dia, me = mes, a = anno2, ds = 1U;

    hora_suma_minutos(min, &h, &mi, &d, &me, &a, &ds);
    return sello(me, d, h);
}

uint8_t husos_verano(uint8_t regla, int16_t base, uint8_t dia, uint8_t mes,
                     uint8_t anno2, uint8_t hora_utc)
{
    const regla_t *r;
    uint32_t ini, fin, t_ini, t_fin;

    if (regla == HUSO_DST_NO || regla >= HUSO_DST_N) { return 0U; }
    r = &k_reglas[regla];

    ini = sello(r->mes_ini, domingo(r->dia_ini, r->mes_ini, anno2), r->hora_ini);
    fin = sello(r->mes_fin, domingo(r->dia_fin, r->mes_fin, anno2), r->hora_fin);

    if (r->utc) {
        t_ini = sello(mes, dia, hora_utc);
        t_fin = t_ini;
    } else {
        /*
         * DOS CONVERSIONES Y NO UNA. La hora de entrada esta escrita en hora
         * de INVIERNO -son las 02:00 de antes de adelantar- y la de salida en
         * hora de VERANO -las 03:00 de antes de atrasar, que son las mismas
         * 02:00 de invierno-. Usar la misma para las dos desplazaria uno de
         * los dos cambios una hora.
         */
        t_ini = sello_local(base, dia, mes, anno2, hora_utc);
        t_fin = sello_local((int16_t)(base + 60), dia, mes, anno2, hora_utc);
    }

    /*
     * Norte: marzo < octubre, o sea el verano es un trozo de EN MEDIO del
     * año. Sur: octubre > abril, y el verano es el trozo que cruza el fin de
     * año, o sea LO DE FUERA. La misma cuenta sirve para los dos sin mas que
     * mirar cual de los dos extremos es mayor.
     */
    if (ini <= fin) { return (uint8_t)(t_ini >= ini && t_fin < fin); }
    return (uint8_t)(t_ini >= ini || t_fin < fin);
}

int16_t husos_ahora(uint8_t i, uint8_t dia, uint8_t mes, uint8_t anno2,
                    uint8_t hora_utc)
{
    const huso_ciudad_t *c = husos_fila(i);
    int16_t m = c->base;

    if (husos_verano(c->regla, c->base, dia, mes, anno2, hora_utc)) {
        m = (int16_t)(m + 60);
    }
    return m;
}

void husos_ordena(int32_t lat_e4, int32_t lon_e4, uint8_t *orden)
{
    static const float K = 3.14159265f / 180.0f;
    float qx, qy, qz, clat;
    float d[N_CIUDADES];
    uint8_t i, j;

    clat = cosf((float)lat_e4 * 1e-4f * K);
    qx = clat * cosf((float)lon_e4 * 1e-4f * K);
    qy = clat * sinf((float)lon_e4 * 1e-4f * K);
    qz = sinf((float)lat_e4 * 1e-4f * K);

    for (i = 0U; i < N_CIUDADES; i++) {
        float la = (float)k_ciudades[i].lat_d * 0.1f * K;
        float lo = (float)k_ciudades[i].lon_d * 0.1f * K;
        float cl = cosf(la);
        float x = cl * cosf(lo) - qx;
        float y = cl * sinf(lo) - qy;
        float z = sinf(la) - qz;

        d[i] = x * x + y * y + z * z;   /* la cuerda al cuadrado */
        orden[i] = i;
    }

    /* Insercion: cincuenta filas, una vez al abrir la pantalla. */
    for (i = 1U; i < N_CIUDADES; i++) {
        uint8_t k = orden[i];
        float   v = d[k];

        j = i;
        while (j > 0U && d[orden[j - 1U]] > v) {
            orden[j] = orden[j - 1U];
            j--;
        }
        orden[j] = k;
    }
}
