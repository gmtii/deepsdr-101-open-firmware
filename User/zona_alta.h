#ifndef ZONA_ALTA_H
#define ZONA_ALTA_H

#include <stdint.h>

/*
 * EL DIRECTORIO DE LA ZONA ALTA - 28/09/2026.
 *
 * Pedido por el usuario.
 *
 * La zona alta de la flash SPI -medio megabyte que el modo USB no enseña-
 * la comparten dos bases de datos: los modelos de avion y las emisoras de
 * onda corta. Hasta hoy eran dos ficheros, cada uno con su direccion fija:
 *
 *     0x101000 .. 0x140000   aviones
 *     0x140000 .. 0x180000   emisoras
 *
 * Y ese 0x140000 era un numero a ojo. No estaba mal elegido -206 kB de
 * emisoras y el resto para los aviones- pero era una decision del FIRMWARE
 * sobre cuanto puede ocupar cada uno, y por tanto una que solo se cambia
 * recompilando. La primera vez que se eligio ya salio corta: el fichero de
 * aviones del usuario media 515 kB y hubo que recortarlo a 258.
 *
 * AHORA HAY UN SOLO FICHERO -BD.BIN- CON UN DIRECTORIO DELANTE, y el
 * reparto lo decide el empaquetador del PC mirando lo que miden las dos de
 * verdad. El firmware no opina: lee donde le dicen que esta cada cosa.
 *
 *     0x101000  el directorio, 4.096 bytes (un bloque de borrado)
 *     0x102000  la primera seccion
 *        ...    las demas, cada una en su bloque
 *
 * LO QUE ESTO ARREGLA, ademas de grabar un fichero en vez de dos:
 *
 *   - el reparto se ajusta solo. Si las emisoras adelgazan, los aviones
 *     crecen, sin tocar el firmware;
 *   - no puede haber solape. Antes habia dos cargadores escribiendo en la
 *     misma zona y uno tenia que mirar donde acababa el otro; ahora hay uno;
 *   - y se puede añadir una tercera base sin cambiar ninguna direccion.
 *
 * COMPATIBILIDAD: si en 0x101000 no hay directorio, se supone la
 * distribucion vieja -aviones en 0x101000, emisoras en 0x140000- para que
 * una radio que ya tenia las dos cargadas siga funcionando despues de
 * actualizar. Ver zona_alta_donde().
 */

/*
 * DONDE ESTA LA ZONA ALTA: EL ULTIMO MEGABYTE DEL CHIP, SEA EL QUE SEA.
 * 02/10/2026.
 *
 * Pedido por el usuario.
 *
 * Esto eran dos constantes, 0x101000 y 0x180000 (y luego 0x200000). Ahora
 * son dos cuentas sobre spi_flash_capacidad():
 *
 *     za_tope() = capacidad
 *     za_base() = capacidad - ZA_BYTES_REGION + ZA_HUECO
 *     el disco USB = capacidad - ZA_BYTES_REGION     (ver disco.c)
 *
 * Con el W25Q16 de fabrica sale EXACTAMENTE lo de siempre -base 0x101000,
 * tope 0x200000, disco de 1 MB-, asi que esto no cambia nada en la radio
 * de hoy. Con un W25Q64 sale base 0x701000, tope 0x800000 y un disco de
 * 7 MB, sin tocar una linea.
 *
 * LA REGION DE DATOS MIDE SIEMPRE LO MISMO, 1.044.480 bytes utiles, ponga
 * el chip que se ponga. Eso es a proposito y tiene dos consecuencias:
 *
 *   - un BD.BIN vale en cualquier chip. El directorio guarda
 *     DESPLAZAMIENTOS desde za_base(), no direcciones absolutas (ver el
 *     mapa de bytes en zona_alta.c), asi que el mismo fichero se carga
 *     igual en un chip de 2 MB que en uno de 16;
 *   - y todo lo que de un chip mas grande va al DISCO del usuario, no a
 *     las bases. Si algun dia hace falta lo contrario, se cambia
 *     ZA_BYTES_REGION y ya: es un solo numero y nadie mas opina.
 *
 * EL HUECO DE 4 kB entre el final del disco y el principio de los datos se
 * conserva tal cual. Es un bloque de borrado de margen: lo ultimo que
 * escribe el volumen y lo primero que escribe la zona alta no comparten
 * bloque, asi que ninguno de los dos puede borrar al otro por el camino.
 */
#define ZA_BYTES_REGION  0x100000UL   /* el ultimo mega, disco aparte */
#define ZA_HUECO         0x1000UL     /* un bloque de borrado de margen */

/*
 * EL HUECO DEL MAPA, ARRIBA DEL TODO - 12/10/2026.
 *
 * Pedido por el usuario.
 *
 * POR QUE NO ES UNA SECCION MAS DEL DIRECTORIO, que es lo primero que uno
 * piensa. Porque el directorio lo escribe ENTERO el empaquetador del PC
 * cada vez que se carga un BD.BIN: el firmware no anade secciones, las
 * lee. Un mapa metido ahi desapareceria la siguiente vez que cargaras la
 * base de datos de aviones - y no con un aviso, sino en silencio, porque
 * el directorio nuevo sencillamente no lo mencionaria y ese sitio pasaria
 * a ser de los aviones-. Cargar una cosa y perder otra sin enterarte es de
 * las peores maneras de fallar que hay.
 *
 * Asi que el mapa vive FUERA del directorio, pegado al techo de la region,
 * y el directorio no llega hasta alli: ZA_TOPE_DATOS lo corta antes.
 *
 * POR QUE 192 kB Y NO LOS 8 QUE HACEN FALTA HOY. Porque hoy el mapa lleva
 * un nivel de detalle y manana lleva cuatro, y los cuatro miden 177.456
 * bytes medidos (11.454 + 19.636 + 45.054 + 101.312). Reservar 8 kB ahora
 * obligaria a mover el hueco mas adelante, y mover un hueco en la flash es
 * una migracion: hay que avisar, hay que regrabar y hay que acordarse. Una
 * migracion es mejor que dos, asi que se reserva ya lo que va a medir.
 *
 * LO QUE ESTO LE CUESTA A BD.BIN: su techo baja de 1.044.480 a 847.872
 * bytes. Las dos bases del usuario andan por los 460 kB juntas, asi que
 * sobra - pero el empaquetador del PC lleva el numero nuevo (ver TECHO en
 * tools/datos_pack.py) y hay que regenerar BD.BIN una vez.
 *
 * Y SI EL BD.BIN QUE YA ESTA EN LA RADIO LLEGA HASTA AQUI, el mapa NO se
 * graba y se dice por que. Escribir encima de los aviones para meter un
 * mapa seria un intercambio malisimo, y uno que ademas no se puede
 * deshacer. Ver mapa_flash_estorba().
 */
#define ZA_MAPA_BYTES    0x30000UL    /* 196.608: los cuatro niveles y su cabecera */

/* Donde empieza el hueco del mapa, y por tanto donde acaban los datos. */
uint32_t za_mapa(void);
#define ZA_MAPA        za_mapa()
#define ZA_TOPE_DATOS  za_mapa()

uint32_t za_base(void);
uint32_t za_tope(void);

#define ZA_BASE   za_base()
#define ZA_TOPE   za_tope()
#define ZA_CAB    4096UL          /* lo que ocupa el directorio */
/*
 * CUANTAS SECCIONES CABEN. Cuatro, no ocho: cada una cuesta 16 bytes de
 * SRAM en este modulo y aqui quedan 164 libres. Hoy son dos -aviones y
 * emisoras- y con cuatro hay sitio para dos mas sin tocar nada. El
 * directorio del fichero reserva 4.096 bytes, asi que subirlo el dia que
 * haga falta es cambiar este numero y recompilar, no cambiar el formato.
 */
#define ZA_SEC_MAX 4U

/* Los tipos de seccion, como cuatro caracteres en un entero. Son las
 * mismas cuatro letras con las que empieza cada fichero, asi que no hay
 * una segunda lista que cuadrar. */
#define ZA_TIPO_AVIONES  0x42343249UL   /* 'I','2','4','B' */
#define ZA_TIPO_EMISORAS 0x53494D45UL   /* 'E','M','I','S' */

/*
 * Lee el directorio. Devuelve 1 si hay uno bueno en 0x101000.
 *
 * `lee` es quien sabe leer la flash - en la radio, spi_flash_read; en el
 * banco, un fichero-. Cuesta una lectura de 256 bytes, asi que se puede
 * llamar en el arranque sin pensarlo.
 */
uint8_t zona_alta_abre(void (*lee)(uint32_t addr, uint8_t *buf, uint32_t len));

/*
 * DONDE ESTA UNA SECCION, en direccion absoluta de la flash.
 *
 * Si hay directorio, lo que diga el directorio. Si NO lo hay, la
 * distribucion vieja: aviones en 0x101000 y emisoras en 0x140000. Eso es
 * lo que permite actualizar el firmware sin volver a cargar los datos.
 *
 * Devuelve 0 si esa seccion no esta.
 */
uint8_t zona_alta_donde(uint32_t tipo, uint32_t *addr, uint32_t *tam);

/* Cuantas secciones tiene el directorio (0 si no hay). Para la pantalla de
 * informacion. */
uint8_t zona_alta_secciones(void);

/* La seccion `i`, para recorrerlas. Devuelve 0 si no existe. */
uint8_t zona_alta_seccion(uint8_t i, uint32_t *tipo, uint32_t *addr,
                          uint32_t *tam);

/*
 * COMPRUEBA LAS SUMAS DE TODAS LAS SECCIONES leyendo la flash entera.
 *
 * Son casi medio megabyte por un bus lento: del orden de dos segundos. NO
 * se llama al arrancar. Se llama UNA vez, justo despues de copiar el
 * fichero, que es cuando de verdad puede haberse copiado mal - acaba de
 * pasar por el USB, por el volumen FAT y por cien bloques de borrado y
 * grabacion-.
 */
uint8_t zona_alta_verifica(void);

/*
 * LA MISMA, PERO AVISANDO DE POR DONDE VA - 28/09/2026.
 *
 * Pedido por el usuario.
 *
 * No estaba colgada: estaba comprobando las sumas. Pero la pantalla se
 * quedaba en el 100 % de "Copiando" con el cartel de "No apagues la radio
 * hasta que termine" debajo, y eso es exactamente el momento en que uno
 * tira del cable. Una barra que no se mueve no dice "espera": dice "esto
 * se ha colgado".
 *
 * El comentario de arriba decia "del orden de dos segundos" y era una
 * cuenta a ojo. Medido con el chip simulado a nivel de pin (ver
 * sim/fat_troceado.c): **medio megabyte en trozos de 256 bytes son 8,5
 * segundos** por este bus. Ocho segundos de barra quieta.
 *
 * `avisa` se llama cada trozo con lo que lleva y el total. A cero, esto
 * es exactamente zona_alta_verifica().
 */
uint8_t zona_alta_verifica_p(void (*avisa)(uint32_t hechos, uint32_t total));

/*
 * La suma que usan el directorio y las secciones: FNV-1a de 32 bits. No es
 * un CRC y no pretende serlo - aqui solo hay que notar que algo se ha
 * copiado mal o que la zona estaba a medio escribir, no defenderse de
 * nadie-. Lo que si tiene, y una suma llana no, es que el ORDEN importa.
 */
/*
 * Si alguna seccion cargada llega al hueco del mapa (ZA_MAPA). 1 = grabar
 * un mapa pisaria datos en uso, asi que no se graba y se dice por que.
 */
uint8_t zona_alta_estorba(void);

uint32_t zona_alta_suma(const uint8_t *b, uint32_t n, uint32_t v);
#define ZA_SUMA_INICIO 0x811C9DC5UL

#endif /* ZONA_ALTA_H */
