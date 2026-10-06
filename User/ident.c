#include "ident.h"
#include "idioma.h"
#include "hfdl_ram.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

/* Ver ident.h para el porque. Aqui el como, y la tabla. */

/*
 * ==========================================================================
 * LA TABLA
 * ==========================================================================
 *
 * Cada fila es una firma. Los rangos a 0,0 no se miran. Una fila cuadra si
 * cuadran todos los rangos que no sean 0,0.
 *
 * EL ORDEN IMPORTA PARA LEERLA, NO PARA DECIDIR: se recorre entera siempre
 * y salen todas las que cuadren. Si eso son tres, se dicen las tres.
 *
 * `probada` a 1 es "esto se comprueba contra una grabacion en el banco".
 * A 0 es "el numero viene de la norma o del catalogo, pero NADIE lo ha
 * visto salir de esta radio". La pantalla lo marca con un interrogante, y
 * eso no es adorno: es la diferencia entre una medida y una cita.
 */
static const ident_firma_t k_firmas[] = {

/* --- portadoras y silencio ------------------------------------------- */
{
    "Portadora sola (heterodino)", IDENT_POR_PICO,
    0U,0U, 0U,0U, 0U,0U, 0U,120U, 81U,100U, 0U,0U, 0U,0U, 0U,0U, 0U,0U, 1U,
    "No lleva datos. Medida: 57% de la potencia en +-8 Hz.",
    "Lone carrier (heterodyne)",
    "Carries no data. Measured: 57% of the power within +-8 Hz."
},
/*
 * EL CW ES LA MISMA RAYA, PERO PARPADEANDO - 05/10/2026.
 *
 * *** El dueño: "implementa la identificacion de todas las señales que
 * conocemos". ***
 *
 * Un tono de Morse y un heterodino son LO MISMO para el espectro: toda la
 * potencia en una raya estrecha. Lo unico que los separa con lo que aqui se
 * mide es el ciclo de trabajo: una portadora esta siempre, y el Morse se
 * enciende y se apaga.
 *
 * Por eso la fila de la portadora, que hasta hoy no miraba el ciclo, ahora
 * pide 81% o mas: sin eso se quedaba con el Morse tambien y esta fila no
 * habria cuadrado nunca.
 *
 * El 15..80 no sale de ninguna norma -el Morse no tiene una- sino de la
 * aritmetica: con la proporcion clasica de 1 a 3 entre punto y raya y los
 * espacios de la norma ITU-R M.1677, el texto corriente sale alrededor del
 * 50%, y los extremos son uno todo rayas y otro todo puntos. Por eso va sin
 * comprobar: hace falta una grabacion.
 */
{
    "CW (Morse)", IDENT_POR_PICO,
    0U,0U, 0U,0U, 0U,0U, 0U,200U, 15U,80U, 0U,0U, 0U,0U, 0U,0U, 0U,0U, 0U,
    "Un tono que se enciende y se apaga. Ponlo en CW.",
    "CW (Morse)",
    "A tone switching on and off. Put it in CW."
},

/* --- FSK de dos tonos ------------------------------------------------- */
/*
 * LAS CUATRO DE 100 BAUDIOS Y 170 Hz SON LA MISMA MODULACION y por eso van
 * en filas distintas separadas SOLO por el ciclo de trabajo. No hay otra
 * forma de distinguirlas sin decodificar:
 *   SITOR-B / NAVTEX  emiten seguido        -> ciclo alto
 *   SITOR-A / DSC     emiten a rafagas      -> ciclo bajo
 * Y dentro de cada pareja, ya no se pueden separar por la señal: NAVTEX es
 * un SERVICIO que va en SITOR-B, no una modulacion distinta.
 */
{
    "SITOR-B / NAVTEX / telex", IDENT_POR_TONOS,
    150U,190U, 950U,1050U, 0U,0U, 0U,0U, 70U,100U, 0U,0U, 0U,0U, 0U,0U, 0U,0U, 1U,
    "Emite seguido. Ponlo en NAVTEX: sale texto en claro.",
    "SITOR-B / NAVTEX / telex",
    "Transmits continuously. Put it in NAVTEX: plain text comes out."
},
{
    "SITOR-A o DSC (a rafagas)", IDENT_POR_TONOS,
    150U,190U, 950U,1050U, 0U,0U, 0U,0U, 0U,69U, 0U,0U, 0U,0U, 0U,0U, 0U,0U, 0U,
    "Misma modulacion que el NAVTEX pero a rafagas. La radio no lo decodifica.",
    "SITOR-A or DSC (bursts)",
    "Same modulation as NAVTEX but in bursts. This radio does not decode it."
},
/*
 * COMPROBADA EN ANTENA - 06/10/2026, la cuarta. Y la fila YA ESTABA: lo que
 * fallaba era el detector de dos tonos, no la tabla.
 *
 * *** El dueño: "5339 que es?" y, con la radio en IDENT, "el modo ident va
 * variando entre lo que es". ***
 *
 * La señal de 5339 kHz. El detector no veia el par de tonos porque los
 * comparaba entre si y la ionosfera los desvanece por separado - ver el
 * comentario de TONO_REALCE en stanag_det.c, que es donde se arreglo.
 *
 * LO BONITO DE ESTA: se midio DOS VECES por caminos que no comparten nada.
 * Yo, con numpy sobre un audio que el dueño grabo con el movil delante del
 * altavoz; y la radio, con su detector, sobre la señal de verdad:
 *
 *                la radio      yo        diferencia
 *     tono bajo     508 Hz     510 Hz      0,4 %
 *     tono alto    1345 Hz    1348 Hz      0,2 %
 *     salto         841 Hz     838 Hz      0,4 %
 *     velocidad    74,7 Bd    75,11 Bd     0,5 %
 *
 * Cuatro numeros, cuatro aciertos por debajo del medio por ciento. Eso es
 * mas que "la fila funciona": es que el detector y el analisis de fuera
 * miden lo MISMO, que es lo unico que hace creible todo lo demas.
 *
 * Y CIFRADA, comprobado y no supuesto, igual que con FUG: la radio puesta
 * en RTTY-U con 850 Hz y 75 Bd saca Baudot CORRECTO -con cambios de letras
 * a figuras coherentes, ".051/891E"- y lo que sale no son palabras:
 * "MYSLBUGBVAMUSNWK", "DJBXB S CPLWHXBMEZBXQ". La demodulacion esta bien y
 * lo que va dentro esta cifrado. Ahi se para la radio.
 *
 * DE LA FRECUENCIA, lo que se sabe: el dueño trajo el factsheet "Unid ALE
 * Nets" de la UDXF y 5339 esta en el, en una red ALE sin identificar con
 * los indicativos HPL, POC, VNL y ZOC. Asi que en esa frecuencia alternan
 * DOS cosas: las rafagas de 8-FSK del ALE -que esta misma pantalla habia
 * descrito bien, con "actividad 24 %"- y este 4481 FSK cuando hay trafico.
 * Parte del "va variando" era la señal cambiando de verdad, no el detector.
 */
{
    "STANAG 4481 FSK (KG-84)", IDENT_POR_TONOS,
    780U,920U, 700U,800U, 0U,0U, 0U,0U, 0U,0U, 0U,0U, 0U,0U, 0U,0U, 0U,0U, 1U,
    "Cifrado con KG-84. Se demodula, no se lee.",
    "STANAG 4481 FSK (KG-84)",
    "Encrypted with KG-84. It demodulates, it does not read."
},
/*
 * COMPROBADA EN ANTENA - 05/10/2026, la tercera. Y la fila NO EXISTIA.
 *
 * *** El dueño: "fug french navy fsk estoy escuchando ahora" / "13.418". ***
 *
 * 850 Hz de salto con 50 baudios es de lo mas visto en trafico naval y
 * diplomatico, y entre las dos filas de al lado se escapaba por los dos
 * lados: la de 4481 pide 70..80 baudios, y la de RTTY comercial pide
 * 400..500 Hz de salto. Sin fila que cuadrara, la pantalla se caia a la
 * pista del ancho y decia "SSTV o fax meteorologico" con dos tonos
 * limpios delante. Un disparate, y de los que mas engañan: suena a
 * respuesta.
 *
 * LO MEDIDO, en 13.418 kHz con FUG emitiendo:
 *   tonos 1195 y 2045 Hz, salto 850 Hz
 *   50,00 baudios EXACTOS -alineando 806 transiciones a una rejilla,
 *   calidad 0,958-, no 75 como se esperaba de la marina francesa
 *   95% de actividad, emision continua
 *
 * Y CIFRADA, comprobado y no supuesto: demodulada en ITA2 aparte, salen
 * letras con reparto plano -O 8, N 7, A 7, H 6- sin palabras ni espacios.
 * La pantalla de la radio, puesta en RTTY-U, enseñaba a la vez lo mismo:
 * "TTTI / A / O / EZ A / E A / HIA". Eso no es texto, es cifra, y encaja
 * con KG-84.
 *
 * La nota lo dice tal cual porque es lo honrado: la radio llega hasta la
 * forma de onda y ahi se para.
 */
{
    "FSK 50 Bd / 850 Hz (naval)", IDENT_POR_TONOS,
    780U,920U, 480U,520U, 0U,0U, 0U,0U, 0U,0U, 0U,0U, 0U,0U, 0U,0U, 0U,0U, 1U,
    "Ponlo en RTTY. Suele ir cifrada: salen letras sin sentido.",
    "FSK 50 Bd / 850 Hz (naval)",
    "Put it in RTTY. Usually encrypted: letters come out meaningless."
},
{
    "RTTY de aficionado", IDENT_POR_TONOS,
    150U,190U, 440U,470U, 0U,0U, 0U,0U, 0U,0U, 0U,0U, 0U,0U, 0U,0U, 0U,0U, 0U,
    "45,45 baudios. Ponlo en RTTY-L o RTTY-U.",
    "Amateur RTTY",
    "45.45 baud. Put it in RTTY-L or RTTY-U."
},
/*
 * COMPROBADA EN ANTENA - 05/10/2026, la segunda.
 *
 * Grabacion de 34,9 s del dueño. El motor midio salto 446 Hz y 49,7 Bd
 * -la fila pide 400..500 y 48,0..52,0- y dijo "RTTY comercial 50 Bd".
 * Y no se quedo en que lo pareciera: la grabacion se demodulo aparte,
 * FSK de dos tonos a 50 baudios con la marca en el tono BAJO, y salio
 * texto ITA2 limpio:
 *
 *     HUMBER (53.5N   2.3E) WT: 17 C
 *     DI  6. 00Z: W      3-4        0.5 M
 *     SA 10. 00Z: W-NW     4        VT M
 *
 * Es el boletin maritimo del DWD: zona de mar, posicion, temperatura del
 * agua, y por dias viento y altura de ola. "SA" y "DI" son sabado y
 * martes en aleman, que es lo que acaba de decidir de quien es.
 *
 * Lo unico que hay que saber para oirlo: la marca es el tono bajo, o sea
 * que si sale basura hay que invertir el desplazamiento.
 *
 * CONFIRMADA CON NOMBRE Y APELLIDOS - 06/10/2026, en 3712 kHz.
 *
 * El dueño la tenia en 3382 sacando el boletin entero y en 3712 sacando
 * basura, con las dos bien sintonizadas -medido sobre sus fotos: las rayas
 * del oscilador encima de los tonos en las dos-. Era la INVERSION: en 3712
 * la marca es el tono ALTO. Con "Inversion: Invertida" puesta salio esto:
 *
 *     RYRYRYRYRYRYRYRYRYRYRYRY...
 *     CQ CQ CQ DE DDH47 DDH9 DDH8
 *     FREQUENCIES  147.3 KHZ  11039 KHZ  14467.3 KHZ
 *
 * DDH47, DDH9 y DDH8 son los indicativos del Deutscher Wetterdienst en
 * Pinneberg, y las tres frecuencias que el mismo anuncia cuadran con las
 * suyas. O sea que ya no es "parece el boletin aleman por el formato": lo
 * dice la propia estacion con su indicativo.
 *
 * Y de paso: el RYRYRY de arriba es el patron de reposo de toda la vida.
 * Alterna los dos tonos a cada simbolo, que es lo que mejor deja ver si el
 * desplazamiento y la velocidad estan bien puestos.
 */
{
    "RTTY comercial 50 Bd", IDENT_POR_TONOS,
    400U,500U, 480U,520U, 0U,0U, 0U,0U, 0U,0U, 0U,0U, 0U,0U, 0U,0U, 0U,0U, 1U,
    "50 baudios, 450 Hz. Ponlo en RTTY.",
    "Commercial RTTY 50 Bd",
    "50 baud, 450 Hz. Put it in RTTY."
},
/*
 * APRS. Los dos tonos y la velocidad salen de nuestro propio ax25.c
 * -AX25_MARCA_HZ 1200, AX25_ESPACIO_HZ 2200, AX25_BAUD 1200-, que es la
 * fuente mas firme que hay para esta radio: es lo que el decodificador
 * espera de verdad.
 *
 * Va por separacion y no por velocidad A PROPOSITO: 1200 baudios a 3 kHz
 * son dos muestras y media por simbolo, y el medidor solo llega a 600. Ver
 * IDENT_POR_SEP en ident.h.
 */
{
    "APRS / paquete AFSK 1200", IDENT_POR_SEP,
    900U,1100U, 0U,0U, 0U,0U, 0U,0U, 0U,0U, 0U,0U, 0U,0U, 0U,0U, 0U,0U, 0U,
    "Tonos de 1200 y 2200 Hz. Ponlo en APRS.",
    "APRS / AFSK 1200 packet",
    "Tones of 1200 and 2200 Hz. Put it in APRS."
},

/* --- PSK, por el periodo de la trama ---------------------------------- */
/*
 * Los cinco periodos estan MEDIDOS contra grabaciones reales, no copiados:
 * ver el banco sim/stanag_aire.c, que los contrasta uno a uno. El margen
 * del 1% viene de ahi: el peor de los cinco quedo a 0,03% de la norma.
 */
{
    "STANAG 4285 o 4481 PSK", IDENT_POR_TRAMA,
    0U,0U, 0U,0U, 10560U,10774U, 0U,0U, 0U,0U, 0U,0U, 0U,0U, 0U,0U, 0U,0U, 1U,
    "106,67 ms. Ponlo en STANAG: a 600 bps y entrelazado largo hay texto.",
    "STANAG 4285 or 4481 PSK",
    "106.67 ms. Put it in STANAG: at 600 bps with long interleave there is text."
},
{
    "STANAG 4529", IDENT_POR_TRAMA,
    0U,0U, 0U,0U, 20957U,21381U, 0U,0U, 0U,0U, 0U,0U, 0U,0U, 0U,0U, 0U,0U, 1U,
    "211,69 ms. Es media banda: 1,24 kHz. No se demodula todavia.",
    "STANAG 4529",
    "211.69 ms. Half bandwidth: 1.24 kHz. Not demodulated yet."
},
{
    "STANAG 4415", IDENT_POR_TRAMA,
    0U,0U, 0U,0U, 6613U,6749U, 0U,0U, 0U,0U, 0U,0U, 0U,0U, 0U,0U, 0U,0U, 1U,
    "66,81 ms. Para canales muy malos. No se demodula todavia.",
    "STANAG 4415",
    "66.81 ms. For very bad channels. Not demodulated yet."
},
{
    "STANAG 4539 / MIL-188-110B", IDENT_POR_TRAMA,
    0U,0U, 0U,0U, 11841U,12081U, 0U,0U, 0U,0U, 0U,0U, 0U,0U, 0U,0U, 0U,0U, 1U,
    "119,61 ms, que son 287 simbolos. No se demodula todavia.",
    "STANAG 4539 / MIL-188-110B",
    "119.61 ms, which is 287 symbols. Not demodulated yet."
},
{
    "STANAG 4538 / MIL-188-141B", IDENT_POR_TRAMA,
    0U,0U, 0U,0U, 1320U,1347U, 0U,0U, 0U,0U, 0U,0U, 0U,0U, 0U,0U, 0U,0U, 1U,
    "13,33 ms y a rafagas: es el 3G ALE, busca enlace.",
    "STANAG 4538 / MIL-188-141B",
    "13.33 ms and in bursts: this is 3G ALE looking for a link."
},

/* --- los tres estrechos de aficionado, por el ancho -------------------- */
/*
 * AQUI EL ANCHO SI IDENTIFICA, y es el unico sitio donde lo hace.
 *
 * Los tres son modos digitales de aficionado que no llevan ni dos tonos
 * que contar ni una trama que se repita dentro de los 256 ms que alcanza
 * la autocorrelacion. Lo que SI tienen es un ancho de banda fijado por la
 * norma y muy distinto entre ellos, y una casilla del espectro mide 1,46
 * Hz: de sobra para separar 6 de 31 y de 50.
 *
 *   WSPR   4-FSK con los tonos a 1,4648 Hz -el numero esta en nuestro
 *          wspr_msg.h- : cuatro tonos son 5,9 Hz. Emite 110,6 s de cada
 *          ventana de 120, o sea que mientras esta, esta entero.
 *   PSK31  BPSK a 31,25 baudios -PSK31_BAUDIO_X100 en nuestro psk31.h-,
 *          con la envolvente en coseno que le quita las colas: el lobulo
 *          util son los mismos 31 Hz de la velocidad.
 *   FT8    8-FSK con los tonos a 6,25 Hz -ver ft8_waterfall_adapter.h-:
 *          ocho tonos son 50 Hz. Emite 12,64 s de cada 15.
 *
 * Los margenes son anchos a proposito por abajo: el ancho a -10 dB de una
 * señal debil se come las faldas y mide de menos.
 *
 * Las tres van SIN COMPROBAR hasta que haya grabacion de cada una. Los
 * numeros salen de la norma y de nuestros propios decodificadores, que es
 * lo que pide la regla, pero medir no es lo mismo que calcular.
 */
{
    "WSPR", IDENT_POR_ANCHO,
    0U,0U, 0U,0U, 0U,0U, 1U,14U, 80U,100U, 0U,0U, 0U,0U, 0U,0U, 0U,0U, 0U,
    "4-FSK de 6 Hz. Ponlo en WSPR: tarda dos minutos por ciclo.",
    "WSPR",
    "4-FSK of 6 Hz. Put it in WSPR: two minutes per cycle."
},
{
    "PSK31", IDENT_POR_ANCHO,
    0U,0U, 0U,0U, 0U,0U, 15U,40U, 30U,100U, 0U,0U, 0U,0U, 0U,0U, 0U,0U, 0U,
    "31,25 baudios en BPSK. Ponlo en PSK31.",
    "PSK31",
    "31.25 baud in BPSK. Put it in PSK31."
},
/*
 * ESTA FILA ES PARA UNA SEÑAL SOLA, no para una banda de FT8. En 7.074 hay
 * cuarenta a la vez y el ancho medido es el del monton: ahi quien
 * identifica es la cadencia, tres filas mas abajo. Esta sirve cuando se
 * ha estrechado el filtro sobre una, o cuando hay una suelta fuera de
 * banda.
 */
{
    "FT8 (una sola)", IDENT_POR_ANCHO,
    0U,0U, 0U,0U, 0U,0U, 41U,80U, 50U,100U, 0U,0U, 0U,0U, 0U,0U, 0U,0U, 0U,
    "8-FSK de 50 Hz. Ponlo en FT8: emite 12,6 s de cada 15.",
    "FT8 (a single one)",
    "8-FSK of 50 Hz. Put it in FT8: 12.6 s out of every 15."
},

/* --- los que se reconocen por el reloj --------------------------------- */
/*
 * *** El dueño: "lo primero que he probado es ft8 en 7074, y no es capaz de
 * identificarlo". ***
 *
 * Estas tres filas son la respuesta, y no miran el espectro: miran el
 * RELOJ. Ver ident_cad_t en ident.h para por que hacia falta otro
 * instrumento.
 *
 * Los tres periodos son de la norma de cada modo y estan en la
 * documentacion de WSJT-X:
 *
 *   FT8   ciclo de 15 s, emision de 12,64 -> 84%
 *   FT4   ciclo de 7,5 s, emision de 4,48 -> 60%
 *   WSPR  ciclo de 120 s, emision de 110,6 -> 92%
 *
 * Los margenes son anchos por abajo en la ocupacion a proposito: si la
 * señal es debil, el medidor cuenta como apagado el principio y el final de
 * cada emision y la ocupacion sale de menos. Por arriba se cierran, porque
 * algo que ocupa el 98% de su ciclo no se esta apagando, esta siempre.
 *
 * El WSPR necesita CUATRO MINUTOS de escucha para que entren dos ciclos. La
 * pantalla dice cuanto lleva, que si no parece que no hace nada.
 */
/*
 * COMPROBADA EN ANTENA el 05/10/2026: el dueño puso el modo en 7.074 kHz y
 * la radio midio "ciclo 15,5 s, ocupa 86%" contra los 15,0 y 84% de la
 * norma. Medio segundo era una anotacion del cuaderno, y por eso el
 * periodo pasa a promediarse: ver cad_calcula().
 */
{
    "FT8 (o JS8 de 15 s)", IDENT_POR_CADENCIA,
    0U,0U, 0U,0U, 0U,0U, 0U,0U, 0U,0U, 140U,160U, 60U,95U, 0U,0U, 0U,0U, 1U,
    "Todas empiezan a la vez cada 15 s. Ponlo en FT8.",
    "FT8 (or 15 s JS8)",
    "They all start together every 15 s. Put it in FT8."
},
{
    "FT4", IDENT_POR_CADENCIA,
    0U,0U, 0U,0U, 0U,0U, 0U,0U, 0U,0U, 68U,82U, 40U,80U, 0U,0U, 0U,0U, 0U,
    "Ciclo de 7,5 s. Esta radio no lo decodifica todavia.",
    "FT4",
    "7.5 s cycle. This radio does not decode it yet."
},
{
    "WSPR", IDENT_POR_CADENCIA,
    0U,0U, 0U,0U, 0U,0U, 0U,0U, 0U,0U, 1150U,1250U, 80U,98U, 0U,0U, 0U,0U, 0U,
    "Ciclo de dos minutos. Ponlo en WSPR.",
    "WSPR",
    "Two-minute cycle. Put it in WSPR."
},

/* --- los que se reconocen por el ritmo del simbolo --------------------- */
/*
 * *** El dueño, con una grabacion de una banda de aficionado: "esto que
 * podria ser?". Era un MFSK de VEINTIUN tonos separados 23,44 Hz y la radio
 * dijo "podria ser ALE 2G" mirando solo el ancho. ***
 *
 * Estas filas son la respuesta. No miran cuantos tonos hay ni donde estan
 * -eso no se puede medir con esta ventana, ver SIMB_W en stanag_det.c-,
 * miran CADA CUANTO CAMBIA DE TONO, que es una medida sola y vale para
 * cualquier cosa que tenga simbolos.
 *
 * Y las tres piden expresamente que NO se hayan visto dos tonos: un RTTY
 * tambien tiene velocidad de simbolo, pero ese ya lo identifican las filas
 * de dos tonos, que son mas firmes. Estas son para cuando hay muchos.
 */
{
    "MFSK multitono (Olivia y parecidos)", IDENT_POR_SIMBOLO,
    0U,0U, 0U,0U, 0U,0U, 150U,2600U, 0U,0U, 0U,0U, 0U,0U, 150U,400U, 4U,60U, 0U,
    "Separacion = velocidad. Divide el ancho entre ella y salen los tonos.",
    "Multitone MFSK (Olivia and the like)",
    "Spacing = speed. Divide the bandwidth by it to get the tones."
},
{
    "ALE 2G (MIL-188-141)", IDENT_POR_SIMBOLO,
    0U,0U, 0U,0U, 0U,0U, 1400U,2400U, 0U,0U, 0U,0U, 0U,0U, 1150U,1350U, 0U,0U, 0U,
    "125 baudios, 8 tonos de 750 a 2500 Hz. Ponlo en ALE.",
    "ALE 2G (MIL-188-141)",
    "125 baud, 8 tones from 750 to 2500 Hz. Put it in ALE."
},
{
    "Teletipo de 100 Bd sin dos tonos claros", IDENT_POR_SIMBOLO,
    0U,0U, 0U,0U, 0U,0U, 100U,900U, 0U,0U, 0U,0U, 0U,0U, 950U,1050U, 0U,0U, 0U,
    "100 baudios. Si fuera SITOR saldria arriba; prueba NAVTEX igual.",
    "100 Bd teletype without two clear tones",
    "100 baud. A SITOR would show up above; try NAVTEX anyway."
},

/* --- imagen por subportadora de FM ------------------------------------ */
/*
 * SSTV Y FAX SON LA MISMA FORMA DE ONDA y esta radio NO LOS SEPARA. Los
 * dos mandan la imagen como frecuencia dentro de la misma ventana -1500 Hz
 * el negro, 2300 el blanco- y los dos van seguidos. Lo unico que los
 * distingue de verdad es cuanto dura un renglon: 500 ms clavados el fax, y
 * entre 428 y 446 ms los modos de SSTV que lleva esta radio.
 *
 * Y ESO NO SE PUEDE MEDIR AQUI: la autocorrelacion mira hasta 768 muestras
 * a 3 kHz, o sea 256 ms. Los dos se salen por arriba.
 *
 * Asi que la fila dice las dos y lo dice. Prometer una de las dos a cara o
 * cruz seria exactamente lo que la cabecera de ident.h dice que este modo
 * no va a hacer nunca.
 */
{
    "SSTV o fax meteorologico", IDENT_POR_ANCHO,
    0U,0U, 0U,0U, 0U,0U, 600U,1300U, 85U,100U, 0U,0U, 0U,0U, 0U,0U, 0U,0U, 0U,
    "Imagen en FM, 1500-2300 Hz. Prueba SSTV y luego WEFAX.",
    "SSTV or weather fax",
    "Image in FM, 1500-2300 Hz. Try SSTV and then WEFAX."
},

/* --- por el ancho, y solo como pista ---------------------------------- */
/*
 * ESTAS NO IDENTIFICAN, ORIENTAN. En cada tramo de ancho de banda hay
 * decenas de cosas distintas, asi que lo que dicen es "mira por aqui", y
 * su texto lo deja claro. Estan porque un ancho medido ya descarta el 90%
 * del catalogo, y eso vale mas que un renglon en blanco.
 *
 * Y SE CALLAN CUANDO HABLA ALGUIEN CON MAS AUTORIDAD: ver ident_casa().
 */
{
    "Modem estrecho sin identificar", IDENT_POR_ANCHO,
    0U,0U, 0U,0U, 0U,0U, 81U,599U, 0U,0U, 0U,0U, 0U,0U, 0U,0U, 0U,0U, 0U,
    "Entre 0,1 y 0,6 kHz. Teletipo o ARQ: hay decenas.",
    "Unidentified narrow modem",
    "Between 0.1 and 0.6 kHz. Teletype or ARQ: there are dozens."
},
{
    "Podria ser ALE 2G", IDENT_POR_ANCHO,
    0U,0U, 0U,0U, 0U,0U, 1400U,2200U, 0U,0U, 0U,0U, 0U,0U, 0U,0U, 0U,0U, 0U,
    "8-FSK de 750 a 2500 Hz a rafagas cortas. Prueba ALE.",
    "Could be ALE 2G",
    "8-FSK from 750 to 2500 Hz in short bursts. Try ALE."
},
{
    "Podria ser HFDL, o un modem de fase", IDENT_POR_ANCHO,
    0U,0U, 0U,0U, 0U,0U, 2201U,3200U, 0U,0U, 0U,0U, 0U,0U, 0U,0U, 0U,0U, 0U,
    "Ocupa el canal entero. Prueba HFDL y mira el periodo de abajo.",
    "Could be HFDL, or a phase modem",
    "Takes the whole channel. Try HFDL and look at the period below."
},
};
#define N_FIRMAS ((uint16_t)(sizeof k_firmas / sizeof k_firmas[0]))

const ident_firma_t *ident_firmas(uint16_t *n)
{
    if (n) { *n = N_FIRMAS; }
    return k_firmas;
}

const char *ident_nombre(const ident_firma_t *f)
{
    return (f == 0) ? "" : tr(f->nombre, f->nombre_en);
}

const char *ident_nota(const ident_firma_t *f)
{
    return (f == 0) ? "" : tr(f->nota, f->nota_en);
}

/*
 * ¿Esta fila es solo una pista? Lo dice su propio nombre, que es lo que lee
 * el dueño: las que orientan empiezan por "Modem" o por "Podria". Ver el
 * final de ident_casa().
 */
/*
 * QUE UNA FILA DE ANCHO SEA UNA PISTA Y NO UNA RESPUESTA, MEDIDO EN LA
 * PROPIA FILA Y NO EN SU NOMBRE - 05/10/2026.
 *
 * Esto miraba si el nombre empezaba por "Modem" o por "Podria". Funcionaba
 * mientras las pistas se llamaran asi, y dejo de funcionar el dia que FUG
 * salio acompañado de "SSTV o fax meteorologico": un nombre que no empieza
 * por ninguna de las dos y que es tan adivinanza como las otras.
 *
 * Lo que de verdad separa una pista de una firma es LO ANCHA QUE SEA SU
 * VENTANA, y eso esta en la propia tabla:
 *
 *   WSPR                       1..14 Hz      13 de rango
 *   PSK31                     15..40 Hz      25
 *   FT8 (una sola)            41..80 Hz      39
 *   ---------------------------------------------------
 *   Modem estrecho           81..599 Hz     518
 *   SSTV o fax              600..1300 Hz    700
 *   Podria ser ALE 2G      1400..2200 Hz    800
 *   Podria ser HFDL        2201..3200 Hz    999
 *
 * Un abismo entre 39 y 518. Las de arriba dicen una anchura concreta de un
 * modo concreto; las de abajo reparten el espectro en cuatro cajones y
 * dicen en cual has caido. Con 100 Hz de corte quedan a un lado y a otro,
 * y el dia que se añada una fila ancha se clasificara sola sin que nadie
 * se acuerde de ponerle "Podria" delante.
 */
#define ANCHO_PISTA  100U

static uint8_t es_pista(const ident_firma_t *f)
{
    if (f->criterio != IDENT_POR_ANCHO) { return 0U; }
    if (f->ancho_max <= f->ancho_min)   { return 0U; }
    return (uint8_t)((uint16_t)(f->ancho_max - f->ancho_min) >= ANCHO_PISTA);
}

/* Un rango a 0,0 no se mira; si no, el valor tiene que caer dentro. */
static uint8_t dentro(uint16_t v, uint16_t lo, uint16_t hi)
{
    if (lo == 0U && hi == 0U) { return 1U; }
    return (uint8_t)(v >= lo && v <= hi);
}

uint16_t ident_casa(const stanag_det_t *m, const ident_cad_t *c,
                    uint8_t *salen, uint16_t max)
{
    uint16_t i, n = 0U;
    ident_cad_t sin_cad;

    if (m == 0 || salen == 0 || max == 0U) { return 0U; }
    if (c == 0) { memset(&sin_cad, 0, sizeof sin_cad); c = &sin_cad; }

    for (i = 0U; i < N_FIRMAS; i++) {
        const ident_firma_t *f = &k_firmas[i];

        /*
         * LO PRIMERO, QUE LA MEDIDA QUE PIDE LA FIRMA EXISTA. Una firma de
         * dos tonos no puede cuadrar si no se han visto dos tonos, por
         * mucho que los demas rangos esten a cero: sin esto, una fila con
         * todos los rangos abiertos cuadraria siempre.
         */
        switch (f->criterio) {
        case IDENT_POR_TONOS:
            if (!m->hay_tonos || m->baudios_x10 == 0U) { continue; }
            break;
        case IDENT_POR_CADENCIA:
            /* Sin cadencia medida no hay nada que comparar. Y con poca
             * firmeza tampoco: un ciclo que apenas destaca del ruido es
             * una coincidencia, no un modo. */
            if (!c->vale || c->periodo_ds == 0U || c->firmeza < 40U) { continue; }
            break;
        case IDENT_POR_SIMBOLO:
            /*
             * Aqui NO se exige que no haya dos tonos, aunque lo parezca.
             * El buscador de dos tonos coge las DOS RAYAS MAS ALTAS, y en
             * un MFSK de veintiuna siempre hay dos mas altas que las
             * demas: con un MFSK sintetico de 21 tonos dice que si. O sea
             * que "hay dos tonos" no significa "es una FSK de dos".
             *
             * Quien manda sobre estas filas no es esa bandera, es el
             * RESULTADO: si alguna fila de dos tonos ha cuadrado de verdad
             * -con su separacion y su velocidad- esa gana y estas se caen.
             * Se hace al final, con las pistas del ancho.
             */
            if (m->simb_x10 == 0U || m->simb_q < 12U) { continue; }
            break;
        case IDENT_POR_SEP:
            /* Dos tonos, y nada mas: la velocidad no se le pide porque a
             * 1200 baudios el medidor no llega. Ver IDENT_POR_SEP. */
            if (!m->hay_tonos || m->sep_hz == 0U) { continue; }
            break;
        case IDENT_POR_TRAMA:
            if (!m->vale || m->periodo_us == 0U) { continue; }
            break;
        case IDENT_POR_PICO:
            /* Una raya sola: mas de la mitad de la potencia en +-8 Hz. Con
             * 57% medido en la grabacion del dueño y 2% en un modem suyo,
             * el 40% parte la diferencia de sobra. */
            if (!m->hay_esp || m->pico_pc < 40U) { continue; }
            break;
        case IDENT_POR_ANCHO:
        default:
            if (!m->hay_esp || m->ancho10_hz == 0U) { continue; }
            /* Y que no sea una portadora: esa ya tiene su fila. */
            if (m->pico_pc >= 40U) { continue; }
            break;
        }

        if (!dentro(m->sep_hz,      f->sep_min,   f->sep_max))   { continue; }
        if (!dentro(m->baudios_x10, f->bd_min,    f->bd_max))    { continue; }
        if (!dentro(m->periodo_us,  f->trama_min, f->trama_max)) { continue; }
        if (!dentro(m->ancho10_hz,  f->ancho_min, f->ancho_max)) { continue; }
        if (!dentro((uint16_t)m->ciclo_pc, (uint16_t)f->ciclo_min,
                    (uint16_t)f->ciclo_max)) { continue; }
        if (!dentro(c->periodo_ds, f->cad_min, f->cad_max)) { continue; }
        if (!dentro((uint16_t)c->ocupa_pc, (uint16_t)f->ocup_min,
                    (uint16_t)f->ocup_max)) { continue; }
        if (!dentro(m->simb_x10, f->sim_min, f->sim_max)) { continue; }
        if (!dentro((uint16_t)m->tonos, (uint16_t)f->ton_min,
                    (uint16_t)f->ton_max)) { continue; }

        salen[n++] = (uint8_t)i;
        if (n >= max) { break; }
    }

    /*
     * LAS PISTAS SE CALLAN CUANDO HAY UN NOMBRE - 05/10/2026.
     *
     * Las filas de IDENT_POR_ANCHO que son pistas -"modem estrecho sin
     * identificar", "podria ser ALE"- cuadran por rangos muy abiertos, y
     * eso esta bien mientras sean lo unico que hay. En cuanto una fila con
     * un criterio fuerte -dos tonos, una trama que se repite, una raya
     * sola- ha dicho un nombre, la pista no añade nada y resta: la
     * pantalla ensena tres renglones y un "STANAG 4285" seguido de "podria
     * ser HFDL" se lee como una duda que no existe.
     *
     * Las tres filas de ancho que SI identifican -WSPR, PSK31, FT8- y la de
     * la imagen no son pistas: se quedan. Se distinguen porque su rango de
     * ancho es estrecho, pero fiarse de eso seria fragil, asi que lo que se
     * mira es el texto que ya tienen: una pista empieza por "Modem" o por
     * "Podria".
     *
     * Se hace aqui y no al construir la tabla porque es una regla sobre el
     * RESULTADO, no sobre las firmas: la misma fila es pista o no segun con
     * quien venga.
     */
    if (n > 1U) {
        uint16_t j, fuertes = 0U, dedos = 0U, k;

        for (j = 0U; j < n; j++) {
            uint8_t c2 = k_firmas[salen[j]].criterio;
            if (c2 == IDENT_POR_TONOS || c2 == IDENT_POR_SEP) { dedos++; }
            /* Una fila de ritmo de simbolo tambien es un nombre, asi que
             * tambien calla a las pistas del ancho. */
            if (c2 != IDENT_POR_ANCHO) { fuertes++; }
        }
        /*
         * Y si una fila de DOS TONOS ha cuadrado, las de ritmo de simbolo
         * se caen: un RTTY tiene velocidad de simbolo igual que un MFSK,
         * pero "170 Hz y 45,45 baudios" es un nombre y "algo que cambia 45
         * veces por segundo" es una familia. Gana el nombre.
         */
        k = 0U;
        for (j = 0U; j < n; j++) {
            const ident_firma_t *f = &k_firmas[salen[j]];
            if (fuertes > 0U && f->criterio == IDENT_POR_ANCHO && es_pista(f)) { continue; }
            if (dedos > 0U && f->criterio == IDENT_POR_SIMBOLO) { continue; }
            salen[k++] = salen[j];
        }
        n = k;
    }

    return n;
}

/*
 * ==========================================================================
 * EL MODO
 * ==========================================================================
 */
static uint8_t  s_activo;
static char     s_lin[IDENT_LINEAS][IDENT_LARGO];
static uint8_t  s_nlin;
static stanag_det_t s_m;
static uint32_t s_medidas;

/*
 * ==========================================================================
 * EL MEDIDOR DE CADENCIA
 * ==========================================================================
 *
 * *** El dueño: "lo primero que he probado es ft8 en 7074, y no es capaz de
 * identificarlo". ***
 *
 * Un cuaderno de energia: una anotacion cada medio segundo, 512 de ellas,
 * o sea los ULTIMOS CUATRO MINUTOS Y PICO. Con eso se ve lo que no cabe en
 * una ventana de 341 ms: que la banda entera se enciende y se apaga a
 * compas.
 *
 * Medio segundo por anotacion no es un numero redondo cualquiera: el ciclo
 * mas corto que hay que reconocer son los 7,5 s del FT4, o sea quince
 * anotaciones. Con una por segundo serian siete y medio, y medio ciclo de
 * error en un periodo de siete no lo distingue de nada.
 *
 * Y 1024 anotaciones -512 segundos- por el WSPR, que tarda 120 s en
 * repetirse. El periodo se mide contando el hueco entre subidas, y para
 * tener DOS huecos que comparar hacen falta tres subidas, o sea 360 s. Con
 * 256 s solo habria uno, y un hueco suelto no es un ciclo, es un salto.
 * Ocho minutos y medio dan cuatro ciclos y margen de sobra.
 *
 * La memoria sale del mismo prestamo que el motor -ver ram_coge()-, no de
 * .bss: dos kilobytes en una radio con mil novecientos bytes libres no se
 * piden al enlazador, se piden prestados a quien no esta.
 */
#define CAD_N       1024U       /* anotaciones: 1024 x 0,5 s = 512 s */
#define CAD_MS       500U       /* cada cuanto se anota */
#define CAD_MUESTRAS (12000U / 2U)   /* 0,5 s a 12 kHz */
#define CAD_MIN      16U        /* lo minimo para intentar medir: 8 s */
#define CAD_HUECOS   16U        /* huecos entre subidas que se guardan */

static float   *s_cad;          /* CAD_N energias, en el prestamo */
static uint16_t s_cad_n;        /* cuantas hay escritas, tope CAD_N */
static uint16_t s_cad_w;        /* donde va la siguiente */
static uint32_t s_cad_acu_n;    /* muestras acumuladas en la de ahora */
static float    s_cad_acu;      /* y su suma de cuadrados */
static uint16_t s_cad_desde;    /* anotaciones desde el ultimo calculo */
static ident_cad_t s_cad_r;

/* Una anotacion cada CAD_MUESTRAS muestras. Corre en la interrupcion de
 * audio, asi que aqui no hay mas que una multiplicacion y una suma. */
static void cad_mete(const float *a, uint16_t n)
{
    uint16_t i;

    if (s_cad == 0) { return; }
    for (i = 0U; i < n; i++) {
        s_cad_acu += a[i] * a[i];
        s_cad_acu_n++;
        if (s_cad_acu_n >= (uint32_t)CAD_MUESTRAS) {
            s_cad[s_cad_w] = s_cad_acu / (float)s_cad_acu_n;
            s_cad_w = (uint16_t)((s_cad_w + 1U) % CAD_N);
            if (s_cad_n < (uint16_t)CAD_N) { s_cad_n++; }
            s_cad_desde++;
            s_cad_acu = 0.0f;
            s_cad_acu_n = 0U;
        }
    }
}

/*
 * Y el calculo, que corre en el bucle principal.
 *
 * SE MIDE EL HUECO ENTRE SUBIDAS, NO LA AUTOCORRELACION. La primera
 * version correlacionaba la secuencia de encendidos consigo misma, que es
 * lo que se hace siempre, y con el FT8 y el FT4 funcionaba. Con el WSPR
 * daba 4 segundos.
 *
 * Y daba 4 segundos por un motivo que no es un error de programacion sino
 * de instrumento: el WSPR esta encendido el 92% del ciclo, o sea que sus
 * apagones duran 9 s y sus encendidos 110. Una onda cuadrada con tramos
 * tan largos correlaciona FORTISIMO consigo misma desplazada un poco -el
 * tramo se solapa con su propio tramo- y el maximo sale en los retardos
 * cortos, no en el periodo. La autocorrelacion mide "cuanto se parece a si
 * misma", y una señal que esta casi siempre encendida se parece a si misma
 * en casi cualquier desplazamiento.
 *
 * Lo que define el ciclo de estos modos no es el parecido: son las
 * SUBIDAS. Todas las emisiones empiezan a la vez, cada 15 s el FT8 y cada
 * 120 el WSPR, y eso se mide contando cuantas anotaciones hay de una
 * subida a la siguiente. Es mas simple, mas barato -no hay bucle de 256
 * retardos- y, sobre todo, no se deja engañar por la duracion del tramo.
 *
 * Tres pasos:
 *
 *  1. El umbral. En lineal no vale: entre el silencio y una emision fuerte
 *     hay cuarenta decibelios y la media lineal la manda el pico. El punto
 *     medio EN DECIBELIOS entre el maximo y el minimo es, en lineal, la
 *     media geometrica de los dos: sqrt(mx*mn). Asi que sale de una raiz y
 *     no de quinientos logaritmos.
 *
 *  2. Las subidas, con rebote. Una anotacion suelta por encima del umbral
 *     en mitad de un apagon es ruido, no una emision: hace falta que dos
 *     seguidas digan lo mismo para cambiar de estado. Sin esto, una banda
 *     con QRM cuenta subidas a docenas.
 *
 *  3. La MEDIANA de los huecos, no la media. Si una emision se pierde, ese
 *     hueco mide el doble y la media se va; la mediana no se entera. Y la
 *     firmeza sale de lo que se separan los huecos entre si: huecos todos
 *     iguales es un ciclo, huecos dispares es casualidad.
 */
static void cad_calcula(void)
{
    uint16_t i, n = s_cad_n;
    float mx = 0.0f, mn = 1e30f, umbral;
    uint32_t on = 0U;
    uint16_t hue[CAD_HUECOS];
    uint8_t  nh = 0U;
    uint16_t ult = 0U;
    uint8_t  hay_ult = 0U;
    uint8_t  est = 0U;      /* el estado con rebote */
    uint8_t  cand = 0U, cuenta = 0U;

    s_cad_r.vale = 0U;
    s_cad_r.segundos = (uint16_t)(((uint32_t)n * CAD_MS) / 1000U);
    if (n < (uint16_t)CAD_MIN || s_cad == 0) { return; }

    for (i = 0U; i < n; i++) {
        uint16_t k = (uint16_t)((s_cad_w + (uint16_t)CAD_N - n + i) % CAD_N);
        float e = s_cad[k];
        if (e < 1e-20f) { e = 1e-20f; }
        if (e > mx) { mx = e; }
        if (e < mn) { mn = e; }
    }
    /* Menos de 6 dB de recorrido es que esto no se apaga: no hay cadencia
     * que medir. 10^(6/10) = 3,98. */
    if (!(mx > mn * 3.98f)) { return; }
    umbral = sqrtf(mx * mn);

    for (i = 0U; i < n; i++) {
        uint16_t k = (uint16_t)((s_cad_w + (uint16_t)CAD_N - n + i) % CAD_N);
        uint8_t  b = (uint8_t)((s_cad[k] > umbral) ? 1U : 0U);

        if (b) { on++; }

        if (b == est) { cuenta = 0U; cand = est; continue; }
        if (b != cand) { cand = b; cuenta = 1U; continue; }
        cuenta++;
        if (cuenta < 2U) { continue; }

        est = b;
        cuenta = 0U;
        if (est == 1U) {                      /* una subida */
            if (hay_ult && nh < (uint8_t)CAD_HUECOS) {
                hue[nh++] = (uint16_t)(i - ult);
            }
            ult = i;
            hay_ult = 1U;
        }
    }

    s_cad_r.ocupa_pc = (uint8_t)((((float)on * 100.0f) / (float)n) + 0.5f);
    if (nh < 2U) { return; }   /* con un solo hueco no hay ciclo, hay un salto */

    /* La mediana, por ordenacion directa: son como mucho dieciseis. */
    for (i = 0U; i < (uint16_t)nh; i++) {
        uint16_t j;
        for (j = (uint16_t)(i + 1U); j < (uint16_t)nh; j++) {
            if (hue[j] < hue[i]) {
                uint16_t t = hue[i]; hue[i] = hue[j]; hue[j] = t;
            }
        }
    }
    {
        uint16_t med = hue[nh / 2U];
        uint16_t dentro = 0U;
        uint32_t suma = 0UL;

        if (med < 8U) { return; }   /* menos de 4 s: ningun modo de estos */

        /*
         * LA MEDIANA DECIDE QUIEN VALE; LA MEDIA DE LOS QUE VALEN DA EL
         * NUMERO - 05/10/2026.
         *
         * *** La radio, en 7.074: "ciclo 15,5 s ocupa 86%". El FT8 va a
         * 15,0. ***
         *
         * Medio segundo de mas, que es EXACTAMENTE una anotacion: el
         * cuaderno apunta cada 0,5 s, asi que un hueco medido solo puede
         * valer 15,0 o 15,5, y la mediana se queda con uno de los dos.
         *
         * Pero los huecos no se equivocan todos en la misma direccion: uno
         * sale largo y el siguiente corto, porque el error es donde cae el
         * corte, no el periodo. Promediando CUATRO huecos el error se
         * reparte y la resolucion baja de medio segundo a un octavo.
         *
         * Se promedian solo los que caen cerca de la mediana, que son los
         * mismos que cuentan para la firmeza: si una emision se pierde, ese
         * hueco mide el doble y la media se iria. La mediana elige, la
         * media afina.
         */
        for (i = 0U; i < (uint16_t)nh; i++) {
            uint16_t d = (hue[i] > med) ? (uint16_t)(hue[i] - med)
                                        : (uint16_t)(med - hue[i]);
            if ((uint32_t)d * 100UL <= (uint32_t)med * 15UL) {
                dentro++;
                suma += hue[i];
            }
        }
        s_cad_r.firmeza = (uint8_t)(((uint32_t)dentro * 100UL) / nh);
        /* x0,5 s -> decimas de segundo, con el redondeo dentro. */
        s_cad_r.periodo_ds = (uint16_t)(((suma * 5UL) + (dentro / 2U)) / dentro);
        s_cad_r.vale = 1U;
    }
}

static void pon(const char *t)
{
    if (s_nlin >= IDENT_LINEAS) { return; }
    (void)snprintf(s_lin[s_nlin], IDENT_LARGO, "%s", t);
    s_nlin++;
}

/*
 * 1 si TODO lo que ha cuadrado son pistas de ancho. Ver donde se usa.
 * Con n == 0 no se llama: eso ya tiene su propia frase.
 */
static uint8_t solo_pistas(const uint8_t *cand, uint16_t n)
{
    uint16_t i;
    for (i = 0U; i < n; i++) {
        if (!es_pista(&k_firmas[cand[i]])) { return 0U; }
    }
    return 1U;
}

static void ponf(const char *fmt, int a, int b, int c)
{
    if (s_nlin >= IDENT_LINEAS) { return; }
    (void)snprintf(s_lin[s_nlin], IDENT_LARGO, fmt, a, b, c);
    s_nlin++;
}

static float *ram_coge(uint32_t floats)
{
    uint8_t *b; uint32_t cap = 0U;
    hfdl_ram_coge();
    b = hfdl_ram_todo(&cap);
    if (b == 0 || cap < (floats * 4U)) { hfdl_ram_suelta(); return 0; }
    return (float *)(void *)b;
}

/*
 * Las ultimas separaciones medidas, para quedarse con la mediana. Ver
 * ident_modo_poll(). Dieciseis a cuatro por segundo son cuatro segundos de
 * memoria: bastante para comerse el desvanecimiento y poco para que la
 * pantalla tarde en reaccionar cuando de verdad cambias de señal.
 */
#define IDENT_SEP_N 16U
static uint16_t s_sep_anillo[IDENT_SEP_N];
static uint8_t  s_sep_pon;
static uint8_t  s_sep_hay;

static uint16_t sep_mediana(void)
{
    uint16_t o[IDENT_SEP_N];
    uint8_t i, j, n = s_sep_hay;

    for (i = 0U; i < n; i++) { o[i] = s_sep_anillo[i]; }
    /* Insercion: son como mucho dieciseis. */
    for (i = 1U; i < n; i++) {
        uint16_t v = o[i];
        for (j = i; j > 0U && o[j - 1U] > v; j--) { o[j] = o[j - 1U]; }
        o[j] = v;
    }
    return (n == 0U) ? 0U : o[n / 2U];
}

/*
 * CUANDO EN LA MISMA FRECUENCIA HAY MAS DE UNA COSA - 06/10/2026.
 *
 * *** El dueño, sobre 5339 kHz: "y el modo ident si detecta stanag y
 * ademas lleva rafagas ale deberia de indicarlo". *** Y tiene razon: ahi
 * hay dos emisiones distintas turnandose, y enseñar solo una es contar la
 * mitad.
 *
 * En 5339 conviven un STANAG 4481 FSK -que esta casi todo el rato- y una
 * red ALE que solo abre la boca cuando alguien llama: sondeos de dos o
 * tres segundos separados por minutos. La mediana de la separacion
 * (ver abajo) arregla el BAILE del nombre, pero al arreglarlo se come
 * justamente eso: la rafaga rara pasa a ser minoria y desaparece.
 *
 * Asi que se cuentan las dos cosas por separado:
 *
 *   - el NOMBRE de arriba sale de la medida suavizada, y es estable;
 *   - esta cuenta mira cada ventana EN CRUDO y apunta quien gano, de modo
 *     que una rafaga de tres segundos deja ocho o diez marcas aunque no
 *     mueva la mediana ni un hertzio.
 *
 * La memoria es exponencial: al llegar a IDENT_ALT_TOPE anotaciones se
 * parten todas por la mitad. Con ~3 ventanas por segundo, 480 son unos
 * 160 s de ventana y unos 5 min de memoria util: suficiente para que un
 * sondeo ALE siga contando cuando vuelve a tocar, y poco para que una
 * señal que se fue hace un cuarto de hora siga saliendo en pantalla.
 *
 * Y las pistas de ancho NO cuentan. "Podria ser ALE 2G" a secas es el
 * cajon de 1400-2200 Hz, no una identificacion; si eso valiera, cualquier
 * ruido ancho acabaria saliendo como segunda señal.
 */
/*
 * Y NO CUENTA UNA VENTANA SUELTA, SINO UNA RACHA - 06/10/2026, el mismo dia.
 *
 * La primera version apuntaba al ganador de cada ventana. El banco
 * sim/ident_turnos.c, en la tirada de control -el 4481 SOLO, sin ALE ni
 * nada mas-, saco esto:
 *
 *     STANAG 4481 FSK (KG-84)
 *     y a ratos: Portadora sola (heterodino)
 *
 * Y no habia ninguna portadora. Es el mismo desvanecimiento selectivo de
 * siempre: de vez en cuando uno de los dos tonos se va, la ventana se
 * queda con una raya sola y el 57% de la potencia en +-8 Hz dispara la
 * fila de la portadora. Una ventana aqui y otra alla, y en tres minutos
 * se juntan las seis que hacen falta.
 *
 * O sea que el aviso se inventaba una segunda señal, que es peor que no
 * darlo: manda al dueño a buscar algo que no esta.
 *
 * Lo que separa una emision de verdad de un hipo del desvanecimiento no es
 * CUANTAS veces aparece, es SI SE QUEDA. Un sondeo ALE ocupa el canal dos
 * o tres segundos seguidos -ocho o nueve ventanas-; un tono que se cae un
 * momento ocupa una y la siguiente ya vuelve a ser lo de antes. Asi que
 * solo cuentan las ventanas a partir de la cuarta seguida: 1,4 s pidiendo
 * el canal. Con datos aleatorios a 75 baudios, que la fila de la portadora
 * aguante 1,4 s son cien bits iguales de fila, y eso no pasa.
 */
#define IDENT_ALT_TOPE   480U   /* anotaciones antes de partir por la mitad */
#define IDENT_ALT_RACHA    4U   /* ventanas seguidas antes de empezar a contar */
#define IDENT_ALT_ENTRA    6U   /* marcas para empezar a salir en pantalla */
#define IDENT_ALT_SIGUE    3U   /* y para seguir saliendo, una vez dentro */
#define IDENT_ALT_NADIE  0xFFU

static uint16_t s_alt[N_FIRMAS];
static uint16_t s_alt_tot;
static uint8_t  s_alt_ult = IDENT_ALT_NADIE;
static uint8_t  s_alt_quien = IDENT_ALT_NADIE;   /* quien gana la racha de ahora */
static uint8_t  s_alt_racha;                     /* y cuantas ventanas lleva */


static void alt_apunta(uint8_t fila)
{
    uint16_t i;

    if ((uint16_t)fila >= N_FIRMAS) { return; }
    if (s_alt[fila] < 0xFFFFU) { s_alt[fila]++; }
    s_alt_tot++;
    if (s_alt_tot >= IDENT_ALT_TOPE) {
        for (i = 0U; i < N_FIRMAS; i++) { s_alt[i] = (uint16_t)(s_alt[i] >> 1); }
        s_alt_tot = (uint16_t)(s_alt_tot >> 1);
    }
}

/* Una ventana mas para `fila`. Solo cuenta si lleva IDENT_ALT_RACHA seguidas. */
static void alt_racha(uint8_t fila)
{
    if (fila == s_alt_quien) {
        if (s_alt_racha < 0xFFU) { s_alt_racha++; }
    } else {
        s_alt_quien = fila;
        s_alt_racha = 1U;
    }
    if (s_alt_racha >= IDENT_ALT_RACHA) { alt_apunta(fila); }
}

/*
 * La mas vista de las que NO se estan enseñando ya. Devuelve
 * IDENT_ALT_NADIE cuando no hay ninguna que merezca el renglon.
 *
 * La puerta es mas floja para la que ya estaba (IDENT_ALT_SIGUE) que para
 * una nueva (IDENT_ALT_ENTRA): sin esa holgura, una cuenta rondando el
 * umbral enciende y apaga el renglon cada pocos segundos, que es el mismo
 * parpadeo que acabamos de quitarle al nombre.
 */
static uint8_t alt_otra(const uint8_t *cand, uint16_t n)
{
    uint16_t i, j, cuenta = 0U, puerta;
    uint8_t  mejor = IDENT_ALT_NADIE;

    for (i = 0U; i < N_FIRMAS; i++) {
        uint8_t fuera = 0U;

        if (es_pista(&k_firmas[i])) { continue; }
        for (j = 0U; j < n; j++) {
            if ((uint16_t)cand[j] == i) { fuera = 1U; }
        }
        if (fuera) { continue; }
        if (s_alt[i] > cuenta) { cuenta = s_alt[i]; mejor = (uint8_t)i; }
    }

    if (mejor == IDENT_ALT_NADIE) { s_alt_ult = IDENT_ALT_NADIE; return mejor; }
    puerta = (mejor == s_alt_ult) ? IDENT_ALT_SIGUE : IDENT_ALT_ENTRA;
    if (cuenta < puerta) { s_alt_ult = IDENT_ALT_NADIE; return IDENT_ALT_NADIE; }

    s_alt_ult = mejor;
    return mejor;
}

uint8_t ident_modo_start(void)
{
    s_sep_pon = 0U;
    s_sep_hay = 0U;
    float *t;

    if (s_activo) { return 1U; }
    ident_modo_borra();
    t = ram_coge(STANAG_DET_FLOATS + CAD_N);
    if (t == 0) { return 0U; }
    if (!stanag_det_init(t, STANAG_DET_FLOATS)) {
        hfdl_ram_suelta();
        return 0U;
    }
    /* El cuaderno de la cadencia va DETRAS del motor, en el mismo
     * prestamo: ver su comentario. */
    s_cad = &t[STANAG_DET_FLOATS];
    s_cad_n = 0U; s_cad_w = 0U; s_cad_acu = 0.0f; s_cad_acu_n = 0U;
    s_cad_desde = 0U;
    memset(&s_cad_r, 0, sizeof s_cad_r);
    s_medidas = 0UL;
    memset(&s_m, 0, sizeof s_m);
    s_activo = 1U;
    return 1U;
}

void ident_modo_stop(void)
{
    if (!s_activo) { return; }
    s_activo = 0U;          /* PRIMERO: calla a la interrupcion */
    s_cad = 0;              /* y el cuaderno, que vive en ese prestamo */
    hfdl_ram_suelta();
}

uint8_t ident_modo_activo(void) { return s_activo; }

void ident_modo_mete(const float *audio, uint16_t n)
{
    if (!s_activo) { return; }
    stanag_det_mete(audio, (uint32_t)n);
    cad_mete(audio, n);
}

void ident_medidas(stanag_det_t *out) { if (out) { *out = s_m; } }
void ident_cadencia(ident_cad_t *out) { if (out) { *out = s_cad_r; } }

/*
 * EL RENGLON DE ARRIBA, QUE ES EL QUE SE LEE.
 *
 * Si cuadra una firma, su nombre. Si cuadran varias, todas separadas por
 * barras mientras quepan. Si no cuadra ninguna, se dice -y debajo quedan
 * los numeros, que es con lo que se puede buscar en el catalogo-.
 */
static void escribe(void)
{
    uint8_t cand[8];
    uint16_t n, i;
    char fila[IDENT_LARGO];

    s_nlin = 0U;

    if (s_medidas == 0UL && !s_cad_r.vale) {
        pon(tr("escuchando...", "listening..."));
        if (s_cad_r.segundos > 0U) {
            ponf(tr("llevo %d s (el WSPR pide 360)", "%d s so far (WSPR needs 360)"),
                 (int)s_cad_r.segundos, 0, 0);
        }
        return;
    }

    n = ident_casa(&s_m, &s_cad_r, cand, 8U);

    if (n == 0U) {
        pon(tr("No se parece a nada que conozca", "It matches nothing I know"));
    } else if (solo_pistas(cand, n)) {
        /*
         * CUANDO LO UNICO QUE CUADRA ES EL ANCHO, DECIRLO ANTES QUE EL
         * NOMBRE - 05/10/2026.
         *
         * Esto salia como cualquier otra respuesta, con su "(?)" chiquito
         * al lado, y el "(?)" no pesa nada al lado de un nombre de
         * protocolo. Dos ejemplos del mismo dia, los dos falsos:
         *
         *   FUG en 13.418, FSK de 850 Hz y 50 baudios, dos tonos limpios
         *   en pantalla          ->  "SSTV o fax meteorologico (?)"
         *
         *   una joroba de ruido sin tonos, sin ritmo y sin ciclo
         *                        ->  "Podria ser ALE 2G (?)"
         *
         * El primero era una fila que faltaba y ya esta puesta. El segundo
         * no tiene arreglo por tabla: no habia NADA que medir, y de un
         * ancho a secas no sale un protocolo. Lo honrado es decir primero
         * que no se ha podido medir nada, y despues en que cajon de
         * anchura ha caido - en ese orden, porque el orden es el que
         * decide que se queda en la cabeza de quien lee.
         */
        pon(tr("Sin estructura medible", "No measurable structure"));
        pon(tr("ni tonos, ni ritmo, ni ciclo", "no tones, no rhythm, no cycle"));
        (void)snprintf(fila, IDENT_LARGO, tr("por el ancho: %s", "by bandwidth: %s"),
                       ident_nombre(&k_firmas[cand[0]]));
        pon(fila);
        pon(ident_nota(&k_firmas[cand[0]]));
    } else {
        for (i = 0U; i < n && s_nlin < 3U; i++) {
            const ident_firma_t *f = &k_firmas[cand[i]];
            (void)snprintf(fila, IDENT_LARGO, "%s%s",
                           ident_nombre(f), f->probada ? "" : " (?)");
            pon(fila);
        }
        /* La nota de la PRIMERA, que es la que mas probabilidades tiene de
         * ser la buena: las demas son el aviso de que no esta cerrado. */
        pon(ident_nota(&k_firmas[cand[0]]));
        if (n > 1U) {
            pon(tr("(varias cuadran: la señal no las separa)",
                "(several match: the signal does not tell them apart)"));
        }
        /*
         * Y si por esta frecuencia pasa algo MAS de vez en cuando, decirlo.
         * Va aqui, pegado al nombre, y no abajo con los numeros: es parte
         * de la respuesta, no de la medida.
         */
        {
            uint8_t otra = alt_otra(cand, n);
            if (otra != IDENT_ALT_NADIE) {
                (void)snprintf(fila, IDENT_LARGO, tr("y a ratos: %s", "and now and then: %s"),
                               ident_nombre(&k_firmas[otra]));
                pon(fila);
            }
        }
    }

    /* Y siempre los numeros, cuadre o no: son lo unico que no opina. */
    ponf(tr("ancho %d  centro %d  pico %d%%", "width %d  centre %d  peak %d%%"),
         (int)s_m.ancho10_hz, (int)s_m.centro_hz, (int)s_m.pico_pc);
    if (s_m.hay_tonos) {
        ponf(tr("tonos %d y %d  salto %d Hz", "tones %d and %d  shift %d Hz"),
             (int)s_m.tono_bajo, (int)s_m.tono_alto, (int)s_m.sep_hz);
    } else {
        pon(tr("un solo tono o fase: no es FSK de dos",
            "one tone only, or phase: not two-tone FSK"));
    }
    /*
     * "activo" y no "ciclo": en la pantalla de la radio salia "ciclo 5%" en
     * este renglon y "ciclo 15,5 s" dos mas abajo, y son dos cosas que no
     * tienen nada que ver. Esta mide, dentro de una ventana de 341 ms, que
     * parte del tiempo hay señal por encima del ruido; la otra, cada cuanto
     * se enciende la banda entera. Dos medidas distintas con el mismo
     * nombre son peor que una sin nombre.
     */
    if (s_m.simb_x10 && !s_m.hay_tonos) {
        ponf(tr("simbolo %d,%d Bd  ~%d tonos", "symbol %d.%d Bd  ~%d tones"),
             (int)(s_m.simb_x10 / 10U), (int)(s_m.simb_x10 % 10U),
             (int)s_m.tonos);
    }
    ponf(tr("%d,%d Bd   activo %d%%", "%d.%d Bd   active %d%%"),
         (int)(s_m.baudios_x10 / 10U), (int)(s_m.baudios_x10 % 10U),
         (int)s_m.ciclo_pc);
    if (s_m.vale) {
        ponf(tr("trama %d,%02d ms  calidad %d%%", "frame %d.%02d ms  quality %d%%"),
             (int)(s_m.periodo_us / 100U), (int)(s_m.periodo_us % 100U),
             (int)s_m.calidad);
    } else {
        pon(tr("sin trama que se repita", "no repeating frame"));
    }
    /*
     * Y la cadencia, que es la medida LENTA: cada cuanto se enciende y se
     * apaga toda la banda. Siempre sale el tiempo que lleva escuchando,
     * tambien cuando aun no hay ciclo, porque cuatro minutos mirando una
     * pantalla que no dice nada se parecen mucho a una radio rota.
     */
    if (s_cad_r.vale) {
        ponf(tr("ciclo %d,%d s  ocupa %d%%", "cycle %d.%d s  takes %d%%"),
             (int)(s_cad_r.periodo_ds / 10U), (int)(s_cad_r.periodo_ds % 10U),
             (int)s_cad_r.ocupa_pc);
    } else {
        ponf(tr("sin ciclo (llevo %d s; WSPR pide 360)",
                "no cycle yet (%d s so far; WSPR needs 360)"),
             (int)s_cad_r.segundos, 0, 0);
    }
}

void ident_modo_poll(void)
{
    stanag_det_t r;
    uint8_t hay_nuevo = 0U;

    if (!s_activo) { return; }

    /*
     * La cadencia, cada ocho anotaciones -cuatro segundos-. No en cada
     * vuelta: son 512 logaritmos y una autocorrelacion de 256 retardos, y
     * eso cuatro veces por segundo seria tirar el bucle principal por una
     * medida que se mueve cada minuto.
     */
    if (s_cad_desde >= 8U) {
        s_cad_desde = 0U;
        cad_calcula();
        hay_nuevo = 1U;
    }

    if (stanag_det_paso(&r)) {
        s_m = r;
        s_medidas++;
        /*
         * LA SEPARACION QUE SE USA ES LA MEDIANA, NO LA ULTIMA - 06/10/2026.
         *
         * *** El dueño, con la radio en IDENT sobre 5339 kHz: "el modo
         * ident va variando entre lo que es". ***
         *
         * ident_casa() busca la fila por la separacion entre tonos, asi que
         * una separacion que baila es un NOMBRE que baila. Y baila por algo
         * real: con desvanecimiento selectivo, en algunas ventanas el tono
         * flojo se va del todo y el detector se queda con otra raya.
         *
         * Medido sobre la grabacion de 5339 (342 ventanas con dos tonos):
         *
         *     800-899 Hz ... 236     600-699 ... 26
         *     500-599 ......  61     700-799 ...  8
         *     400-499 ......  10     100-199 ...  1
         *
         * La buena es la mayoritaria -el 71 %- pero una de cada tres
         * ventanas dice otra cosa, y la pantalla ensenaba la ULTIMA. De ahi
         * el baile.
         *
         * La mediana de las ultimas dieciseis se come las ventanas malas
         * mientras sean minoria, que es justo lo que son. La MEDIANA y no la
         * media: con la media, una ventana que dice 124 Hz arrastra el
         * numero hacia abajo; con la mediana, no cuenta mas que las demas.
         */
        if (r.hay_tonos && r.sep_hz > 0U) {
            s_sep_anillo[s_sep_pon] = r.sep_hz;
            s_sep_pon = (uint8_t)((s_sep_pon + 1U) % IDENT_SEP_N);
            if (s_sep_hay < IDENT_SEP_N) { s_sep_hay++; }
            s_m.sep_hz = sep_mediana();
        }
        /*
         * Y lo que dice ESTA ventana, sin suavizar, para la cuenta de las
         * señales que se turnan. Tiene que ser la medida CRUDA: con la
         * mediana, una rafaga de tres segundos no mueve nada y no se
         * enteraria nadie. Ver alt_apunta().
         */
        {
            uint8_t  c[8];
            uint16_t nc = ident_casa(&r, &s_cad_r, c, 8U);
            if (nc > 0U && !solo_pistas(c, nc)) { alt_racha(c[0]); }
            else { s_alt_quien = IDENT_ALT_NADIE; s_alt_racha = 0U; }
        }
        hay_nuevo = 1U;
    }

    if (hay_nuevo) { escribe(); }
}

uint8_t     ident_modo_lineas(void) { return s_nlin; }

const char *ident_modo_linea(uint8_t i)
{
    if (i >= IDENT_LINEAS) { return ""; }
    return s_lin[i];
}

void ident_modo_borra(void)
{
    uint8_t i;
    for (i = 0U; i < IDENT_LINEAS; i++) { s_lin[i][0] = '\0'; }
    s_nlin = 0U;
    s_medidas = 0UL;
    memset(s_alt, 0, sizeof s_alt);
    s_alt_tot = 0U;
    s_alt_ult = IDENT_ALT_NADIE;
    s_alt_quien = IDENT_ALT_NADIE;
    s_alt_racha = 0U;
    memset(&s_m, 0, sizeof s_m);
    memset(&s_cad_r, 0, sizeof s_cad_r);
    s_cad_n = 0U; s_cad_w = 0U; s_cad_acu = 0.0f; s_cad_acu_n = 0U;
    s_cad_desde = 0U;
}
