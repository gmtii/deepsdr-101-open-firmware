#include "idioma.h"

/*
 * Un byte de estado y tres funciones. Ver idioma.h para el porque del
 * mecanismo; aqui solo esta la parte que se ejecuta.
 *
 * NO guarda nada por su cuenta: quien decide cuando se escribe CONFIG.CSV
 * es main.c, igual que con todos los demas ajustes. Este fichero no conoce
 * la flash, ni la pantalla, ni nada del GD32 - por eso el simulador lo
 * compila tal cual, sin sustitutos.
 */

static uint8_t s_idioma = IDIOMA_ES;

uint8_t idioma(void)
{
    /*
     * Cinturon y tirantes. s_idioma solo se escribe desde idioma_pon(),
     * que ya recorta, pero esto es lo que indexa las tablas de texto de
     * toda la interfaz: si alguna vez se cuela otro valor, el precio de
     * enterarse es una pantalla en espanol y no una lectura fuera de
     * tabla.
     */
    return (s_idioma == IDIOMA_EN) ? IDIOMA_EN : IDIOMA_ES;
}

void idioma_pon(uint8_t v)
{
    s_idioma = (v == IDIOMA_EN) ? IDIOMA_EN : IDIOMA_ES;
}

uint8_t idioma_alterna(void)
{
    idioma_pon((uint8_t)((idioma() == IDIOMA_ES) ? IDIOMA_EN : IDIOMA_ES));
    return idioma();
}

const char *tr(const char *es, const char *en)
{
    if (idioma() == IDIOMA_EN) {
        /*
         * Un nulo aqui seria un hueco en blanco en la pantalla y nadie
         * sabria de donde sale. Cayendo al espanol al menos se ve QUE
         * cadena es la que falta.
         */
        return (en != 0) ? en : es;
    }
    return es;
}

const char *idioma_nombre(void)
{
    /*
     * Cada idioma escrito en si mismo, no traducido al otro. En la pantalla
     * pone "Idioma: Espanol" o "Language: English", nunca "Idioma: Ingles":
     * quien busca su idioma en una radio que no entiende busca la palabra
     * que reconoce.
     */
    return (idioma() == IDIOMA_EN) ? "English" : "Espa\xC3\xB1ol";
}
