/*
 * EL ESPEJO: CORRECTOR DE DESEQUILIBRIO I/Q - 07/10/2026
 *
 * *** El dueño, con un video girando el encoder hacia abajo: "todas las
 * señales al girar el encoder disminuyendo la frecuencia avanzan de
 * izquierda a derecha pero hay una señal que lo hace de derecha a
 * izquierda". ***
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
 *     donde el dueño ve el espejo -3,29 MHz en el video-.
 *   - El mezclador y el codec. Dos ramas analogicas distintas, dos
 *     ganancias distintas y un retardo entre ellas.
 *
 * Un grado de error de fase, o medio dB de ganancia, deja el espejo a unos
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
 * Mide UNO DE CADA OCHO bloques y corrige todos. El promedio ya tarda un
 * segundo largo, asi que medir ocho veces mas despacio no cambia nada de lo
 * que hace, y quita tres multiplicaciones por muestra de dentro de la
 * interrupcion.
 */
void  iqbal_bloque(int16_t *crudo, uint32_t n);

/*
 * LA OTRA MEDIDA: EL ESPEJO LEIDO DEL PROPIO ESPECTRO - 07/10/2026.
 *
 * *** El dueño, despues de tocar la fila con el espejo en pantalla: "diria
 * que no se inmuta". ***
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
 * y con razon: "eres un coñazo". ***
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
 * *** El dueño, la primera vez que la lanzo: "a b sin corregir" - y ahi se
 * quedaba. ***
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

#endif /* IQBAL_H */
