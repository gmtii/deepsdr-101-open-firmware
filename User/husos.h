#ifndef HUSOS_H_INCLUDED
#define HUSOS_H_INCLUDED

#include <stdint.h>

/*
 * ======================================================================
 * LA TABLA DE CIUDADES, Y LAS CINCO REGLAS DE HORARIO DE VERANO
 * ======================================================================
 *
 * POR QUE UNA TABLA Y NO EL LOCALIZADOR A SECAS
 * ---------------------------------------------
 * Del localizador sale la LONGITUD, y de la longitud sale el huso SOLAR -15
 * grados por hora-. Eso no es el huso horario de ningun sitio: España entera
 * esta en longitudes de Londres y va con la hora de Berlin, China tiene un
 * solo huso para 60 grados de ancho, y la India esta a media hora. El huso
 * es una decision politica y no se calcula; hay que mirarlo.
 *
 * Lo que SI hace el localizador es ORDENAR: la lista sale con lo mas cercano
 * arriba, asi que el que esta en Hungria no tiene que buscar Budapest entre
 * cincuenta nombres, lo tiene en la primera fila. Ver husos_ordena().
 *
 * LO QUE SE VE EN CADA FILA, Y POR QUE SON DOS NUMEROS
 * ---------------------------------------------------
 * Cada fila enseña el huso BASE ("UTC+1") y el que sale HOY ("ahora +2").
 * Son dos porque una regla de horario de verano caduca: Mexico lo quito en
 * 2022, Brasil en 2019, Turquia en 2016. Si algun dia una de estas filas se
 * queda atras, lo que se vera es que "ahora" no cuadra con el reloj de la
 * pared, y eso es una pista. Una tabla que solo enseñara el resultado final
 * mentiria sin dejar rastro.
 *
 * ESTO NO ES zoneinfo Y NO PRETENDE SERLO. Son las ciudades grandes, con la
 * regla que tenian al escribir esto. Lo que manda, siempre, es lo que diga
 * una emisora: el RDS de la FM local trae el desplazamiento de verdad y
 * hora_huso_mide() lo pone por encima de esto. Ver hora_comun.h.
 */

/* Las reglas. 0 es "aqui no se cambia la hora", que es el caso de mas de la
 * mitad del planeta y de casi toda la mitad de la tabla. */
#define HUSO_DST_NO   0U
#define HUSO_DST_EU   1U   /* ultimo domingo de marzo .. ultimo de octubre */
#define HUSO_DST_USA  2U   /* 2.o domingo de marzo .. 1.o de noviembre */
#define HUSO_DST_AU   3U   /* 1.o de octubre .. 1.o de abril (sur) */
#define HUSO_DST_NZ   4U   /* ultimo de septiembre .. 1.o de abril */
#define HUSO_DST_CL   5U   /* 1.o de septiembre .. 1.o de abril */
#define HUSO_DST_N    6U

typedef struct {
    char    nombre[14];
    int16_t base;      /* minutos sobre UTC en horario de INVIERNO */
    uint8_t regla;     /* HUSO_DST_* */
    int16_t lat_d;     /* latitud en DECIMAS de grado, + norte */
    int16_t lon_d;     /* longitud en DECIMAS de grado, + este */
} huso_ciudad_t;

uint8_t              husos_n(void);
const huso_ciudad_t *husos_fila(uint8_t i);

/*
 * 1 si en esa regla esta vigente el horario de verano en esa fecha.
 *
 * LA FECHA VA EN UTC, igual que en hora_verano_espanna(). `base` es el huso
 * de INVIERNO del sitio, y hace falta porque tres de las cinco reglas estan
 * escritas en hora local y hay que convertirlas: sin eso, en Sidney el
 * cambio se iba catorce horas. Ver el comentario de k_reglas[] en husos.c,
 * que cuenta como aparecio.
 */
uint8_t husos_verano(uint8_t regla, int16_t base, uint8_t dia, uint8_t mes,
                     uint8_t anno2, uint8_t hora_utc);

/* El huso que le toca HOY a esa ciudad: base, mas 60 si su regla esta
 * vigente. Es lo que se le pasa a hora_huso_pon() al elegirla. */
int16_t husos_ahora(uint8_t i, uint8_t dia, uint8_t mes, uint8_t anno2,
                    uint8_t hora_utc);

/*
 * Ordena las filas por cercania a un punto y deja el resultado en `orden`,
 * que tiene que tener husos_n() huecos. lat_e4/lon_e4 son diezmilesimas de
 * grado, que es lo que da ft8_decoder_grid_e4().
 *
 * Se ordena por la CUERDA entre los dos puntos de la esfera y no por la
 * diferencia de latitud y longitud: restar longitudes pone Auckland y las
 * Fiyi en extremos opuestos de la lista por culpa del antimeridiano, y cerca
 * de los polos un grado de longitud no mide lo mismo que uno de latitud. La
 * cuerda crece igual que la distancia por la superficie, que es lo unico que
 * hace falta para ordenar.
 */
void husos_ordena(int32_t lat_e4, int32_t lon_e4, uint8_t *orden);

#endif /* HUSOS_H_INCLUDED */
