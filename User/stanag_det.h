#ifndef STANAG_DET_H
#define STANAG_DET_H

#include <stdint.h>

/*
 * QUE HAY AHI: IDENTIFICAR LA SEÑAL SIN DEMODULARLA - 02/10/2026.
 *
 * *** Por el dueño del proyecto: "implementa stanag" ... "los cuatro, un
 * boton y que vaya cambiando, a no ser que hay alguna forma de saber cual
 * es automaticamente". ***
 *
 * La hay, y no es la que yo propuse primero.
 *
 * MI PRIMERA IDEA ERA MEDIR LA TASA DE SIMBOLO, y es peor. Hay otra mejor
 * y la trajo el dueño: *** "este repo nos vale para identificar señales
 * no?" https://github.com/sdrpower2/LimeAuto ***. Lo que hace ese
 * programa es la FUNCION DE AUTOCORRELACION de la señal, y buscar en ella
 * el periodo de la TRAMA. Una señal con trama repite su estructura cada
 * tantos milisegundos, y eso deja un pico limpio en la autocorrelacion sin
 * necesidad de enganchar, demodular ni saber nada de la norma.
 *
 * Es mejor por tres razones:
 *   - no hay que acertar con la constelacion ni con el filtro;
 *   - funciona igual con señales que no conocemos -si algun dia aparece
 *     algo con trama en la cascada, el numero sale igual-;
 *   - y el numero que da se puede contrastar con la norma.
 *
 * EL NUMERO DEL 4285, CON TRES TESTIGOS QUE NO SE CONOCEN ENTRE SI
 * ---------------------------------------------------------------
 * Yo dije de memoria que la trama del 4285 eran 1.616 simbolos. ERA FALSO,
 * y si llego a escribir esto con ese numero habria buscado el pico seis
 * veces mas lejos de donde esta y no habria encontrado nada nunca.
 *
 *   1. gr-digitalhf (ohleck), python/physical_layer/STANAG_4285.py:
 *      la trama son 80 simbolos de preambulo + 176 de datos = 256.
 *      A 2400 baudios, 256/2400 = 106,67 ms.
 *   2. El README de LimeAuto, textualmente: "the recurrence of the
 *      autocorrelation peak is 106.66 ms (STANAG 4285)".
 *   3. Y su codigo, limeauto/src/LimeAuto.cpp: busca los picos en las
 *      muestras 5333 y 10667 a 50 kHz de muestreo, que son 106,66 ms y
 *      213,33 ms. Los dos primeros multiplos.
 *
 * Tres caminos distintos al mismo numero. Eso ya no es una suposicion.
 *
 * LO QUE SE LE COPIA A LIMEAUTO Y LO QUE NO
 * -----------------------------------------
 * Su codigo no sirve aqui: esta atado a un LimeSDR, trabaja a 50 kHz y usa
 * una transformada de 32.768 puntos -256 kB solo de buffer, cuando a esta
 * radio le quedan 1.960 bytes libres-. Lo que se le coge es el METODO y
 * tres numeros de su heuristica: promediar varias pasadas, llamar pico a
 * lo que pase de la linea base mas 1,2 desviaciones tipicas, y no dar un
 * modo por bueno sin la mitad larga de los aciertos. Su licencia es
 * Apache 2.0.
 *
 * Y aqui sale mas barato, porque no analizamos 50 kHz de banda: la señal
 * ya viene filtrada y centrada por la radio. Se baja a banda base y se
 * diezma a 3 kHz complejos, y con eso 1024 muestras son 341 ms de
 * profundidad -tres tramas del 4285- en 32 kB en vez de 256.
 *
 * SOBRE QUE SE HACE LA AUTOCORRELACION, QUE NO ES UN DETALLE
 * ----------------------------------------------------------
 * La primera version de este fichero la hacia sobre la ENVOLVENTE del
 * audio, y ESTABA MAL. Lo vi al ir a escribir el banco, que es justo para
 * lo que sirve escribirlo.
 *
 * La envolvente solo ve la AMPLITUD. Y una PSK tiene la amplitud
 * practicamente constante: la informacion va en la fase. Una señal 4285
 * perfecta tiene una envolvente plana, asi que su autocorrelacion no
 * tendria ningun pico y el detector no habria encontrado nada nunca,
 * contra señal real ni contra la de mi propio generador.
 *
 * Lo que de verdad se repite cada trama es el PREAMBULO: los mismos 80
 * simbolos, con las mismas fases. Eso solo se ve correlacionando la señal
 * COMPLEJA consigo misma: al desplazarla 106,67 ms los dos preambulos se
 * alinean, sus fases coinciden y sale el pico. Por eso aqui hay un
 * oscilador y un diezmado complejo en vez de un valor absoluto.
 *
 * El oscilador va a 1500 Hz por el mismo motivo que el de JTTY: 12000/1500
 * son 8 EXACTO, o sea que la tabla tiene ocho valores y la fase no acumula
 * error nunca.
 */

/*
 * CUANTA MEMORIA PIDE, Y POR QUE NO SE LA QUEDA
 * ---------------------------------------------
 * Quedan 1.960 bytes de RAM libres en la radio, asi que un buffer estatico
 * de treinta kilobytes aqui dentro no entra, y mentiria el que dijera que
 * si. Se hace como HFDL, WSPR, AIS, ALE y JTTY: la memoria se la presta la
 * cascada mientras el modo esta activo y se devuelve al salir (ver
 * hfdl_ram.h). Por eso el trabajo entra por el init en vez de vivir aqui.
 *
 *   anillo complejo        1024 complejos = 2048 floats
 *   la transformada        2048 complejos = 4096 floats
 *   la autocorrelacion      1024 floats
 *   la tabla de giros       1024 floats
 *                          ------------------------------
 *                          8192 floats = 32 kB
 */
/* 8192 hasta el 05/10/2026; los 1024 de mas son el espectro promediado
 * que hace falta para ver la rejilla de un MFSK. Ver mide_rejilla(). */
#define STANAG_DET_FLOATS   9520U

/*
 * 1024 muestras complejas a 3 kHz: 341 ms de memoria. El pico del 4285 cae
 * en la 320 EXACTA -106,67 ms por 3 kHz- y le caben tres repeticiones.
 * Que caiga en una muestra justa no es casualidad: 2400 baudios y 3 kHz
 * tienen la misma raiz, y por eso se eligio diezmar por 4 y no por 3 o 5.
 */
#define STANAG_DET_N        1024U
#define STANAG_DET_HZ       3000U

/*
 * LO QUE ESTE MODULO MIDE, DESDE EL 03/10/2026, ES MAS QUE LA TRAMA
 * ------------------------------------------------------------------
 * *** El dueño: "te dejo trabajo para esta noche, haz el modo Ident con
 * todo lo que ya sabes". ***
 *
 * Nacio para contestar una sola pregunta -cada cuanto se repite la trama-
 * y eso identifica los STANAG, que es para lo que se hizo. Pero la noche
 * del 2 al 3 de octubre, buscando NAVTEX, hubo que contestar a mano otras
 * cuatro preguntas delante de cada grabacion: cuanto ocupa, si es una
 * portadora sola, si son dos tonos y a cuantos baudios. Todas salen de lo
 * que este modulo YA calcula y tiraba a la basura.
 *
 * La transformada directa se hace de todas formas para la autocorrelacion,
 * y entre ella y la inversa el espectro de potencia esta ahi entero. De
 * ahi salen el ancho de banda, el pico y el par de tonos sin gastar ni una
 * operacion de mas ni un byte de memoria: no se GUARDA el espectro, se
 * MIDE al vuelo y se queda media docena de numeros.
 *
 * Los baudios y el ciclo de trabajo salen de una pasada por el anillo que
 * ya esta en memoria.
 *
 * Quien convierte estos numeros en un nombre es ident.c, que es otra cosa
 * y vive aparte: aqui solo se mide.
 */

/* Lo que se ha medido. Todo a cero mientras no haya medida. */
typedef struct {
    uint8_t  vale;        /* 1 = hay una medida que mirar */
    uint16_t periodo_us;  /* periodo de trama en DECENAS de microsegundos;
                           * 10667 son los 106,67 ms del 4285. En decenas y
                           * no en milisegundos porque en milisegundos
                           * enteros el 4285 y el 4529 se pisan. */
    uint16_t retardo;     /* el mismo pico, en muestras de 3 kHz: para el
                           * banco, que compara contra lo que el mismo
                           * genero sin tener que creerse la conversion. */
    uint8_t  picos;       /* cuantos multiplos del periodo se ven */
    uint8_t  calidad;     /* 0..100, cuanto destaca el pico */

    /* --- lo añadido el 03/10/2026, todo en hercios de AUDIO --- */
    uint8_t  hay_esp;     /* 1 = las medidas de abajo valen */
    uint16_t centro_hz;   /* donde esta el pico del espectro */
    uint16_t ancho3_hz;   /* ancho de banda ocupado a -3 dB del pico */
    uint16_t ancho10_hz;  /* ... a -10 dB. Es el que mejor separa: 18 Hz en
                           * una portadora, 205 en un SITOR, 2531 en un
                           * STANAG 4285. Medido contra las tres. */
    uint16_t ancho20_hz;  /* ... a -20 dB */
    uint8_t  pico_pc;     /* % de la potencia que cae en +-8 Hz del pico.
                           * 57 en la portadora que grabo el dueño, 2 en su
                           * grabacion de un modem, 0 en el 4481 de prueba. */
    uint8_t  hay_tonos;   /* 1 = hay DOS rayas que parecen una FSK */
    uint16_t tono_bajo;   /* la de abajo */
    uint16_t tono_alto;   /* la de arriba */
    uint16_t sep_hz;      /* lo que se separan: 170 en SITOR, 850 en 4481 FSK */
    uint16_t baudios_x10; /* velocidad en DECIMAS de baudio: 1000 son 100,0.
                           * En decimas porque el RTTY de aficionado va a
                           * 45,45 y en enteros se confunde con el de 45. */
    uint8_t  ciclo_pc;    /* % del tiempo con señal. Un SITOR-B va al 100;
                           * un DSC son rafagas y baja mucho. Es lo que
                           * separa dos cosas con la MISMA modulacion. */

    /* --- la velocidad de simbolo de lo que no son dos tonos, 05/10/2026 --- */
    /*
     * *** El dueño mando una grabacion: "esto que podria ser?" Era un MFSK
     * de VEINTIUN tonos, y la radio contesto "podria ser ALE 2G" mirando
     * solo el ancho. ***
     *
     * baudios_x10, el de arriba, solo arranca cuando se han visto DOS
     * tonos: cuenta rachas del discriminador y con veintiun tonos no hay
     * rachas que contar. Asi que no habia velocidad, y sin velocidad lo
     * unico que quedaba era el ancho, que en un MFSK no dice casi nada.
     *
     * simb_x10 la mide de otra manera -ver SIMB_W en el .c: se busca
     * CUANDO CAMBIA DE TONO, no cual es- y vale para cualquier cosa que
     * tenga simbolos, con tonos o con fase. Va en DECIMAS de baudio, igual
     * que baudios_x10.
     *
     * ALCANCE: de 10 a 150 baudios. Ahi dentro caben el RTTY, el SITOR, el
     * NAVTEX, el ALE 2G, el PSK31 y toda la familia Olivia. Los STANAG y el
     * HFDL van a 1800 y 2400 y se salen por arriba; a esos los identifica
     * el periodo de la trama.
     *
     * tonos es una CUENTA, no una medida: el ancho dividido entre la
     * velocidad, que es el numero de tonos SI el modo es MFSK ortogonal
     * -separacion igual a la velocidad, como Olivia, Contestia y MFSK-. En
     * un modo donde no lo sea -el ALE 2G va a 125 baudios con los tonos a
     * 250 Hz- sale el doble. Quien lo use tiene que saberlo.
     */
    uint16_t simb_x10;    /* velocidad de simbolo en decimas de baudio. 0 = no */
    uint8_t  tonos;       /* ancho / velocidad, si fuera ortogonal (tope 60) */
    uint8_t  simb_q;      /* 0..100, cuanto destaca el simbolo */

    /*
     * --- LA RELACION CONSTANTE DE 7 BITS - 08/10/2026 ---
     *
     * *** El dueño, con una grabacion de 16.911 kHz: "que es y porque rtty
     * en auto no lo pilla, ni el ident". ***
     *
     * Era una FSK de 50,00 baudios y 201 Hz de salto, y la fila que le
     * corresponde -"FSK 50 Bd / 200 Hz (cifrada)", ver ident.c- YA ESTABA
     * puesta y comprobada en antena. No cuadro por la segunda condicion:
     * pedia velocidad de simbolo de 46,0 a 55,0 Bd y simb_x10 midio 25,0.
     * La mitad exacta, y no por casualidad: en una FSK de DOS tonos con
     * bits al azar el tono cambia cada dos bits de media, asi que ese
     * medidor cuenta CAMBIOS DE TONO, no simbolos. Es el mismo error que
     * ya estaba escrito en ident.c sobre baudios_x10, repetido con el otro
     * medidor. Y al caerse la fila ganaba "MFSK multitono (Olivia)", que
     * es peor que no decir nada.
     *
     * Asi que la segunda condicion pasa a ser una que SI esta en la señal:
     * el alfabeto. El NAVTEX, el SITOR y esta cifrada usan el CCIR 476,
     * que es un codigo de SIETE bits con EXACTAMENTE CUATRO UNOS -de ahi
     * "relacion constante"-: de los 128 patrones solo 35 son validos. Si
     * se trocea el flujo en grupos de siete por el sitio bueno, casi todos
     * los grupos tienen cuatro unos; por cualquier otro sitio, o con otro
     * alfabeto, no.
     *
     * cr7_pc es ese porcentaje, con el mejor de los siete desplazamientos.
     * MEDIDO CON ESTE MOTOR, que es el numero que vale. Fuera de la radio,
     * con recuperacion de reloj de verdad, los mismos casos dan veinte
     * puntos mas (99, 100, 93); esos numeros no sirven para poner un
     * umbral aqui, por la misma razon por la que un banco que mide una
     * copia no vale: lo que decide es lo que mide quien va a decidir.
     *
     *   grabaciones de verdad          sinteticas (ver sim/cr7_test.c)
     *   SITOR-B            86 %        CCIR 100 Bd / 170 Hz     87 %
     *   16911 del dueño    79 %        CCIR  75 Bd / 850 Hz     84 %
     *   NAVTEX 8424        78 %        CCIR  50 Bd / 200 Hz     78 %
     *   RTTY 3712 Baudot   36 %        CCIR  50 Bd / 200 a 6 dB 75 %
     *   STANAG 4481 FSK    27 %        Baudot 50 Bd             40 %
     *                                  al azar 75 Bd / 850 Hz   37 %
     *                                  al azar 100 Bd / 170 Hz  36 %
     *                                  al azar 50 Bd / 200 a 6dB 33 %
     *                                  al azar 50 Bd / 200 Hz   31 %
     *
     * Con bits al azar el valor esperado es 35/128 = 27%, y ahi se queda
     * todo lo que no es CCIR 476. El umbral de la tabla es 60%: quince
     * puntos por debajo del peor que si y veinte por encima del mejor que
     * no. El par que de verdad manda son las dos lineas de 50 Bd / 200 Hz:
     * mismo salto, misma velocidad, mismos dos tonos, 78 contra 31. Eso no
     * lo separa el espectro, solo lo separa el alfabeto.
     *
     * cr7_bd_x10 solo significa algo cuando cr7_pc es alto: cuando no hay
     * alfabeto que encontrar, el candidato ganador entre tres casillas que
     * rondan el azar es el que toque.
     *
     * Y EL BAUDIO SALE DE LA PROPIA MEDIDA, que es la otra mitad del
     * arreglo. Trocear al baudio equivocado deshace la relacion constante
     * -mirar la columna de 100 del 16911, o la de 50 del SITOR-, asi que
     * cr7_bd_x10 es el candidato que la maximiza y es un numero mas fiable
     * que los dos medidores de velocidad: no estima, PRUEBA, igual que
     * rtty_auto.c hace con la velocidad por la misma clase de motivo.
     *
     * ALCANCE: 50, 75 y 100 baudios, que a 3 kHz son 60, 40 y 30 muestras
     * por bit EXACTAS. Por eso son esos tres y no una rejilla: con enteros
     * no hay deriva de fase que corregir.
     *
     * cr7_pc = 0 significa "todavia no hay bastantes grupos", no "no es".
     * Hacen falta unos 3 s de señal con dos tonos.
     */
    uint8_t  cr7_pc;      /* % de grupos de 7 bits con 4 unos. 0 = sin medir */
    uint16_t cr7_bd_x10;  /* el baudio que da ese %, en decimas. 500/750/1000 */
} stanag_det_t;

/*
 * Arranca de cero. `trabajo` tiene que apuntar a STANAG_DET_FLOATS floats
 * que sigan siendo nuestros hasta el stop. Devuelve 0 -y entonces no se
 * mide nada- si no los hay: quedarse sin memoria se dice, no se supone.
 */
uint8_t stanag_det_init(float *trabajo, uint32_t n_floats);

/*
 * Mete audio de banda lateral a 12 kHz, el mismo que comen FT8, ALE y
 * JTTY (ver demod_am.c). Se le puede dar en trozos de cualquier tamaño.
 * Esto es barato: una multiplicacion por una tabla de ocho y una suma.
 * Puede ir en la interrupcion.
 */
void stanag_det_mete(const float *audio, uint32_t n);

/*
 * Mira si hay medida nueva. VA EN EL BUCLE PRINCIPAL, no en la
 * interrupcion: aqui dentro hay dos transformadas de 1024 puntos.
 *
 * Devuelve 1 cuando ha habido una medida nueva desde la ultima vez.
 */
uint8_t stanag_det_paso(stanag_det_t *out);

/*
 * POR DONDE VA LA CADENA, PARA PODER MIRARLA DESDE LA RADIO - 02/10/2026.
 *
 * *** El dueño, con el modo puesto y una señal delante: "llenando la
 * memoria", y ahi se quedaba. ***
 *
 * Esa frase sale mientras no ha habido NINGUNA medida, y eso puede ser por
 * tres motivos muy distintos: que no entre audio, que entre y el anillo no
 * se llene, o que se llene y la media aun no este hecha. Desde fuera los
 * tres se ven exactamente igual, y adivinar cual es ya nos ha costado un
 * rato esta tarde.
 *
 * Asi que se sacan los tres contadores y la pantalla los enseña. No es
 * para el usuario: es para que la primera prueba contra el aire diga en
 * que etapa se para, en vez de decir que no funciona.
 */
void stanag_det_diag(uint32_t *muestras, uint32_t *llenas, uint8_t *pasadas);

#endif
