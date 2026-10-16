#include "dcf77.h"
#include "rapido.h"   /* rapido_atan2: ver dcf77_fase_iq() */
#include "idioma.h"
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

/*
 * *** EL FILTRO QUE LE FALTABA A LA FASE - 10/10/2026, Y ES EL FALLO. ***
 *
 * Pedido por el usuario., y el numero de
 * marca "entre 13 y 30". Pico a pico de 26 a 60 RADIANES cuando ALS162
 * modula +-1. Eso no es señal: es el integrador paseandose.
 *
 * De donde sale. tools/als_fase.py -el guion de PC que saco la fase de la
 * grabacion con la que este modulo sale verde- hace esto:
 *
 *     bb, aa = sg.butter(4, 40 / (fs / 2))
 *     z = sg.filtfilt(bb, aa, z)
 *
 * O sea que la grabacion venia filtrada A 40 Hz antes de sacarle el angulo.
 * La radio NO hacia eso: demod_am.c promediaba 64 muestras de I y de Q a
 * 96 kHz y llamaba a atan2 directamente. Una ventana rectangular de 0,667 ms
 * es un ancho de ruido equivalente de ~750 Hz.
 *
 * Diecinueve veces mas potencia de ruido entrando en la medida. Y s_tri es
 * la INTEGRAL de la derivada de la fase, asi que ese ruido no se promedia:
 * hace un paseo aleatorio. Con 0,15 rad el paseo llega a ~2,3 rad -esta
 * escrito doce lineas mas abajo-; con cuatro veces mas ruido llega a la
 * decena, que es justo lo que el midio.
 *
 * Y LO PEOR: el banco salia verde porque le estaban dando el trabajo YA
 * HECHO. Un banco que mide una copia limpiada por un paso que el aparato no
 * da no mide nada. Esta es la regla de la casa incumplida por el lado que
 * mas duele.
 *
 * Asi que la extraccion de fase se muda AQUI -dcf77_fase_iq()-, que es donde
 * se puede medir, y lleva un paso bajo sobre I y Q por delante del atan2. El
 * elemento dura 100 ms: todo su contenido esta muy por debajo de estos
 * hercios y no se pierde nada de lo que importa.
 *
 * DOS POLOS A 25 Hz, Y NO ES A OJO. Barrido de orden y corte contra la
 * grabacion de verdad con ruido inyectado, contando tramas buenas
 * (sim/als_iq.c):
 *
 *     1 polo  a 40 Hz   decodifica hasta sigma 0,2
 *     1 polo  a 25 Hz   hasta 0,4
 *     2 polos a 40 Hz   hasta 0,2
 *     2 polos a 25 Hz   hasta 0,4   <- y el numero aguanta hasta 1,6
 *     2 polos a 15 Hz   ya se come el elemento
 *     3 polos a 15 Hz   no decodifica ni sin ruido
 *
 * Y LO QUE DE VERDAD IMPORTA, porque es la pregunta que hizo el usuario -"no
 * tengo claro si estoy recibiendo realmente el als162"-: con dos polos a
 * 25 Hz el numero de marca SEPARA los dos casos, que es lo que antes no
 * hacia:
 *
 *     con ALS162 delante   0,72 .. 0,81   de sigma 0 a 1,6
 *     solo ruido           0,11 .. 0,50
 *
 * Antes, sin filtro, los dos casos daban un numero enorme y se veian
 * IGUAL. Por eso no se podia saber. Ahora 0,7 para arriba es que el
 * elemento esta ahi; medio punto para abajo es que ahi no hay nada.
 */
#ifndef FASE_IQ_HZ
#define FASE_IQ_HZ 25.0f
#endif
#ifndef FASE_IQ_POLOS
#define FASE_IQ_POLOS 2
#endif
static float   s_iq_i[FASE_IQ_POLOS], s_iq_q[FASE_IQ_POLOS];
static float   s_a_iq;           /* el coeficiente, de s_fs */
static uint8_t s_iq_listo;
/*
 * *** Y CENTRADO EN LA PORTADORA, NO EN EL CERO - 10/10/2026. ***
 *
 * El paso bajo de arriba se estreno a 25 Hz y sim/alstest.c se puso rojo en
 * su seccion 3, que es la que mezcla 190 Hz de error de oscilador CON
 * ruido. Y era cierto.
 *
 * Sin ruido el desvio no estorba: el filtro atenua I y Q por igual, y
 * atan2 no mira la amplitud. Medido de 0 a 200 Hz de desvio: 2 tramas
 * buenas en todos.
 *
 * Con ruido es otra cosa. El filtro esta centrado en continua, asi que
 * atenua la portadora -que esta a 190 Hz- y NO atenua el ruido que cae
 * dentro de su paso. La relacion señal-ruido despues del filtro sale PEOR
 * que sin filtro. Medido, tramas buenas con sigma 0,2:
 *
 *     desvio        0 Hz   20 Hz   65 Hz   110 Hz   200 Hz
 *     25 Hz            2       2       0        0        0   <- se cae
 *     150 Hz           2       2       2        2        2
 *     sin filtro       2       2       2        1        0
 *
 * Y ensanchar a 150 Hz no vale, porque entonces vuelve a desbocarse el
 * numero de marca, que es justo lo que habia que arreglar:
 *
 *     sigma          0,8    1,6    2,4
 *     25 Hz         0,75   0,81   3,12
 *     150 Hz        4,90  15,36  16,32   <- otra vez el paseo
 *
 * O sea que hace falta el filtro ESTRECHO y CENTRADO. El desvio ya lo
 * estima feed_fase() en s_deriva -radianes por muestra-, asi que se usa
 * ese: se desrota I y Q antes de filtrar y el filtro ve la portadora en
 * continua, que es donde su paso la espera.
 *
 * No hay pescadilla mordiendose la cola: el filtro escala el fasor pero no
 * lo gira, y atan2 no mira la amplitud, asi que s_deriva sale bien aunque
 * la desrotacion todavia no haya arrancado. Converge sola.
 *
 * El fasor se gira recursivamente -una multiplicacion compleja por
 * muestra- en vez de llamar a cosf/sinf, que aqui se llaman solo cuando el
 * desvio se mueve de verdad. Y se renormaliza cada 256 muestras con un
 * paso de Newton, porque multiplicar miles de veces un vector unitario lo
 * acaba encogiendo.
 */
static float   s_rot_c, s_rot_s;    /* el fasor de desrotacion */
static float   s_rot_w;             /* el desvio con el que se calculo */
static float   s_rot_cw, s_rot_sw;  /* su paso por muestra */
static uint32_t s_rot_n;

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

/*
 * *** LOS 100 ms DE SILENCIO DE DELANTE - 10/10/2026, Y ES EL FALLO ENTERO
 * DE ALS162. ***
 *
 * La norma de Allouis, palabra por palabra:
 *
 *   "One signal element is always sent at each second between 0 and 58."
 *   "Two signal elements are sent in sequence to represent a binary one;
 *    otherwise it is interpreted as binary zero."
 *   "Additional non-timing data is sent by phase modulation during the
 *    rest of each second."
 *   "the second marker (and data bit) is always preceded by 100 ms without
 *    any phase modulation"
 *
 * Las dos primeras ya estaban aqui y estaban BIEN. La tercera NO ESTABA, y
 * es la que rompia el modo: el resto de cada segundo lleva mas modulacion
 * de fase, que no es horaria, y este contador la contaba igual. Con tres o
 * mas elementos en un segundo saltaba la rama de "imposible: ruido", se
 * tiraba el bit y no se completaba una trama en la vida.
 *
 * MEDIDO sobre la grabacion de TDF del usuario (ver sim/als_real.c): **4,19
 * elementos por segundo** contados con histeresis, 5,73 con umbral unico, y
 * repartidos por todo el segundo -concentracion circular 0,155, o sea
 * uniforme-. El tope de aqui eran dos.
 *
 * Y la cuarta frase es el arreglo, porque es lo que SEPARA una cosa de la
 * otra: el elemento horario es el unico que viene detras de 100 ms sin
 * modulacion. Los de datos van pegados unos a otros. Asi que contar deja de
 * ser "todo lo que suba del umbral" y pasa a ser "lo que suba del umbral
 * detras de un silencio", mas su pareja si la trae.
 *
 * 80 ms y no 100: el integrador con fuga rebota despues del elemento y el
 * rebote se come un poco del silencio por el principio. Los dos elementos
 * de un "1" van SEGUIDOS -el primero ocupa 0..100 ms y el segundo
 * 100..200-, asi que entre ellos no hay silencio ninguno y esta puerta no
 * los puede confundir: al segundo lo recoge la rama de s_elem_n != 0.
 */
#ifndef QUIETO_MIN_MS
#define QUIETO_MIN_MS 60.0f
#endif
/*
 * Lo torcido que puede llegar un marcador respecto de la rejilla de 1 s y
 * seguir valiendo. Medido sobre la grabacion limpia: de 337 candidatos, los
 * 165 buenos caen a menos de 50 ms. Con 70 hay aire para el ruido sin abrir
 * la puerta a los de datos, que caen en cualquier punto del segundo.
 */
#ifndef SEG_VENTANA_MS
#define SEG_VENTANA_MS  70.0f
#endif
/* Por debajo de esto el triangulo se considera plano. Ver el comentario del
 * contador de silencio: se mide sobre s_tri y no sobre el filtrado. */
#ifndef QUIETO_UMBRAL
#define QUIETO_UMBRAL   0.30f
#endif
/*
 * Y SOBRE LA ENVOLVENTE DE s_tri, NO SOBRE s_tri A PELO.
 *
 * Mirar |s_tri| muestra a muestra funciona con señal limpia y se cae con
 * ruido: s_tri es la INTEGRAL, o sea que integrar ruido blanco da un paseo
 * aleatorio y el valor instantaneo cruza cualquier umbral todo el rato. Es
 * la misma razon por la que existe el filtro de 12 Hz de `alta`, solo que
 * alli el problema era detectar y aqui es medir el hueco.
 *
 * Asi que el silencio se mide sobre una envolvente rapida -un polo a 25 Hz
 * sobre |s_tri|-, que es lo bastante corta para no comerse los 100 ms del
 * hueco y lo bastante larga para que el paseo aleatorio no la mueva. Son
 * 40 ms de constante contra los 100 del hueco.
 */
#ifndef QUIETO_ENV_HZ
#define QUIETO_ENV_HZ   25.0f
#endif
#ifndef QUIETO_LPF_HZ
/*
 * Y ANTES DE LA ENVOLVENTE, UN PASO BAJO PROPIO.
 *
 * s_tri es la INTEGRAL de la derivada de la fase, asi que con ruido blanco
 * de entrada es un PASEO ALEATORIO: con 0,15 rad de ruido y la fuga de 2 Hz
 * -una constante de 80 ms, o sea 120 muestras- el paseo llega a ~2,3 rad,
 * que se come cualquier umbral. Subir el umbral no arregla nada: se midio de
 * 0,30 a 0,90 y el resultado es el mismo.
 *
 * `alta` ya esta filtrado, pero a 12 Hz, y eso arrastra la cola del elemento
 * anterior y tapa justo el hueco que hay que medir (cubria el 75% de los
 * marcadores contra el 100% sin filtrar).
 *
 * 35 Hz parte la diferencia: baja el ruido por sqrt(35/750) = 0,22 y deja
 * pasar entero un hueco de 100 ms, cuyo contenido esta muy por debajo de 35
 * Hz. Es un filtro propio y no uno reutilizado porque las dos medidas piden
 * cosas distintas: detectar quiere suavidad, medir el hueco quiere borde.
 */
#define QUIETO_LPF_HZ   35.0f
#endif
/*
 * CON SIGNO, Y NO ES UN DETALLE. La correccion del enganche deja aqui el
 * error que queda respecto de la rejilla, y ese error es NEGATIVO cuando el
 * marcador llega antes de tiempo - que es la mitad de las veces -. En
 * uint32_t, un -92 se convierte en 4.294.967.204, el "segundo" pasa a durar
 * 33 dias y la rama del segundo 59 se dispara en CADA MUESTRA: 50 "raras"
 * por segundo y ni un bit. Visto en el banco de la grabacion de verdad.
 */
static int32_t  s_seg_n;          /* muestras desde el ultimo marcador */
static uint8_t  s_seg_enganchado; /* 1 = la rejilla de 1 s esta cogida */
static uint8_t  s_seg_tentativo;  /* 1 = un primer marcador sin confirmar */
static uint8_t  s_seg_vacios;     /* segundos seguidos sin marcador */
/*
 * Donde tiene que caer el SEGUNDO elemento de un "1", contado desde el
 * primero. La norma lo pone "in sequence": el primero ocupa 0..100 ms y el
 * segundo 100..200, asi que el centro cae a 100 ms. Con el refractario de
 * 60 por abajo y 160 por arriba quedan +-60 ms de aire, que es mas de lo
 * que el filtro y la propagacion pueden correr un flanco, y bastante menos
 * que los 250 ms a los que empiezan los datos del resto del segundo.
 */
#define ELEM_PAREJA_MAX_MS 160.0f
static uint32_t s_quieto_n;      /* muestras seguidas con la fase plana */
static uint32_t s_quieto_ult;    /* y lo que duro el ultimo, ya cerrado */
static float    s_triq;          /* s_tri filtrado solo para medir el hueco */
static float    s_a_triq;
static float    s_env;           /* y su envolvente rapida */
static float    s_a_env;
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
    /* La fila del RDS es la unica cuyo nombre es una frase y no un
     * indicativo de emisora: las demas ("DCF77 - 77,5 kHz, Alemania") son
     * nombres propios y no se traducen. */
    if (e == HORA_RDS) { return tr("RDS - la FM que escuches", "RDS - whatever FM you tune"); }
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
/* Por que se rechazo la ultima trama. Ver dcf77_motivo(). */
static uint8_t  s_motivo = DCF_MOT_NINGUNO;

/* La trama anterior que paso todas las comprobaciones, para exigir que la
 * siguiente sea exactamente un minuto mayor. */
static uint8_t  s_prev_valida;
static uint8_t  s_prev_h, s_prev_m, s_prev_d, s_prev_mes, s_prev_a, s_prev_ds, s_prev_ver;

static uint8_t  s_h, s_m, s_d, s_mes, s_a, s_ds, s_ver;
/* La hora UTC TAL COMO LA EMITIO, antes de sumarle el huso. Solo la ponen
 * las emisoras que emiten UTC de verdad -aqui MSF-, y existe para una cosa:
 * poder MEDIR el huso restandosela al reloj de la radio. Ver el boton de la
 * pantalla del huso en main.c. DCF77 y ALS162 emiten hora local alemana y
 * francesa, asi que dejan s_utc_hay a cero y no se puede medir con ellas. */
static uint8_t  s_utc_h, s_utc_m, s_utc_hay;


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
    /* Y el UTC crudo se olvida: cambiar de emisora deja un utc_hay a 1 con
     * la hora de la anterior, y con eso se mediria el huso contra una
     * emisora que ya no se esta oyendo. */
    s_utc_hay = 0U; s_utc_h = 0U; s_utc_m = 0U;
    s_motivo = DCF_MOT_NINGUNO;
    s_quieto_n = 0U;
    s_quieto_ult = 0U;
    s_triq = 0.0f;
    s_env = 0.0f;
    s_seg_n = 0;
    s_seg_enganchado = 0U;
    s_seg_tentativo = 0U;
    s_seg_vacios = 0U;
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
    s_iq_listo = 0U;
    s_rot_c = 1.0f; s_rot_s = 0.0f; s_rot_n = 0U;
    s_rot_w = 0.0f; s_rot_cw = 1.0f; s_rot_sw = 0.0f;
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
    s_a_iq = 6.2831853f * FASE_IQ_HZ / s_fs;
    if (s_a_iq > 1.0f) { s_a_iq = 1.0f; }
    s_a_deriva = 6.2831853f * FASE_DERIVA_HZ / s_fs;
    s_a_fuga   = 6.2831853f * FASE_FUGA_HZ   / s_fs;
    s_a_triq   = 6.2831853f * QUIETO_LPF_HZ  / s_fs;
    if (s_a_triq > 1.0f) { s_a_triq = 1.0f; }
    s_a_env    = 6.2831853f * QUIETO_ENV_HZ  / s_fs;
    if (s_a_env > 1.0f) { s_a_env = 1.0f; }
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
/*
 * *** POR QUE SE RECHAZO LA ULTIMA TRAMA - 09/10/2026. ***
 *
 * Un usuario de Hungria con ALS162 a S5-S6, la barra de marca llena y el
 * contador de bits dando vueltas de 0 a 59 sin acabar nunca. Con lo que
 * habia en pantalla -bits, marca, ciclo, raras- eso no se puede separar en
 * dos casos que piden arreglos distintos:
 *
 *   a) las tramas NO se completan: el hueco llega con un numero de bits que
 *      no es 59. Eso es sincronismo o cuenta de elementos.
 *   b) las tramas SI se completan y se RECHAZAN. Entonces hace falta saber
 *      por cual de las ocho puertas, porque cada una apunta a otra cosa.
 *
 * Y dentro de (b) hay una sospecha concreta que esto existe para confirmar o
 * descartar: ESTA FUNCION ES EL DECODIFICADOR DE DCF77, y ALS162 la usa tal
 * cual. El banco sim/alstest.c tambien fabrica la trama con los bits 0, 17,
 * 18 y 20 puestos como los de DCF77 -su comentario dice "el reparto de
 * 20..58 es identico, que es todo lo que se decodifica"- y eso NO cuadra con
 * lo que hay aqui: las tres primeras puertas miran los bits 0, 17 y 18, que
 * estan FUERA de ese 20..58. El banco da por buena justo la suposicion que
 * habria que comprobar, asi que sale verde la crea uno o no. Un banco que
 * mide una copia de lo que yo creo no mide la señal.
 *
 * Si el motivo que sale de la radio es B0, B20 o Z1Z2, la suposicion es
 * falsa y Allouis no reparte esos bits como DCF77. Si es una paridad, es
 * ruido. Son dos arreglos que no se parecen en nada, y sin este numero
 * habria que elegir a cara o cruz.
 */
uint8_t dcf77_motivo(void)
{
    return s_motivo;
}

static uint8_t decodifica(const uint8_t *b)
{
    uint8_t h, m, d, mes, a, ds;

    if (b[0] != 0U)  { s_motivo = DCF_MOT_B0;  return 0U; }  /* siempre 0 */
    if (b[20] != 1U) { s_motivo = DCF_MOT_B20; return 0U; }  /* siempre 1 */
    /* Z1 y Z2 son opuestos por definicion */
    if (b[17] == b[18]) { s_motivo = DCF_MOT_Z1Z2; return 0U; }

    if (!paridad_par_ok(b, 21U, 28U)) { s_motivo = DCF_MOT_PAR_MIN; return 0U; }
    if (!paridad_par_ok(b, 29U, 35U)) { s_motivo = DCF_MOT_PAR_HOR; return 0U; }
    if (!paridad_par_ok(b, 36U, 58U)) { s_motivo = DCF_MOT_PAR_FEC; return 0U; }

    m   = bcd(b, 21U, 7U);
    h   = bcd(b, 29U, 6U);
    d   = bcd(b, 36U, 6U);
    ds  = bcd(b, 42U, 3U);
    mes = bcd(b, 45U, 5U);
    a   = bcd(b, 50U, 8U);

    if (m > 59U || h > 23U || mes < 1U || mes > 12U
        || d < 1U || d > k_dias_mes[mes] || ds < 1U || ds > 7U || a > 99U) {
        s_motivo = DCF_MOT_RANGO;
        return 0U;
    }

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
     * *** EL CUARTO SITIO DONDE ESTABA ESPAÑA CABLEADA - 10/10/2026. ***
     *
     * Decia, literalmente:
     *     h = (h + (verano ? 2 : 1)) % 24;
     * con un comentario que explicaba muy bien por que: "España peninsular
     * va una hora por delante en invierno y dos en verano". Perfecto, si la
     * radio esta en España.
     *
     * Este no salio al repasar husos: salio buscando otra cosa, DESPUES de
     * haber dado por limpios hora_wwv.c, hora_rbu.c y el byte 40 del
     * EMISORAS.BIN. Tres sitios eran cuatro. Lo escribo aqui porque el
     * fallo de verdad no fue la suma: fue dar por cerrada una lista de
     * sitios sin haberla sacado de un grep.
     *
     * Ahora se suma el huso, que es UN numero (hora_comun.c), y se arrastra
     * el calendario: la version anterior dejaba la fecha como venia -lo
     * decia su propio comentario- y por tanto a las 23:30 UTC la fecha era
     * la de ayer. Con hora_suma_minutos() eso sale gratis.
     *
     * Y `s_ver` deja de salir del bit 58. Ese bit dice si hay horario de
     * verano EN EL REINO UNIDO, que es la regla europea; en Sidney o en
     * Tokio no dice nada de donde estas. Quien si lo sabe -DCF77 y ALS162,
     * que emiten hora local- lo sigue poniendo.
     */
    s_utc_h = h; s_utc_m = m; s_utc_hay = 1U;   /* para medir el huso */
    {
        uint8_t dsx = (uint8_t)((ds == 0U) ? 7U : ds);
        uint8_t dd = d, mm = mes, aa = anno, hh = h, mi = m;

        hora_suma_minutos(hora_huso(), &hh, &mi, &dd, &mm, &aa, &dsx);
        s_h = hh; s_m = mi; s_d = dd; s_mes = mm; s_a = aa;
        /* MSF cuenta 0=domingo y DCF77 1=lunes: hora_suma_minutos() ya
         * devuelve el convenio de DCF77, que es el que usa dcf_info_t. */
        s_ds = dsx;
    }
    s_ver = 0U;
    (void)verano;
    return 1U;
}

/* Fin de trama: llega el hueco de dos segundos. */
static void cierra_trama(void)
{
    if (s_n_bits != 59U) {
        /* El hueco del minuto llego con otro numero de bits: la trama no se
         * llego a completar. Es el caso (a) de dcf77_motivo(). */
        if (s_n_bits > 0U) { s_malas++; s_motivo = DCF_MOT_CORTA; }
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
    s_motivo = DCF_MOT_NINGUNO;   /* esta paso: lo de antes ya no describe nada */
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

    /*
     * ======================================================================
     * EL SEGUNDO, CON ENGANCHE A LA REJILLA - REHECHO EL 10/10/2026
     * ======================================================================
     *
     * Lo de antes contaba TODA subida de la fase y, con tres o mas en un
     * segundo, declaraba ruido. En Allouis eso no funciona nunca, y la
     * norma dice por que:
     *
     *   "Additional non-timing data is sent by phase modulation during the
     *    rest of each second."
     *
     * MEDIDO sobre una grabacion limpia de 168 s (WebSDR en 161,1 kHz, ver
     * sim/als_real.c): **6,24 elementos por segundo**, de los cuales solo
     * **1,05/s** son horarios. Los otros cinco son ese canal de datos, con
     * LA MISMA forma y LA MISMA amplitud:
     *
     *     pico a pico del elemento   horarios 1,809 rad   datos 1,904 rad
     *
     * O sea que por la forma no se separan, y por la amplitud tampoco.
     *
     * LA NORMA OFRECE UN DISCRIMINANTE - "the second marker (and data bit)
     * is always preceded by 100 ms without any phase modulation" - PERO
     * MEDIDO NO BASTA SOLO: exigiendo 60 ms de silencio pasan el 78% de los
     * horarios y tambien el 17% de los de datos; subiendo a 100 ms se cae al
     * 41% de los horarios. Sirve para PROPONER candidatos, no para decidir.
     *
     * LO QUE DECIDE ES EL RELOJ. De 337 candidatos en 168 s, 165 caen a
     * menos de 50 ms de una rejilla de 1,000 s, y esos 165 son los
     * marcadores: sus tres huecos de 2,001 s estan separados exactamente
     * 60,000 s y son los segundos 59. Asi que esto se engancha a la rejilla
     * y descarta lo que no cae en ella - que es lo que hace cualquier
     * receptor de verdad, y lo que las ramas de envolvente ya hacian a su
     * manera -.
     *
     * COMO ENGANCHA. Un candidato tentativo arranca el reloj; si el
     * siguiente llega a 1000 +-SEG_VENTANA_MS, enganchado. Ya enganchado
     * solo se miran los candidatos de esa ventana, y el reloj se corrige
     * DESPACIO -una octava parte del error- para que un marcador torcido por
     * el ruido no arrastre la rejilla entera. Un segundo sin marcador es el
     * 59 y cierra la trama; tres seguidos es que se ha perdido.
     */
    s_seg_n++;
    s_n_desde_flanco++;
    ms = (uint32_t)((float)s_n_desde_flanco * 1000.0f / s_fs);

    {
        int32_t  seg_ms = (int32_t)((float)s_seg_n * 1000.0f / s_fs);
        uint8_t  sube   = 0U;

        /* --- 3. Una subida del triangulo, con su histeresis --- */
        if (!s_fase_arriba) {
            if (alta > FASE_UMBRAL) { s_fase_arriba = 1U; sube = 1U; }
        } else if (alta < FASE_UMBRAL * 0.5f) {
            s_fase_arriba = 0U;
        }

        if (sube) {
            /* El silencio que hubo justo antes de ESTA subida: el
             * latcheado, no el de ahora. Ver s_quieto_ult. */
            uint32_t quieto_ms = (uint32_t)((float)s_quieto_ult * 1000.0f / s_fs);

            if (s_elem_n == 1U
                && (ms - s_elem_ultimo_ms) >= (uint32_t)ELEM_REFRACTARIO_MS
                && (ms - s_elem_ultimo_ms) <= (uint32_t)ELEM_PAREJA_MAX_MS) {
                /*
                 * LA PAREJA. "Two signal elements are sent in sequence to
                 * represent a binary one": el primero ocupa 0..100 ms y el
                 * segundo 100..200, asi que tiene que caer AHI y no en
                 * cualquier punto de la ventana - que es lo que valia antes,
                 * y por eso el primer elemento de datos se contaba como
                 * pareja y un '0' se leia '1' -. A este no se le pide
                 * silencio delante: va pegado al primero por construccion.
                 */
                s_elem_ultimo_ms = ms;
                s_elem_n = 2U;
                s_ms_marca = 200U;
            } else if (s_elem_n == 0U && quieto_ms >= (uint32_t)QUIETO_MIN_MS) {
                /* UN CANDIDATO A MARCADOR: silencio delante y, si ya hay
                 * rejilla, en su sitio. */
                uint8_t en_ventana = (uint8_t)(seg_ms >= 1000 - (int32_t)SEG_VENTANA_MS
                                               && seg_ms <= 1000 + (int32_t)SEG_VENTANA_MS);

                if (!s_seg_enganchado || en_ventana) {
                    if (s_seg_tentativo && en_ventana) {
                        s_seg_enganchado = 1U;
                    } else if (!s_seg_enganchado) {
                        s_seg_tentativo = 1U;
                        s_n_bits = 0U;
                        s_prev_valida = 0U;
                    }

                    s_ms_ciclo = (uint16_t)((seg_ms > 9999 || seg_ms < 0)
                                            ? 9999 : seg_ms);
                    if (s_seg_enganchado) {
                        /* Una octava parte del error se queda, para que la
                         * rejilla siga despacio y no a tirones. Con signo:
                         * ver el comentario de s_seg_n. */
                        int32_t err = s_seg_n - (int32_t)(s_fs + 0.5f);
                        s_seg_n = err - err / 8;
                    } else {
                        s_seg_n = 0;
                    }
                    s_seg_vacios = 0U;
                    s_n_desde_flanco = 0U;
                    ms = 0U;
                    s_elem_cerrado = 0U;
                    s_elem_ultimo_ms = 0U;
                    s_elem_n = 1U;
                    s_ms_marca = 100U;
                }
            }
            /* Lo demas son los datos del resto del segundo: se ignoran
             * ENTEROS. Ni cuentan, ni mueven el reloj, ni son "raras". */
        }

        /*
         * El contador de silencio, DESPUES de mirar la puerta: lo que decide
         * es cuanto llevaba plano ANTES de esta muestra. En valor absoluto,
         * porque el elemento baja a -1 rad por el medio y mirar solo la
         * subida daria por "silencio" media forma de cada elemento.
         */
        /*
         * *** Y EL SILENCIO SE MIDE SOBRE s_tri, NO SOBRE `alta`. ***
         *
         * `alta` es s_tri pasado por el filtro de 12 Hz, que esta ahi para
         * que integrar ruido no se convierta en un paseo aleatorio. Para
         * DETECTAR el elemento va bien; para medir el SILENCIO de delante
         * va mal, porque ese filtro arrastra la cola de lo anterior y se
         * come el hueco.
         *
         * Medido sobre la grabacion limpia, cuantos de los 165 marcadores
         * de verdad quedan cubiertos por un candidato:
         *
         *     silencio sobre |alta| < 0,175, 60 ms   ->  123 de 165 (75%)
         *     silencio sobre |alta| < 0,175, 40 ms   ->  144 de 165 (87%)
         *     silencio sobre |tri|  < 0,30,  60 ms   ->  165 de 165 (100%)
         *     silencio sobre |tri|  < 0,30,  80 ms   ->  162 de 165 (98%)
         *
         * Con el 75% se pierde un marcador de cada cuatro, y cada perdida
         * la lee la rama del segundo 59 como fin de minuto: la trama se
         * cierra corta una y otra vez y no se completa jamas. Con el 100%
         * se pierde ninguno, y los candidatos de mas -que son la mitad- los
         * tira la ventana de la rejilla, que es para lo que esta.
         */
        s_triq += s_a_triq * (s_tri - s_triq);
        s_env  += s_a_env * ((s_triq < 0.0f ? -s_triq : s_triq) - s_env);
        if (s_env > QUIETO_UMBRAL) {
            /*
             * *** Y SE LATCHEA AL ROMPERSE, QUE ES LO QUE COSTO LA TARDE. ***
             *
             * La primera version leia s_quieto_n directamente en la puerta
             * y la puerta no se abria NUNCA. La razon: el elemento sube en
             * rampa, asi que cruza este 0,175 ANTES de llegar al 0,35 que
             * dispara `sube`. Entre los dos cruces el contador ya se habia
             * puesto a cero, y cuando la puerta iba a mirarlo valia 0.
             *
             * Se vio porque el banco de la grabacion de verdad daba cero
             * bits y "raras" subiendo una vez por segundo, mientras que la
             * misma cadena replicada en Python decia que hay 1,59 silencios
             * de 80 ms o mas por segundo. Los silencios estaban; lo que no
             * estaba era quien se acordara de ellos.
             */
            if (s_quieto_n != 0U) { s_quieto_ult = s_quieto_n; }
            s_quieto_n = 0U;
        } else if (s_quieto_n < 0xFFFFFF00UL) {
            s_quieto_n++;
        }

        /* --- 4. Cerrada la ventana de la pareja, lo contado ES el bit --- */
        if (s_elem_n > 0U && !s_elem_cerrado && ms >= (uint32_t)ELEM_VENTANA_MS) {
            s_elem_cerrado = 1U;
            if (s_n_bits < 59U) {
                s_bits[s_n_bits] = (uint8_t)(s_elem_n >= 2U ? 1U : 0U);
                s_n_bits++;
                if (s_estado == DCF_BUSCANDO || s_estado == DCF_PERDIDO) {
                    s_estado = DCF_LEYENDO;
                }
            }
            s_elem_n = 0U;
        }

        /* --- 5. Un segundo sin marcador: ese es el 59, y cierra la trama --- */
        if (seg_ms > 1000 + (int32_t)SEG_VENTANA_MS) {
            s_seg_n -= (int32_t)(s_fs + 0.5f);   /* el reloj sigue solo */
            s_elem_n = 0U;
            s_elem_cerrado = 1U;
            s_seg_vacios++;
            if (s_seg_vacios == 1U && s_seg_enganchado) {
                /*
                 * UNO es el segundo 59 y es lo normal: "the 59th second has
                 * no phase modulation". Aqui se cierra el minuto.
                 */
                cierra_trama();
            } else if (s_seg_vacios >= 3U) {
                /* TRES seguidos ya no es el 59: se ha perdido la señal. */
                s_seg_enganchado = 0U;
                s_seg_tentativo = 0U;
                s_marcas_raras++;
                s_n_bits = 0U;
                s_prev_valida = 0U;
                if (s_estado == DCF_LEYENDO || s_estado == DCF_CONFIRMANDO) {
                    s_estado = DCF_PERDIDO;
                }
            }
        }
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
/*
 * I y Q -ya promediados por el bloque- a fase, con el paso bajo delante.
 * Ver el comentario de s_iq_i: esto estaba en demod_am.c sin filtro y sin
 * banco que lo mirara.
 *
 * El primer par entra tal cual en vez de arrancar desde cero: si no, el
 * filtro tarda cinco constantes en llegar al sitio y durante ese rato el
 * angulo gira entero, que es precisamente lo que el desenrollado de
 * feed_fase() no puede comerse.
 */
float dcf77_fase_iq(float i, float q)
{
    int k;
    float ir, qr;

    /* 1. El paso del fasor, recalculado solo cuando el desvio se mueve. */
    {
        float d = s_deriva - s_rot_w;

        if (d > 0.001f || d < -0.001f) {
            s_rot_w  = s_deriva;
            s_rot_cw =  cosf(s_rot_w);
            s_rot_sw = -sinf(s_rot_w);   /* giro de -w: desrota */
        }
    }

    /* 2. Un paso del fasor, y renormalizar de vez en cuando. */
    {
        float c = s_rot_c * s_rot_cw - s_rot_s * s_rot_sw;
        float t = s_rot_c * s_rot_sw + s_rot_s * s_rot_cw;

        s_rot_c = c; s_rot_s = t;
        s_rot_n++;
        if ((s_rot_n & 0xFFU) == 0U) {
            float m = s_rot_c * s_rot_c + s_rot_s * s_rot_s;
            float g = 1.5f - 0.5f * m;   /* Newton de 1/sqrt alrededor de 1 */

            s_rot_c *= g; s_rot_s *= g;
        }
    }

    /* 3. Desrotar, y ya si el filtro. */
    ir = i * s_rot_c - q * s_rot_s;
    qr = i * s_rot_s + q * s_rot_c;

    if (!s_iq_listo) {
        for (k = 0; k < FASE_IQ_POLOS; k++) { s_iq_i[k] = ir; s_iq_q[k] = qr; }
        s_iq_listo = 1U;
    } else {
        float vi = ir, vq = qr;
        for (k = 0; k < FASE_IQ_POLOS; k++) {
            s_iq_i[k] += s_a_iq * (vi - s_iq_i[k]); vi = s_iq_i[k];
            s_iq_q[k] += s_a_iq * (vq - s_iq_q[k]); vq = s_iq_q[k];
        }
    }

    /*
     * Y se devuelve el angulo CON la rotacion puesta otra vez, porque
     * feed_fase() es quien estima s_deriva y necesita ver la rampa. Si se
     * le diera el angulo ya desrotado, s_deriva caeria a cero, la
     * desrotacion se apagaria y volveria a empezar: un lazo que oscila.
     */
    return rapido_atan2(s_iq_q[FASE_IQ_POLOS - 1], s_iq_i[FASE_IQ_POLOS - 1])
         - rapido_atan2(s_rot_s, s_rot_c);
}

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
    out->utc_hay    = s_utc_hay;
    out->utc_hora   = s_utc_h;
    out->utc_minuto = s_utc_m;
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
