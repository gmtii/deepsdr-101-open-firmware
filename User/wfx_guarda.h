#ifndef WFX_GUARDA_H
#define WFX_GUARDA_H

#include <stdint.h>

/*
 * CUANDO SE ABRE EL FICHERO DE UNA CARTA DE FAX - 20/10/2026.
 *
 * LA REGLA, DICHA POR EL USUARIO Y CON RAZON:
 *
 *   "cuando le den a guardar que abra un fichero y que vaya escribiendo
 *    ahi todo lo que va decodificando, y cuando se salgan o le den otra vez
 *    al boton que cierre el fichero. Punto."
 *
 * Eso es todo. EL BOTON MANDA, y el estado del decodificador no pinta nada
 * en esta decision.
 *
 * LO QUE HABIA ANTES Y POR QUE ESTABA MAL. El fichero se abria y se cerraba
 * siguiendo al decodificador: se abria al entrar en imagen y se cerraba al
 * salir. Y el decodificador entra y sale CONSTANTEMENTE con señal justa.
 * De ahi salieron, en un mismo dia y por tres causas distintas:
 *
 *   - veintisiete ficheros de una sesion, por cerrar al primer parpadeo;
 *   - diez minutos de carta sin grabar nada, por una guarda que impedia
 *     reabrir despues de un cierre;
 *   - y veintiseis ficheros de 8 kB, por una reapertura automatica que puse
 *     para arreglar lo anterior.
 *
 * Tres intentos de hacer bien una maquina de estados que no tenia que
 * existir. Lo que el usuario quiere es un interruptor, y un interruptor no
 * tiene estados: esta puesto o no lo esta.
 *
 * EL `fallo` SIGUE EN LA CONDICION. Sin el, un disco sin hueco haria que
 * esto reintentara reservar 448 kB en cada vuelta del bucle, y cada intento
 * recorre la tabla de asignacion entera. Con el, se intenta una vez y se
 * queda quieto hasta que el usuario toque el boton, que es lo que limpia
 * ese aviso.
 */
static inline uint8_t wfx_abre_toca(uint8_t hay_fichero, uint8_t fallo,
                                    uint8_t guarda)
{
    if (hay_fichero != 0U) { return 0U; }
    if (fallo       != 0U) { return 0U; }
    if (guarda      == 0U) { return 0U; }
    return 1U;
}

#endif /* WFX_GUARDA_H */
