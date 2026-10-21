#ifndef WFX_RECOLOCA_H
#define WFX_RECOLOCA_H

#include <stdint.h>

/*
 * RECOLOCAR UNA CARTA YA GRABADA - 20/10/2026.
 *
 * QUE PROBLEMA RESUELVE. El decodificador de fax engancha el renglon donde
 * le pilla el pulso de fase de la emisora, y eso no es una propiedad de la
 * emisora: depende del instante. La misma radio saco una carta corrida 21
 * puntos y otra corrida 269. El resultado es una carta recta pero PARTIDA:
 * el trozo que deberia ir pegado a un margen aparece pegado al otro.
 *
 * COMO SE ARREGLA. Al acabar la carta -por el tono de fin o porque se para
 * de guardar- se enseña la imagen con una LINEA VERTICAL que se mueve con
 * el mando y con el dedo. Donde se deje la linea es el nuevo borde
 * izquierdo: lo que estaba a su derecha se pega a la izquierda y lo que
 * estaba a su izquierda se va al final por el otro lado.
 *
 *     nuevo[i] = viejo[(corte + i) mod ancho]
 *
 * No se pierde ni un punto: es un giro, no un recorte. Los dos casos -la
 * linea cerca del borde derecho o cerca del izquierdo- son la misma
 * cuenta; lo unico que cambia es cual de los dos trozos es el grande.
 *
 * POR QUE AL FINAL Y NO EN VIVO. Hubo un boton que corria la imagen
 * mientras entraba. Se quito: si el fichero se graba ya corrido, una carta
 * en la que se toca el boton a mitad queda con DOS centrados dentro, y eso
 * es una costura, no un desplazamiento: ninguna linea vertical posterior
 * puede deshacerla. El fichero se graba siempre crudo y se gira una vez,
 * entero, cuando ya no entra nada mas.
 *
 * COMO SE HACE SIN PARAR LA RADIO. Un bloque de 4 kB por vuelta del bucle
 * principal. Cada bloque cuesta un borrado de sector mas su escritura
 * -unos 51 ms-, asi que una carta de 285 bloques son unos quince segundos
 * con su barra de progreso, igual que la captura de pantalla.
 *
 * EL RENGLON QUE CRUZA LA FRONTERA. Un renglon mide 756 bytes y un bloque
 * 4096: no caben un numero entero de renglones en un bloque, asi que hay
 * un renglon a caballo entre cada dos. Su mitad de la izquierda ya se ha
 * reescrito cuando toca la de la derecha, o sea que su original ya no esta
 * en el chip. Por eso se guarda el renglon girado entero en `s_carro` al
 * cerrar un bloque y se usa su cola al abrir el siguiente. Sin eso, uno de
 * cada seis renglones saldria mitad girado y mitad no, que es exactamente
 * la costura que esto viene a quitar.
 */

typedef enum {
    WFX_REC_PARADO = 0,   /* no hay trabajo */
    WFX_REC_ANDA,         /* quedan bloques */
    WFX_REC_BIEN,         /* acabo y el fichero esta girado */
    WFX_REC_MAL           /* se corto por el camino: ver wfx_recoloca_porque() */
} wfx_rec_estado_t;

/*
 * EL TAMAÑO DEL BUFFER PRESTADO, DICHO EN UN SITIO - 21/10/2026.
 *
 * Era una promesa escrita en prosa aqui abajo ("buf4k son 4096 bytes") y un
 * `#define BLOQUE 4096U` dentro de wfx_recoloca.c, sin nada que los atara.
 * Quien presta el buffer es main.c, con su propio GSV_BLOQUE. Tres numeros
 * iguales por costumbre y no por construccion: el dia que uno baje, esto
 * lee 4096 bytes del chip encima de un buffer mas corto y se lleva por
 * delante lo que haya detras, en silencio y en el bucle principal.
 *
 * Ahora el numero sale de aqui, wfx_recoloca.c lo usa, y main.c comprueba
 * al compilar que su buffer da. Es el tamaño de borrado del chip, asi que
 * no puede ser otro.
 */
#define WFX_REC_BLOQUE  4096U

/*
 * Prepara el trabajo. `buf4k` son WFX_REC_BLOQUE bytes prestados por quien
 * llama -el mismo que usa el grabador, que a estas alturas ya esta libre- y
 * tienen que seguir siendo suyos hasta que esto acabe.
 *
 * `corte` es la columna que pasa a ser el borde izquierdo, 0..ancho-1. Con
 * 0 no hay nada que hacer y devuelve 0 sin tocar el fichero.
 *
 * Devuelve 0 si no se puede empezar.
 */
uint8_t wfx_recoloca_empieza(const char name8[8], const char ext3[3],
                             uint16_t ancho, uint16_t alto, uint16_t corte,
                             uint8_t *buf4k);

/* Un bloque. Se llama desde el bucle principal mientras devuelva WFX_REC_ANDA. */
wfx_rec_estado_t wfx_recoloca_paso(void);

/* 0..100, para la barra. */
uint8_t wfx_recoloca_pct(void);

/* En que estado esta sin dar un paso. */
wfx_rec_estado_t wfx_recoloca_estado(void);

/* Por que fallo, para enseñarlo. Devuelve 0 si no fallo. */
const char *wfx_recoloca_porque(void);

/* Olvida el trabajo (al salir del modo, o tras enseñar el resultado). */
void wfx_recoloca_olvida(void);

#endif /* WFX_RECOLOCA_H */
