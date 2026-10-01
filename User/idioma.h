#ifndef IDIOMA_H
#define IDIOMA_H

#include <stdint.h>

/*
 * Idioma de la interfaz. Espanol por defecto, ingles como alternativa.
 *
 * POR QUE ASI Y NO CON UNA TABLA DE IDENTIFICADORES
 *
 * Lo normal en un programa grande es un enum de identificadores de texto y
 * dos tablas paralelas indexadas por el. Aqui no: son ~600 cadenas repartidas
 * por main.c, y con identificadores cada cadena se lee en un sitio y su
 * traduccion en otro. Al revisar el cambio no se ve si "Rapida" se ha
 * traducido bien; se ve un numero.
 *
 * Aqui las dos van JUNTAS, en el mismo sitio donde estaba la espanola:
 *
 *     case AJ_RFAGC: return tr("Activo", "On");
 *     { T("Calibrar tactil", "Touch calibration"), AJ_PAG_EQUIPO, AJ_CAL_TACTIL },
 *
 * Se revisa de un vistazo, no se puede olvidar una, y el compilador avisa si
 * a un T() le falta un miembro. Cuesta un puntero mas por cadena en flash
 * (~2,4 kB con las 600) y ni un byte de RAM.
 *
 * DONDE SE RESUELVE
 *
 * tr() es de tiempo de ejecucion: vale para lo que se decide al dibujar.
 * T() es un inicializador de tabla, para lo que es constante: la tabla
 * guarda los dos punteros y quien la lee indexa con idioma().
 *
 * NO SE TRADUCE
 *
 * Lo que no es interfaz sino dato o protocolo: prefijos de indicativo,
 * estaciones terrestres de HFDL, palabras de ALE, modos de SSTV, nombres de
 * emisora, y sobre todo las claves de CONFIG.CSV - traducirlas dejaria los
 * ajustes guardados del usuario sin leer.
 *
 * TAMPOCO cambia el formato de los numeros: la frecuencia se sigue viendo
 * 14.090,00 y los anchos 1,8 kHz en los dos idiomas. Decision del dueno el
 * 29/09/2026. El formateo esta escrito a mano byte a byte en una docena de
 * sitios de main.c y cambiarlo es otro trabajo, no este.
 */

#define IDIOMA_ES 0U
#define IDIOMA_EN 1U
#define IDIOMA_N  2U

/*
 * El tipo de un texto de tabla. Se declara en el struct como
 *
 *     texto_t nombre;
 *
 * que es un const char *nombre[IDIOMA_N], y se lee con nombre[idioma()].
 */
typedef const char *texto_t[IDIOMA_N];

/* Inicializador de un texto_t. El orden es el mismo que el de los dos
 * argumentos de tr(): espanol primero, siempre. */
#define T(es, en) { (es), (en) }

/* Idioma actual, IDIOMA_ES o IDIOMA_EN. Nunca devuelve otra cosa. */
uint8_t idioma(void);

/* Pone el idioma. Un valor fuera de rango se queda en espanol: esto lo
 * llama tambien la carga de CONFIG.CSV, y un fichero editado a mano no
 * puede dejar la interfaz indexando fuera de las tablas. */
void idioma_pon(uint8_t v);

/* Alterna entre los dos y devuelve el que queda. Es lo que hace el boton. */
uint8_t idioma_alterna(void);

/* El texto que toca. Los dos punteros han de ser validos: si falta la
 * traduccion se escribe la misma cadena dos veces, a proposito y a la
 * vista, en vez de dejar un 0 que se vea como hueco en blanco. */
const char *tr(const char *es, const char *en);

/* Nombre del idioma para el propio boton. En ES dice "Espanol", en EN
 * dice "English": cada uno en su idioma, que es lo que espera quien no
 * entiende el otro. */
const char *idioma_nombre(void);

#endif /* IDIOMA_H */
