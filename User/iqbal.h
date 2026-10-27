/*
 * EL ESPEJO: CORRECTOR DE DESEQUILIBRIO I/Q - 07/10/2026
 *
 * Esa señal no existe. Es el ESPEJO de otra, y la prueba es justo la que el
 * manda: en el eje de la pantalla, una señal de verdad se queda donde esta y
 * parece moverse hacia la derecha cuando bajas el dial. Un espejo vive en
 * 2*fLO - f, asi que cuando el LO baja un kilohercio, el espejo baja DOS, y
 * en la pantalla se va hacia la izquierda. No hay ninguna otra cosa que haga
 * eso. Y de ahi sale la segunda prueba, que se puede leer en la pantalla sin
 * medir nada: el punto medio entre la señal y su espejo ES el LO.
 *
 * DE DONDE SALE EL ESPEJO. Este receptor es de cuadratura: mezcla con dos
 * copias del LO separadas 90 grados y se queda con I y Q. La cancelacion de
 * la banda que no quieres depende ENTERAMENTE de que esos 90 grados sean 90
 * y de que las dos ramas tengan la misma ganancia. No lo son nunca:
 *
 *   - El reloj. La radio no lleva un Si5351 sino un clon chino, y el metodo
 *     de JF3HZB para sacar las dos fases esta pensado para el original. Por
 *     debajo de 5 MHz el clon no reparte igual, que es exactamente la banda
 *     donde el usuario ve el espejo -3,29 MHz en el video-.
 *   - El mezclador y el codec. Dos ramas analogicas distintas, dos
 *     ganancias distintas y un retardo entre ellas.
 *
 * Un grado de error de fase deja el espejo a 41 dB; MEDIO DECIBELIO de
 * ganancia lo deja a 31, no a 40 -aqui los daba por equivalentes y no lo
 * son; para 40 harian falta 0,17 dB. Corregido el 27/10/2026-. O sea unos
 * 40 dB; tres grados lo dejan a 30. Eso es una señal perfectamente visible
 * en la pantalla, que es lo que se ve en el video.
 *
 * LO QUE HACE ESTO. No arregla el reloj: arregla las MUESTRAS, que es donde
 * el error se puede medir. La idea es vieja y no necesita saber nada de la
 * señal: si I y Q fuesen perfectos, estarian descorrelacionados y tendrian
 * la misma potencia. Entonces
 *
 *     theta = E[I*Q] / E[I*I]          cuanto se ha inclinado Q sobre I
 *     Q1    = Q - theta*I              se le quita lo que tiene de I
 *     g     = raiz( E[I*I] / E[Q1*Q1] )   y se iguala la potencia
 *     Q'    = g * Q1
 *
 * Es Gram-Schmidt de toda la vida: ortogonalizar Q contra I y normalizar. No
 * hay lazo, no hay paso de adaptacion, no hay nada que pueda oscilar: son
 * dos medias y una division. Y no supone NADA sobre lo que se esta oyendo,
 * asi que vale igual para AM, para SSB y para los veintidos modos digitales.
 *
 * CUIDADO CON LO QUE SE MIDE. Las medias tienen que salir de ruido de banda
 * ancha o de muchas señales a la vez. Con una portadora sola y limpia justo
 * en el centro, I y Q estan correlacionados POR LA SEÑAL y no por el
 * desequilibrio, y la correccion se iria detras de la señal. Por eso el
 * promedio es muy lento -segundos- y por eso los dos coeficientes estan
 * sujetos por los topes de abajo: lo peor que puede hacer cuando se
 * equivoca es no hacer nada.
 */
#ifndef IQBAL_H
#define IQBAL_H

#include <stdint.h>

/* Topes. theta de +-0,5 son +-26 grados y g entre 0,5 y 2 son +-6 dB: mucho
 * mas de lo que cualquier hardware roto puede dar de si, y poco bastante
 * para que una estimacion disparatada no pueda destrozar la señal. */
#define IQB_THETA_MAX   0.5f
#define IQB_G_MIN       0.5f
#define IQB_G_MAX       2.0f

void  iqbal_init(void);

/* Activar o desactivar. Apagado, iqbal_aplica() no toca nada y el estimador
 * sigue corriendo: asi se puede mirar cuanto esta midiendo sin cambiar la
 * señal, que es como se comprueba si esto hace falta. */
void  iqbal_set_on(uint8_t on);
uint8_t iqbal_get_on(void);

/* Mide sobre un bloque. No cambia nada. */
void  iqbal_mide(const int16_t *i, const int16_t *q, uint32_t n);

/* Corrige un bloque en el sitio. Si esta apagado, no hace nada. */
void  iqbal_aplica(int16_t *i, int16_t *q, uint32_t n);

/*
 * LAS DOS COSAS DE GOLPE SOBRE EL BLOQUE CRUDO ENTRELAZADO, que es como
 * llega del DMA: par = I, impar = Q.
 *
 * Esta es la que usa la radio, y va en la INTERRUPCION del DMA, antes de que
 * el bloque llegue a nadie. El sitio importa: ahi solo pasa una vez, asi que
 * el demodulador y la pantalla ven los mismos numeros y nadie corrige dos
 * veces. Si esto se pusiera en los dos sitios por separado, el espectro
 * acabaria enseñando una señal distinta de la que se oye - que es
 * exactamente el tipo de fallo que luego nadie encuentra.
 *
 * Mide UNO DE CADA OCHO bloques y corrige todos. Eso multiplica por ocho la
 * constante de tiempo -son 10,9 segundos a 96 kHz, no "un segundo largo"
 * como ponia aqui; corregido el 27/10/2026-, lo cual no cambia nada de lo
 * que hace mientras la radio se queda en una banda, y quita tres multiplicaciones por muestra de dentro de la
 * interrupcion.
 */
void  iqbal_bloque(int16_t *crudo, uint32_t n);

/*
 * LA OTRA MEDIDA: EL ESPEJO LEIDO DEL PROPIO ESPECTRO - 07/10/2026.
 *
 * Y era de esperar, porque la fila marcaba 51 dB y 0,1 grados: con eso
 * medido, el corrector aplica practicamente la identidad y encenderlo o
 * apagarlo no puede cambiar nada. No son dos datos, es el mismo dos veces.
 *
 * La pregunta que queda es por que el medidor dice que la cuadratura esta
 * limpia mientras en la pantalla se ve un espejo mucho mas fuerte que eso. Y
 * la sospecha tiene nombre: lo de arriba es UN SOLO NUMERO PARA LOS 96 kHz
 * ENTEROS, una ganancia y una fase. Eso vale si el error entre I y Q es un
 * desfase puro. Si lo que hay es un RETARDO entre las dos ramas, o filtros
 * distintos, la correccion correcta en +20 kHz es la equivocada en -20, y el
 * promedio de la banda sale casi cero: justo lo que se esta viendo.
 *
 * Esto mide lo otro, y no cuesta nada porque el panadaptador ya calcula la
 * FFT compleja: coge el pico mas alto del espectro y lo compara con su bin
 * ESPEJO -el simetrico respecto al centro-. Eso es el rechazo de imagen tal
 * y como sale en la pantalla, en esa frecuencia concreta y no en promedio.
 *
 * Con los dos numeros al lado, la respuesta es inmediata:
 *
 *   - espectro mucho MENOR que el global  -> el desequilibrio depende de la
 *     frecuencia, y hace falta un filtro corto en Q en vez de un coeficiente;
 *   - los dos parecidos y altos           -> lo que se esta mirando NO es un
 *     espejo, y hay que buscar en otro lado.
 *
 * Se le pasa el espectro en dB ya promediado, en orden fftshift: el indice
 * n/2 es el centro. Devuelve el ultimo valor creible, o 0 si no hay ninguna
 * señal lo bastante por encima del suelo como para que la medida signifique
 * algo -un pico de ruido comparado con otro pico de ruido da un numero y no
 * dice nada-.
 */
void  iqbal_mira_espectro(const float *db, uint16_t n);
float iqbal_espejo_db(void);     /* rechazo visto en el espectro, 0 si no hay */
int16_t iqbal_espejo_bin(void);  /* a cuantos bins del centro estaba el pico */

/*
 * LA PRUEBA A/B, QUE LA HACE LA RADIO Y NO EL DUEÑO - 07/10/2026.
 *
 * *** Tres mensajes seguidos pidiendole que apunte numeros de una pantalla,
 * y con razon: el metodo era un engorro. ***
 *
 * Y la culpa es del metodo, no suya. La pregunta era si los 14 dB que sale en
 * el espectro son espejo de verdad o una emisora que casualmente cae en el
 * bin simetrico. Eso se contesta apagando el corrector y mirando si el numero
 * cambia... y hasta ahora eso significaba: toca, apunta, toca, apunta, y
 * acuerdate de cual era cual. Cuatro numeros leidos de una pantalla pequeña
 * para una respuesta que es SI o NO.
 *
 * Esto lo hace la radio. Un toque en la fila y:
 *
 *   1. apaga el corrector y espera a que el promedio de pantalla se asiente;
 *   2. mide el rechazo del espectro durante casi un segundo;
 *   3. lo enciende, espera otra vez y vuelve a medir;
 *   4. deja el corrector como estaba y enseña los dos numeros juntos.
 *
 * Tres segundos. Y la lectura es inmediata:
 *
 *   - el numero SUBE al encender  -> era espejo y el corrector hace algo;
 *   - no se mueve                 -> en ese bin hay una emisora de verdad, o
 *                                    el desequilibrio no es plano y un solo
 *                                    coeficiente no puede con el.
 *
 * El que NO puede moverse es el global: el estimador corre igual apagado. Si
 * se moviera, lo que esta mal es esto y no la radio, y por eso tambien se
 * enseña: es la etiqueta de control de la prueba.
 *
 * EL PLAZO DE SEGURIDAD, Y POR QUE EXISTE.
 *
 * iqbal_ab_paso() cuenta cuadros de pantalla, y en la primera version se
 * llamaba desde dentro de un bloque que se salta entero mientras hay un menu
 * abierto. La fila esta en la pagina de Informacion, o sea que la prueba se
 * lanzaba desde un menu: no podia avanzar ni un cuadro, se quedaba clavada
 * en la fase 1 y DEJABA EL CORRECTOR APAGADO.
 *
 * Eso ya esta arreglado donde tocaba, pero el arreglo no basta, porque el
 * fallo no era de la maquina de estados sino de quien la movia. Una maquina
 * que depende de que alguien la llame y que, si no la llaman, deja la radio
 * peor de como estaba, es una trampa esperando a la proxima refactorizacion.
 *
 * Asi que la prueba lleva ademas un plazo contado en BLOQUES DE AUDIO, que
 * llegan 375 veces por segundo por interrupcion y no dependen de que haya un
 * menu abierto, de que se este dibujando ni de nada que pueda cambiar. Si la
 * prueba pasa de ese plazo sin terminar, se aborta sola y el corrector vuelve
 * a como estaba. Sin resultado, pero sin dejar la radio tocada.
 */
void    iqbal_ab_lanza(void);
uint8_t iqbal_ab_corriendo(void);   /* 0 = parada; si no, 1 o 2 segun la fase */
uint8_t iqbal_ab_hay(void);         /* ya hay un resultado que enseñar */
float   iqbal_ab_off(void);         /* rechazo del espectro, corrector apagado */
float   iqbal_ab_on(void);          /* y encendido */

/* Un paso de la prueba. Se llama UNA VEZ POR CUADRO de pantalla, justo
 * despues de iqbal_mira_espectro(), que es quien le da la medida. Si no hay
 * prueba en marcha no hace nada. */
void    iqbal_ab_paso(void);

/* Lo que ha medido, para la pagina de Informacion y para el banco. */
float iqbal_theta(void);        /* inclinacion de Q sobre I */
float iqbal_g(void);            /* ganancia que se le aplica a Q */
float iqbal_fase_grados(void);  /* theta en grados, que es lo que se entiende */
float iqbal_gan_db(void);       /* g en dB */
/* Rechazo de imagen estimado, en dB, con lo medido. Es el numero que dice si
 * esto hace falta: por encima de 45-50 dB el espejo no se ve en pantalla. */
float iqbal_rechazo_db(void);
uint32_t iqbal_bloques(void);   /* bloques medidos desde el arranque */
/* Y los que la guarda de la pareja simetrica ha dejado fuera. Ver
 * IQB_PAR_MAX en iqbal.c: en una banda llena -40 metros- esto sube y el
 * estimador se queda con lo ultimo bueno en vez de inventarse un
 * desequilibrio que no existe. */
uint32_t iqbal_pares(void);

/*
 * ===========================================================================
 * EL ESPEJO QUE DEPENDE DE LA FRECUENCIA, Y POR QUE UN COEFICIENTE NO PUEDE
 * ===========================================================================
 *
 * Y el numero es exactamente el que sale del banco. Medido en el simulador,
 * con el corrector de arriba -un solo coeficiente complejo-:
 *
 *     desequilibrio PLANO (3 grados, 0,8 dB)        26,8 dB  ->  105 dB
 *     desequilibrio CON PENDIENTE (retardo I/Q)     20,0 dB  ->   22 dB
 *
 * O sea: cuando el desequilibrio es el mismo en toda la banda, un coeficiente
 * lo borra. Cuando VARIA con la frecuencia -y varia, porque el retardo entre
 * las dos ramas analogicas es un retardo y no un angulo- un coeficiente solo
 * puede cancelar su media, y lo que queda son los 22-24 dB que se midieron.
 *
 * LA SALIDA ES UN FIR CORTO sobre el conjugado, que es lo mismo pero con un
 * valor distinto en cada frecuencia:
 *
 *     y(n) = z(n) + suma_m  w[m] * conj(z(n-m))        (z = I + jQ)
 *
 * CAUSAL, y eso NO es un detalle de implementacion: el primer intento uso un
 * filtro centrado -tomas a ambos lados, que obliga ademas a retrasar la
 * señal- y rendia mucho PEOR. El banco lo dijo: 59 dB el centrado contra 75
 * el causal en el mismo caso. Tiene sentido visto a posteriori; lo que
 * desequilibra las dos ramas es un RETARDO, que es causal, y el filtro
 * centrado malgastaba la mitad de las tomas en adelantos que no hacen falta.
 *
 * CUANTAS TOMAS. Medido, que es la unica forma:
 *
 *                        sin nada  1 coef   2     3     4     5
 *     plano                 26,8   105,7   93,9  90,4  88,0  76,7
 *     con pendiente         20,0    22,1   44,8  66,4  79,9  75,2
 *     pendiente fuerte      15,5    17,5   33,1  47,8  61,9  73,2
 *
 * CUATRO. Es la fila que mejor se porta en los tres casos a la vez: en el
 * arriba es el mejor de todos (+58 dB sobre lo que hay hoy), en el plano
 * sigue dando 88 dB -de sobra: por encima de 50 el espejo no se ve- y en el
 * peor caso saca 62. Cinco tomas solo ganan en el caso extremo y pierden en
 * los otros dos, ademas de costar una multiplicacion compleja mas por
 * muestra dentro de la interrupcion.
 *
 * ===========================================================================
 * COMO SE ESTIMA, Y POR QUE NO A CIEGAS
 * ===========================================================================
 *
 * Hubo tres intentos de deducir el filtro de la estadistica del ruido, como
 * hace el corrector de un coeficiente. Los tres salieron MAL -uno llego a
 * empeorar el rechazo de 26 a 22 dB- y el motivo no es un despiste: con
 * entrada blanca, la estadistica de segundo orden NO determina el filtro
 * entero. Queda una ambiguedad que ninguna cantidad de promediado quita.
 *
 * Asi que esto no deduce: MIDE. Cuando en el panorama hay una portadora
 * fuerte, su imagen esta en el bin simetrico, y el cociente complejo entre
 * los dos ES el desequilibrio a esa frecuencia, sin ambiguedad ninguna:
 *
 *     W(-k) = - Z(-k) / conj(Z(k))
 *
 * Sintonizando por la banda se juntan puntos a distintas frecuencias y por
 * ellos pasa el FIR, por minimos cuadrados.
 *
 * DOS COSAS QUE LO ROMPEN SI SE OLVIDAN, y las dos se aprendieron midiendo:
 *
 *   - SOLO LOS PICOS, no todo lo que pase un umbral. Las faldas de la
 *     ventana a los lados de una portadora pasan el umbral de sobra y la
 *     medida que dan es basura.
 *   - SOLO SI ENFRENTE NO HAY NADA. Si el bin espejo esta a menos de 17 dB
 *     -IQB_ESPEJO_BAJO vale 0,02; aqui ponia 6, que era el umbral de antes
 *     del 24/10. Corregido el 27/10/2026-
 *     del pico, eso no es una imagen: son dos emisoras, y medirlo contamina
 *     el ajuste.
 *
 * Y LAS RANURAS, que es lo que hace que esto quepa: acumular un numero por
 * bin serian 2.048 complejos, 16 kB, y en esta radio quedan 1.732 bytes de
 * SRAM. Se acumula en DIECISEIS casillas repartidas por la banda -288
 * bytes-, y el banco dice que el resultado es el mismo al decimal.
 *
 * EL LAZO ES CERRADO, y no por elegancia: el panadaptador hace la FFT sobre
 * las muestras YA CORREGIDAS -tiene que ser asi, ver iqbal_bloque()-, o sea
 * que lo que se mide es el RESIDUO. Por eso iqbal_resuelve() SUMA lo que
 * encuentra a las tomas que ya hay, en vez de sustituirlas. Medido: converge
 * en una vuelta y se queda quieto (59,2 -> 58,7 -> 58,7 -> 58,7 en la
 * version centrada con la que se midio), sin oscilar.
 */
#define IQB_FIR_TOMAS    4U
#define IQB_RANURAS     16U

/*
 * Un cuadro del panadaptador, en COMPLEJO y en orden natural de la FFT (bin
 * 0 = continua). Lo llama fft.c justo despues de las mariposas, antes de
 * pasar a dB: ahi los numeros ya estan y la fase todavia no se ha tirado.
 *
 * No corrige nada ni resuelve nada: solo rellena las ranuras. Es barato -un
 * recorrido del array- y se puede llamar en cada FFT.
 */
void iqbal_calibra(const float *re, const float *im, uint32_t n);

/*
 * Resuelve el FIR con lo que haya en las ranuras y lo SUMA a lo que ya hay.
 * Devuelve 1 si ha cambiado algo.
 *
 * NO se llama desde la interrupcion: lleva un sistema de CUATRO incognitas
 * -IQB_FIR_TOMAS; aqui ponia cinco, que son las RANURAS minimas- y
 * se resuelve en el bucle de pantalla, una vez cada varios segundos. Si no
 * hay ranuras suficientes -hacen falta IQB_RANURAS_MIN con medidas- no toca
 * nada y devuelve 0, que es lo correcto: sin portadoras no hay con que medir,
 * y lo ultimo que se midio sigue siendo lo mejor que se sabe.
 */
uint8_t iqbal_resuelve(void);

/* 1 si el FIR esta en uso. Mientras sea 0 la radio se comporta EXACTAMENTE
 * como antes: el corrector de un coeficiente y nada mas. */
uint8_t iqbal_fir_hay(void);

/* Tira el FIR y las ranuras. Para cuando se cambia de banda: el
 * desequilibrio depende de la frecuencia del LO, asi que lo medido en 15 MHz
 * no vale en 145. */
void iqbal_fir_olvida(void);

/*
 * Y TIRA TAMBIEN EL CORRECTOR PLANO - 27/10/2026.
 *
 * Para cuando lo que cambia no es la frecuencia sino la RELACION I/Q del
 * frontal: invertir el sentido de la cuadratura, rehacer la maniobra de
 * 90 grados de la banda baja, mover el punto de trabajo del VCO. En esos
 * tres casos lo aprendido no es que se quede viejo: queda ACTIVAMENTE
 * equivocado -invertir el sentido es conjugar la señal, y entonces el
 * theta aprendido corrige justo al reves-.
 *
 * Y son precisamente los tres toques que existen PARA MEDIR, asi que
 * dejar el corrector contaminando la medida es lo peor que se puede
 * hacer ahi.
 *
 * Esto deja al corrector sin nada que aplicar: vuelve a aprender desde
 * cero, que a 96 kHz son 10,9 segundos de constante de tiempo.
 */
void iqbal_plano_olvida(void);

/*
 * APRENDER Y CORREGIR SON DOS COSAS. Esto apaga SOLO el aprendizaje: lo ya
 * medido se sigue aplicando. Hace falta en FM ancha, donde la portadora
 * vive en continua y el estimador no puede funcionar - se inventa un
 * espejo que no existe y despues se lo lleva a onda corta. El razonamiento
 * entero, con las medidas, esta delante de iqbal_aprende() en iqbal.c.
 */
void iqbal_aprende(uint8_t si);
uint8_t iqbal_get_aprende(void);

/* Cuantas ranuras tienen medida ahora mismo, para la pagina de Informacion:
 * es lo que dice si la calibracion esta hecha o le falta banda por recorrer. */
uint8_t iqbal_cal_ranuras(void);

#endif /* IQBAL_H */
