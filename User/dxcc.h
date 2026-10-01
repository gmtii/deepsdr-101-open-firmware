#ifndef DXCC_H
#define DXCC_H

/*
 * DE QUE PAIS ES UN INDICATIVO - 25/09/2026.
 *
 * *** Por el dueno del proyecto: "el wsjtx te indica el pais de cada cq". ***
 *
 * LO QUE ESTO NO ES, dicho antes que nada: no es la lista DXCC. La de verdad
 * (cty.dat) son unas 340 entidades con cientos de rangos de prefijos y
 * excepciones por indicativo, y ocupa del orden de medio megabyte. En esta
 * radio quedan NUEVE kilobytes de flash. No cabe, y fingir que cabe seria
 * peor que no tenerla.
 *
 * Lo que es: los prefijos que de verdad se oyen desde Espana en HF, con el
 * nombre corto del pais. Acierta en la inmensa mayoria de lo que aparece en
 * una banda de FT8 y falla -diciendo que no sabe- en lo raro. Un hueco es
 * una respuesta honrada; un pais equivocado, no.
 *
 * COMO BUSCA: por prefijo mas largo primero. "EA8" tiene que ganarle a "EA",
 * o todas las Canarias saldrian como Espana peninsular.
 *
 * LO QUE NO MIRA:
 *  - Los sufijos de portable. "EA4ABC/P" sigue siendo Espana, y eso sale
 *    bien porque solo se mira el principio; pero "DL/EA4ABC" -prefijo de
 *    pais delante- saldria como Alemania, que es lo que el operador esta
 *    diciendo, asi que tambien vale.
 *  - Las excepciones por indicativo concreto, que en la lista de verdad son
 *    cientos.
 */

/* El pais de `indicativo`, o 0 si el prefijo no esta en la tabla. La cadena
 * devuelta vive en flash y no hay que liberarla. */
const char *dxcc_pais(const char *indicativo);

/* Cuantos prefijos conoce. Lo usa la ventana de informacion y el banco. */
unsigned dxcc_cuantos(void);

/*
 * El prefijo de la fila `i`, o 0 si se pasa. Existe SOLO para los bancos:
 * con esto sim/dxcc_test.c puede recorrer la tabla entera y medir TODOS los
 * nombres en los dos idiomas, en vez de llevar una lista de los mas largos
 * escrita a mano - que es la clase de lista que se queda vieja el dia que se
 * anade un pais y nadie se entera.
 */
const char *dxcc_prefijo(unsigned i);

#endif /* DXCC_H */
