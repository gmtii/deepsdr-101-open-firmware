#ifndef RDS_PI_H_INCLUDED
#define RDS_PI_H_INCLUDED

#include <stdint.h>

/*
 * ============================================================================
 * DE CODIGO DE PROGRAMA A NOMBRE DE CADENA
 * ============================================================================
 * 24/09/2026.
 *
 * QUE ES EL CODIGO. Cada grupo de RDS lleva 16 bits que identifican el
 * PROGRAMA, no el transmisor: todos los repetidores de una cadena emiten el
 * mismo en toda Espana. Eso es lo que permite que la radio del coche salte
 * sola a otra frecuencia de la misma emisora cuando te alejas.
 *
 * Y no es un numero suelto, va en cuatro trozos de cuatro bits:
 *
 *     E     2      C A
 *     |     |      |
 *     |     |      referencia de programa: lo que distingue una cadena de
 *     |     |      otra dentro del mismo pais y la misma cobertura
 *     |     cobertura: 0 local, 1 internacional, 2 nacional,
 *     |     3 supra-regional, del 4 al F las doce regionales
 *     pais
 *
 * OJO CON EL PAIS. Ese primer digito NO identifica un pais de forma unica en
 * el mundo: hay solo dieciseis valores para doscientos paises, asi que se
 * repiten (la F, por ejemplo, la comparten Francia, Noruega, Bielorrusia y
 * Egipto). Para desambiguar existe un codigo de pais extendido que viaja en
 * otro grupo distinto. Aqui no se decodifica ese grupo, asi que esta tabla
 * SOLO vale para Espana: si algun dia esta radio escucha FM fuera de aqui,
 * puede enseñar el nombre equivocado de una cadena que no es. Esta escrito
 * para que no sorprenda.
 *
 * DE DONDE SALEN ESTOS NUMEROS. Los cinco primeros, de la lista de la
 * Wikipedia en espanol del Radio Data System. El de COPE, de escucharlo en
 * esta misma radio el 24/09/2026 en 106,3 MHz: el nombre de emisora decia
 * COPE y el radiotexto "Cope Madrid 106.3", asi que la pareja esta
 * confirmada en el aire, que es la mejor fuente que hay. El de RNE
 * Clasica (E212), por el dueño del proyecto el 24/09/2026; encaja con sus
 * vecinos, que son RNE 1 en E211 y RNE 3 en E213 - las tres cadenas de la
 * misma casa, seguidas en la referencia de programa, que es como se
 * reparten estos numeros.
 *
 * COMO SE AMPLIA. Una linea. Y si el codigo que llega no esta en la lista,
 * la radio enseña el codigo en crudo en vez de callarse: asi se puede anotar
 * y anadir. Esa es, de hecho, la forma en que se hacen estas listas.
 */

/* El nombre de la cadena, o 0 si ese codigo no esta en la lista. */
const char *rds_pi_nombre(uint16_t pi);

/* Cuantas hay, y la fila i. Para el banco del simulador, que comprueba que
 * no haya codigos repetidos ni nombres demasiado largos para el hueco. */
uint16_t rds_pi_cuantas(void);
uint16_t rds_pi_codigo(uint16_t i);
const char *rds_pi_nombre_i(uint16_t i);

#endif /* RDS_PI_H_INCLUDED */
