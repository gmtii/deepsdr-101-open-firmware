#ifndef PEORES_H
#define PEORES_H
/*
 * EL PEOR CASO DE CADA AJUSTE - la cadena mas larga que su valor puede
 * llegar a enseñar.
 *
 * Vivia dentro de sim/config.c, que mide si cada valor cabe en su celda. El
 * 23/09/2026 hizo falta en un segundo sitio: sim/chipstest.c, que mide la
 * barra de estado y necesita el peor caso del ajuste ENGANCHADO AL MANDO -
 * su nombre y su valor salen ahi juntos, en la pastilla.
 *
 * Y lo importante es que salgan JUNTOS y de la misma fila. La primera
 * version de chipstest probaba el nombre mas largo con el valor mas largo
 * de otro ajuste ("Despl. RTTY" con "12,5 kHz"), una pareja que no existe:
 * el desplazamiento de RTTY se enseña en hercios. Medir un peor caso
 * imposible es pedirle a la pantalla sitio para algo que nunca va a pasar,
 * y de ahi salen decisiones de diseno equivocadas.
 *
 * El firmware no declara estos textos en ningun sitio porque los construye
 * al vuelo, asi que esta lista se escribe a mano - pero una sola vez, y un
 * ajuste nuevo sin su fila hace fallar los dos bancos.
 */

/*
 * 29/09/2026: tres columnas. La clave sigue siendo el nombre ESPANOL -asi
 * traducir un ajuste no le deja sin medir- y ahora hay un peor caso por
 * idioma, porque el ingles no siempre es mas corto: "Palette" mide mas que
 * "Del color" no, pero "High contrast" si mide mas que "Oscura", y
 * gfx2_text_in() no recorta: lo que no cabe se pinta encima de la celda de
 * al lado.
 */
typedef struct { const char *nombre, *peor, *peor_en; } peor_t;
static const peor_t k_peores[] = {
    /* El idioma se escribe en si mismo: "Español" o "English". Las dos
     * caben de sobra, pero van declaradas igual que las demas para que el
     * banco las mida y no se cuelen por ser cortas.
     *
     * REPASADA ENTERA EL 30/09/2026, y ocho filas estaban mal: declaraban
     * como "peor caso" un valor que ya no era el peor, o directamente uno de
     * otra rama. Las ocho se han vuelto a medir con gfx2_text_w() y la misma
     * font_ui_18b que usa el banco, no a ojo. Todas cabian igual -el hueco de
     * la celda son 172 px y el peor de verdad son 146-, asi que no tapaban
     * ningun fallo; lo que si era falso es el MARGEN que el banco imprimia:
     * decia 42 px de sobra cuando son 26.
     *
     * Y la moraleja, que es la de siempre en este proyecto: un "peor caso"
     * escrito a mano envejece en silencio. Cuando se pueda recorrer la lista
     * de verdad -como hace sim/dxcc_test.c con los 329 prefijos- es mejor
     * recorrerla. */
    /* El localizador: seis caracteres, y el mas ancho que se puede teclear
     * con las seis casillas es todo mayusculas y digitos anchos. Igual en
     * los dos idiomas - no es texto, es un localizador. */
    /* "sin poner" cuando CONFIG.CSV no trae un localizador valido: es mas
     * ancho que el propio localizador en español, aunque no en ingles. */
    { "QTH",            "sin poner", "RR99xx" },
    /* El sentido del mando: "Invertido" es mas ancho que "Normal". */
    { "Mando",          "Invertido", "Reversed" },
    { "Idioma", "Español", "English" },
    { "AGC", "Sin AGC", "No AGC" },
    /* Los DOS cortes desde la etapa 34, que es mas ancho que "4,0 kHz". */
    { "Ancho (BW)", "0,3-6,0 kHz", "0,3-6,0 kHz" },
    { "Volumen", "-47,5 dB", "-47,5 dB" },
    { "Silenciador", "-120 dB", "-120 dB" },
    { "Reducción", "100", "100" },
    /* Los CUATRO niveles, no solo el mas fuerte: "Apagado" y "Medium" son
     * mas anchos que "Fuerte"/"Strong". Medido, no supuesto. */
    { "Blanker", "Apagado", "Medium" },
    { "Ganancia", "-47,5 dB", "-47,5 dB" },
    { "AGC de RF", "Apagado", "Off" },
    /* El peor caso NO es "-12 dB": cuando el AGC de RF sube por encima del
     * suelo pedido, la celda pone tambien lo que esta haciendo el automatico
     * entre parentesis. El propio main.c lo dice: "El peor caso,
     * '-12 dB (-12 dB)', son exactamente 15 caracteres". 146 px y no 62. */
    { "Atenuador", "-12 dB (-12 dB)", "-12 dB (-12 dB)" },
    { "Brillo", "100 %", "100 %" },
    { "Escala", "-110 / -20", "-110 / -20" },
    { "Autoescala", "Apagada", "Off" },
    { "Zoom", "8x", "8x" },
    { "Paleta", "Clásica verde", "Classic green" },
    { "Estilo", "Contorno", "Outline" },
    { "Traza", "Del color", "Palette" },
    { "Suavizado", "100 %", "100 %" },
    { "Contorno", "3", "3" },
    /* El puente: "Suelto" es mas ancho que "Unido". */
    { "Puente", "Suelto", "Joined" },
    /* En español la mas ancha es "Muy rápida" y en ingles "Very slow": no
     * tienen por que ser la misma fila. */
    { "Cascada", "Muy rápida", "Very slow" },
    { "Analizador", "Apagado", "Off" },
    { "NCO", "Apagado", "Off" },
    { "RDS", "Apagado", "Off" },
    /* "Contrastada", no "Oscura": la columna inglesa ya estaba bien y la
     * española se habia quedado con el primer tema de la lista. */
    { "Tema", "Contrastada", "High contrast" },
    { "Altavoz", "Activo", "Muted" },
    { "Táctil", "Sensible", "Normal" },
    /* Los tres valores son "Activo", "Apagado" y "Sin base"; en ingles
     * "On", "Off" y "No database". El peor es el ingles, que es el que se
     * ve cuando no hay EMISORAS.BIN cargado - o sea el que va a ver
     * mucha gente la primera vez. */
    { "BD en espectro", "Sin base", "No database" },
    { "Muestreo", "96 kHz", "96 kHz" },
    { "Filtro FM", "96 kHz", "96 kHz" },
    { "Reposo", "tocar", "tap" },
    { "Calibrar táctil", "tocar", "tap" },
    { "Calibrar PPM", "tocar", "tap" },
    { "Información", "tocar", "tap" },
    { "Desplazamiento", "1000 Hz", "1000 Hz" },
    { "Baudios", "45.45", "45.45" },
    { "Inversión", "Invertida", "Inverted" },
    { "Tono CW", "1200 Hz", "1200 Hz" },
    /* Ya no lleva "ppm": la celda pone lo pedido y lo medido, "36 / 40". */
    { "Velocidad CW", "36 / 40", "36 / 40" },
    /* La otra rama, que es la larga. */
    { "Autoenganche", "Fijo en el tono", "Fixed on pitch" },
    /* Las cuatro fuerzas Y "Apagado", que es la quinta cosa que puede
     * poner y en español es la mas ancha de todas. */
    { "Notch", "Apagado", "Maximum" },
    /* La mas larga de las cuatro velocidades del fax. */
    { "Velocidad fax", "240 lpm", "240 lpm" },
    { "Hora por radio", "tocar", "tap" }
};


#endif /* PEORES_H */
