/*
 * LA BASE DE MODELOS: SU LECTOR, LEYENDO MI FICHERO.
 *
 * POR QUE EXISTE (25/09/2026). Porque este es el unico fallo de toda la
 * sesion que NO se nota. Un decodificador roto saca basura y se ve; una base
 * de datos con un desplazamiento mal saca un modelo PLAUSIBLE Y EQUIVOCADO,
 * y en la pantalla no hay forma de distinguirlo.
 *
 * Aqui se juntan las dos mitades que tienen que encajar:
 *   - tools/icao24.py, que escribe el fichero (mio);
 *   - User/icao24_db.c, que lo lee (del proyecto padre).
 * Si el formato que yo escribo no es el que el espera, salta aqui.
 *
 * LO UNICO FINGIDO es el chip: spi_flash_read() lee de un fichero en vez de
 * por el bus SPI. Todo lo demas -la busqueda binaria, la comprobacion de la
 * cabecera, el troceado del nombre- es su codigo tal cual.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "icao24_db.h"
#include "spi_flash.h"

#define I24_BASE 0x101000U

static uint8_t *s_chip;
static uint32_t s_chip_len;

/* --- el chip, fingido --- */
void spi_flash_read(uint32_t addr, uint8_t *buf, uint32_t len)
{
    uint32_t i;
    for (i = 0U; i < len; i++) {
        buf[i] = (addr + i < s_chip_len) ? s_chip[addr + i] : 0xFFU;
    }
}
spi_flash_async_status_t spi_flash_async_save_poll(void) { return SPI_FLASH_ASYNC_IDLE; }
/* Solo mirar, sin avanzar la maquina de estados. Ver spi_flash.c. */
uint8_t spi_flash_async_save_en_vuelo(void) { return 0U; }

/* Desde el 02/10/2026 zona_alta.c saca de aqui donde empieza la zona alta.
 * Se contesta 2 MB, el chip de la radio, que es lo que hace que este banco
 * siga mirando las direcciones de siempre. Quien comprueba que la cuenta
 * se adapta a otros chips es sim/capacidad.c. */
uint32_t spi_flash_capacidad(void)   { return 2UL * 1024UL * 1024UL; }
void     spi_flash_olvida_capacidad(void) { }

static int fallos;
static void exige(int cond, const char *que)
{
    printf("    %-44s %s\n", que, cond ? "bien" : "MAL");
    if (!cond) { fallos++; }
}

static void mira(uint32_t icao, const char *espera)
{
    char m[32];
    bool hay = icao24_db_lookup(icao, m, sizeof m);

    printf("    %06X -> %-22s", (unsigned)icao, hay ? m : "(no esta)");
    if (espera == NULL) {
        printf("  %s\n", !hay ? "bien" : "MAL");
        if (hay) { fallos++; }
    } else {
        int ok = hay && (strncmp(m, espera, strlen(espera)) == 0);
        printf("  %s\n", ok ? "bien" : "MAL");
        if (!ok) { fallos++; }
    }
}

int main(int argc, char **argv)
{
    const char *bin = (argc > 1) ? argv[1] : "out/ICAO24.BIN";
    FILE *f = fopen(bin, "rb");
    long n;

    if (!f) { printf("no se pudo abrir %s\n", bin); return 1; }
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);

    /* El "chip": la zona alta empieza en I24_BASE, asi que el fichero se
     * coloca ahi dentro de una imagen mas grande. Todo lo de antes queda a
     * 0xFF, como una flash borrada de verdad. */
    s_chip_len = I24_BASE + (uint32_t)n;
    s_chip = malloc(s_chip_len);
    memset(s_chip, 0xFF, s_chip_len);
    if (fread(s_chip + I24_BASE, 1, (size_t)n, f) != (size_t)n) { return 1; }
    fclose(f);

    printf("  base de aviones: %ld bytes en 0x%X\n\n", n, I24_BASE);

    /* Los cuatro del CSV de prueba - ver tools/icao24_prueba.csv. Se piden
     * el primero, el ultimo y dos de en medio: la biseccion falla de formas
     * distintas segun donde caiga. */
    mira(0x3C4B26, "A320");
    mira(0x4CA7C6, "B738");
    mira(0x39D2B1, "A21N");
    mira(0x7C1234, "B77W");

    /* EL QUE COSTO UN FICHERO ENTERO (25/09/2026). 71C735 -un 777-300ER de
     * Korean, cazado en el aire desde aqui- viene en el CSV de OpenSky con
     * la columna de clase VACIA, como uno de cada seis. Filtrando por esa
     * columna se quedaba fuera, y el fichero salia "bien": 72.000 aviones,
     * 464 kB, y la mitad de los que la radio ve de verdad sin modelo.
     *
     * Ahora la clase sirve para aprender que codigos son de reactor, no
     * para decidir fila a fila: B77W esta marcado L2J en 7C1234, asi que
     * 71C735 entra aunque no lo diga. Si alguien vuelve al filtro directo,
     * esta linea se pone roja. */
    mira(0x71C735, "B77W");

    /* Y el reves, que es la otra mitad del trato: A7B8C9 tambien trae la
     * clase vacia, pero su tipo -C172- solo aparece como L1P en todo el
     * fichero. "Sin clase" no puede significar "pasa". */
    mira(0xA7B8C9, NULL);

    /* LOS PUESTOS A MANO. 39D2B2 no sale en ningun CSV de OpenSky -ni en el
     * de 2025 ni en el de 2021-, y la radio lo caza. Va en
     * tools/icao24_mios.csv y tiene que llegar hasta aqui: entra sin pasar
     * por el filtro de clases y sin caerse en el reparto. */
    mira(0x39D2B2, "A359");

    /* Y uno que NO esta: tiene que decir que no, no devolver el de al lado.
     * Es la comprobacion que caza una biseccion con el limite mal puesto. */
    mira(0x000001, NULL);
    mira(0xFFFFFE, NULL);

    printf("\n");
    /*
     * EL VEREDICTO MIRA EL CONTADOR. Aqui ponia `exige(1, ...)`: las
     * llamadas a mira() de arriba si suman a `fallos`, pero este renglon
     * decia "bien" aunque hubieran fallado todas. Misma forma que el de
     * sim/ncotest.c y que los dos que ya estaban cazados en jtty_aire.c y
     * mapa_pant.c. Ver el comentario de ncotest.
     */
    exige(fallos == 0, "su lector entiende el fichero de tools/icao24.py");

    printf("\n%s (%d fallos)\n", fallos ? "HAY FALLOS" : "TODO CORRECTO", fallos);
    return fallos ? 1 : 0;
}
