#ifndef AVIONES_PANTALLA_H
#define AVIONES_PANTALLA_H

/*
 * La pantalla de "copiando la base de aviones", que sale sola al arrancar
 * cuando hay un ICAO24.BIN en el disco. Ver aviones_pantalla.c.
 *
 * Sin dependencias de hardware a proposito: el reloj se le pasa, igual que
 * a splash.c, para que el simulador de host pueda compilarla tal cual y se
 * pueda mirar como queda sin la placa delante.
 */
#include <stdint.h>

/* Lo que se ensena debajo de la barra. */
/*
 * LA COMPROBACION ES UNA FASE, NO UN HUECO - 28/09/2026.
 *
 * *** El dueño, viendo arrancar la V2.03: "ha llegado al final y no
 * avanza" ... y despues: "aaaahora". ***
 *
 * No estaba colgada. Despues de copiar se comprueban las sumas de las
 * secciones -medio megabyte por un bus lento, 8,5 segundos medidos-, y
 * durante ese rato la pantalla se quedaba en el 100 % de COPIANDO con el
 * cartel de "No apagues la radio hasta que termine" debajo. Ocho segundos
 * de barra llena y quieta no dicen "espera": dicen "se ha colgado", y
 * justo entonces es cuando uno tira del cable.
 *
 * Asi que la comprobacion tiene ahora su fase, su texto y su barra, que
 * vuelve a empezar de cero.
 */
typedef enum {
    AVIP_BUSCANDO = 0,   /* abriendo el fichero */
    AVIP_COPIANDO,       /* va por `pct` */
    AVIP_COMPROBANDO,    /* copiada; sumando para ver si llego entera */
    AVIP_HECHO,          /* copiada y borrada del disco */
    AVIP_FALLO           /* no se pudo: `motivo` lo dice */
} avip_fase_t;

/*
 * EL ROTULO SE PASA, NO SE DA POR SUPUESTO - 28/09/2026.
 *
 * *** El dueño, con una foto de la pantalla: un cartel que decia "Base de
 * datos de aviones" y "ICAO24.BIN, del disco a la zona alta" mientras lo
 * que habia fallado era el cargador de EMISORAS. ***
 *
 * Esta pantalla la escribio la carga de aviones y por eso llevaba su
 * titulo escrito dentro. Cuando llego la segunda base se reutilizo tal
 * cual, y entonces el cartel mentia sobre lo que estaba haciendo: en un
 * mensaje de error, eso no es un detalle - es lo unico que hay para saber
 * que ha pasado, y mandaba a mirar al sitio equivocado-.
 *
 * `titulo` y `que` a cero dan los de siempre, para no tocar a quien ya
 * llamaba con cuatro argumentos.
 */
void aviones_pantalla_t(avip_fase_t fase, uint8_t pct, const char *motivo,
                        uint32_t bytes, const char *titulo, const char *que);

/* Pinta la pantalla entera. `pct` 0..100, `motivo` solo se mira en FALLO. */
void aviones_pantalla(avip_fase_t fase, uint8_t pct, const char *motivo,
                      uint32_t bytes);

/* Solo para el banco: ver su comentario en el .c. `out` necesita 16 bytes. */
void aviones_pantalla_miles_dbg(char *out, uint32_t v);

#endif /* AVIONES_PANTALLA_H */
