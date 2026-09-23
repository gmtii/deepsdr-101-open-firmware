#include "cw.h"
#include <math.h>

/*
 * Nucleo del decodificador de CW - ver cw.h para el contrato del
 * modulo y para por que esto no se parece al RTTY mas que en el
 * detector de tono.
 *
 * CADENA, POR CADA BLOQUE DE 32 MUESTRAS A 12 kHz (2,667 ms):
 *
 *   1. Goertzel al tono, sobre una ventana de 64 muestras solapada al
 *      50 % (el bloque de ahora mas el anterior). Ver mas abajo por
 *      que 64 y no 32.
 *   2. Envolvente: raiz de la potencia y un suavizado ligero de un
 *      salto, solo para quitarle temblor al detector.
 *   3. Suelo y pico por minimo y maximo deslizantes exactos sobre los
 *      ultimos 2 segundos. Ver mas abajo por que no son filtros IIR.
 *   4. Umbral con histeresis entre esos dos, mas un squelch que cierra
 *      cuando el pico no destaca del suelo: si no hay senal, este
 *      decodificador CALLA en vez de inventar letras, que es el fallo
 *      clasico del genero.
 *   5. Medida de duraciones en saltos, y clasificacion contra una
 *      estimacion de la duracion del punto que se persigue a si misma.
 *   6. Los elementos se acumulan en una clave numerica y la letra sale
 *      de una tabla de 128 entradas, sin arbol y sin busqueda.
 *
 * POR QUE LA VENTANA ES DE 64 Y NO DE 32
 * --------------------------------------
 * Con 32 muestras el bin del Goertzel mide 12000/32 = 375 Hz de semi-
 * anchura: un filtro de 750 Hz, demasiado abierto para CW, donde el
 * filtro tipico es de 250-500 Hz. Con 64 la semianchura baja a 187,5
 * Hz, o sea un filtro de unos 375 Hz, que es justo lo que se quiere.
 *
 * Lo que se paga es resolucion temporal: la ventana emborrona 5,33 ms
 * de los bordes. A 20 PPM el punto dura 60 ms, asi que es un 9 %; a 40
 * PPM dura 30 ms y es un 18 %. Aceptable. El SALTO sigue siendo de
 * 2,667 ms, que es lo que fija la precision con la que se mide una
 * duracion, y ese no cambia.
 *
 * La ventana se deja RECTANGULAR, y la decision esta medida, no
 * razonada. Una ventana de Hann baja el primer lobulo lateral de -13 dB
 * a -31 dB, que en una banda llena de estaciones deberia importar, pero
 * a cambio duplica la anchura del lobulo principal y cuesta 1,8 dB de
 * ruido. Se implementaron las dos y se paso el banco entero con cada
 * una.
 *
 * Al principio Hann ganaba: con una segunda estacion a 400 Hz y 10 dB
 * por encima, Hann decodificaba perfecto y la rectangular fallaba un
 * 30 %. Pero ese fallo no era del filtro. Era del umbral, que dejaba
 * que el batido de la interferencia moviera la tecla. Arreglado el
 * umbral -ver mas abajo-, la rectangular pasa ese mismo caso igual de
 * bien, y ademas gana en todo el barrido de ruido: a 3 dB, entre dos y
 * cuatro veces menos errores.
 *
 * Tambien se probo Hann sobre 96 muestras, que sobre el papel es la
 * jugada redonda: mismo ancho de ruido equivalente que la rectangular
 * de 64 -los dos dan 187,5 Hz- pero con los lobulos laterales mucho mas
 * bajos. En el banco no salio: arregla la interferencia de +20 dB pero
 * estropea la de +10, y empeora el barrido de ruido entero. Descartada.
 *
 * O sea que la ventana nunca fue el problema. Merece la pena anotarlo
 * porque la explicacion teorica de por que hacia falta Hann era
 * correcta en todo menos en la conclusion, y porque la version de 96
 * era la que mejor pinta tenia de las tres.
 *
 * POR QUE HAY UN BANCO DE SONDAS Y NO UN SOLO DETECTOR
 * ----------------------------------------------------
 * Al principio habia un solo Goertzel, fijo en el tono ajustado, y el
 * operador tenia que colocar la senal justo ahi. Sobre el papel se
 * sintoniza mirando la raya del osciloscopio; en la radio de verdad no
 * funciono. El detector mide 375 Hz de ancho, asi que con la senal a
 * 150 o 200 Hz del tono recoge una fraccion de lo que hay y lo que
 * decodifica es lo que se le cuela por los lobulos laterales: texto
 * plausible y sin sentido, que es el peor fallo posible porque no se
 * distingue de uno bueno.
 *
 * Y el fallo de fondo era de reparto de trabajo: estaba pidiendole a una
 * persona una precision de decenas de hercios mirando una pantalla de
 * 7,5 Hz por pixel, para ahorrarle a la maquina una busqueda que le
 * cuesta cuatro duros. Ahora hay NUEVE Goertzel repartidos cada 62,5 Hz
 * alrededor del tono ajustado, o sea mas o menos 250 Hz a cada lado, y
 * el decodificador se queda con el que mas energia sostenida ve. El tono
 * ajustado deja de ser "donde hay que poner la senal" y pasa a ser "por
 * donde buscarla".
 *
 * Nueve sondas de 64 muestras son 576 multiplicaciones por salto, 216
 * mil por segundo. Sobre un Cortex-M4F a 200 MHz eso no se nota, y a
 * cambio desaparece el unico ajuste de la cadena que exigia pulso firme.
 *
 * LIMITE CONOCIDO: una estacion a 400 Hz y 20 dB por encima de la
 * deseada la tapa del todo. Por el lobulo lateral de -24 dB llega 4 dB
 * POR ENCIMA de lo que se quiere leer, asi que el detector engancha a
 * la otra. No hay umbral que arregle eso: lo que hay que hacer es
 * estrechar el filtro de la radio o moverse de frecuencia, igual que
 * haria una persona.
 *
 * POR QUE EL UMBRAL ES UN AGRUPADOR DE DOS MEDIAS
 * ------------------------------------------------
 * Un par de filtros asimetricos (ataque rapido, caida lenta) es lo
 * habitual y tiene un problema que no se arregla eligiendo mejor las
 * constantes: durante una raya larga a poca velocidad -a 5 PPM una raya
 * dura 720 ms- el seguidor del SUELO esta viendo senal, no ruido, y
 * sube. Cuanto mas lento va el operador, mas se descoloca el umbral.
 *
 * Tambien se probo un minimo y un maximo deslizantes exactos sobre los
 * ultimos 2 segundos, y es PEOR, aunque parezca lo contrario. El
 * minimo de 750 muestras de una envolvente de ruido no es el nivel de
 * ruido: es el desvanecimiento mas profundo que ha tenido el ruido en
 * dos segundos, que se acerca a cero. Y el maximo es su pico mas alto.
 * O sea que con la antena en silencio el "recorrido" salia enorme y
 * todo lo de abajo se disparaba sobre ruido puro. Medido en
 * sim/cwtest.c: fue el primer fallo que canto el banco de pruebas.
 *
 * Lo que hay ahora son dos medias que se persiguen, que es un k-medias
 * de dos grupos hecho de forma incremental: cada muestra va a la media
 * alta o a la baja segun de cual este mas cerca, y solo mueve esa. Es
 * robusto al ruido porque PROMEDIA en vez de quedarse con un extremo,
 * y no tiene el problema del filtro IIR porque durante una raya larga
 * las muestras caen todas del lado alto: la media baja no se entera y
 * no sube.
 *
 * POR QUE NO BASTA CON UN SQUELCH DE NIVEL
 * ----------------------------------------
 * Con la antena en ruido, esas dos medias se separan igual -el grupo
 * alto coge la mitad de arriba de la distribucion de Rayleigh y el bajo
 * la de abajo- y su cociente sale alrededor de 2,4. Una senal de CW
 * utilizable a 5 dB en 500 Hz da un cociente de unos 2,3. O sea que el
 * NIVEL NO SEPARA: cualquier umbral que calle el ruido mata tambien las
 * senales debiles, que son justo las que uno quiere decodificar.
 *
 * Lo que si separa es la FORMA. El Morse tiene una firma que el ruido
 * no tiene: las duraciones se agrupan en dos montones con una relacion
 * de 3 a 1, y el hueco entre elementos mide lo mismo que el punto. El
 * ruido da duraciones exponenciales, de un solo monton. Asi que la
 * puerta de salida de este decodificador no mira cuanta senal hay, mira
 * si lo que llega TIENE FORMA DE MORSE: ver cw_gate_update().
 *
 * Es la diferencia entre un decodificador que escribe basura plausible
 * cuando no hay nada y uno que se calla, y es la unica razon por la que
 * el ultimo caso de sim/cwtest.c -ruido solo, la salida tiene que estar
 * vacia- se puede pasar sin sacrificar sensibilidad.
 */

#define CW_FS_HZ      12000.0f
#define CW_HOP        CW_BLOCK_SAMPLES            /* 32 muestras de avance */
#define CW_WIN        (CW_BLOCK_SAMPLES * 2U)     /* 64 muestras de ventana */
#define CW_HOP_MS     (1000.0f * (float)CW_HOP / CW_FS_HZ)  /* 2,6667 ms */

_Static_assert(CW_BLOCK_SAMPLES == 32U, "cw.c da por supuesto bloques de 32 muestras");

/*
 * Saltos que dura un punto, para una velocidad dada:
 *   punto_ms = 1200 / PPM      (definicion de la palabra PARIS)
 *   saltos   = punto_ms / 2,6667 = 450 / PPM
 * y al reves, PPM = 450 / saltos. Es la unica conversion entre
 * velocidad y tiempo que hay en todo el fichero, y esta aqui sola
 * para que no aparezca un 450 suelto en ningun otro sitio.
 */
#define CW_HOPS_PER_DOT(wpm)  (450.0f / (wpm))
#define CW_WPM_FROM_HOPS(h)   (450.0f / (h))

#define CW_DOT_HOPS_MIN  CW_HOPS_PER_DOT(CW_WPM_MAX)   /* 7,5 */
#define CW_DOT_HOPS_MAX  CW_HOPS_PER_DOT(CW_WPM_MIN)   /* 90 */

/* --- banco de sondas ------------------------------------------------ */
/*
 * Nueve sondas cada 62,5 Hz cubren 250 Hz a cada lado del tono
 * ajustado. El reparto sale de la anchura del propio detector: con 64
 * muestras el lobulo principal mide 187,5 Hz a cada lado, asi que
 * sondas cada 62,5 Hz se solapan de sobra y el tono cae siempre a menos
 * de 31 Hz del centro de alguna. A esa distancia la perdida por caer
 * entre dos bins es de decimas de dB, o sea nada.
 */
#define CW_PROBES        9U
#define CW_PROBE_STEP    62.5f

/*
 * QUE SE MIDE EN CADA SONDA PARA ELEGIR
 * -------------------------------------
 * No la energia. La MANIPULACION.
 *
 * La primera version se quedaba con la sonda mas fuerte, y eso esta mal
 * de una forma que el banco canto en cuanto se le puso una segunda
 * estacion: una portadora continua a 400 Hz y solo 6 dB por encima se
 * llevaba el enganche siempre. Y es logico, porque le gana por dos
 * lados a la vez: emite mas fuerte Y emite todo el rato, mientras que
 * una senal de CW esta apagada mas de la mitad del tiempo. Con el
 * enganche puesto en la portadora, el error paso del 6 % al 78 %.
 *
 * Asi que se mide la DESVIACION MEDIA de la magnitud respecto a su
 * propia media, que es lo que distingue una cosa de la otra:
 *
 *   senal de CW manipulada   desviacion grande (se apaga y se enciende)
 *   portadora continua       desviacion casi nula (nunca cambia)
 *   ruido                    desviacion pequena, proporcional al ruido
 *
 * O sea: la portadora puede ser todo lo fuerte que quiera y no gana,
 * porque lo que se puntua es justo lo unico que no tiene. Es la misma
 * idea que la puerta de calidad, un nivel mas abajo: el nivel no
 * distingue lo que se quiere de lo que no, la forma si.
 *
 * Las dos medias son lentas, un segundo, porque un punto suelto no debe
 * mover el enganche.
 */
#define CW_PROBE_ALPHA   0.00267f
/*
 * Y para cambiar de sonda hace falta ganarle a la actual por este
 * margen. Sin histeresis, dos sondas vecinas con energia parecida -que
 * es lo normal, porque se solapan- se irian pasando el enganche varias
 * veces por segundo, y cada cambio mueve la referencia de todo lo que
 * viene despues.
 */
#define CW_PROBE_MARGEN  1.25f
/*
 * La desviacion se mide sobre la magnitud SUAVIZADA, no sobre la cruda.
 * Importa por una razon concreta: dos tonos separados 400 Hz producen un
 * batido a 400 Hz, y una sonda que ve los dos a medias mide una
 * desviacion enorme que no es manipulacion, es el batido. Con la
 * interferencia y la senal al mismo nivel -donde el batido es mas
 * profundo- el enganche se iba a una sonda intermedia y el error subia
 * al 76 %, mientras que con la interferencia 6 dB mas fuerte no pasaba
 * nada. Un fallo que empeora cuando la interferencia BAJA es la pista de
 * que lo que molesta es el batido y no el nivel.
 *
 * Suavizar unos 9 ms deja pasar la manipulacion -un punto a 45 PPM dura
 * 27 ms- y se come el batido, que es cientos de veces mas rapido.
 */
#define CW_PROBE_SUAVE   0.25f
/*
 * Y ninguna sonda cambia el enganche si no DESTACA sobre las demas. Con
 * la antena en ruido las nueve miden practicamente lo mismo, y con nueve
 * candidatas alguna supera a la actual por un 25 % a cada rato: el
 * enganche se pasaba la vida saltando, y cada salto tira la confianza y
 * obliga a convencer a la puerta otra vez desde cero. El resultado era
 * que con senal limpia y sin ruido la puerta no llegaba a abrir a
 * tiempo, y lo que salia por la relectura era basura.
 *
 * Exigir que la ganadora saque un 60 % a la media de todas distingue
 * "hay algo aqui" de "todas miden ruido".
 */
#define CW_PROBE_DESTACA 1.60f
/*
 * Y hasta que el enganche lleve este rato quieto, la puerta no abre.
 *
 * Hace falta porque las medias lentas tardan su segundo en decidir, y
 * durante ese segundo el decodificador esta escuchando en el centro de
 * la ventana mientras la senal esta en otro sitio: mide elementos
 * atenuados y con los bordes desplazados. Sin esta espera, la puerta se
 * convencia con esos y soltaba media linea de basura por delante del
 * mensaje bueno. Medido en el banco con la estacion 190 Hz desviada:
 * salia "TTTT T T TT TT THE EIIIK BROWN..." donde ponia "THE QUICK
 * BROWN...".
 *
 * Un segundo de retraso al empezar a leer una estacion no se nota. Media
 * linea de basura delante del texto si, y ademas es indistinguible de un
 * decodificador que va mal.
 */
#define CW_LOCK_ESTABLE  375U   /* 1 s */

/* --- envolvente ----------------------------------------------------- */
/*
 * Suavizado de la envolvente. No es un numero fijo: se promedia sobre
 * aproximadamente un cuarto del punto, sea cual sea la velocidad. Es
 * un filtro adaptado a mano pobre, y es la mejora de sensibilidad mas
 * barata que hay aqui: a 10 PPM el punto dura 120 ms y se puede
 * promediar 30, a 60 PPM dura 20 y solo se pueden promediar 5. Fijar
 * la constante obligaria a elegir una velocidad y estropear el resto.
 */
#define CW_ENV_SPAN    4.0f    /* el punto se divide entre esto */
#define CW_ENV_A_MIN   0.12f
#define CW_ENV_A_MAX   0.60f

/* --- umbral: las dos medias que se persiguen ------------------------ */
/*
 * 0,006 son unos 450 ms de constante de tiempo. Tiene que ser lento
 * comparado con un elemento -si no, la media alta se hunde dentro de
 * una raya- y rapido comparado con un cambio de estacion.
 */
#define CW_KM_ALPHA        0.006f  /* ya enganchado: ~450 ms */
/*
 * Buscando enganche se va cinco veces mas rapido (~90 ms). Importa mas
 * de lo que parece: mientras las dos medias suben desde el nivel de
 * ruido hacia el de la senal, el umbral esta demasiado bajo y los
 * primeros elementos se miden MAS LARGOS de lo que son y los primeros
 * huecos mas cortos. O sea que las primeras letras salen mal, y la
 * primera letra suele ser la que mas falta hace porque es la del
 * indicativo. Con 450 ms eso duraba una palabra entera.
 */
#define CW_KM_ALPHA_FAST   0.030f
/*
 * Ataque. Una muestra que esta POR ENCIMA de la media alta solo puede
 * ser senal, y una que esta por DEBAJO de la media baja solo puede ser
 * ruido: en esos dos casos concretos no hay ninguna duda que resolver y
 * se puede saltar directamente. El resto del tiempo mandan las
 * constantes lentas de arriba.
 *
 * Ojo con el detalle que hace que esto sea correcto y no el filtro
 * asimetrico de siempre: la media BAJA solo salta HACIA ABAJO. Durante
 * una raya larga las muestras estan todas arriba, o sea que la media
 * baja no las ve y no sube, que era justo el fallo del filtro IIR
 * clasico.
 *
 * Sin esto, al empezar una transmision las dos medias venian del nivel
 * de ruido y el umbral tardaba en subir: los primeros elementos se
 * median largos y los primeros huecos cortos, y a veces dos elementos
 * de la misma letra se fundian en uno. En la practica: la primera letra
 * salia mal, y la primera letra es la del indicativo.
 */
#define CW_KM_ATTACK       0.60f
/*
 * Zona muerta. Una muestra que cae en el tercio central del recorrido
 * no pertenece a ningun grupo: es una TRANSICION, el flanco de subida o
 * de bajada de un elemento, y no dice nada ni del nivel de senal ni del
 * de ruido.
 *
 * Sin esto el agrupador estaba mal, y de una forma que no se veia
 * mirando el codigo sino mirando la traza: al terminar cada elemento,
 * la envolvente baja pasando por TODOS los valores intermedios, y en
 * cuanto cruzaba el punto medio, cada una de esas muestras se le daba
 * de comer a la media baja. O sea que la media baja no medía el ruido,
 * medía la bajada de los elementos, y salia unas quinientas veces mas
 * alta que el ruido de verdad. Con eso el umbral quedaba descolocado y
 * las letras se fundian unas con otras: la primera transmision
 * decodificaba "RQ" donde ponia "CQ".
 *
 * Es exactamente el fallo que se le achacaba al filtro IIR clasico, y
 * el agrupador lo tenia igual. Lo evita en la parte PLANA de un
 * elemento -ahi las muestras caen todas del lado alto- pero no en los
 * flancos, que con la manipulacion suavizada de una emisora de verdad
 * mas el propio filtro de envolvente ocupan casi medio punto.
 */
#define CW_KM_DEAD         0.33f
/*
 * El ataque solo se aplica a un SALTO de verdad, de 4 dB o mas. Sin
 * esta condicion, la media alta seguia al ruido muestra a muestra, con
 * lo que la envolvente quedaba siempre pegada a ella y por encima del
 * umbral de suelta: con la antena en ruido la tecla se quedaba BAJADA
 * indefinidamente en vez de parpadear.
 *
 * Y eso se comia la primera raya de la transmision, porque el "elemento"
 * que venia arrastrandose desde el ruido y la primera raya eran el mismo
 * elemento: salia mas largo que cualquier raya posible, se descartaba
 * por absurdo, y "CQ" se decodificaba "RQ". La primera letra otra vez,
 * que es la del indicativo.
 */
#define CW_KM_JUMP         1.60f
/*
 * Recorrido minimo para que el umbral signifique algo. Con la entrada
 * en ruido no hay dos grupos que separar, asi que las dos medias se
 * juntan -las muestras del tercio de arriba tiran de la alta hacia
 * abajo y las del de abajo tiran de la baja hacia arriba- y acaban
 * valiendo lo mismo. Ahi el umbral queda exactamente sobre el nivel
 * medio del ruido y la tecla hace lo que le da la gana.
 *
 * Cuando pasa eso, la respuesta correcta no es un umbral mejor: es que
 * no hay nada, y la tecla tiene que estar SUBIDA.
 *
 * Esto es un squelch de nivel, que mas arriba se descarta como forma de
 * juzgar si hay senal. No se contradice: 1,15 son 1,2 dB, o sea que
 * solo distingue "las dos medias se han juntado" de "no se han
 * juntado". Una senal de CW del peor caso que este decodificador
 * intenta sacar anda por 2,0. Esto no decide nada sobre senales
 * debiles; solo desactiva el umbral cuando ha dejado de existir.
 */
#define CW_SPAN_MIN        1.15f
/*
 * Calentamiento. Durante los primeros CW_WARM_HOPS saltos (170 ms) no
 * se decide nada: solo se acumula el nivel medio de lo que entra, y con
 * el se siembran las dos medias, una a cada lado.
 *
 * Sembrarlas a partir de la PRIMERA muestra, que es lo que se hacia
 * antes, no funciona, y el modo de fallo es feo: en la primera muestra
 * el filtro de envolvente todavia no ha arrancado y vale casi cero, o
 * sea que las dos medias nacen pegadas al cero. La alta sube en cuanto
 * llega algo, pero la baja se queda ahi: para subir necesita muestras
 * del tercio inferior del recorrido, y con la baja en cero ese tercio
 * esta por debajo del ruido, asi que no llega ninguna. Resultado: el
 * umbral de suelta se queda en el 40 % del pico y la tecla no sube
 * nunca. Se comia el primer elemento de la transmision.
 *
 * Con una media de 170 ms de lo que sea que haya -ruido, normalmente-
 * las dos medias nacen una a cada lado del nivel real y el problema
 * desaparece de raiz, sin red de seguridad que valga.
 */
#define CW_WARM_HOPS       64U
#define CW_THR_ON     0.60f /* fraccion del recorrido bajo->alto para bajar la tecla */
#define CW_THR_OFF    0.40f /* ...y para soltarla. La diferencia es la histeresis */

/* --- adaptacion de la velocidad ------------------------------------ */
#define CW_ADAPT_FAST     0.35f  /* primeros elementos tras un silencio */
#define CW_ADAPT_SLOW     0.15f  /* en regimen */
#define CW_ADAPT_N_FAST   6U
/*
 * Franja ambigua: un elemento de entre 1,5 y 2,5 puntos se CLASIFICA
 * igual (la frontera es 2,0) pero no se usa para corregir la
 * estimacion. Dejar que un elemento del que no se esta seguro mueva la
 * referencia es como se va la estimacion a un extremo y ya no vuelve.
 */
#define CW_SURE_DOT       1.5f
#define CW_SURE_DASH      2.5f
#define CW_DASH_BOUNDARY  2.0f

/* --- huecos, en puntos --------------------------------------------- */
/*
 * Los valores nominales son 1 entre elementos, 3 entre letras y 7
 * entre palabras. Las fronteras se ponen en 2 y en 5, o sea a mitad de
 * camino de cada par, porque quien manda a mano no cumple esas
 * proporciones: al aficionado que va despacio le salen los huecos de
 * letra mas largos de lo que le sale la letra (eso tiene nombre,
 * Farnsworth, y es incluso deliberado cuando se aprende).
 */
#define CW_GAP_LETTER    2.0f
#define CW_GAP_WORD      5.0f

/*
 * Una pulsacion mas larga que esto no es un elemento de codigo: es
 * alguien afinando con la tecla bajada, o un portador. No se clasifica,
 * no corrige la velocidad y ademas tira la letra a medias, porque lo
 * que venia antes ya no significa nada.
 *
 * Hay DOS limites y la diferencia entre ellos importa:
 *
 *   - El absoluto sale de la propia definicion del rango soportado: la
 *     raya mas larga posible son 3 puntos a la velocidad minima, y se
 *     le da un 17 % de margen. Nada legitimo puede pasar de ahi.
 *
 *   - El relativo, seis veces el punto estimado, es mas fino pero SOLO
 *     se aplica con la puerta abierta, o sea cuando ya se sabe que el
 *     punto estimado significa algo.
 *
 * Aplicar el relativo siempre fue un fallo real y costo entender por
 * que 10 PPM no decodificaba NADA: con la estimacion sembrada en 20 PPM
 * el limite caia en 135 saltos, que es exactamente lo que mide una raya
 * a 10 PPM. Media raya se descartaba por absurda, y descartarla borraba
 * ademas la letra a medias. Y como se descartaba, tampoco corregia la
 * estimacion: el estimador no podia aprender que iba lento porque lo
 * que se lo habria ensenado era justo lo que tiraba. Un limite que
 * depende de lo que se esta intentando medir es un lazo cerrado que se
 * puede trabar, y este se trababa.
 */
#define CW_MARK_ABSURD   6.0f
#define CW_MARK_MAX_HOPS ((uint16_t)(3.5f * CW_DOT_HOPS_MAX))
/*
 * ...y una mas corta que esto no es un punto, es un chasquido del
 * ruido. 7 saltos son 18,7 ms; el punto mas corto que este
 * decodificador admite, el de 45 PPM, dura 10 saltos. Ver el comentario
 * de CW_WPM_MAX en cw.h para por que el tope de velocidad se eligio
 * para poder poner este numero aqui y no al reves.
 */
#define CW_MARK_MIN_HOPS 7U
/*
 * Y una vez ENGANCHADO, el minimo sube a medio punto estimado.
 *
 * Los 7 saltos de arriba son el suelo absoluto: no se sabe a que
 * velocidad va la senal, asi que hay que dejar pasar hasta el punto mas
 * corto que el aparato admite. Pero cuando la puerta esta abierta SI se
 * sabe, y entonces un "elemento" que dura menos de la mitad de un punto
 * no es un punto: es un chasquido.
 *
 * Esto se vio en el aire y no en el banco. Con un QSO copiando bien
 * salian letras sueltas entre las palabras -E, T, TT, EEE- que no las
 * mandaba nadie: eran golpes de ruido de 7 a 11 saltos durante las
 * pausas, justo por encima del suelo absoluto. A 20 PPM el punto son
 * 22,5 saltos, asi que medio punto los corta todos sin acercarse
 * siquiera a un punto de verdad.
 *
 * Una E de mas no parece grave hasta que se cuenta que E y T son UN solo
 * elemento: son exactamente las letras que el ruido sabe falsificar, y
 * las que mas ensucian un texto por lo dificil que es distinguirlas de
 * las buenas al leer.
 */
#define CW_MARK_MIN_LOCK 0.50f

/* --- puerta de calidad: "esto tiene forma de Morse" ----------------- */
/*
 * Tres condiciones, todas sobre la FORMA de lo que llega y ninguna
 * sobre su nivel. Ver la cabecera del fichero para por que el nivel no
 * sirve.
 *
 *   1. Error de ajuste: cada elemento tiene que caer cerca de 1 o de 3
 *      puntos. Se promedia el error relativo; con codigo de maquina sale
 *      ~0,02, con codigo a mano ~0,15, con ruido no baja de 0,4.
 *   2. Bimodalidad: la media de las rayas partida por la media de los
 *      puntos tiene que dar aproximadamente 3. Esta es la firma que el
 *      ruido no sabe falsificar: sus duraciones son exponenciales, de un
 *      solo monton.
 *   3. Coherencia del hueco: el hueco entre elementos de una misma letra
 *      mide un punto, por definicion del codigo. Si el hueco corto y el
 *      punto no se parecen, lo que se esta midiendo no es Morse.
 *
 * Con histeresis, porque una puerta que parpadea en mitad de una
 * palabra es peor que una que tarda medio segundo de mas en abrir.
 */
#define CW_Q_OPEN        0.30f
#define CW_Q_CLOSE       0.48f
#define CW_Q_ALPHA       0.25f
#define CW_RATIO_MIN     2.15f
#define CW_RATIO_MAX     4.20f
#define CW_GAPFIT_MAX    0.60f
#define CW_LEN_ALPHA     0.25f
/* Fraccion de rayas admisible. Un mensaje entero de "EEEEE" o de
 * "TTTTT" no pasaria la puerta; es un precio pequeno y conocido a
 * cambio de no escribir nunca sobre ruido. */
#define CW_FRAC_MIN      0.12f
#define CW_FRAC_MAX      0.88f
#define CW_FRAC_ALPHA    0.15f
/*
 * Ademas de que el error MEDIO sea bajo, hacen falta CW_GOOD_N
 * elementos SEGUIDOS que encajen. Son dos condiciones distintas y la
 * segunda es la que de verdad aguanta contra el ruido.
 *
 * Esta medido, en el propio banco: con la entrada en ruido, el cociente
 * entre las dos medias vale de 1,9 a 3,6, y con una senal de CW de 3 a
 * 8 dB vale de 2,5 a 7,1. Se solapan. O sea que por NIVEL no hay forma
 * de distinguir el ruido de una senal debil, y cualquier umbral que
 * calle el ruido mata tambien lo que se quiere leer.
 *
 * Lo que el ruido no sabe hacer es acertar ocho veces seguidas. Un
 * elemento suelto puede caer por casualidad cerca de un punto o de una
 * raya; ocho consecutivos, con la referencia moviendose, no. Ocho
 * elementos son unas dos letras, que es tambien lo que cabe en la
 * cuarentena: o sea que la puerta tarda en abrir justo lo que se puede
 * recuperar despues, y no se pierde nada.
 */
#define CW_GOOD_N        8U
#define CW_ERR_GOOD      0.35f

/* --- ancla: el elemento mas corto ----------------------------------- */
/*
 * El punto ES el elemento mas corto del codigo. Eso da una medida de la
 * velocidad que NO depende de haber clasificado bien nada, y por eso es
 * la unica que puede sacar al estimador de un enganche equivocado.
 *
 * Hace falta porque el estimador normal solo tiene un margen de captura
 * de mas o menos un 50 %: la frontera punto/raya esta en 2 puntos
 * estimados, asi que si la senal va al doble de rapido de lo sembrado,
 * las rayas de verdad caen POR DEBAJO de la frontera y se cuentan como
 * puntos. Entonces los dos grupos se confunden en uno, la relacion 3 a 1
 * no aparece y ya no hay nada que empuje la estimacion al sitio. Medido:
 * a 40 PPM partiendo de 20 no decodificaba nada, a ninguna relacion
 * senal-ruido, y no era un problema de ruido sino de trabazon.
 *
 * Se calcula como el MINIMO EXACTO de los ultimos CW_ANCHOR_N
 * elementos, no como un seguidor con constantes de ataque y caida. Se
 * probo primero con un seguidor asimetrico y estaba mal pensado: una
 * raya mide el triple que un punto, asi que cada raya tiraba del
 * "minimo" hacia arriba y el ancla acababa oscilando en torno a metro y
 * medio punto en vez de quedarse en el punto. Con eso disparaba la
 * resiembra de mas abajo contra codigo perfectamente bueno, y la
 * estimacion se quedaba un 30 % alta: suficiente para que la puerta
 * tardase media transmision en abrir y se perdiera el indicativo.
 *
 * Un minimo de verdad sobre una ventana corta no tiene ese problema y
 * ademas no tiene nada que ajustar. Doce elementos son dos o tres
 * letras: en cualquier texto real hay un punto ahi dentro.
 */
#define CW_ANCHOR_N      12U
/* Cuanto tiene que discrepar el ancla de la estimacion para que se le
 * haga caso. Solo se comprueba con la puerta cerrada. */
#define CW_ANCHOR_LO     0.80f
#define CW_ANCHOR_HI     1.25f

/* --- historial de duraciones, para releer hacia atras -------------- */
/*
 * La puerta necesita ocho elementos para convencerse, o sea unas dos
 * letras, y mientras tanto la estimacion de velocidad todavia se esta
 * colocando. O sea que las primeras letras de una transmision se
 * decodifican mal Y ademas con la puerta cerrada. Y la primera letra es
 * la que mas falta hace, porque es la del indicativo.
 *
 * Se probo a guardar esas letras tal cual y soltarlas si la puerta
 * abria poco despues. No vale, y por una razon de fondo: esas letras se
 * decodificaron con la velocidad equivocada, asi que soltarlas es
 * soltar basura; y si se descartan en cuanto un elemento no encaja -que
 * es lo que hay que hacer para que no salga la chachara del ruido
 * anterior- entonces se descartan siempre, porque justo los primeros
 * elementos de verdad son los que peor encajan.
 *
 * Lo que se guarda aqui no son letras sino DURACIONES en bruto. Cuando
 * la puerta abre, se vuelven a leer hacia atras con la velocidad que YA
 * se conoce. No es recuperar una decision dudosa: es tomarla otra vez,
 * ahora bien, sobre los mismos datos.
 *
 * Positivo es elemento y negativo es hueco. 48 entradas son unas seis
 * letras, de sobra para lo que tarda la puerta.
 */
#define CW_HIST_N        48U
/* No se relee mas atras de esto (4 s) ni mas alla de un elemento que no
 * encaje: la relectura se extiende hacia atras solo mientras lo que hay
 * siga pareciendo Morse. */
#define CW_HIST_MAX_HOPS 1500U
#define CW_HIST_ERR_MAX  0.50f

/*
 * Tras este rato SIN MEDIR NI UN ELEMENTO se vuelve a sembrar la
 * velocidad desde el valor de arranque, para que una senal nueva no se
 * encuentre la estimacion donde la dejo la anterior. 3 s.
 *
 * La condicion es "sin elementos", no "con la puerta cerrada", y la
 * diferencia no es un matiz: con la segunda, decodificar a 10 PPM era
 * IMPOSIBLE. Partiendo de 20 PPM sembrados, aprender que la senal va a
 * la mitad lleva mas de tres segundos -a esa velocidad una sola letra
 * ya dura un segundo-, o sea que la resiembra llegaba siempre antes que
 * el enganche y borraba justo lo aprendido. La puerta cerrada no
 * significa que no se este aprendiendo; significa que todavia no se ha
 * terminado. Solo el silencio significa que no hay nada que aprender.
 */
#define CW_QUIET_RESEED_HOPS 1125U

/* Tras este silencio se considera acabada la transmision: la proxima
 * letra no llevara un espacio delante. 5 s. */
#define CW_IDLE_RESET_HOPS 1875U

/*
 * Tabla de Morse. La clave se construye elemento a elemento partiendo
 * de 1: clave = clave*2 + (raya ? 1 : 0). El 1 inicial es lo que hace
 * que "punto" y "punto punto" no colisionen, y con hasta 6 elementos
 * la clave cae siempre entre 2 y 127. O sea: la tabla ES el
 * decodificador, sin arbol, sin busqueda y sin comparar cadenas. 128
 * bytes.
 *
 * Estan las 26 letras, los 10 digitos y la puntuacion que se usa de
 * verdad en el aire, incluidas las senales de procedimiento que se
 * escriben como signos: '=' es BT (separador de parrafo), '+' es AR
 * (fin de mensaje) y '(' es KN (adelante solo tu). 52 simbolos, sin
 * colisiones, generado y comprobado por script. El unico ausente
 * conocido es '$' (...-..-), que tiene 7 elementos y se saldria de la
 * tabla; no se usa en el aire.
 */
static const char k_morse[128] = {
      0 ,   0 , 'E' , 'T' , 'I' , 'A' , 'N' , 'M' , 'S' , 'U' , 'R' , 'W' , 'D' , 'K' , 'G' , 'O' ,
    'H' , 'V' , 'F' ,   0 , 'L' ,   0 , 'P' , 'J' , 'B' , 'X' , 'C' , 'Y' , 'Z' , 'Q' ,   0 ,   0 ,
    '5' , '4' ,   0 , '3' ,   0 ,   0 ,   0 , '2' ,   0 ,   0 , '+' ,   0 ,   0 ,   0 ,   0 , '1' ,
    '6' , '=' , '/' ,   0 ,   0 ,   0 , '(' ,   0 , '7' ,   0 ,   0 ,   0 , '8' ,   0 , '9' , '0' ,
      0 ,   0 ,   0 ,   0 ,   0 ,   0 ,   0 ,   0 ,   0 ,   0 ,   0 ,   0 , '?' , '_' ,   0 ,   0 ,
      0 ,   0 , '\"',   0 ,   0 , '.' ,   0 ,   0 ,   0 ,   0 , '@' ,   0 ,   0 ,   0 , '\'',   0 ,
      0 , '-' ,   0 ,   0 ,   0 ,   0 ,   0 ,   0 ,   0 ,   0 , ';' , '!' ,   0 , ')' ,   0 ,   0 ,
      0 ,   0 ,   0 , ',' ,   0 ,   0 ,   0 ,   0 , ':' ,   0 ,   0 ,   0 ,   0 ,   0 ,   0 ,   0 ,
};

/* --- buffer de salida, mismo patron y mismo tamano que rtty.c ------- */
#define CW_RINGBUF_SIZE 128U
static char     s_ring[CW_RINGBUF_SIZE];
static uint16_t s_ring_head;
static uint16_t s_ring_tail;

/* --- ajustes ------------------------------------------------------- */
static float   s_pitch_hz = 700.0f;     /* tono ajustado: por donde se busca */
static uint8_t s_enabled;
static float   s_wpm_hint = 20.0f;
static uint8_t s_autotune = 1U;

/* Un juego de coeficientes por sonda, calculados al cambiar el tono. */
static float   s_coeff[CW_PROBES], s_cos[CW_PROBES], s_sin[CW_PROBES];
static float   s_probe_hz[CW_PROBES];
static float   s_probe_avg[CW_PROBES];  /* media lenta de la magnitud */
static float   s_probe_env[CW_PROBES];  /* magnitud suavizada, ver CW_PROBE_SUAVE */
static float   s_probe_dev[CW_PROBES];  /* y su desviacion media: la puntuacion */
static uint8_t s_lock = CW_PROBES / 2U; /* sonda enganchada; arranca en el centro */
static uint16_t s_lock_hops;            /* saltos desde el ultimo cambio de enganche */

/* --- deteccion ------------------------------------------------------ */
static float    s_prev[CW_BLOCK_SAMPLES];  /* bloque anterior, mitad vieja de la ventana */
static float    s_env;                     /* envolvente suavizada */
static float    s_km_hi, s_km_lo;          /* las dos medias que se persiguen */
static uint8_t  s_km_seeded;
static uint16_t s_warm_n;                  /* saltos de calentamiento consumidos */
static float    s_warm_sum;

/* --- temporizacion --------------------------------------------------- */
static uint8_t  s_key;        /* estado de la tecla tras la histeresis */
static uint16_t s_run;        /* saltos que lleva la tecla en este estado */
static uint8_t  s_inhibit;    /* tras una suelta forzada, ver cw_process() */
static uint16_t s_inhibit_n;
static float    s_dot_hops;   /* estimacion viva de la duracion del punto */
static uint8_t  s_adapt_n;    /* elementos desde el ultimo silencio, tope CW_ADAPT_N_FAST */

/* --- puerta de calidad ----------------------------------------------- */
static float    s_len_dot;    /* media de los elementos clasificados como punto */
static float    s_len_dash;   /* ...y como raya */
static float    s_len_gap;    /* media de los huecos cortos, que deberian medir un punto */
static float    s_anchor;                  /* minimo de los ultimos elementos; ver CW_ANCHOR_* */
static float    s_anchor_ring[CW_ANCHOR_N];
static uint8_t  s_anchor_idx;
static float    s_q;          /* error de ajuste medio; 0 es perfecto */
static float    s_frac_dash;  /* fraccion reciente de rayas */
static uint8_t  s_gate;       /* 1 = lo que llega tiene forma de Morse */
static uint8_t  s_good_run;   /* elementos seguidos que encajan; ver CW_GOOD_N */
static uint16_t s_quiet_hops; /* saltos desde el ultimo elemento medido */

/* --- letra en curso -------------------------------------------------- */
static uint8_t  s_clave;      /* acumulador de elementos; 1 = vacio */
static uint8_t  s_n_elem;
static uint8_t  s_desborde;   /* mas de 6 elementos: la letra ya no es valida */
static uint8_t  s_word_done;  /* el espacio de esta pausa ya se ha emitido */
static uint8_t  s_any_output; /* ya ha salido algo: evita un espacio inicial */
static int16_t  s_hist[CW_HIST_N];  /* + elemento, - hueco, en saltos */
static uint8_t  s_hist_n;           /* entradas validas, tope CW_HIST_N */
static uint8_t  s_hist_w;           /* proxima escritura */

/*
 * Goertzel de un bin sobre la concatenacion de dos bloques, sin
 * copiarlos: la recursion se arrastra de un array al siguiente, que da
 * exactamente lo mismo que recorrer la ventana entera. Devuelve
 * potencia, no amplitud.
 */
static float goertzel_pow(const float *a, const float *b, uint32_t n_each, uint8_t k)
{
    float s0, s1 = 0.0f, s2 = 0.0f;
    float coeff = s_coeff[k];
    float real, imag;
    uint32_t i;

    for (i = 0; i < n_each; i++) {
        s0 = a[i] + coeff * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    for (i = 0; i < n_each; i++) {
        s0 = b[i] + coeff * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    real = s1 - s2 * s_cos[k];
    imag = s2 * s_sin[k];
    return real * real + imag * imag;
}

/* Reparte las sondas alrededor del tono ajustado y calcula sus
 * coeficientes. Se llama al cambiar el tono; las medias lentas NO se
 * borran aqui a proposito, para que mover el tono unos hercios con el
 * mando no tire el enganche que ya se tenia. */
static void cw_coeffs(void)
{
    uint8_t k;

    for (k = 0U; k < CW_PROBES; k++) {
        float hz = s_pitch_hz + ((float)k - (float)(CW_PROBES / 2U)) * CW_PROBE_STEP;
        float w;

        if (hz < 100.0f) { hz = 100.0f; }
        s_probe_hz[k] = hz;
        w = 2.0f * 3.14159265358979f * (hz / CW_FS_HZ);
        s_cos[k]   = cosf(w);
        s_sin[k]   = sinf(w);
        s_coeff[k] = 2.0f * s_cos[k];
    }
}

static void ring_push(char c)
{
    uint16_t next = (uint16_t)((s_ring_head + 1U) % CW_RINGBUF_SIZE);

    if (next == s_ring_tail) {
        return;   /* lleno: se tira el caracter nuevo antes que pisar los no leidos */
    }
    s_ring[s_ring_head] = c;
    s_ring_head = next;
}

/* Vuelve a poner la velocidad en el valor de arranque y con ella las
 * medias de longitud, que son lo que la sostiene. */
static void cw_seed_speed(void)
{
    s_dot_hops  = CW_HOPS_PER_DOT(s_wpm_hint);
    s_len_dot   = s_dot_hops;
    s_len_dash  = s_dot_hops * 3.0f;
    s_len_gap   = s_dot_hops;
    s_anchor    = s_dot_hops;
    {
        uint8_t i;
        for (i = 0U; i < CW_ANCHOR_N; i++) { s_anchor_ring[i] = s_dot_hops; }
    }
    s_anchor_idx = 0U;
    s_adapt_n    = 0U;
}

/* Pone a cero todo lo que depende de la senal, dejando intactos los
 * ajustes (tono, siembra de velocidad). Lo usan cw_init() y el
 * encendido, para que cada vez que se entra en CW se empiece limpio en
 * vez de arrastrar el suelo de ruido de hace diez minutos. */
static void cw_reset_state(void)
{
    uint32_t i;

    s_ring_head = 0U;
    s_ring_tail = 0U;

    for (i = 0; i < CW_BLOCK_SAMPLES; i++) { s_prev[i] = 0.0f; }
    for (i = 0; i < CW_PROBES; i++) {
        s_probe_avg[i] = 0.0f;
        s_probe_env[i] = 0.0f;
        s_probe_dev[i] = 0.0f;
    }
    s_lock      = CW_PROBES / 2U;
    s_lock_hops = 0U;
    s_env       = 0.0f;
    s_km_hi     = 0.0f;
    s_km_lo     = 0.0f;
    s_km_seeded = 0U;
    s_warm_n    = 0U;
    s_warm_sum  = 0.0f;

    s_key     = 0U;
    s_run     = 0U;
    s_inhibit   = 0U;
    s_inhibit_n = 0U;
    cw_seed_speed();

    s_q         = 1.0f;   /* arranca desconfiando: la puerta nace cerrada */
    s_frac_dash = 0.5f;
    s_gate       = 0U;
    s_good_run   = 0U;
    s_quiet_hops = 0U;

    s_clave      = 1U;
    s_n_elem     = 0U;
    s_desborde   = 0U;
    s_word_done  = 0U;
    s_any_output = 0U;
    s_hist_n     = 0U;
    s_hist_w     = 0U;
}

void cw_init(void)
{
    s_enabled = 0U;
    cw_coeffs();
    cw_reset_state();
}

static void cw_hist_push(int16_t v)
{
    s_hist[s_hist_w] = v;
    s_hist_w = (uint8_t)((s_hist_w + 1U) % CW_HIST_N);
    if (s_hist_n < CW_HIST_N) { s_hist_n++; }
}

/* Indice real dentro del anillo de la entrada k-esima contando desde la
 * mas antigua que sigue guardada. */
static int16_t cw_hist_at(uint8_t k)
{
    uint8_t first = (uint8_t)((s_hist_w + CW_HIST_N - s_hist_n) % CW_HIST_N);
    return s_hist[(first + k) % CW_HIST_N];
}

/*
 * La puerta acaba de abrir: releer el historial con la velocidad que ya
 * se conoce y sacar el texto que se habia estado decodificando a
 * ciegas.
 *
 * Primero se busca hacia atras hasta donde merece la pena: se para al
 * llegar a 4 s, o antes, al primer elemento que no encaje ni como punto
 * ni como raya. Asi la relectura abarca exactamente el tramo que sigue
 * pareciendo Morse y ni un elemento mas: la chachara del ruido anterior
 * a la transmision se queda fuera sola, sin ninguna regla especial.
 *
 * Y luego se lee hacia delante, pero solo hasta el ultimo hueco de
 * letra. Lo que venga despues es la letra que se esta formando AHORA
 * MISMO, que el camino normal ya tiene a medias en s_clave y va a
 * emitir el solo. Sacarla aqui tambien la sacaria dos veces.
 */
static void cw_replay(void)
{
    uint8_t  k, ini = 0U;
    uint32_t acc = 0U;
    uint8_t  clave = 1U, n_elem = 0U, desb = 0U;
    uint8_t  ultima_letra = 0U;   /* k tras el ultimo hueco de letra */

    if (s_hist_n == 0U) { return; }

    /* hacia atras, mientras siga pareciendo Morse */
    for (k = s_hist_n; k > 0U; k--) {
        int16_t v = cw_hist_at((uint8_t)(k - 1U));
        float   l = (float)((v < 0) ? -v : v);

        acc += (uint32_t)l;
        if (acc > CW_HIST_MAX_HOPS) { ini = k; break; }
        if (v > 0) {
            float d = l / s_dot_hops;
            float e = (d >= CW_DASH_BOUNDARY)
                    ? ((d > 3.0f ? d - 3.0f : 3.0f - d) / 3.0f)
                    : (d > 1.0f ? d - 1.0f : 1.0f - d);
            if (e > CW_HIST_ERR_MAX) { ini = k; break; }
        }
    }

    /* donde acaba la parte que se puede sacar: el ultimo hueco de letra */
    for (k = ini; k < s_hist_n; k++) {
        int16_t v = cw_hist_at(k);
        if (v < 0 && (float)(-v) >= CW_GAP_LETTER * s_dot_hops) {
            ultima_letra = (uint8_t)(k + 1U);
        }
    }

    for (k = ini; k < ultima_letra; k++) {
        int16_t v = cw_hist_at(k);
        float   l = (float)((v < 0) ? -v : v);

        if (v > 0) {
            if (n_elem < 6U) {
                clave = (uint8_t)(clave * 2U + (l >= CW_DASH_BOUNDARY * s_dot_hops));
                n_elem++;
            } else {
                desb = 1U;
            }
        } else if (l >= CW_GAP_LETTER * s_dot_hops) {
            if (n_elem > 0U && !desb) {
                char c = k_morse[clave & 0x7FU];
                if (c != '\0') { ring_push(c); s_any_output = 1U; }
            }
            clave = 1U; n_elem = 0U; desb = 0U;
            if (s_any_output && l >= CW_GAP_WORD * s_dot_hops) { ring_push(' '); }
        }
    }

    s_hist_n = 0U;   /* releido: no se puede volver a sacar */
    s_hist_w = 0U;
}

/* Cierra la letra que hubiera en curso y la manda a la salida. No
 * emite nada si estaba vacia, si se desbordo (mas de 6 elementos), si
 * la combinacion no existe en la tabla, o si la puerta de calidad esta
 * cerrada: es preferible un hueco a una letra inventada. */
/*
 * Vuelve a evaluar las tres condiciones de forma. Se llama al terminar
 * cada elemento, que es cuando hay informacion nueva. Ver la tabla de
 * constantes CW_Q_* para que mide cada una.
 */
static void cw_gate_update(void)
{
    float ratio  = (s_len_dot > 0.01f) ? (s_len_dash / s_len_dot) : 0.0f;
    float gapfit = (s_len_dot > 0.01f)
                 ? ((s_len_gap > s_len_dot ? s_len_gap - s_len_dot
                                           : s_len_dot - s_len_gap) / s_len_dot)
                 : 9.0f;
    uint8_t forma = (uint8_t)(ratio  >= CW_RATIO_MIN && ratio <= CW_RATIO_MAX &&
                              gapfit <= CW_GAPFIT_MAX &&
                              s_frac_dash >= CW_FRAC_MIN && s_frac_dash <= CW_FRAC_MAX);

    if (!s_gate) {
        s_gate = (uint8_t)(forma && s_q < CW_Q_OPEN && s_good_run >= CW_GOOD_N &&
                           s_lock_hops >= CW_LOCK_ESTABLE);
        if (s_gate) {
            cw_replay();
        }
    } else {
        s_gate = (uint8_t)(forma && s_q < CW_Q_CLOSE);
    }
}

/*
 * Unica salida del decodificador. Con la puerta cerrada no sale nada:
 * lo que se decodifico mientras tanto se vuelve a leer del historial de
 * duraciones cuando la puerta abre, ver cw_replay().
 */
static void cw_emit(char c)
{
    if (s_gate) {
        ring_push(c);
        s_any_output = 1U;
    }
}

static void cw_flush_letter(void)
{
    if (s_n_elem > 0U && !s_desborde) {
        char c = k_morse[s_clave & 0x7FU];
        if (c != '\0') {
            cw_emit(c);
        }
    }
    s_clave    = 1U;
    s_n_elem   = 0U;
    s_desborde = 0U;
}

/* Un elemento acaba de terminar: clasificarlo, aprender de el si se
 * puede, y anadirlo a la letra en curso. */
static void cw_mark(uint16_t len)
{
    float l = (float)len;
    float d, err;
    uint8_t raya;
    float k;

    if (len < CW_MARK_MIN_HOPS) {
        return;                      /* chasquido, no un punto */
    }
    if (s_gate && l < CW_MARK_MIN_LOCK * s_dot_hops) {
        return;                      /* enganchado se puede ser mas exigente */
    }
    if (len > CW_MARK_MAX_HOPS || (s_gate && l > CW_MARK_ABSURD * s_dot_hops)) {
        /* tecla bajada demasiado tiempo: ni es codigo ni debe mover la
         * estimacion, y lo que hubiera empezado ya no vale */
        s_clave    = 1U;
        s_n_elem   = 0U;
        s_desborde = 0U;
        return;
    }

    /* ancla: minimo exacto de los ultimos CW_ANCHOR_N elementos */
    {
        uint8_t i;
        s_anchor_ring[s_anchor_idx] = l;
        s_anchor_idx = (uint8_t)((s_anchor_idx + 1U) % CW_ANCHOR_N);
        s_anchor = s_anchor_ring[0];
        for (i = 1U; i < CW_ANCHOR_N; i++) {
            if (s_anchor_ring[i] < s_anchor) { s_anchor = s_anchor_ring[i]; }
        }
        if (s_anchor < CW_DOT_HOPS_MIN) { s_anchor = CW_DOT_HOPS_MIN; }
        if (s_anchor > CW_DOT_HOPS_MAX) { s_anchor = CW_DOT_HOPS_MAX; }
    }

    /* Buscando enganche, si el ancla discrepa mucho de la estimacion se
     * le hace caso a ella: es la medida que no depende de haber
     * clasificado bien, o sea la unica que puede deshacer un enganche
     * equivocado. Con la puerta abierta no se toca nada. */
    if (!s_gate) {
        float r = (s_len_dot > 0.01f) ? (s_anchor / s_len_dot) : 1.0f;
        if (r < CW_ANCHOR_LO || r > CW_ANCHOR_HI) {
            s_len_dot  = s_anchor;
            s_len_dash = s_anchor * 3.0f;
            s_dot_hops = s_anchor;
        }
    }

    s_quiet_hops = 0U;
    cw_hist_push((int16_t)((len > 30000U) ? 30000 : len));
    raya = (uint8_t)(l >= CW_DASH_BOUNDARY * s_dot_hops);

    /*
     * Error de ajuste, en unidades relativas para que un punto y una
     * raya pesen lo mismo: la raya se compara con 3 puntos y se divide
     * por 3.
     */
    d   = l / s_dot_hops;
    err = raya ? ((d > 3.0f ? d - 3.0f : 3.0f - d) / 3.0f)
               : (d > 1.0f ? d - 1.0f : 1.0f - d);
    if (err > 2.0f) { err = 2.0f; }
    s_q += (err - s_q) * CW_Q_ALPHA;
    if (err < CW_ERR_GOOD) {
        if (s_good_run < CW_GOOD_N) { s_good_run++; }
    } else {
        /*
         * Un elemento que no encaja rompe la racha, y con ella tira la
         * cuarentena. Es la misma idea que la justifica: lo que se
         * guarda son los caracteres que FORMAN PARTE de la racha que va
         * a convencer a la puerta. Si la racha se rompe, esos
         * caracteres ya no son esa historia y no hay derecho a
         * sacarlos despues.
         *
         * Sin esto, la chachara del ruido anterior a la transmision se
         * quedaba en la cuarentena y salia por delante del mensaje de
         * verdad en cuanto la puerta abria: "H Q CQ" donde ponia "CQ
         * CQ". Y era peor que perder el primer caracter, porque un
         * caracter perdido se nota y uno inventado no.
         */
        s_good_run = 0U;
    }
    s_frac_dash += ((raya ? 1.0f : 0.0f) - s_frac_dash) * CW_FRAC_ALPHA;

    /*
     * Aprender, pero solo de los elementos de los que no hay duda. Las
     * dos medias de longitud son la unica fuente de la velocidad: el
     * punto sale de promediar lo que dicen las dos, que es mejor
     * estimacion que cualquiera por separado porque cada una ve la
     * mitad de los elementos.
     */
    k = (s_adapt_n < CW_ADAPT_N_FAST) ? CW_ADAPT_FAST : CW_ADAPT_SLOW;
    if (l < CW_SURE_DOT * s_dot_hops) {
        s_len_dot += (l - s_len_dot) * k;
    } else if (l > CW_SURE_DASH * s_dot_hops) {
        s_len_dash += (l - s_len_dash) * k;
    }
    s_dot_hops = 0.5f * s_len_dot + 0.5f * (s_len_dash / 3.0f);
    if (s_dot_hops < CW_DOT_HOPS_MIN) { s_dot_hops = CW_DOT_HOPS_MIN; }
    if (s_dot_hops > CW_DOT_HOPS_MAX) { s_dot_hops = CW_DOT_HOPS_MAX; }
    if (s_adapt_n < CW_ADAPT_N_FAST) { s_adapt_n++; }

    cw_gate_update();

    if (s_n_elem < 6U) {
        s_clave = (uint8_t)(s_clave * 2U + raya);
        s_n_elem++;
    } else {
        s_desborde = 1U;
    }
}

/* Un hueco acaba de terminar. Solo interesa el CORTO, el que separa dos
 * elementos de la misma letra, porque por definicion del codigo mide un
 * punto y por tanto es una comprobacion independiente de la velocidad
 * -sale de los silencios, no de los elementos-. */
static void cw_gap(uint16_t len)
{
    float l = (float)len;

    if (len < CW_MARK_MIN_HOPS) { return; }
    cw_hist_push((int16_t)-((len > 30000U) ? 30000 : (int)len));
    if (l < CW_GAP_LETTER * s_dot_hops) {
        s_len_gap += (l - s_len_gap) * CW_LEN_ALPHA;
    }
}

void cw_process(const float *audio, uint32_t n)
{
    float pot, mag, span, thr;
    uint8_t key_new;
    uint32_t i;

    if (n != CW_BLOCK_SAMPLES) {
        return;   /* mismo contrato estricto que rtty_process() */
    }

    /* --- 1: las nueve sondas, y cual gana --- */
    {
        float mag[CW_PROBES];
        uint8_t k, mejor = s_lock;

        float dev_media = 0.0f;

        for (k = 0U; k < CW_PROBES; k++) {
            float d;
            mag[k] = sqrtf(goertzel_pow(s_prev, audio, CW_BLOCK_SAMPLES, k));
            s_probe_env[k] += (mag[k] - s_probe_env[k]) * CW_PROBE_SUAVE;
            s_probe_avg[k] += (s_probe_env[k] - s_probe_avg[k]) * CW_PROBE_ALPHA;
            d = s_probe_env[k] - s_probe_avg[k];
            if (d < 0.0f) { d = -d; }
            s_probe_dev[k] += (d - s_probe_dev[k]) * CW_PROBE_ALPHA;
            dev_media += s_probe_dev[k];
        }
        dev_media /= (float)CW_PROBES;

        if (s_autotune) {
            for (k = 0U; k < CW_PROBES; k++) {
                if (s_probe_dev[k] > s_probe_dev[mejor] * CW_PROBE_MARGEN &&
                    s_probe_dev[k] > dev_media * CW_PROBE_DESTACA) { mejor = k; }
            }
            if (mejor != s_lock) {
                /*
                 * Cambiar de sonda invalida lo medido hasta ahora: los
                 * elementos anteriores se midieron a traves de un filtro
                 * centrado en otro sitio, o sea atenuados y con los
                 * bordes corridos. Asi que se tira la confianza -el error
                 * medio, la racha de aciertos, la puerta y el historial
                 * que se relee- y se empieza a convencer de cero.
                 *
                 * Es la misma regla que ya aplica una racha rota: lo que
                 * dejo de ser una medida coherente no puede seguir
                 * contando como prueba. Lo que NO se tira es la velocidad
                 * estimada: un filtro descentrado atenua, pero las
                 * duraciones que da siguen siendo aproximadamente las
                 * buenas, y volver a aprenderlas cuesta mas que
                 * corregirlas.
                 */
                s_lock      = mejor;
                s_lock_hops = 0U;
                s_q         = 1.0f;
                s_good_run  = 0U;
                s_gate      = 0U;
                s_hist_n    = 0U;
                s_hist_w    = 0U;
                s_clave     = 1U;
                s_n_elem    = 0U;
                s_desborde  = 0U;
            }
        }
        if (s_lock_hops < CW_LOCK_ESTABLE) { s_lock_hops++; }
        pot = mag[s_lock] * mag[s_lock];
    }
    for (i = 0; i < CW_BLOCK_SAMPLES; i++) { s_prev[i] = audio[i]; }

    /* La division por la longitud de ventana solo mantiene el numero en
     * un rango comodo; aqui todo son cocientes, la escala no importa. */
    mag = sqrtf(pot) / (float)CW_WIN;
    {
        float a = CW_ENV_SPAN / s_dot_hops;
        if (a < CW_ENV_A_MIN) { a = CW_ENV_A_MIN; }
        if (a > CW_ENV_A_MAX) { a = CW_ENV_A_MAX; }
        s_env += (mag - s_env) * a;
    }

    /* --- 3: las dos medias --- */
    if (!s_km_seeded) {
        s_warm_sum += s_env;
        s_warm_n++;
        if (s_warm_n < CW_WARM_HOPS) {
            return;   /* aun calentando: no se decide nada */
        }
        {
            float m = s_warm_sum / (float)s_warm_n;
            if (m < 1.0e-9f) { m = 1.0e-9f; }
            s_km_hi = m * 1.35f;
            s_km_lo = m * 0.70f;
        }
        s_km_seeded = 1U;
    }
    {
        float ka   = s_gate ? CW_KM_ALPHA : CW_KM_ALPHA_FAST;
        float sp   = s_km_hi - s_km_lo;
        float hi_z = s_km_lo + sp * (1.0f - CW_KM_DEAD);
        float lo_z = s_km_lo + sp * CW_KM_DEAD;

        if (s_env > s_km_hi * CW_KM_JUMP) {
            s_km_hi += (s_env - s_km_hi) * CW_KM_ATTACK;   /* algo ha empezado */
        } else if (s_env * CW_KM_JUMP < s_km_lo) {
            s_km_lo += (s_env - s_km_lo) * CW_KM_ATTACK;   /* algo ha terminado */
        } else if (s_env >= hi_z) {
            s_km_hi += (s_env - s_km_hi) * ka;
        } else if (s_env <= lo_z) {
            s_km_lo += (s_env - s_km_lo) * ka;
        }
        /* y si cae en medio, no toca nada: es un flanco */
    }

    /* --- 4: umbral con histeresis --- */
    span = s_km_hi - s_km_lo;
    if (span < 0.0f) { span = 0.0f; }
    thr = s_km_lo + span * (s_key ? CW_THR_OFF : CW_THR_ON);
    key_new = (uint8_t)(s_env > thr);

    if (s_km_hi < s_km_lo * CW_SPAN_MIN) {
        /* las dos medias se han juntado: no hay nada que separar */
        key_new   = 0U;
        s_inhibit = 0U;
    }

    /*
     * Red de seguridad: ningun elemento del codigo puede durar mas que
     * CW_MARK_MAX_HOPS, asi que si la tecla lleva mas que eso bajada,
     * lo que pasa no es que alguien este mandando una raya larguisima:
     * es que el umbral esta mal puesto y se ha quedado enganchado. Se
     * suelta a la fuerza, se tira lo que hubiera, y no se vuelve a
     * admitir una bajada hasta que la envolvente caiga de verdad por
     * debajo del umbral de suelta.
     *
     * Sin el enclavamiento esto no serviria de nada: al soltar a la
     * fuerza con la envolvente todavia alta, la bajada siguiente
     * ocurriria en el salto siguiente y volveriamos a lo mismo.
     */
    if (s_key && s_run >= CW_MARK_MAX_HOPS) {
        s_key       = 0U;
        s_run       = 0U;
        s_inhibit   = 1U;
        s_inhibit_n = 0U;
        s_clave     = 1U;
        s_n_elem    = 0U;
        s_desborde  = 0U;
        key_new     = 0U;
    }
    if (s_inhibit) {
        /* Caduca por nivel o por tiempo, lo que llegue antes. Por tiempo
         * tambien, porque un enclavamiento que solo se abre con una
         * condicion de nivel puede quedarse colgado si esa condicion
         * deja de darse, y entonces el decodificador se queda mudo sin
         * que nada lo diga. */
        s_inhibit_n++;
        if (s_env <= s_km_lo + span * CW_THR_OFF || s_inhibit_n >= CW_WARM_HOPS) {
            s_inhibit   = 0U;
            s_inhibit_n = 0U;
        } else {
            key_new = 0U;
        }
    }

    /* --- 5: duraciones --- */
    if (key_new != s_key) {
        if (s_key) {
            cw_mark(s_run);        /* acaba de terminar un elemento */
        } else {
            cw_gap(s_run);         /* ...o un hueco */
            s_word_done = 0U;
        }
        s_key = key_new;
        s_run = 0U;
    }
    if (s_run < 30000U) { s_run++; }

    if (!s_key) {
        /*
         * Las pausas se resuelven MIENTRAS duran, no cuando acaban. Si
         * se esperase al siguiente elemento, la ultima letra de una
         * transmision no saldria nunca, que es exactamente el momento
         * en que uno la esta esperando.
         */
        if (s_n_elem > 0U && (float)s_run >= CW_GAP_LETTER * s_dot_hops) {
            cw_flush_letter();
        }
        if (!s_word_done && s_any_output &&
            (float)s_run >= CW_GAP_WORD * s_dot_hops) {
            cw_emit(' ');
            s_word_done = 1U;
        }
        if (s_run >= CW_IDLE_RESET_HOPS) {
            /* silencio largo: la proxima transmision empieza limpia, sin
             * heredar un espacio ni la prisa por adaptarse */
            s_any_output = 0U;
            s_adapt_n    = 0U;
        }
    }

    /* --- 6: mantenimiento de la puerta y de la cuarentena --- */
    if (s_quiet_hops < CW_QUIET_RESEED_HOPS) {
        s_quiet_hops++;
    } else {
        /* Silencio largo: devolver la velocidad al valor de arranque,
         * para que la proxima senal no herede la estimacion de la
         * anterior ni la que dejo el ruido. */
        cw_seed_speed();
        s_quiet_hops = 0U;
    }
}

uint8_t cw_get_char(char *out)
{
    if (s_ring_tail == s_ring_head) {
        return 0U;
    }
    *out = s_ring[s_ring_tail];
    s_ring_tail = (uint16_t)((s_ring_tail + 1U) % CW_RINGBUF_SIZE);
    return 1U;
}

void cw_set_enabled(uint8_t on)
{
    if (on && !s_enabled) {
        cw_reset_state();   /* entrar en CW empieza de cero, no continua */
    }
    s_enabled = (uint8_t)(on ? 1U : 0U);
}

uint8_t cw_get_enabled(void) { return s_enabled; }

void cw_set_pitch_hz(float hz)
{
    if (hz < 200.0f)  { hz = 200.0f; }
    if (hz > 2000.0f) { hz = 2000.0f; }
    s_pitch_hz = hz;
    cw_coeffs();   /* el estado de temporizacion NO se toca: se puede
                    * mover el tono con el mando sin perder el enganche */
}

float cw_get_pitch_hz(void) { return s_pitch_hz; }

/* Donde esta escuchando de VERDAD, que es lo que hay que ensenar en
 * pantalla: la raya del osciloscopio tiene que marcar la sonda
 * enganchada, no el tono ajustado. Si marcase el ajustado volveria a
 * pedirle al operador que cuadre a mano algo que la maquina ya ha
 * cuadrado sola. */
float cw_get_detect_hz(void) { return s_probe_hz[s_lock]; }

/* Desviacion entre lo que se oye y lo que se pidio oir, en Hz. Con el
 * banco enganchado esto dice cuanto hay que mover el mando para que el
 * pitido suene al tono preferido: util, pero ya no obligatorio. */
float cw_get_offset_hz(void) { return s_probe_hz[s_lock] - s_pitch_hz; }

void    cw_set_autotune(uint8_t on) { s_autotune = (uint8_t)(on ? 1U : 0U);
                                      if (!on) { s_lock = CW_PROBES / 2U; } }
uint8_t cw_get_autotune(void)       { return s_autotune; }

void cw_set_wpm_hint(float wpm)
{
    if (wpm < CW_WPM_MIN) { wpm = CW_WPM_MIN; }
    if (wpm > CW_WPM_MAX) { wpm = CW_WPM_MAX; }
    s_wpm_hint = wpm;
    cw_seed_speed();
}

float   cw_get_wpm(void)        { return CW_WPM_FROM_HOPS(s_dot_hops); }
float   cw_get_level(void)      { return s_env; }
float   cw_get_floor(void)      { return s_km_lo; }
float   cw_get_peak(void)       { return s_km_hi; }
float   cw_get_quality(void)    { return s_q; }
uint8_t cw_get_key(void)        { return s_key; }
uint8_t cw_get_decode_ok(void)  { return s_gate; }
