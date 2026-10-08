/*
 * EL ESPEJO QUE DEPENDE DE LA FRECUENCIA - 07/10/2026
 *
 * *** El dueño, con la prueba A/B hecha por la radio: "a b 12 13 db". ***
 *
 * Un decibelio. El corrector plano de iqbal.c no toca lo que se ve en la
 * pantalla, y eso ya no es una sospecha: es una medida que ha hecho la propia
 * radio apagando y encendiendo el corrector sobre la misma señal.
 *
 * POR QUE NO LLEGA EL DE iqbal.c. Aquel corrige con DOS NUMEROS para los 96
 * kHz enteros: una ganancia y una fase. Eso es exacto si lo unico que separa
 * a I de Q es un angulo. Pero si entre las dos ramas hay un RETARDO -medio
 * periodo de muestreo basta-, o filtros antialias que no son identicos, el
 * desequilibrio ya no es un numero: es una FUNCION DE LA FRECUENCIA. Un
 * retardo de medio periodo son 0,9 grados a 1 kHz y 43 grados a 48. Corregir
 * eso con una constante es elegir un punto de la banda y empeorar el resto, y
 * el promedio sobre toda la banda se va casi a cero -que es exactamente lo
 * que enseñaba la fila: 0,1 grados en una frecuencia y 3,8 en otra-.
 *
 * LO QUE HACE ESTO. Lo mismo, pero con un FILTRO en vez de con un numero.
 *
 *     y[n] = z[n-D] + suma_k  w[k] * conj(z[n-k])
 *
 * donde z = I + jQ. La parte de conj(z) es exactamente la imagen: todo lo que
 * aparece en la frecuencia espejo sale de ahi. Con w de un solo coeficiente
 * esto es el corrector plano otra vez; con cuatro, puede poner una correccion
 * DISTINTA EN CADA FRECUENCIA, que es lo que hace falta.
 *
 * El retardo D del camino principal no es un adorno: el filtro de
 * desequilibrio del hardware no tiene por que ser causal, y sin ese retardo
 * no hay forma de representar su mitad izquierda.
 *
 * COMO SE BUSCAN LOS COEFICIENTES, y es lo bonito. Una señal de radio normal
 * es "propia": su correlacion consigo misma SIN conjugar -E{z[n]*z[n-l]}, sin
 * asterisco- vale cero para todos los retardos. El desequilibrio es lo UNICO
 * que la hace distinta de cero. Asi que no hace falta saber nada de lo que se
 * esta oyendo: basta con empujar esa correlacion a cero en cuatro retardos.
 *
 * Y la cuenta sale redonda. Si z es razonablemente blanca con potencia P,
 *
 *     E{y[n]*y[n-l]} ~= C_z(l) + w[l]*P
 *
 * siendo C_z la correlacion sin conjugar de la entrada. Igualar a cero da
 * w[l] = -C_z(l)/P directamente, asi que el ajuste es
 *
 *     w[l] -= mu * c_l / P
 *
 * con c_l medido sobre el bloque. Sin lazo, sin decisiones, sin saber que
 * modulacion hay encima. La aproximacion pide que la entrada sea de banda
 * ancha -96 kHz de ruido de HF con emisoras dentro lo es-; con una portadora
 * sola y limpia no vale, y por eso mu es pequeño y los coeficientes estan
 * sujetos: lo peor que puede hacer cuando se equivoca es no hacer nada.
 *
 * ESTO NO SUSTITUYE A iqbal.c, VA DETRAS. El plano se come la parte constante
 * -que es la grande y la que converge en un segundo- y este se queda con lo
 * que depende de la frecuencia, que es poco y tarda mas. Separarlos ademas
 * deja la prueba A/B contando una historia que se entiende: el plano ya se
 * medio antes y dio 12->13.
 */
#ifndef IQANCHO_H
#define IQANCHO_H

#include <stdint.h>

/*
 * Cuatro coeficientes. A 96 kHz son 42 microsegundos de memoria, que cubre de
 * sobra cualquier diferencia de retardo entre dos ramas del mismo circuito
 * -son nanosegundos- y el rizado de dos filtros antialias que no casan, que
 * es lo que de verdad se esta corrigiendo. Mas coeficientes no corrigen mas:
 * añaden ruido de adaptacion y CPU dentro de la interrupcion.
 */
#define IQA_TAPS   6U
/*
 * Y SIN RETARDO EN EL CAMINO PRINCIPAL. La primera version lo llevaba, con
 * el argumento de que el filtro de desequilibrio del hardware no tiene por
 * que ser causal. El argumento es cierto y la consecuencia era desastrosa,
 * y la cuenta lo dice sin lugar a dudas. Desarrollando a primer orden:
 *
 *     c_l = C_z(l) + P * ( w[D-l] + w[D+l] )
 *
 * o sea que el retardo l NO habla de w[l]: habla de la SUMA de los dos
 * coeficientes que caen a l de distancia de D. Con D = 2 y cuatro
 * coeficientes eso da
 *
 *     l=0 -> 2*w[2]      l=1 -> w[1]+w[3]
 *     l=2 -> w[0]        l=3 -> nada en absoluto
 *
 * Tres combinaciones observables para cuatro incognitas, y un retardo -el
 * 3- que no depende de ningun coeficiente. Ajustar w[3] con c_3 es empujar
 * un coeficiente con ruido puro: en el banco se iba al tope y destrozaba una
 * señal que estaba PERFECTA -de 35 dB de rechazo a 1,4-.
 *
 * Con D = 0 la misma cuenta se vuelve diagonal, que es lo que hacia falta:
 *
 *     c_0 = C_z(0) + 2*P*w[0]      c_l = C_z(l) + P*w[l]   (l >= 1)
 *
 * Cada retardo habla de UN coeficiente. Y no se pierde nada por el camino:
 * la correccion que hace falta es -B(w)/A(w), con A el camino de la señal y
 * B el de la imagen, y ese cociente es causal y estable siempre que A no
 * tenga ceros fuera -que no los tiene, porque A es "casi uno" y B es
 * "casi cero": eso es lo que significa que el desequilibrio sea pequeño-.
 */
#define IQA_D      0U

/* Topes de los coeficientes. 0,25 en modulo son unos 12 dB de desequilibrio
 * por coeficiente: mas que cualquier hardware roto, y poco bastante para que
 * una estimacion disparatada no pueda destrozar la señal. */
#define IQA_W_MAX  0.25f

void    iqa_init(void);
void    iqa_set_on(uint8_t on);
uint8_t iqa_get_on(void);

/*
 * Filtra y adapta un bloque crudo entrelazado EN EL SITIO: par = I, impar = Q.
 * Se llama justo DESPUES de iqbal_bloque(), en la interrupcion del DMA, que
 * es el unico sitio por donde pasan todas las muestras una sola vez.
 */
void    iqa_bloque(int16_t *crudo, uint32_t n);

/* Energia de los coeficientes. Si se dispara, el lazo ha divergido. Quien lo
 * use TIENE que mirarlo: un corrector divergido no avisa. */
float   iqa_energia(void);
/* El mayor |w|, para la pagina de Informacion: dice cuanto esta corrigiendo. */
float   iqa_mayor(void);

#endif /* IQANCHO_H */
