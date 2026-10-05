#ifndef BIP_H
#define BIP_H

#include <stdint.h>

/*
 * ============================================================================
 * EL PITIDO: QUE LA RADIO PUEDA CONTESTAR SIN USAR LA PANTALLA
 * ============================================================================
 * 05/10/2026.
 *
 * *** El dueño, pidiendo las capturas de pantalla: "pulsando la esquina
 * superior izquierda durante 4 segundos y que avise con un pitido o algo
 * asi". ***
 *
 * Y hace falta de verdad, no es un adorno: un gesto que hay que MANTENER
 * cuatro segundos necesita decir "ya" en el momento exacto en que ha contado,
 * porque si no, o se suelta antes o se tiene el dedo ahi diez segundos por si
 * acaso. Un aviso en la pantalla no vale para eso - el dedo la esta tapando.
 *
 * DE DONDE SALE EL SONIDO. Esta radio no tiene zumbador: lo unico que suena
 * es el altavoz, y a el llega lo que escriba el demodulador en su bloque de
 * audio. Asi que el pitido se mete AHI, justo antes de entregar el bloque al
 * I2S, y mientras dura TAPA el audio del aire en vez de mezclarse con el. Es
 * a proposito: un tono mezclado con una emisora se confunde con la emisora.
 *
 * COSTE. Un seno de la tabla del NCO y una envolvente por muestra, y solo
 * mientras suena: unos 25 ciclos por muestra, el 1,2 % del chip durante las
 * decimas que dura. Apagado no cuesta ni una comparacion por muestra - se
 * sale en la primera linea.
 *
 * LA ENVOLVENTE NO ES COSMETICA. Un tono que empieza y acaba de golpe suena
 * como un chasquido, y en un altavoz pequeño el golpe se oye mas que el tono.
 * Cinco milisegundos de subida y de bajada lo quitan entero.
 */

/* La secuencia mas larga que se puede pedir de una vez. */
#define BIP_TRAMOS 4U

/*
 * Pide un tono. `hz` a 0 significa SILENCIO, que es como se piden dos
 * pitidos seguidos sin que se junten. Se encolan hasta BIP_TRAMOS; lo que no
 * quepa se tira, que es mejor que hacer esperar a quien llama.
 *
 * Se puede llamar desde el bucle principal con el audio corriendo: lo unico
 * que comparte con la interrupcion es la cola, y se escribe de forma que un
 * tramo a medio encolar no suene.
 */
void bip_pide(uint16_t hz, uint16_t ms);

/* Lo de siempre: dos cortos para "empieza", uno largo para "hecho", y uno
 * grave para "no ha podido ser". Estan aqui y no en quien llama para que
 * todos los avisos de la radio suenen igual. */
void bip_listo(void);     /* hecho */
void bip_empieza(void);   /* en marcha */
void bip_error(void);     /* no ha podido ser */

/* Si queda algo por sonar. */
uint8_t bip_sonando(void);

/*
 * Mete el pitido en un bloque de audio ESTEREO entrelazado, en el sitio.
 * `n` son MARCOS (pares de muestras), no muestras. Si no hay nada que sonar
 * se va sin tocar nada.
 */
void bip_mete(int16_t *estereo, uint32_t n, float fs_hz);

#endif /* BIP_H */
