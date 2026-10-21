/*
 * WEFAX. Ver wefax.h para el porque de cada pieza.
 */
#include "wefax.h"
#include "disc_fm.h"
#include <string.h>
#include <math.h>

/*
 * EL CERROJO CORTO - 21/10/2026.
 *
 * wefax_process() corre DENTRO de la interrupcion de audio y el resto de
 * este fichero corre en el bucle principal. Lo que tocan los dos lados
 * tiene que estar protegido, y aqui no lo estaba: ni con `volatile` ni con
 * nada.
 *
 * `volatile` solo obliga a ir a memoria; NO hace atomico un `lista--`, que
 * en un Cortex-M4 son tres instrucciones interrumpibles. Y sin `volatile`
 * es todavia peor: a -O2 el compilador ve que nadie fuera de este fichero
 * toca `s_fase`, se la guarda en un registro durante las N muestras del
 * bloque y la vuelca al salir. Si el bucle principal escribe un cero ahi
 * en medio -que es lo que hace el boton de Sincroniza-, ese cero DESAPARECE
 * al terminar la interrupcion. Un boton que a veces no hace nada y no deja
 * rastro.
 *
 * Es el mismo reparto que ya usan User/encoder.c y User/sdr_rx.c.
 */
#if defined(__GNUC__) && defined(__ARM_ARCH)
#include "gd32f4xx.h"
/*
 * Y CON BARRERA, porque el CMSIS de este arbol no la trae - 21/10/2026.
 * core_cmFunc.h declara "cpsid i" y el MSR sin clobber de memoria, asi que
 * formalmente el compilador puede mover accesos a traves del par. Casi todo
 * lo que va dentro es volatile y no se mueve, pero el memcpy de
 * wefax_linea_en() no lo es. La barrera cuesta cero instrucciones.
 */
#define WFX_CIERRA()  uint32_t wfx_pm_ = __get_PRIMASK(); __disable_irq(); \
                      __asm volatile("" ::: "memory")
#define WFX_ABRE()    do { __asm volatile("" ::: "memory"); \
                           __set_PRIMASK(wfx_pm_); } while (0)
#else
#define WFX_CIERRA()  do { } while (0)
#define WFX_ABRE()    do { } while (0)
#endif

/*
 * AQUI VIVIO TCMRAM_BSS, Y SE FUE EL 21/10/2026.
 *
 * Era el atributo que ponia el anillo de lineas en la TCM. El anillo salio
 * de ahi -ver su comentario: escribe 1,5 kB por segundo y eso lo lleva la
 * RAM normal- y la macro se quedo, definida dos veces por el #ifdef y usada
 * CERO. El tercer resto de esta familia en esta revision, detras de
 * s_tira_ranura y WFX_FUERA_MS.
 */

#define PI_F 3.14159265358979f

/*
 * Las cuatro velocidades, EN ORDEN CRECIENTE. El orden importa porque esta
 * lista es tambien la que recorre el ajuste de la interfaz: puesta como
 * estaba -120 primero, para que el indice 0 fuese la normal- el mando iba
 * 120, 60, 90, 240, que no es el orden de nada. Ahora el indice 0 es la mas
 * lenta y la normal es WEFAX_LPM_DEF_I.
 */
static const uint16_t k_lpm[WEFAX_LPM_N] = { 60U, 90U, 120U, 240U };
#define WEFAX_LPM_DEF_I 2U   /* 120 lpm, la normal */

/* Lineas de la señal de fase que se miran antes de decidir la velocidad. */
#define FASE_LINEAS 16U

/*
 * LO QUE DURA LA EMISION, EN UN SOLO SITIO - 21/10/2026.
 *
 * Estos dos numeros no son nuestros: los pone la emisora. Una carta de fax
 * meteorologico empieza con CINCO segundos de tono de arranque y sigue con
 * TREINTA de señal de fase, y de ahi salen dos cuentas de este fichero que
 * no tienen nada que ver entre si:
 *
 *   - cuantas pasadas de enganche caben (pasadas_que_caben), que sale de
 *     los 30 de la fase;
 *   - y cuanto se espera como mucho antes de ponerse a pintar
 *     (tope_lineas_fase), que sale de los 5 MAS los 30, porque esa cuenta
 *     empieza cuando se reconoce el tono de arranque.
 *
 * Estaban escritos como un `/ 2U` con un comentario «30 s» al lado y como
 * un `35UL` con su derivacion en prosa. Dos codificaciones distintas del
 * mismo dato, en dos funciones que nadie lee juntas: el dia que una
 * emisora cambie -o que haya que admitir otra- se corrige una y la otra se
 * queda, y lo que sale es una carta con franja o una carta sin cabecera,
 * segun cual se quede.
 *
 * Las cuentas salen EXACTAMENTE iguales que antes: k_lpm/2 es k_lpm*30/60
 * para las cuatro velocidades, y 5 + 30 son 35.
 */
#define FASE_SEG      30U   /* la señal de fase */
#define ARRANQUE_SEG   5U   /* y el tono que va delante */

/*
 * Y EL ANCHO DEL PULSO BLANCO DE LA SEÑAL DE FASE, por lo mismo.
 *
 * Es el 5% de la linea, y tambien lo pone la emisora. Salia DOS VECES: en
 * prosa -"El pulso ocupa el primer 5% de la linea, asi que su centro va en
 * el 2,5%"- y como un `0.025f` sin nada que lo atara a esa frase. Quien
 * corrigiera el 5% tendria que acordarse de dividirlo a mano.
 */
#define FASE_PULSO  0.05f

/* ==========================================================================
 * LA ESCALA DE GRISES
 * ========================================================================== */
uint8_t wefax_gris_de_hz(float hz)
{
    float t = (hz - WEFAX_NEGRO_HZ) / (WEFAX_BLANCO_HZ - WEFAX_NEGRO_HZ);
    if (t <= 0.0f) { return 0U; }
    if (t >= 1.0f) { return 255U; }
    return (uint8_t)(t * 255.0f + 0.5f);
}

float wefax_hz_de_gris(uint8_t gris)
{
    return WEFAX_NEGRO_HZ
         + ((float)gris / 255.0f) * (WEFAX_BLANCO_HZ - WEFAX_NEGRO_HZ);
}

/* ==========================================================================
 * ESTADO
 * ========================================================================== */
static volatile uint8_t s_on;
static float   s_fs;
static volatile uint8_t s_estado;
/*
 * Volatil porque lo escribe el bucle principal -wefax_set_lpm()- y lo lee
 * la interrupcion en pasadas_que_caben(), tope_lineas_fase() y
 * reloj_calcula(). Es un byte, asi que no hay rotura posible; lo que
 * `volatile` impide es que el compilador se lo guarde en un registro
 * durante el bloque entero y no se entere del cambio.
 */
static volatile uint8_t s_lpm_i = WEFAX_LPM_DEF_I;
/*
 * La escribe la ISR -el lazo de la señal de fase, via wefax_set_ppm()- y
 * tambien el bucle principal al cambiar de velocidad. Volatil y, donde lo
 * toca el bucle, dentro del cerrojo. */
static volatile float s_ppm;

/* --- 1. bajada a banda base y discriminador --- */
/* El discriminador vive en disc_fm.c y lo comparte con el SSTV: son las
 * mismas tres decisiones medidas, y copiadas en dos ficheros solo se
 * corregiria una el dia que hiciera falta. */
static disc_fm_t s_disc;

/* --- 2. reloj de linea --- */
/* Los cuatro los escriben los DOS lados: la interrupcion en cada muestra y
 * el bucle principal en wefax_sincroniza(). Ver el cerrojo de arriba. */
static volatile uint32_t s_fase;
static volatile uint32_t s_inc;
static volatile uint32_t s_px_ant;
static volatile float    s_acc;
static volatile uint32_t s_acc_n;

/* --- 3. la linea --- */
/*
 * MAS DE UN RENGLON, Y POR QUE.
 *
 * La interrupcion escribe puntos todo el rato y el bucle principal pinta
 * cuando puede. Con un solo renglon, el instante en que se pinta cae en
 * medio de la escritura del siguiente: la mitad de arriba seria de la linea
 * nueva y la de abajo de la vieja. Eso no se ve como un fallo, se ve como
 * ruido - que es la peor forma de tener un fallo.
 *
 * Empezo con DOS. Hoy son ocho y en anillo; el bloque de abajo cuenta por
 * que dos no bastaban.
 */
/*
 * EL ANILLO DE LINEAS - DE DOS A OCHO. 20/10/2026.
 *
 * *** Aqui habia DOS buffers y una bandera: la interrupcion llenaba uno, al
 * cerrarse cambiaba al otro, y el recogedor devolvia "el que acaba de
 * cerrarse". ***
 *
 * Eso tiene dos agujeros, y los dos se notan en el fichero guardado:
 *
 *   - SE PISA. El bucle recibe el puntero a la linea cerrada y se pone a
 *     escribirla en la flash, que cuesta decenas de milisegundos. Dos
 *     lineas despues, la interrupcion vuelve a ese MISMO buffer y empieza a
 *     escribir encima mientras el bucle todavia lo esta leyendo. Sale un
 *     renglon partido por la mitad, con media linea de otro momento;
 *   - SE PIERDE. La bandera es una sola. Si se cierran dos lineas antes de
 *     que el bucle pase, la primera desaparece sin que nadie se entere.
 *
 * Es exactamente lo que ya se arreglo en sstv.c, donde el anillo subio de 4
 * a 12 renglones por este mismo motivo -"las lineas que cierre mientras
 * tanto hay que poder recogerlas despues"-. El fax se quedo con dos.
 *
 * Ocho lineas son 6 kB y cuatro segundos a 120 lpm. Y la capacidad UTIL es
 * de siete -ver el tope en wefax_process()-, o sea tres segundos y medio:
 * setenta veces el peor bloqueo de la flash que hemos medido (51 ms).
 *
 * Aqui ponia "tres ordenes de magnitud de holgura", que son mil veces y no
 * setenta; y la cuenta se hacia sobre las ocho, que no son las que caben.
 * Setenta sigue siendo de sobra, pero un numero inflado trece veces es lo
 * que hace que alguien se crea que el anillo puede encogerse.
 */
#define WFX_ANILLO 8U

/*
 * Y EL ANILLO SALE DE LA TCM. Estaba ahi por velocidad, y no la necesita:
 * la interrupcion escribe UN byte por pixel, o sea unos 1,5 kB por segundo
 * a 120 lpm. Eso lo lleva la RAM normal sin enterarse, y la TCM -que son
 * 64 kB y estan llenos- hace falta para lo que de verdad corre.
 */
static uint8_t s_linea[WFX_ANILLO][WEFAX_ANCHO];
static volatile uint8_t s_esc;          /* en cual escribe la interrupcion */
static volatile uint8_t s_lee;          /* cual toca entregar al bucle */
static volatile uint8_t s_linea_lista;  /* cuantas hay sin recoger */
static uint32_t s_lineas;

/* --- 4. señal de fase --- */
/*
 * EL SEGUIDOR DE FASE, Y POR QUE ES VOLATIL - 21/10/2026.
 *
 * Todos estos los escribe la interrupcion -fase_linea(), apt_mira()- Y
 * TAMBIEN el bucle principal: wefax_set_lpm() y wefax_fuerza_imagen() los
 * ponen a cero con la interrupcion viva. Es el mismo reparto que el del
 * cerrojo de arriba, y por tanto el mismo peligro que cuenta su cabecera:
 * sin `volatile`, a -O2 el compilador ve que nadie fuera de este fichero
 * los toca, se los guarda en registros durante las N muestras del bloque y
 * los vuelca al salir, con lo que el cero que acaba de escribir el bucle
 * DESAPARECE. Un mando de velocidad que a veces no reinicia el enganche y
 * no deja rastro.
 *
 * s_fase_pos[] no entra: solo lo toca la interrupcion.
 */
static volatile uint8_t  s_fase_n;
static float    s_fase_pos[FASE_LINEAS];
static volatile float    s_fase_bruta;     /* la posicion cruda anterior, para desenvolver */
static volatile float    s_fase_off;       /* vueltas acumuladas, en puntos */
static volatile uint8_t  s_fase_ajustes;   /* cuantos ajustes se han hecho ya */
static volatile uint16_t s_fase_lineas;    /* lineas pasadas en la señal de fase */
static volatile float    s_desliz;      /* puntos que se corre el pulso por linea */
/*
 * LA LINEA PARTIDA - 24/09/2026.
 *
 * Al final de cada pasada se corre el acumulador de linea para traer el
 * pulso a su sitio. Eso parte EN DOS la linea que se estaba acumulando en
 * ese momento: la que sale a continuacion lleva un trozo de una linea y un
 * trozo de la siguiente, y su pulso cae donde le da la gana.
 *
 * Esa linea entraba en el ajuste por minimos cuadrados de la pasada
 * siguiente como un punto mas. Y como es el PRIMERO de los dieciseis, o sea
 * el mas alejado del centro, es tambien el que mas peso tiene en la
 * pendiente: un pulso 380 puntos fuera de sitio inclina el ajuste 8 puntos
 * por linea, y la correccion sale once veces mayor de lo que toca.
 *
 * Esto llevaba ahi desde la etapa 26 y el banco lo daba por bueno, porque a
 * 120 lpm el salto era de diez puntos y la linea partida apenas mentia. Que
 * fuese pequeño era CASUALIDAD: el tono de arranque dura 5 s, y 5 s son 10
 * lineas justas a 120 lpm, 5 a 60 y 20 a 240 - pero SIETE Y MEDIA a 90. Con
 * la velocidad que no cuadra, el salto era de media linea y salio el fallo.
 * Una emisora de verdad no empieza la fase en un limite de linea nuestro a
 * ninguna velocidad, asi que esto se habria visto en el aire y en las
 * cuatro.
 *
 * El arreglo es tirar esa linea, que es la unica de la que SABEMOS que
 * miente, y no tocar las demas.
 */
static volatile uint8_t  s_fase_tira;   /* la proxima linea esta partida: no mirarla */
/*
 * Y LA RANURA EN CURSO, QUE ES OTRA COSA - 21/10/2026.
 *
 * s_fase_tira dice "no MIDAS la proxima linea". Esto dice "no la ENTREGUES".
 * Hacen falta las dos porque el salto de fase parte la linea en curso y esa
 * linea, ademas de no servir para medir, no sirve para enseñar: sus puntos
 * de la izquierda no los ha escrito nadie en esta pasada y llevan dentro
 * restos de la señal de fase, con el pulso blanco incrustado en medio.
 *
 * El primer intento de arreglarlo fue poner s_px_ant a cero al entrar en
 * imagen. No valia para nada: fase_linea() se llama DESDE DENTRO del cierre
 * de linea de wefax_process(), y al salir de ahi `s_px_ant = px` lo
 * sobreescribe sin remedio una linea mas abajo. El banco lo enseño: a 60
 * lpm, con el pulso al 30% de la linea, el primer renglon de la carta salia
 * con quinientos puntos de otra cosa y el pulso de fase pintado dentro.
 *
 * Marcando la RANURA, el arreglo no depende de la aritmetica del punto.
 */
static float    s_pulso_pos;   /* donde cayo el ultimo pulso */
/*
 * *** Y LA ESPERA A QUE LA SEÑAL DE FASE SE ACABE - 21/10/2026. ***
 *
 * *** Aqui se entraba en imagen en cuanto estaban hechas las pasadas del
 * enganche, y lo que quedaba de señal de fase SE PINTABA COMO CARTA. ***
 *
 * La señal de fase dura treinta segundos. Las pasadas gastan 17 lineas
 * cada una -dieciseis medidas y la que parte el salto- y el resto sobra:
 *
 *     60 lpm ->  30 lineas, 1 pasada  (17) -> sobran 13
 *     90 lpm ->  45 lineas, 2 pasadas (34) -> sobran 11
 *    120 lpm ->  60 lineas, 2 pasadas (34) -> sobran 26
 *    240 lpm -> 120 lineas, 2 pasadas (34) -> sobran 86
 *
 * Esas lineas sobrantes son negras con el pulso blanco de la fase dentro, y
 * salian encoladas como las primeras filas de la carta: una franja negra
 * rayada encima de todas las cartas, de hasta OCHENTA Y SEIS renglones. Y
 * no es solo fealdad - esa franja entra en el fichero, se guarda en la
 * flash y el recolocado la gira con el resto.
 *
 * El banco lo venia enseñando sin que nadie lo leyera: la prueba 11
 * imprimia "llegan 146 de 60 lineas de imagen" a 240 lpm. Ochenta y seis
 * lineas de mas no son un margen, son la franja.
 *
 * El arreglo es separar DOS cosas que estaban pegadas: cuando se acaba de
 * MEDIR (las pasadas) y cuando se acaba la SEÑAL (que la emisora decide).
 * Hechas las pasadas se deja de medir y se sigue en WEFAX_FASE -donde no se
 * encola nada- hasta que tres lineas seguidas dejan de parecer lineas de
 * fase. Tres y no una porque un chasquido no puede adelantar el final; y
 * tres renglones de carta perdidos, si se adelanta, contra los ochenta y
 * seis que costaba lo de antes.
 *
 * El tope de siempre -35 s de lineas- sigue estando, y no es solo "por si
 * la señal no se acaba nunca": con señal fuerte es EL que entra en imagen,
 * porque cae justo al acabar la señal. Esta regla es la que llega a tiempo
 * cuando el tono de arranque se ha reconocido tarde, que es lo que pasa con
 * señal justa. Ver tope_lineas_fase(), donde esta la cuenta entera.
 */
#define FASE_FIN_N 3U
static volatile uint8_t s_fase_hecho;   /* las pasadas ya estan hechas */
static volatile uint8_t s_fase_fin_n;   /* lineas seguidas que ya no son de fase */

/* --- 5. tonos de arranque y parada --- */
/*
 * LA VENTANA DEL DETECTOR DE TONOS: 240 MUESTRAS.
 *
 * Son 20 ms Y SEIS CICLOS DE 300 Hz **A 12 kHz**, que es la tasa a la que
 * llama a esto demod_am.c -DEMOD_DEC_FS_HZ- y la unica a la que se ha
 * llamado nunca. Conviene decirlo porque el resto de este fichero SI es
 * independiente de la tasa -wefax_start() la recibe por argumento- y este
 * numero no: a otra tasa la ventana dura otra cosa, su resolucion en
 * frecuencia cambia, y los umbrales de apt_mira() -el 200 y el 6.000, que
 * estan medidos sobre amplitud al cuadrado con ESTA ventana- dejan de
 * significar lo que dice su comentario.
 *
 * No se comprueba al compilar porque la tasa es un argumento, no una
 * constante que este fichero vea. Si algun dia cambia, lo que hay que
 * revisar es este numero y los dos umbrales, no solo este numero.
 */
#define APT_N 240U
/*
 * *** Y SIN GUARDAR LA VENTANA - 21/10/2026. ***
 *
 * Aqui habia un `static float s_apt_buf[APT_N]`: NOVECIENTOS SESENTA BYTES
 * de .bss permanentes para guardar las 240 muestras y pasarselas enteras a
 * goertzel() cada 20 ms.
 *
 * No hacian falta. Un Goertzel ES un filtro recursivo: su estado son DOS
 * numeros -s1 y s2- que se actualizan muestra a muestra, y la ventana no se
 * usaba para nada mas que para recorrerla. Guardarla era guardar la entrada
 * de un filtro que ya la ha digerido.
 *
 * Cuesta exactamente lo mismo -dos multiplicaciones y dos restas por
 * muestra y por tono, que son las mismas 480 por ventana- solo que
 * repartidas en vez de en una rafaga al cerrarla. Y se ahorran dos cosf()
 * por ventana: el coeficiente no cambia, asi que se calcula una vez en
 * wefax_start().
 *
 * 960 bytes sobre los 4.792 que quedaban libres en la SRAM principal: una
 * quinta parte de lo que habia, por un buffer que el algoritmo no pide.
 */
static uint16_t s_apt_n;
/* Los dos filtros en marcha, y sus coeficientes. Ver apt_mira(). */
static float    s_g3_s1, s_g3_s2, s_g3_coef;
static float    s_g4_s1, s_g4_s2, s_g4_coef;

/* El coeficiente de un tono, una vez. Lo llama wefax_start(), que esta mas
 * arriba que apt_mira(): por eso vive aqui y no al lado de su pareja. */
static float goertzel_coef(float hz, float fs)
{
    return 2.0f * cosf(2.0f * PI_F * hz / fs);
}
/* Volatiles por lo mismo que el seguidor de fase: wefax_fuerza_imagen()
 * los pone a cero desde el bucle principal. */
static volatile float    s_apt300, s_apt450;

/* ==========================================================================
 * ARRANQUE
 * ========================================================================== */
/*
 * El incremento del acumulador para una correccion dada, SIN publicarlo.
 * Se separa porque esto es aritmetica de doble precision -por software en
 * este chip- y no puede hacerse con las interrupciones cerradas; quien
 * necesite cambiar la velocidad de forma atomica calcula primero y publica
 * despues. Ver wefax_set_lpm().
 */
/*
 * Y LA VELOCIDAD SE PASA, NO SE LEE - 21/10/2026.
 *
 * wefax_set_lpm() tiene que calcular el incremento de la velocidad NUEVA
 * antes de publicar nada, porque el calculo es de doble precision y no
 * puede hacerse con las interrupciones cerradas. Leyendo s_lpm_i aqui
 * dentro, la unica forma de conseguirlo era publicar s_lpm_i PRIMERO y
 * entrar al cerrojo despues - y en esa ventana, que son las decenas de
 * microsegundos que cuesta borrar los 6 kB del anillo, la interrupcion ve
 * la velocidad nueva con el reloj y el contador de lineas de la vieja.
 * tope_lineas_fase() se encoge a la mitad al bajar de 240 a 60 lpm, asi
 * que el tope de seguridad podia saltar justo ahi y mandar a imagen la
 * señal de fase que el usuario acababa de pedir medir otra vez.
 *
 * Pasandola por argumento, s_lpm_i se publica dentro del cerrojo con todo
 * lo demas y la ventana no existe.
 */
static uint32_t reloj_calcula_i(uint8_t lpm_i, float ppm)
{
    double lps;

    if (s_fs <= 0.0f) { return s_inc; }
    if (lpm_i >= WEFAX_LPM_N) { return s_inc; }
    /* Una vuelta del acumulador por linea. lineas/s = lpm/60. */
    lps = (double)k_lpm[lpm_i] / 60.0 * (1.0 + (double)ppm * 1e-6);
    return (uint32_t)(lps / (double)s_fs * 4294967296.0 + 0.5);
}

static uint32_t reloj_calcula(float ppm)
{
    return reloj_calcula_i(s_lpm_i, ppm);
}

static void reloj_fija(void)
{
    /*
     * Sin frecuencia de muestreo todavia no hay reloj que fijar, y dividir
     * por cero da un infinito que al convertirlo a entero es comportamiento
     * indefinido. Se llega aqui de verdad: CONFIG.CSV restaura la velocidad
     * del fax al arrancar, mucho antes de que nadie entre en el modo.
     */
    s_inc = reloj_calcula(s_ppm);
}

/*
 * CON LA INTERRUPCION PARADA, Y NO POR GUSTO - 21/10/2026.
 *
 * Esto borra 6 kB y mueve los tres indices del anillo, y lo llama el bucle
 * principal -el mando de velocidad, y apply_demod_mode() al reentrar en el
 * modo- con la interrupcion viva. Sin el cerrojo, la interrupcion esta
 * escribiendo en s_linea[s_esc] a mitad de renglon cuando s_esc se pone a
 * cero: salta de la ranura 5 a la 0 por la mitad y deja media linea en cada
 * una. Son unos pocos microsegundos de interrupciones cerradas, una vez por
 * cambio de velocidad.
 */
/*
 * SIN CERROJO: lo pone quien llama. Separado de linea_reinicia() para que
 * wefax_set_lpm() pueda hacer el anillo Y el seguidor de fase dentro de UNA
 * sola seccion critica - ver su comentario.
 */
static void anillo_cero(void)
{
    s_fase = 0U;
    s_px_ant = 0U;
    s_acc = 0.0f;
    s_acc_n = 0U;
    s_esc = 0U;
    s_lee = 0U;
    s_linea_lista = 0U;
    /* Y la marca de "la ranura en curso esta partida" se va con la ranura:
     * aqui se empieza a escribir una limpia desde el punto cero, asi que la
     * primera que se cierre es entera. Dejarla armada tiraba una buena. */
}

/* El seguidor de fase entero, tambien sin cerrojo. s_desliz NO entra: es
 * solo para la pantalla y cada llamante decide si lo borra. */
static void fase_cero(void)
{
    s_fase_n = 0U;
    s_fase_bruta = -1.0f;
    s_fase_off = 0.0f;
    s_fase_ajustes = 0U;
    s_fase_lineas = 0U;
    s_fase_tira = 0U;
    s_fase_hecho = 0U;
    s_fase_fin_n = 0U;
}

static void linea_reinicia(void)
{
    /*
     * El borrado va FUERA del cerrojo - 21/10/2026. Son 6 kB, o sea decenas
     * de microsegundos con todas las interrupciones cerradas, y no hace
     * falta: lo que tiene que ser atomico son los tres indices del anillo.
     * Mientras se borra, la interrupcion puede estar escribiendo en una
     * ranura; da igual, porque acto seguido se declaran todas vacias.
     */
    memset((void *)s_linea, 0, sizeof s_linea);

    WFX_CIERRA();
    anillo_cero();
    WFX_ABRE();
}

void wefax_start(float fs_hz)
{
    if (fs_hz <= 0.0f) { return; }
    /* Lo PRIMERO, antes de borrar nada: apply_demod_mode() no tiene salida
     * temprana si el modo no cambia, asi que se puede entrar aqui con la
     * interrupcion viva llamando a disc_fm_paso() sobre s_disc mientras
     * disc_fm_init() lo pone a cero. */
    s_on = 0U;
    s_fs = fs_hz;

    disc_fm_init(&s_disc, WEFAX_CENTRO_HZ, fs_hz);

    s_ppm = 0.0f;
    reloj_fija();
    linea_reinicia();

    s_lineas = 0UL;
    fase_cero();
    s_desliz = 0.0f;
    s_pulso_pos = 0.0f;
    s_apt_n = 0U;
    s_g3_coef = goertzel_coef(WEFAX_ARRANQUE_HZ, fs_hz);
    s_g4_coef = goertzel_coef(WEFAX_PARADA_HZ, fs_hz);
    s_g3_s1 = s_g3_s2 = 0.0f;
    s_g4_s1 = s_g4_s2 = 0.0f;
    s_apt300 = s_apt450 = 0.0f;

    s_estado = WEFAX_BUSCA;
    s_on = 1U;
}

void wefax_stop(void)      { s_on = 0U; s_estado = WEFAX_PARADO; }
uint8_t wefax_activo(void) { return s_on; }

void wefax_set_lpm(uint16_t lpm)
{
    uint8_t i;
    for (i = 0U; i < WEFAX_LPM_N; i++) {
        if (k_lpm[i] == lpm) {
            /*
             * Cambiar de velocidad tira el sincronismo: el ritmo que
             * llevabamos no significa nada con otra velocidad, y dejar la
             * fase donde estaba solo produce una linea partida.
             *
             * Y LOS DOS JUNTOS - 21/10/2026. s_ppm la escribe tambien la
             * interrupcion, desde el lazo de la señal de fase. Si entra
             * entre el cero y reloj_fija(), la correccion de inclinacion de
             * la velocidad ANTERIOR sobrevive al cambio de velocidad, que
             * es justo lo que estas dos lineas quieren impedir.
             */
            {
                /*
                 * Se calcula FUERA y se publica DENTRO. El calculo es
                 * aritmetica de doble precision, que en este chip va por
                 * software -la FPU es solo de simple- y son decenas de
                 * microsegundos: eso no puede hacerse con las
                 * interrupciones cerradas. Lo unico que tiene que ser
                 * atomico es que s_ppm y s_inc cambien JUNTOS, porque la
                 * interrupcion lee los dos.
                 */
                uint32_t inc = reloj_calcula_i(i, 0.0f);

                /*
                 * *** Y EL REINICIO ENTERO VA DENTRO DEL MISMO CERROJO -
                 * 21/10/2026. ***
                 *
                 * El seguidor de fase se ponia a cero AQUI ABAJO, fuera de
                 * toda seccion critica, con la interrupcion viva. Y la
                 * interrupcion puede estar justo dentro de fase_linea():
                 * entre el cero de s_fase_n y el de s_fase_bruta cabe una
                 * linea entera, y entonces el seguidor arranca con la
                 * cuenta de la velocidad NUEVA y la posicion cruda de la
                 * VIEJA. Es el mismo fallo que este bloque dice haber
                 * arreglado para s_ppm, dejado a medias: lo que se arreglo
                 * fue el reloj y no el seguidor.
                 *
                 * El borrado de los 6 kB se queda fuera por lo de siempre
                 * -son decenas de microsegundos- y va ANTES, para que la
                 * ultima palabra sobre los indices la tenga el cerrojo.
                 */
                memset((void *)s_linea, 0, sizeof s_linea);

                WFX_CIERRA();
                s_lpm_i = i;
                s_ppm = 0.0f;
                s_inc = inc;
                anillo_cero();
                fase_cero();
                s_desliz = 0.0f;
                WFX_ABRE();
            }
            /*
             * EL SEGUIDOR DE FASE ENTERO, NO MEDIO. Aqui se reiniciaba solo
             * s_fase_n y s_fase_tira, y s_fase_bruta se quedaba con la
             * posicion de la velocidad anterior: el primer desenvolvimiento
             * metia ±756 puntos en s_fase_off y salia una carta con una
             * inclinacion disparatada por girar el mando a mitad de la señal
             * de fase - que es justo cuando se gira, al ver que no engancha.
             * Por eso fase_cero() los pone TODOS.
             *
             * Y s_fase_tira queda a CERO, no armado: anillo_cero() pone a
             * cero s_fase y tambien s_px_ant, s_acc y s_acc_n, asi que la
             * primera linea que se cierre despues es entera. Armarlo solo
             * tiraba una linea buena de la señal de fase.
             */
            return;
        }
    }
}

uint16_t wefax_get_lpm(void) { return k_lpm[s_lpm_i]; }

/* La lista, sin tocar nada. Ver wefax.h: la llaman los bancos, que si no
 * la llevarian copiada a mano. */
uint16_t wefax_lpm_opcion(uint8_t i) { return (i < WEFAX_LPM_N) ? k_lpm[i] : 0U; }

/*
 * El indice, para la interfaz y para CONFIG.CSV. Va aqui y no en main.c a
 * proposito: un indice guardado alli seria una SEGUNDA copia de cual es la
 * velocidad en vigor, y de esas ya se han separado tres en este proyecto.
 * Preguntando, solo hay una.
 */
uint8_t wefax_get_lpm_idx(void) { return s_lpm_i; }

void wefax_set_lpm_idx(uint8_t i)
{
    if (i < WEFAX_LPM_N) { wefax_set_lpm(k_lpm[i]); }
}

/*
 * LA CORRECCION DE INCLINACION. Ver wefax.h, donde vivian sus dos
 * declaraciones publicas hasta el 21/10/2026.
 *
 * Es ESTATICA a proposito: hace lee-modifica-escribe sobre s_ppm y escribe
 * s_inc, las dos sin cerrojo, y eso es seguro UNICAMENTE porque el unico que
 * la llama es fase_linea(), o sea la propia interrupcion. Publica, cualquiera
 * podia colgarla de un mando y el fallo no daria ni un aviso del compilador.
 */
static void ppm_fija(float ppm)
{
    if (ppm > 30000.0f)  { ppm = 30000.0f; }
    if (ppm < -30000.0f) { ppm = -30000.0f; }
    s_ppm = ppm;
    reloj_fija();
}

void wefax_sincroniza(void)
{
    /* Los cuatro de una vez: si la interrupcion entra en medio, la linea en
     * curso queda con un principio de un sitio y un final de otro. */
    WFX_CIERRA();
    s_fase = 0U;
    s_px_ant = 0U;
    s_acc = 0.0f;
    s_acc_n = 0U;
    WFX_ABRE();
}

void wefax_fuerza_imagen(void)
{
    /*
     * Y SE LIMPIA LA COLA ANTES DE EMPEZAR A PINTAR - 21/10/2026.
     *
     * Lo que haya sin recoger es de ANTES de este boton: lineas de la señal
     * de fase, o de la carta anterior. Entregarlas como primeras lineas de
     * la imagen es pintar basura justo donde el usuario esta mirando si
     * engancho o no.
     *
     * Y el seguidor de fase entero a cero -fase_cero()-: s_fase_tira lo
     * pone a uno el salto del ajuste (ver su comentario) y solo lo consume
     * fase_linea(), que en imagen ya no se llama. Si no se limpia aqui, se
     * queda armado hasta la carta siguiente y le tira una linea buena de su
     * señal de fase.
     */
    WFX_CIERRA();
    s_estado = WEFAX_IMAGEN;
    fase_cero();
    /*
     * Y LA RANURA SE DEJA LIMPIA, no se tira - 21/10/2026.
     *
     * La version de hace un rato DESCARTABA la ranura en curso aqui,
     * "porque los puntos de la izquierda son de hace ocho lineas". Cierto,
     * pero la respuesta era la cara: tirar el primer renglon de la carta,
     * que es justo donde el usuario mira si el boton ha enganchado o no.
     *
     * Se arregla donde se crea el problema: volviendo al principio de
     * linea, la ranura se reescribe entera desde el punto cero y el primer
     * renglon es bueno.
     *
     * Y SON LOS CUATRO, NO TRES. El primer intento ponia s_px_ant, s_acc y
     * s_acc_n y se dejaba s_fase, que es el que de verdad decide por donde
     * va el punto: px sale de s_fase, asi que sin tocarlo la ranura NO se
     * reescribe desde cero y el comentario mentia. No se rompio de milagro,
     * porque el unico llamante hace wefax_sincroniza() justo antes - o sea
     * que la funcion dependia exactamente de lo que su comentario decia que
     * ya no necesitaba. Son los mismos cuatro que ponen linea_reinicia() y
     * wefax_sincroniza().
     */
    s_fase = 0U;
    s_px_ant = 0U;
    s_acc = 0.0f;
    s_acc_n = 0UL;
    /*
     * *** Y EL DETECTOR DE TONOS, QUE SE QUEDABA CARGADO - 21/10/2026. ***
     *
     * Todas las demas entradas a un estado lo ponen a cero -las dos de
     * apt_mira()- y esta no. Y esta es justo la que se usa cuando el
     * decodificador NO engancha solo, o sea cuando las dos medias llevan un
     * rato subiendo con lo que haya en el aire. Entrando en imagen con
     * s_apt450 ya cerca de su umbral -que dentro de imagen son 200, el bajo
     * de los dos-, la comprobacion de parada de este mismo bloque dispara en
     * la siguiente ventana de 20 ms y devuelve a BUSCA: el boton de
     * Sincroniza parece no hacer nada, y no deja rastro de por que.
     */
    s_apt300 = 0.0f;
    s_apt450 = 0.0f;
    s_lee = s_esc;
    s_linea_lista = 0U;
    WFX_ABRE();
}

/*
 * AQUI VIVIO EL CENTRADO A MANO, Y SE FUE EL 20/10/2026.
 *
 * Era un desplazamiento en puntos que se aplicaba al renglon ya terminado,
 * justo antes de entregarlo, movido con un boton del panel. Se quito a
 * peticion, y con un motivo que es el que ordena todo lo demas: si la carta
 * ya viene corrida, la linea vertical con la que se elige el centrado AL
 * FINAL no marca la columna que uno cree.
 *
 * Y habia una razon mas gorda debajo: corriendo el renglon antes de
 * grabarlo, el centrado entraba en el fichero, y una carta en la que se
 * tocaba el boton a mitad quedaba grabada con DOS centrados dentro. Eso no
 * es un desplazamiento, es una costura, y ninguna recolocacion posterior
 * puede deshacerla.
 *
 * Lo que hay en su lugar: User/wfx_recoloca.c, que gira el fichero entero
 * una sola vez cuando ya no entra nada mas.
 */

uint8_t wefax_linea_en(uint8_t *dst)
{

    /*
     * LOS TRES, DE UNA VEZ - 21/10/2026.
     *
     * `s_linea_lista--` son tres instrucciones en un Cortex-M4, y la
     * interrupcion hace `s_linea_lista++` en la suya. Si cae en medio se
     * pierde una de las dos cuentas, y el anillo se descoloca PARA SIEMPRE:
     * ni el lector ni el escritor vuelven a cuadrar solos. `volatile` no
     * arregla esto, solo obliga a ir a memoria.
     *
     * Y s_lee tambien lo escribe la interrupcion -en los cambios de estado
     * y al descartar la mas vieja-, asi que leerlo, usarlo y avanzarlo
     * tiene que ser una sola cosa.
     *
     * *** Y SE COPIA, NO SE PRESTA EL PUNTERO - 21/10/2026. ***
     *
     * Antes esto devolvia un puntero a la ranura y el bucle se iba con el
     * en la mano a escribir 4 kB en la flash. El tope de N-1 protege la
     * COLA, no ese puntero: desde que se entrega la ranura A, a la
     * interrupcion le faltan 8 - lista cierres para volver a ella, y con el
     * anillo lleno -que es el unico caso en que el margen sirve de algo-
     * eso es UN cierre, medio segundo a 120 lpm. Y una escritura de bloque
     * puede costar 800 ms cuando toca escanear la FAT y borrar.
     *
     * O sea que el renglon partido seguia ahi, solo que mas estrecho.
     * Copiando dentro del cerrojo desaparece por construccion: la ranura
     * deja de importar en cuanto se vuelve. Son unos 3 us de interrupciones
     * cerradas cada medio segundo. Es lo que hace sstv.c desde siempre.
     */
    if (!dst) { return 0U; }

    WFX_CIERRA();
    if (s_linea_lista == 0U) {
        WFX_ABRE();
        return 0U;
    }
    memcpy(dst, (const void *)s_linea[s_lee], WEFAX_ANCHO);
    s_lee = (uint8_t)((s_lee + 1U) % WFX_ANILLO);
    s_linea_lista--;              /* una menos sin recoger */
    WFX_ABRE();
    return 1U;
}


void wefax_info(wefax_info_t *out)
{
    if (!out) { return; }
    out->estado = s_estado;
    out->lpm    = k_lpm[s_lpm_i];
    out->ppm    = s_ppm;
    out->lineas = s_lineas;
    out->fase_n = s_fase_n;
    out->nivel  = disc_fm_nivel(&s_disc);
}

void wefax_fase_dbg(float *desliz_px_linea, float *pos_pulso)
{
    if (desliz_px_linea) { *desliz_px_linea = s_desliz; }
    if (pos_pulso)       { *pos_pulso = s_pulso_pos; }
}

/* ==========================================================================
 * LA SEÑAL DE FASE
 * ==========================================================================
 *
 * Cada linea de fase es negra con un pulso blanco que ocupa un 5% de la
 * linea. Aqui se busca el centro de ese pulso, y con la posicion en varias
 * lineas seguidas salen las dos cosas que hacen falta:
 *
 *   - DONDE empieza la linea: el pulso marca el principio, asi que basta con
 *     correr el contador para que el pulso caiga en el punto cero.
 *   - A QUE RITMO: si vamos mas lentos que la emisora, el pulso se va
 *     corriendo hacia la izquierda linea a linea; si mas rapidos, hacia la
 *     derecha. Lo que se corre por linea, dividido por el ancho, ES el error
 *     de velocidad.
 *
 * Ese segundo dato es el que quita la inclinacion, y no se puede sacar de
 * una sola linea por mucho que se mire: hace falta ver el pulso MOVERSE.
 */
/*
 * CUANTAS PASADAS CABEN, Y CUANTO SE ESPERA - 21/10/2026.
 *
 * *** LAS DOS PASADAS NO CABEN A 60 lpm, Y SE COMIAN LA CABECERA DE LA
 * CARTA. ***
 *
 * Hacen falta FASE_LINEAS por pasada mas la que parte el salto, o sea 17
 * lineas por pasada. Y la señal de fase dura 30 segundos, que NO son las
 * mismas lineas a cada velocidad:
 *
 *     60 lpm ->  30 lineas   dos pasadas NO caben
 *     90 lpm ->  45 lineas   caben justas
 *    120 lpm ->  60 lineas   caben
 *    240 lpm -> 120 lineas   caben de sobra
 *
 * A 60 lpm la fase se acababa en la linea 30 con UNA pasada hecha, y
 * entonces el decodificador se quedaba en WEFAX_FASE esperando a juntar
 * dieciseis lineas que ya no iban a venir: las de imagen las rechaza
 * pulso_centro() por no ser negras, y cada rechazo pone el contador a cero.
 * Seguia ahi hasta el tope de seguridad -setenta lineas- y en ese estado NO
 * SE ENCOLA NADA, o sea que las cuarenta lineas de imagen de ese rato se
 * decodificaban y se tiraban. Faltaba la franja de arriba de la carta, que
 * es justo donde el DWD pone la cabecera con la hora y el nombre.
 *
 * Es el mismo error de razonamiento que ya costo el fallo de los 90 lpm que
 * cuenta el comentario de s_fase_tira: una cuenta hecha a 120 lpm y dada
 * por general. Treinta y dos lineas son dieciseis segundos A 120.
 *
 * Asi que las pasadas salen de la velocidad, y el tope de espera tambien:
 * 35 segundos de lineas, que es la señal de fase mas un margen, en vez de
 * un 70 que a 60 lpm son setenta segundos y a 240 solo diecisiete.
 */
static uint8_t pasadas_que_caben(void)
{
    uint16_t lineas_en_fase =
        (uint16_t)(((uint32_t)k_lpm[s_lpm_i] * FASE_SEG) / 60UL);
    uint16_t por_pasada = (uint16_t)(FASE_LINEAS + 1U);

    return (uint8_t)((lineas_en_fase >= (uint16_t)(2U * por_pasada)) ? 2U : 1U);
}

/*
 * LOS 35 SEGUNDOS NO SON "LA FASE MAS UN MARGEN" - 21/10/2026.
 *
 * Eso ponia aqui, y la cuenta no sale: la señal de fase dura 30 s, pero
 * este contador NO empieza con ella. Empieza cuando apt_mira() reconoce el
 * tono de ARRANQUE, que es lo que mete el decodificador en WEFAX_FASE, y
 * desde ahi quedan los segundos que le falten al tono -de 5- mas los 30 de
 * la fase. O sea 35 justos menos lo que haya tardado el reconocimiento.
 * Margen: el retardo del detector, y nada mas.
 *
 * ESO CAMBIA QUIEN HACE EL TRABAJO, y conviene decirlo porque los
 * comentarios de aqui lo contaban al reves. Esto no es el tope de
 * seguridad de un mecanismo que suele salir por otro lado: con señal
 * fuerte el detector tarda una linea, el tope cae justo al acabar la señal
 * y es EL que entra en imagen; la regla de las tres lineas seguidas sin
 * pulso -ver s_fase_hecho- solo llega a tiempo cuando el reconocimiento ha
 * tardado, que es lo que pasa con señal justa. Las dos aprietan el mismo
 * instante desde lados distintos, y por eso ninguna de las dos sobra.
 *
 * Medido, no supuesto: el caso 11 del banco manda 5 s de tono y 30 de fase
 * a las cuatro velocidades y llegan 59 de las 60 lineas de imagen, con
 * CERO lineas de fase dentro, en las cuatro. Subiendo este numero a 40 la
 * salida pasa a darla la regla de las tres lineas y llegan 56: tres
 * renglones menos en la cabecera de cada carta, que es justo donde el DWD
 * pone la hora y el nombre.
 *
 * O SEA QUE ESTE NUMERO NO ES AJUSTABLE A OJO:
 *
 *   - bajarlo CORTA la señal de fase y mete franja negra en la carta;
 *   - subirlo le regala a la regla de las tres lineas los tres renglones
 *     de arriba de todas las cartas, y ademas alarga lo que se tira cuando
 *     el tono de arranque es un falso positivo (en ese caso no hay señal
 *     de fase que acabe y esto es la unica salida).
 *
 * Sale de 5 + 30, y si algun dia cambia uno de los dos, cambia aqui.
 */
static uint16_t tope_lineas_fase(void)
{
    /* Los 5 del tono de arranque mas los 30 de la fase. Ver arriba: el
     * numero sale de los dos y no se escribe a mano. */
    uint32_t n = ((uint32_t)k_lpm[s_lpm_i]
                  * (uint32_t)(ARRANQUE_SEG + FASE_SEG)) / 60UL;
    if (n < (uint32_t)(FASE_LINEAS + 2U)) { n = (uint32_t)(FASE_LINEAS + 2U); }
    return (uint16_t)n;
}

static float pulso_centro(const uint8_t *l)
{
    uint32_t i, suma = 0UL, media = 0UL;
    float sx = 0.0f, sy = 0.0f, r, ang, pos;

    /*
     * MEDIA CIRCULAR, no media a secas.
     *
     * El pulso de fase esta al PRINCIPIO de la linea, o sea pegado al borde,
     * y cuando nuestro reloj va corrido el pulso se sale por un lado y entra
     * por el otro. En esas lineas queda PARTIDO: mitad en el punto 750 y
     * mitad en el 5. Una media normal de esas posiciones da 377, o sea el
     * centro de la linea - justo donde no esta.
     *
     * Y eso no se ve como un error pequeño: es un punto disparatado metido en
     * el ajuste de velocidad, que arrastra la recta entera. Se veia como un
     * decodificador que a veces enderezaba la imagen y a veces la torcia mas,
     * sin patron.
     *
     * La forma correcta de promediar algo que da la vuelta es tratarlo como
     * un ANGULO: cada punto blanco es un vector unitario, se suman, y el
     * angulo de la suma es la posicion. Con el pulso partido, los dos trozos
     * apuntan casi al mismo sitio y la suma cae donde tiene que caer.
     *
     * Y de regalo: la LONGITUD de esa suma dividida por el numero de puntos
     * dice si estaban todos juntos (cerca de 1) o desperdigados (cerca de 0).
     * O sea que la misma cuenta que da la posicion dice tambien si lo que se
     * esta mirando es un pulso o un trozo de mapa.
     */
    /*
     * ======================================================================
     * DOS PASADAS, Y LA TRIGONOMETRIA EN LA SEGUNDA - 10/10/2026
     * ======================================================================
     *
     * Esto era UN bucle con cosf() y sinf() de la biblioteca dentro, y
     * corre DENTRO DE LA INTERRUPCION (wefax_process -> fase_linea). Con una
     * linea muy clara llegaba a pagar 756 pares de seno y coseno mas 756
     * divisiones float: del orden de 380.000 ciclos en UNA sola entrada a la
     * interrupcion, contra los 532.480 que dura el bloque entero. No es
     * lentitud media: es un pico de latencia que se come el bloque.
     *
     * Y lo peor es que para una linea de imagen -la mayoria- ese trabajo SE
     * TIRA: la guarda de `media > 80` la rechaza DESPUES, y `media` es una
     * suma de enteros que se puede tener antes de tocar un solo seno.
     *
     * Asi que primero la suma de enteros y la guarda, y solo si la linea es
     * negra de verdad, la trigonometria. El caso peor pasa a ser el caso
     * raro.
     *
     * Y LOS SENOS, GIRANDO UN FASOR. El angulo avanza en pasos FIJOS de
     * 2*pi/756, asi que en vez de calcularlo desde cero en cada punto se
     * gira un vector unitario: cuatro multiplicaciones y dos sumas por
     * punto, sin biblioteca y sin la division por WEFAX_ANCHO.
     *
     * La deriva de un giro acumulado en float a lo largo de 756 pasos es del
     * orden de 1e-5 rad; el umbral que decide (`r < 0.85f`) no se entera. Y
     * se renormaliza cada 64 pasos, que cuesta una raiz cada 64 puntos y
     * acota la deriva de la LONGITUD, que es la que mide `r`.
     */
    for (i = 0U; i < WEFAX_ANCHO; i++) { media += l[i]; }
    media /= WEFAX_ANCHO;
    if (media > 80UL) { return -1.0f; }                  /* la linea no es negra */

    {
        /* El paso del giro: cos y sen de 2*pi/756, una vez. */
        const float paso = 2.0f * PI_F / (float)WEFAX_ANCHO;
        const float cp = cosf(paso), sp = sinf(paso);
        float cx = 1.0f, cy = 0.0f;   /* el fasor, empieza en angulo 0 */

        for (i = 0U; i < WEFAX_ANCHO; i++) {
            if (l[i] >= 160U) { sx += cx; sy += cy; suma++; }
            {
                float nx = cx * cp - cy * sp;
                cy = cx * sp + cy * cp;
                cx = nx;
            }
            if ((i & 63U) == 63U) {
                float m2 = cx * cx + cy * cy;
                if (m2 > 0.0f) {
                    float inv = 1.0f / sqrtf(m2);
                    cx *= inv; cy *= inv;
                }
            }
        }
    }

    /*
     * Tres condiciones, y las tres hacen falta. Una linea de fase es NEGRA
     * con un pulso blanco corto y JUNTO; una linea de imagen cualquiera tiene
     * zonas claras repartidas. Sin estas tres, el seguidor de fase se cree
     * una linea de mapa y mide la velocidad contra ella - y eso paso: con la
     * señal de fase mas corta de lo que dura de verdad, la segunda pasada
     * caia dentro de la imagen y "medía" dieciocho veces el deslizamiento
     * real.
     */
    if (suma < 4UL || suma > WEFAX_ANCHO / 4U) { return -1.0f; }

    r = sqrtf(sx * sx + sy * sy) / (float)suma;
    if (r < 0.85f) { return -1.0f; }                     /* no estaban juntos */

    ang = atan2f(sy, sx);
    if (ang < 0.0f) { ang += 2.0f * PI_F; }
    pos = ang / (2.0f * PI_F) * (float)WEFAX_ANCHO;
    if (pos >= (float)WEFAX_ANCHO) { pos = 0.0f; }
    return pos;
}

static void fase_linea(const uint8_t *l)
{
    float p;

    s_fase_lineas++;

    /* La linea que acaba de partir el salto del acumulador - ver
     * s_fase_tira. Se cuenta (para el tope de seguridad de abajo) pero no
     * se mide. */
    if (s_fase_tira) {
        s_fase_tira = 0U;
        return;
    }

    /*
     * Tope de seguridad: si la señal de fase se acaba (o nunca fue una señal
     * de fase) no se puede esperar aqui para siempre. Ver tope_lineas_fase():
     * son 35 segundos de lineas A ESTA VELOCIDAD, y no un numero fijo que a
     * 60 lpm es el doble de lo que hace falta y a 240 la mitad.
     */
    if (s_fase_lineas > tope_lineas_fase()) {
        s_estado = WEFAX_IMAGEN;
        s_fase_n = 0U;
        /* Por aqui se sale SIN haber dado ningun salto, asi que la ranura
         * en curso esta entera y la primera fila de la carta es buena. */
        return;
    }

    p = pulso_centro(l);

    /*
     * LAS PASADAS YA ESTAN HECHAS: SOLO SE ESPERA EL FINAL - 21/10/2026.
     *
     * Ver el comentario de s_fase_hecho. Aqui ya no se mide nada: se mira
     * si la linea SIGUE siendo de fase, y en cuanto dejan de serlo tres
     * seguidas se entra en imagen. Mientras tanto seguimos en WEFAX_FASE,
     * o sea que no se encola ni un renglon de la franja.
     *
     * La ranura en curso es entera: el ultimo salto del acumulador fue
     * hace lineas y lo que haya desde entonces se ha escrito del punto
     * cero al 755. Por eso NO se descarta la primera fila de la carta.
     */
    if (s_fase_hecho) {
        if (p < 0.0f) {
            s_fase_fin_n++;
            if (s_fase_fin_n >= FASE_FIN_N) {
                s_estado = WEFAX_IMAGEN;
                s_fase_n = 0U;
            }
        } else {
            s_fase_fin_n = 0U;
        }
        return;
    }

    if (p < 0.0f) {
        /* Esta linea no tiene un pulso reconocible: se empieza otra vez. Una
         * medida de velocidad sacada de lineas salteadas no vale, porque no
         * se sabe cuantas lineas hay entre dos posiciones. */
        s_fase_n = 0U;
        s_fase_bruta = -1.0f;
        s_fase_off = 0.0f;
        return;
    }

    /*
     * DESENVOLVER. El pulso esta al PRINCIPIO de la linea, o sea pegado al
     * borde, que es justo donde se sale por un lado y entra por el otro. Sin
     * esto, un salto de 5 a 750 se lee como "se ha corrido 745 puntos" en vez
     * de "se ha corrido 11 hacia atras", y el ajuste sale del reves y
     * enorme.
     *
     * Esto costo la primera version: con la emisora LENTA salia bien y con la
     * RAPIDA salia disparatado, porque solo en ese sentido el pulso cruza el
     * cero. Un fallo que solo aparece en la mitad de los casos.
     */
    if (s_fase_bruta >= 0.0f) {
        float d = p - s_fase_bruta;
        if (d >  (float)WEFAX_ANCHO * 0.5f) { s_fase_off -= (float)WEFAX_ANCHO; }
        if (d < -(float)WEFAX_ANCHO * 0.5f) { s_fase_off += (float)WEFAX_ANCHO; }
    }
    s_fase_bruta = p;
    s_pulso_pos = p;

    s_fase_pos[s_fase_n] = p + s_fase_off;
    s_fase_n++;

    if (s_fase_n < FASE_LINEAS) { return; }

    {
        /*
         * Ajuste por minimos cuadrados de posicion = a + b * linea. Se usa la
         * formula cerrada para una x que va de 0 a n-1.
         *
         * Por minimos cuadrados y no con la primera y la ultima porque el
         * centro del pulso lleva ruido: con dos puntos, el ruido de esos dos
         * ES la medida. Con dieciseis, se promedia.
         */
        float n = (float)FASE_LINEAS;
        float sx = 0.0f, sy = 0.0f, sxy = 0.0f, sxx = 0.0f;
        uint8_t i;
        float b, den;

        for (i = 0U; i < FASE_LINEAS; i++) {
            float x = (float)i, y = s_fase_pos[i];
            sx += x; sy += y; sxy += x * y; sxx += x * x;
        }
        den = n * sxx - sx * sx;
        if (den > 0.0f) {
            b = (n * sxy - sx * sy) / den;

            /*
             * b son los puntos que se corre el pulso por linea, y su SIGNO
             * dice hacia donde hay que ir - al reves de lo que parece.
             *
             * Si el pulso se corre hacia la DERECHA es que las lineas de la
             * emisora duran MAS que las nuestras: vamos demasiado rapidos y
             * hay que FRENAR. Por eso la correccion lleva el signo cambiado.
             * Escrito con el signo "natural" el ajuste empeora el doble en
             * vez de arreglar, que es lo que hacia la primera version.
             */
            s_desliz = b;
            ppm_fija(s_ppm - (b / (float)WEFAX_ANCHO) * 1.0e6f);
        }

        /*
         * Y el desfase: correr el contador para que el pulso caiga donde le
         * toca. El pulso ocupa el primer 5% de la linea, asi que su centro va
         * en el 2,5%: llevarlo al punto cero desplazaria la imagen entera
         * hacia la izquierda ese trozo.
         *
         * Se RESTA. Sumar al acumulador adelanta lo que se ve; para traer un
         * pulso que aparece en la posicion p hasta la posicion q hay que
         * correr el acumulador q - p, que con p mayor que q es negativo. La
         * primera version sumaba p a secas, y eso corre la imagen justo al
         * doble y en el sentido contrario.
         */
        {
            /*
             * *** POR EL CAMINO CORTO, Y SIN DESBORDAR - 21/10/2026. ***
             *
             * Esto era:
             *     d = (destino - s_fase_bruta) / 756;
             *     s_fase = (uint32_t)((int32_t)s_fase + (int32_t)(d * 2^32));
             *
             * y tiene dos cosas mal, las dos del mismo sitio. `d` va de
             * -0,975 a +0,025, asi que `d * 2^32` baja hasta -4,19e9: fuera
             * de un int32_t, que llega a -2,147e9. Convertir un float que no
             * cabe es comportamiento indefinido; el compilador emite un
             * vcvt que SATURA. Y la suma de los dos int32_t es desbordamiento
             * con signo, otra vez indefinido, compilado a -O2 sin -fwrapv.
             *
             * El efecto, siempre que el pulso cae en la mitad derecha de la
             * linea -o sea en la mitad de los enganches-: se pide correr mas
             * de media linea y se corre solo media. La primera pasada se
             * desperdicia.
             *
             * Y no hacia falta: la fase es circular, asi que la correccion
             * se puede dar siempre por el camino CORTO, que nunca pasa de
             * media vuelta y por tanto siempre cabe. Reducido a [-0,5, 0,5)
             * el producto cabe justo en un int32_t, y la suma se hace en
             * uint32_t, donde dar la vuelta esta definido y es justo lo que
             * queremos.
             */
            float q = FASE_PULSO * 0.5f;        /* su centro: la mitad */
            float pp = s_fase_bruta / (float)WEFAX_ANCHO;       /* [0, 1) */
            float d = q - pp;

            if (d <  -0.5f) { d += 1.0f; }
            if (d >=  0.5f) { d -= 1.0f; }
            s_fase += (uint32_t)(int32_t)(d * 4294967296.0f);
            s_fase_tira = 1U;   /* este salto parte la linea en curso */
        }

        s_fase_n = 0U;
        s_fase_bruta = -1.0f;
        s_fase_off = 0.0f;
        s_fase_ajustes++;

        /*
         * DOS PASADAS. La primera quita el grueso del error; la segunda mide
         * lo que queda, que es donde estan las decimas de punto por linea que
         * deciden si a las mil lineas la carta sale recta o con un par de
         * grados de caida. Treinta y dos lineas son dieciseis segundos, y la
         * señal de fase dura treinta.
         */
        if (s_fase_ajustes >= (uint8_t)pasadas_que_caben()) {
            /*
             * *** AQUI NO SE ENTRA EN IMAGEN - 21/10/2026. ***
             *
             * Se entraba, y con ello se pintaba como carta lo que quedaba
             * de señal de fase: hasta ochenta y seis renglones negros
             * rayados encima de todas las cartas. El porque entero, con la
             * cuenta a cada velocidad, esta en el comentario de
             * s_fase_hecho.
             *
             * Lo que se hace es dejar de MEDIR y seguir en WEFAX_FASE
             * -donde no se encola nada- hasta que la señal se acabe de
             * verdad. Como seguimos en fase, la linea que acaba de partir
             * el salto del acumulador se cierra en la rama de fase, que no
             * la encola, y s_fase_tira -que se acaba de armar cinco lineas
             * mas arriba- hace que tampoco se mida. Por eso aqui ya no hace
             * falta el DESCARTE de esa ranura, que hacia falta cuando se
             * entraba en imagen de golpe:
             *
             *   el salto deja s_fase dado la vuelta pero s_px_ant se queda
             *   con el px de ANTES, asi que la muestra siguiente salta de 0
             *   a ~453 sin cruzar el cero y los puntos 1..452 de ese
             *   renglon no los escribe nadie. La primera fila de TODAS las
             *   cartas salia mitad basura.
             *
             * Quien devuelva el `s_estado = WEFAX_IMAGEN` a esta linea
             * tiene que devolver tambien ese descarte, o vuelve esa primera
             * fila. Lo que quedaba de el esta en la rama de imagen del
             * anillo, en wefax_process().
             */
            s_fase_hecho = 1U;
            s_fase_fin_n = 0U;
        }
    }
}

/* ==========================================================================
 * LOS TONOS DE ARRANQUE Y PARADA
 * ==========================================================================
 *
 * No son tonos de audio: son el BRILLO alternando entre negro y blanco a 300
 * Hz (arranque) o a 450 Hz (parada) durante cinco segundos. O sea que hay que
 * buscarlos en lo que sale del discriminador, no en el audio.
 */
/*
 * Y el cierre de la ventana: la misma cuenta de siempre, pero sobre el
 * estado que el filtro lleva acumulado en vez de sobre la ventana guardada.
 */
static float goertzel_fin(float s1, float s2, float coef)
{
    float m;
    const uint32_t n = APT_N;

    m = s1 * s1 + s2 * s2 - coef * s1 * s2;

    /* Normalizado a AMPLITUD AL CUADRADO, dividiendo por (n/2)^2. Asi el
     * umbral de mas abajo es un numero con significado -cuanto tiene que
     * oscilar el brillo- y no un numero que depende del tamano de la
     * ventana. Un tono de arranque real oscila de negro a blanco, o sea una
     * amplitud de unos 160 en la escala de grises. */
    return m / (0.25f * (float)n * (float)n);
}

static void apt_mira(float v)
{
    float s0;

    /* Los dos filtros, una muestra. Es lo mismo que hacia el bucle de
     * goertzel(), solo que aqui y ahora en vez de 240 veces seguidas al
     * cerrar la ventana. */
    s0 = v + s_g3_coef * s_g3_s1 - s_g3_s2;  s_g3_s2 = s_g3_s1;  s_g3_s1 = s0;
    s0 = v + s_g4_coef * s_g4_s1 - s_g4_s2;  s_g4_s2 = s_g4_s1;  s_g4_s1 = s0;

    s_apt_n++;
    if (s_apt_n < APT_N) { return; }
    s_apt_n = 0U;

    {
        float m3 = goertzel_fin(s_g3_s1, s_g3_s2, s_g3_coef);
        float m4 = goertzel_fin(s_g4_s1, s_g4_s2, s_g4_coef);

        /* La ventana siguiente empieza de cero, igual que empezaba el bucle
         * de goertzel() con s1 = s2 = 0. */
        s_g3_s1 = s_g3_s2 = 0.0f;
        s_g4_s1 = s_g4_s2 = 0.0f;

        /*
         * Media exponencial de medio segundo: 25 bloques de 20 ms. Los
         * tonos duran cinco, asi que esto los ve de sobra.
         *
         * *** Y NO ES LO QUE IMPIDE QUE LO DISPARE UN GOLPE - 21/10/2026. ***
         *
         * Eso decia aqui, y es falso. La cuenta: goertzel_fin() devuelve
         * unos 26.000 para una alternacion de negro a blanco a fondo, asi
         * que UNA sola ventana de 20 ms mete 0,04 * 26.000 = 1.040 en la
         * media. El umbral de BUSCA son 200. O sea que la media no frena
         * nada: con el primer bloque ya va cinco veces por encima.
         *
         * Medido: desde WEFAX_BUSCA, un golpe de 0,2 s a 300 Hz entra en
         * la señal de fase, y entra igual con la media al doble de rapida.
         * Lo que de verdad filtra es la OTRA guarda, la de que el bin del
         * tono lleve al menos tres veces lo que lleve el otro -ver mas
         * abajo, y el caso 12d del banco-: eso es lo que deja fuera el
         * ruido, que reparte por igual.
         *
         * Y SE DEJA ASI, con lo que cuesta dicho. Un falso positivo desde
         * BUSCA cuesta 35 segundos en WEFAX_FASE, donde NO se encola ni un
         * renglon, y despues el tope manda a imagen y se pinta -y se
         * graba, si el boton esta puesto- lo que haya en el aire. O sea un
         * fichero de ruido que se ve y se borra.
         *
         * Lo otro -exigir que el tono se sostenga, por ejemplo un segundo-
         * se penso y no sale a cuenta: el reconocimiento tardaria ese
         * segundo de mas, y esos 35 s del tope se cuentan DESDE el
         * reconocimiento (ver tope_lineas_fase()), asi que la carta buena
         * empezaria casi dos renglones mas tarde. Cambiar un fichero de
         * ruido que se borra por dos renglones de menos en la cabecera de
         * TODAS las cartas no es un cambio bueno.
         */
        s_apt300 += 0.04f * (m3 - s_apt300);
        s_apt450 += 0.04f * (m4 - s_apt450);

        /*
         * *** EL TONO DE ARRANQUE TAMBIEN SACA DE IMAGEN - 21/10/2026. ***
         *
         * Antes, de WEFAX_IMAGEN solo se salia con el tono de parada. Y en
         * onda corta el final de una carta es justo donde se desvanece: si
         * ese tono llega enterrado y s_apt450 no pasa del umbral, el
         * decodificador se queda en imagen PARA SIEMPRE. La carta siguiente
         * -sus cinco segundos de arranque y sus treinta de señal de fase-
         * se pinta como imagen, a la velocidad y la fase de la anterior, y
         * no hay reenganche posible.
         *
         * Y arrastra al guardado: el fichero se cierra con la transicion de
         * imagen a no-imagen, asi que tampoco se cerraba. Un tono de parada
         * perdido eran DOS CARTAS COSIDAS en un fichero.
         *
         * Un tono de arranque sostenido es tan definitivo como uno de
         * parada: dice que empieza una carta, y una carta que empieza
         * termina la anterior. Lleva la misma guarda de 3 a 1 entre los dos
         * bines, que es la que ya impide que el contenido de una imagen
         * dispare el de parada.
         */
        {
            /*
             * Y EN IMAGEN SE EXIGE EL DOBLE - 21/10/2026.
             *
             * Buscando una carta, un falso positivo cuesta entrar en fase y
             * salir a los pocos segundos por el tope. Dentro de una carta
             * cuesta PARTIR EL FICHERO y tirar la media docena de segundos
             * que van a la señal de fase, asi que el listón no puede ser el
             * mismo.
             *
             * Y hay con que subirlo: el tono de arranque de verdad es una
             * alternacion de negro a blanco A FONDO, o sea la mayor
             * amplitud que puede dar el discriminador. Una trama de
             * semitono de una carta, aunque caiga justo en 300 Hz, llega
             * con una fraccion de eso.
             *
             * *** EL NUMERO, MEDIDO DE VERDAD - 21/10/2026. ***
             *
             * Aqui habia una cuenta hecha sobre el papel: "un cuadrado de
             * +-128 niveles da un fundamental de unos 163, o sea unos
             * 26.000 de salida", y de ahi "el 200 es un 2,8% de fondo de
             * escala" y "con 6.000 hace falta un 15%".
             *
             * El 163 esta bien -el fundamental de un cuadrado es 4/pi
             * veces su amplitud- pero lo que llega aqui NO es el cuadrado:
             * es lo que sale del discriminador, con sus dos promediadores
             * en cascada, y eso se come la mitad. Medido poniendo una
             * sonda en este mismo sitio, con el brillo alternando de 0 a
             * 255 a fondo:
             *
             *     a 300 Hz (arranque)  ->  15.009
             *     a 450 Hz (parada)    ->   7.241
             *
             * La diferencia entre los dos es el filtro: a 450 Hz atenua
             * mas que a 300. O sea que no hay UN fondo de escala, hay uno
             * por tono, y la cuenta de arriba usaba un tercero que no es
             * ninguno de los dos.
             *
             * Los numeros de verdad, en AMPLITUD del vaiven del brillo,
             * que es lo que se puede mirar en una carta:
             *
             *     200 en BUSCA (300 Hz)   ->  +-15 niveles de 128  (12%)
             *     200 en imagen (450 Hz)  ->  +-21 niveles         (16%)
             *     6.000 en imagen (300)   ->  +-81 niveles         (63%)
             *
             * Y los tres cuadran con los barridos de los casos 12b, 12c y
             * 12d del banco, que midieron +-82, +-24 y el salto de 350 Hz
             * sin saber nada de esta cuenta.
             *
             * LO QUE ESO SIGNIFICA, Y NO ES LO QUE DECIA. "Un tono de
             * verdad pasa con cuatro veces de margen aunque llegue
             * desvanecido" es falso para el de 6.000: un tono a fondo da
             * 15.009, o sea DOS VECES Y MEDIA el umbral, y en amplitud
             * hace falta que llegue al 63% del vaiven. Un tono de arranque
             * desvanecido seis decibelios -la mitad de amplitud- NO saca
             * de imagen.
             *
             * Y eso es justo cuando haria falta, porque esta rama existe
             * para cuando el tono de FIN se perdio en un desvanecimiento.
             * Se deja igual y se dice por que: bajarlo es lo que parte el
             * fichero con una trama de la propia carta -con 2.000 basta
             * una trama de +-48, que una carta si puede dar-, y partir una
             * carta buena por la mitad es peor que coser dos cuando la
             * propagacion se cae del todo. El que de verdad tiene que
             * cerrar la carta es el tono de parada, que esta en 200 por
             * eso mismo.
             */
            float umbral = (s_estado == WEFAX_IMAGEN) ? 6000.0f : 200.0f;

            if ((s_estado == WEFAX_BUSCA || s_estado == WEFAX_IMAGEN)
                && s_apt300 > umbral && s_apt300 > s_apt450 * 3.0f) {
                s_estado = WEFAX_FASE;
                fase_cero();
                            s_apt300 = s_apt450 = 0.0f;
            }
        }
        /*
         * El de PARADA se queda en 200 dentro de la imagen, y la asimetria
         * es a proposito: el tono de parada es el final ESPERADO de una
         * carta -el camino normal- y perderlo cuesta que dos cartas se
         * cosan en un fichero. El de arranque dentro de la imagen es la
         * escapatoria para cuando ese se pierde, o sea un segundo intento:
         * ahi conviene ser exigente, porque un falso positivo parte el
         * fichero por la mitad.
         */
        if (s_estado == WEFAX_IMAGEN || s_estado == WEFAX_FASE) {
            if (s_apt450 > 200.0f && s_apt450 > s_apt300 * 3.0f) {
                s_estado = WEFAX_BUSCA;
                /* B6: y el seguidor de fase no sobrevive a la carta. Si el
                 * descarte de linea se queda armado, le tira una linea buena
                 * a la siguiente; y si se queda armado "las pasadas ya estan
                 * hechas", la siguiente no mide nada. */
                fase_cero();
                s_apt300 = s_apt450 = 0.0f;
            }
        }
    }
}

/* ==========================================================================
 * LA ENTRADA
 * ========================================================================== */
void wefax_process(const float *audio, uint32_t n)
{
    uint32_t k;

    if (!s_on || !audio) { return; }

    for (k = 0U; k < n; k++) {
        float hz = disc_fm_paso(&s_disc, audio[k]);
        float gris_f;
        uint32_t px;

        gris_f = (float)wefax_gris_de_hz(hz);
        apt_mira(gris_f - 128.0f);

        /* 4. repartir en puntos: el acumulador da una vuelta por linea y su
         * parte alta dice en que punto de la linea vamos. */
        s_fase += s_inc;
        px = (uint32_t)(((uint64_t)s_fase * (uint64_t)WEFAX_ANCHO) >> 32);

        if (px != s_px_ant) {
            if (s_acc_n > 0UL) {
                float g = s_acc / (float)s_acc_n;
                if (g < 0.0f) { g = 0.0f; }
                if (g > 255.0f) { g = 255.0f; }
                s_linea[s_esc][s_px_ant] = (uint8_t)g;
            }
            s_acc = 0.0f;
            s_acc_n = 0UL;

            /* Vuelta al principio: la linea esta entera. */
            if (px < s_px_ant) {
                /*
                 * PRIMERO SE CIERRA, DESPUES SE PUBLICA - 09/10/2026.
                 *
                 * Estaba al reves: se subia s_linea_lista y SOLO DESPUES se
                 * cambiaba de buffer. Y el recogedor devolvia entonces
                 * s_linea[s_esc ^ 1] -habia dos buffers, no ocho-, o sea
                 * "el que acaba de cerrarse", contando con que s_esc ya
                 * hubiera cambiado. Si el bucle
                 * principal entraba entre las dos lineas -y entra: esto va
                 * en la interrupcion y aquello graba 1.809 bytes en la
                 * flash- se llevaba el buffer EQUIVOCADO, que es la linea
                 * ANTERIOR, y ademas se comia la bandera: la linea buena se
                 * perdia y la que se dibujaba y se guardaba era la vieja
                 * con la nueva escribiendose encima de izquierda a derecha.
                 *
                 * Asi es como lo hacen sstv.c y hell.c: se guarda el dato y
                 * despues se avanza. Este era el unico al reves.
                 */
                uint8_t cerrada = s_esc;

                s_lineas++;
                s_esc = (uint8_t)((s_esc + 1U) % WFX_ANILLO);
                if (s_estado == WEFAX_FASE) {
                    fase_linea(s_linea[cerrada]);
                    /*
                     * Y EL LECTOR SIGUE AL ESCRITOR MIENTRAS NO HAYA NADA
                     * QUE RECOGER.
                     *
                     * Sin esto, el anillo se descoloca: durante los treinta
                     * segundos de señal de fase se cierran sesenta lineas
                     * que NO se encolan, el escritor da siete vueltas al
                     * anillo y el lector se queda clavado en la ranura
                     * cero. Cuando empieza la imagen, la primera linea que
                     * se entrega no es la que se acaba de cerrar sino una
                     * ranura cualquiera con una linea de la fase dentro.
                     *
                     * Lo encontro el banco de sim/wefaxtest.c comparando la
                     * misma señal recogida al momento y recogida tarde:
                     * salian imagenes distintas, y no por el retraso.
                     *
                     * Y SE VACIA LA CUENTA, no solo el indice - 21/10/2026.
                     * Poner s_lee = s_esc dejando s_linea_lista > 0 dice
                     * "hay lineas que recoger, y la primera es la que
                     * estoy escribiendo ahora". El recogedor pasa la
                     * guarda y entrega el buffer vivo. Se llega ahi al
                     * acabar una carta: el tono de parada manda a BUSCA
                     * sin tocar la cuenta, y lo que quedara sin recoger se
                     * graba como renglones de basura al pie del fichero.
                     */
                    s_lee = s_esc;
                    s_linea_lista = 0U;
                } else if (s_estado == WEFAX_IMAGEN) {
                    /*
                     * Y SI EL BUCLE NO HA RECOGIDO, SE TIRA LA MAS VIEJA.
                     *
                     * Con el anillo lleno -ocho lineas sin recoger, o sea
                     * cuatro segundos de bucle parado- hay que elegir entre
                     * perder la vieja o perder la nueva. Se pierde la
                     * vieja y se adelanta el lector: asi lo que se guarda
                     * sigue siendo lo ultimo que ha entrado por la antena,
                     * que es lo que el usuario esta mirando en la pantalla.
                     */
                    /*
                     * *** EL TOPE ES SIETE, NO OCHO - 21/10/2026. ***
                     *
                     * Con ocho sin recoger, s_esc y s_lee son LA MISMA
                     * ranura: el invariante del anillo es
                     * s_esc = (s_lee + lista) % 8, asi que lista = 8 los
                     * hace coincidir. Y entonces el recogedor entregaba al
                     * bucle principal justo la ranura que esta
                     * interrupcion esta rellenando de izquierda a derecha.
                     *
                     * O sea el mismo renglon partido por la mitad -media
                     * linea de ahora y media de hace ocho- que el anillo
                     * de ocho vino a quitar cuando habia dos buffers. El
                     * agujero se mudo del final del anillo al principio.
                     *
                     * La capacidad util de un anillo con escritor y lector
                     * separados es N-1. Siete lineas siguen siendo 3,5 s a
                     * 120 lpm, que era el margen que se buscaba.
                     */
                    /*
                     * *** AQUI VIVIO EL DESCARTE DE LA RANURA PARTIDA, Y
                     * SE FUE EL 21/10/2026 CON LO QUE LO HACIA FALTA. ***
                     *
                     * Habia un `if (s_tira_ranura)` que tiraba el primer
                     * renglon de la carta. Hacia falta mientras el paso de
                     * fase a imagen se daba JUSTO DESPUES del salto del
                     * acumulador: ese salto deja s_fase dado la vuelta pero
                     * s_px_ant con el px de antes, asi que la muestra
                     * siguiente pasaba de 0 a ~453 sin cruzar el cero y los
                     * puntos 1..452 de ese renglon no los escribia nadie.
                     *
                     * Hoy, hechas las pasadas, se sigue en WEFAX_FASE hasta
                     * que la señal se acaba -ver s_fase_hecho-: el renglon
                     * partido se cierra en la rama de fase, que no encola, y
                     * cuando se entra en imagen han pasado lineas enteras.
                     * O sea que la bandera no se armaba NUNCA y esta rama no
                     * se ejecutaba nunca: codigo muerto cuyo comentario
                     * decia que protegia la primera fila de cada carta.
                     *
                     * Quien devuelva el paso a imagen a donde estaba tiene
                     * que devolver tambien el descarte, y esto es lo unico
                     * que queda escrito de el.
                     */
                    if (s_linea_lista >= (WFX_ANILLO - 1U)) {
                        s_lee = (uint8_t)((s_lee + 1U) % WFX_ANILLO);
                    } else {
                        s_linea_lista++;
                    }
                } else {
                    /* Ni fase ni imagen: no se encola, asi que el lector se
                     * pone al dia. Misma razon que arriba, y la cuenta se
                     * vacia por la misma razon que arriba. */
                    s_lee = s_esc;
                    s_linea_lista = 0U;
                }
            }
            s_px_ant = px;
        }

        s_acc += gris_f;
        s_acc_n++;
    }
}
