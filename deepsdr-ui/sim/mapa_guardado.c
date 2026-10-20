/*
 * EL MAPA GRABADO EN LA FLASH (User/mapa_flash.c).
 *
 * LO QUE AQUI SE MIDE, y por que cada cosa:
 *
 *  1. Que el ritual entero funciona: fichero en el disco -> grabado ->
 *     comprobado -> BORRADO del disco. Lo ultimo es la mitad de lo que se
 *     pidio y es lo que distingue esto de la version de ayer.
 *
 *  2. QUE UNA COPIA A MEDIAS NO DEJA MEDIO MAPA. Se corta la grabacion por
 *     la mitad -como quien tira del cable- y se comprueba que al mirar no
 *     hay mapa ninguno. Esta es la prueba importante: un mapa a medias que
 *     se cargara seria basura en pantalla, y lo que tiene que pasar es que
 *     salga el de serie.
 *
 *  3. Que el fichero NO se borra si la copia no se pudo verificar. Mientras
 *     la copia no valga, el disco tiene la unica copia que existe.
 *
 *  4. Que un BD.BIN que llegue hasta el hueco del mapa hace que NO se grabe
 *     -y que no se borre un solo bloque de los aviones-.
 *
 *  5. Que el boton de borrar borra, y que despues sale el de serie.
 *
 *  6. Y que un byte cambiado en la flash se nota al cargar.
 */
#include "mapa_flash.h"
#include "mapa_bin.h"
#include "zona_alta.h"
#include "spi_flash.h"
#include "formato.h"
#include "w25q_sim.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int fallos;
static void di(const char *q, int bien, const char *extra)
{
    printf("    %-54s %s%s%s\n", q, bien ? "ok" : "*** MAL ***",
           extra ? "  " : "", extra ? extra : "");
    if (!bien) { fallos++; }
}

/* Un .map distinto del de prueba, con otro color de mar y su suma
 * rehecha. Hace falta para la prueba de SUSTITUIR un mapa por otro. */
/* 256 kB: el .map v2 son tres niveles y mide unos 70 kB. Con 64 este
 * banco medía un fichero truncado y decía "copiado, pero no cuadra". */
static uint8_t s_otro[256U * 1024U];

static void haz_otro(const uint8_t *d, uint32_t n)
{
    uint32_t v;

    memcpy(s_otro, d, n);
    s_otro[12] ^= 0x30U;          /* el rojo del mar: otro mapa a la vista */
    v = zona_alta_suma(s_otro, n - 4UL, ZA_SUMA_INICIO);
    s_otro[n - 4UL] = (uint8_t)(v & 0xFFU);
    s_otro[n - 3UL] = (uint8_t)((v >> 8) & 0xFFU);
    s_otro[n - 2UL] = (uint8_t)((v >> 16) & 0xFFU);
    s_otro[n - 1UL] = (uint8_t)((v >> 24) & 0xFFU);
}

/* ------------------------------------------------------------------ */
/* El .map de verdad, el que saca tools/mapa_pack.py.                  */
static uint8_t  s_map[256U * 1024U];
static uint32_t s_map_n;

static int lee_prueba(void)
{
    FILE *f = fopen("datos/prueba.map", "rb");

    if (f == 0) { return 0; }
    s_map_n = (uint32_t)fread(s_map, 1U, sizeof s_map, f);
    fclose(f);
    return (s_map_n > 64U);
}

/* Deja un fichero .MAP en el disco, en bloques seguidos (que es lo que
 * pide el camino de restaurar). */
static int pon_en_disco(const char *n8, const uint8_t *d, uint32_t n)
{
    uint8_t bloque[4096];
    uint32_t addr = 0UL, i, bloques = (n + 4095UL) / 4096UL;
    spi_anade_r_t porque;

    if (!spi_flash_reserva(n8, "MAP", bloques * 4096UL, &addr, &porque)) {
        return 0;
    }
    for (i = 0UL; i < bloques; i++) {
        uint32_t k = n - i * 4096UL;

        if (k > 4096UL) { k = 4096UL; }
        memset(bloque, 0, sizeof bloque);
        memcpy(bloque, &d[i * 4096UL], k);
        spi_flash_write_block_4k(addr + i * 4096UL, bloque);
    }
    return spi_flash_recorta(n8, "MAP", n) ? 1 : 0;
}

/* Graba del tirón, devolviendo el veredicto final. `corta` = parar despues
 * de tantos pasos (0 = no cortar). */
static mapa_flash_r_t graba(const char *n8, uint8_t *pozo, uint32_t cap,
                            uint32_t corta)
{
    mapa_flash_r_t r = mapa_flash_graba_abre(n8);
    uint32_t pasos = 0UL;
    int8_t paso;

    if (r != MAPA_FLASH_OK) { return r; }
    do {
        paso = mapa_flash_graba_paso();
        pasos++;
        if (corta != 0UL && pasos >= corta) { return MAPA_FLASH_COPIA_MAL; }
    } while (paso > 0);
    if (paso != 0) { return MAPA_FLASH_COPIA_MAL; }
    return mapa_flash_graba_cierra(n8, pozo, cap);
}

/* Un directorio de zona alta hecho a mano, con una seccion que llega
 * hasta `fin` (desplazamiento desde ZA_BASE). */
static void pon_directorio(uint32_t fin)
{
    uint8_t b[4096];
    uint32_t v, tam = fin;

    memset(b, 0xFF, sizeof b);
    memset(b, 0, 160U);
    b[0] = 'D'; b[1] = 'S'; b[2] = 'D'; b[3] = 'R';
    b[4] = 1U; b[5] = 1U;
    b[8]  = (uint8_t)(tam & 0xFFU);
    b[9]  = (uint8_t)((tam >> 8) & 0xFFU);
    b[10] = (uint8_t)((tam >> 16) & 0xFFU);
    b[11] = (uint8_t)((tam >> 24) & 0xFFU);
    /* la seccion: tipo aviones, desde 4096, hasta `fin` */
    {
        uint8_t *p = &b[16];
        uint32_t off = 4096UL, sec = fin - 4096UL;

        p[0] = 'I'; p[1] = '2'; p[2] = '4'; p[3] = 'B';
        p[4] = (uint8_t)(off & 0xFFU); p[5] = (uint8_t)((off >> 8) & 0xFFU);
        p[6] = (uint8_t)((off >> 16) & 0xFFU); p[7] = (uint8_t)((off >> 24) & 0xFFU);
        p[8]  = (uint8_t)(sec & 0xFFU); p[9] = (uint8_t)((sec >> 8) & 0xFFU);
        p[10] = (uint8_t)((sec >> 16) & 0xFFU); p[11] = (uint8_t)((sec >> 24) & 0xFFU);
    }
    v = zona_alta_suma(b, 12U, ZA_SUMA_INICIO);
    v = zona_alta_suma(&b[16], 16U, v);
    b[12] = (uint8_t)(v & 0xFFU);
    b[13] = (uint8_t)((v >> 8) & 0xFFU);
    b[14] = (uint8_t)((v >> 16) & 0xFFU);
    b[15] = (uint8_t)((v >> 24) & 0xFFU);
    spi_flash_write_block_4k(za_base(), b);
    (void)zona_alta_abre(spi_flash_read);
}

static uint32_t s_mb = 2UL;          /* el chip con el que se prueba */

static void disco_limpio(void)
{
    w25q_reset();
    w25q_tam(s_mb * 1024UL * 1024UL);
    spi_flash_olvida_capacidad();
    spi_flash_geo_olvida();
    (void)formato_haz();
    mapa_bin_quita();
}

int main(void)
{
    static uint8_t pozo[48U * 1024U];
    uint32_t n = 0UL;

    printf("\nEL MAPA GRABADO EN LA FLASH (User/mapa_flash.c)\n");

    if (!lee_prueba()) {
        printf("  no esta datos/prueba.map - genera con tools/mapa_pack.py\n");
        return 1;
    }

    /* ---------------------------------------------------------------- */
    printf("\n  1. EL RITUAL ENTERO\n");
    disco_limpio();
    di("el .map se deja en el disco", pon_en_disco("27      ", s_map, s_map_n), 0);
    di("y esta", spi_flash_fichero_hay("27      ", "MAP"), 0);
    di("antes de grabar no hay mapa en la flash", !mapa_flash_hay(0), 0);
    {
        mapa_flash_r_t r = graba("27      ", pozo, sizeof pozo, 0UL);

        di("se graba", r == MAPA_FLASH_OK, mapa_flash_porque_txt(r));
    }
    di("ahora SI hay mapa en la flash", mapa_flash_hay(&n) != 0U, 0);
    /*
     * EL LARGO GUARDADO ES EL DEL .map, NO EL DEL FICHERO EN EL DISCO.
     * Aqui el fichero ocupa dos bloques de 4 kB y el directorio dice
     * 8.192; el mapa mide 7.195. Lo que tiene que quedar grabado es lo
     * segundo. Ver el comentario de s_total en mapa_flash_graba_cierra().
     */
    di("y mide lo que mide el MAPA, sin el relleno", n == s_map_n, 0);
    di("Y EL FICHERO SE HA BORRADO DEL DISCO",
       !spi_flash_fichero_hay("27      ", "MAP"), 0);

    /* ---------------------------------------------------------------- */
    printf("\n  2. Y SE CARGA DE LA FLASH, COMO EN EL ARRANQUE\n");
    mapa_bin_quita();
    di("antes de cargar, el de serie", !mapa_bin_hay(), 0);
    {
        mapa_bin_r_t r = mapa_flash_carga(pozo, sizeof pozo);

        di("carga", r == MAPA_BIN_OK, mapa_bin_porque_txt(r));
    }
    di("y trae trazos", mapa_bin_trazos() > 50U, 0);

    /* ---------------------------------------------------------------- */
    printf("\n  3. UNA COPIA A MEDIAS NO DEJA MEDIO MAPA\n");
    disco_limpio();
    (void)pon_en_disco("27      ", s_map, s_map_n);
    {
        mapa_flash_r_t r = graba("27      ", pozo, sizeof pozo, 1UL);

        di("se corta por la mitad", r == MAPA_FLASH_COPIA_MAL, 0);
    }
    di("NO queda mapa en la flash", !mapa_flash_hay(0), 0);
    di("y el fichero sigue en el disco",
       spi_flash_fichero_hay("27      ", "MAP"), 0);
    mapa_bin_quita();
    di("cargar da 'el de serie'",
       mapa_flash_carga(pozo, sizeof pozo) == MAPA_BIN_NADA, 0);

    /* ---------------------------------------------------------------- */
    printf("\n  3b. Y CAMBIAR UN MAPA POR OTRO, CORTANDOLO, TAMPOCO\n");
    /*
     * ESTE ES EL CASO QUE DE VERDAD DUELE y el que faltaba: ya hay un mapa
     * bueno guardado, el usuario copia otro, y la copia se corta. La
     * cabecera vieja seguiria apuntando a un cuerpo que ya esta medio
     * machacado por el nuevo: un mapa que se carga y que es basura.
     *
     * Por eso lo PRIMERO que hace graba_abre() es borrar la cabecera. Si
     * se quita esa linea, este bloque se pone en rojo y el anterior no:
     * el anterior arranca de un chip recien reseteado y ahi no hay
     * cabecera vieja que estorbe.
     */
    disco_limpio();
    (void)pon_en_disco("27      ", s_map, s_map_n);
    di("primero entra uno bueno",
       graba("27      ", pozo, sizeof pozo, 0UL) == MAPA_FLASH_OK, 0);
    di("y esta guardado", mapa_flash_hay(0) != 0U, 0);
    haz_otro(s_map, s_map_n);
    di("se deja OTRO en el disco", pon_en_disco("28      ", s_otro, s_map_n), 0);
    {
        mapa_flash_r_t r = graba("28      ", pozo, sizeof pozo, 1UL);

        di("y la copia se corta", r == MAPA_FLASH_COPIA_MAL, 0);
    }
    di("NO queda el viejo ni medio nuevo", !mapa_flash_hay(0), 0);
    mapa_bin_quita();
    di("cargar da 'el de serie'",
       mapa_flash_carga(pozo, sizeof pozo) == MAPA_BIN_NADA, 0);
    di("y el fichero nuevo sigue en el disco",
       spi_flash_fichero_hay("28      ", "MAP"), 0);

    /* ---------------------------------------------------------------- */
    printf("\n  4. UN .map ROTO NO BORRA EL FICHERO DEL DISCO\n");
    disco_limpio();
    {
        /* 64 kB y no 8.192: el .map crecio a 9.487 al arreglar el criterio del
         * empaquetador, y este memcpy se salio del buffer. Un banco que
         * supone el tamano de lo que mide se rompe en cuanto lo medido
         * crece. */
        static uint8_t roto[256U * 1024U];

        memcpy(roto, s_map, s_map_n);
        roto[200] ^= 0xFFU;            /* dentro del cuerpo: la suma falla */
        (void)pon_en_disco("27      ", roto, s_map_n);
        {
            mapa_flash_r_t r = graba("27      ", pozo, sizeof pozo, 0UL);

            di("no se da por bueno", r == MAPA_FLASH_SUMA_MAL,
               mapa_flash_porque_txt(r));
        }
        di("EL FICHERO SIGUE EN EL DISCO",
           spi_flash_fichero_hay("27      ", "MAP"), 0);
        di("y no hay mapa guardado", !mapa_flash_hay(0), 0);
    }

    /* ---------------------------------------------------------------- */
    printf("\n  5. SI LA BASE DE DATOS OCUPA EL SITIO, NO SE GRABA\n");
    disco_limpio();
    (void)pon_en_disco("27      ", s_map, s_map_n);
    pon_directorio((za_tope() - za_base()) - 1024UL);   /* llega al techo */
    di("el directorio estorba", zona_alta_estorba() != 0U, 0);
    {
        mapa_flash_r_t r = mapa_flash_graba_abre("27      ");

        di("se niega", r == MAPA_FLASH_ESTORBA, mapa_flash_porque_txt(r));
    }
    di("y el fichero sigue en el disco",
       spi_flash_fichero_hay("27      ", "MAP"), 0);

    printf("\n     y con una base de datos normal NO estorba\n");
    pon_directorio(400UL * 1024UL);
    di("no estorba", zona_alta_estorba() == 0U, 0);
    di("y se graba", graba("27      ", pozo, sizeof pozo, 0UL) == MAPA_FLASH_OK, 0);

    /* ---------------------------------------------------------------- */
    printf("\n  6. EL BOTON DE BORRAR\n");
    di("hay mapa que borrar", mapa_flash_hay(0) != 0U, 0);
    di("se borra", mapa_flash_borra() != 0U, 0);
    di("y ya no hay", !mapa_flash_hay(0), 0);
    mapa_bin_quita();
    di("cargar da 'el de serie'",
       mapa_flash_carga(pozo, sizeof pozo) == MAPA_BIN_NADA, 0);
    di("borrar otra vez no hace nada", mapa_flash_borra() == 0U, 0);

    /* ---------------------------------------------------------------- */
    printf("\n  7. UN BYTE CAMBIADO EN LA FLASH SE NOTA\n");
    disco_limpio();
    (void)pon_en_disco("27      ", s_map, s_map_n);
    di("se graba", graba("27      ", pozo, sizeof pozo, 0UL) == MAPA_FLASH_OK, 0);
    {
        /*
         * Se baja un bit del cuerpo. Programar solo sabe bajar bits, asi
         * que esto es un cambio que la flash SI puede sufrir de verdad: no
         * hace falta borrar el bloque para fabricarlo.
         */
        uint8_t  b = 0U;
        uint32_t donde = za_mapa() + 4096UL + 100UL;

        /*
         * SE BUSCA UN BYTE QUE TENGA ALGUN BIT A UNO. Programar solo sabe
         * BAJAR bits: pedirle que baje uno que ya esta abajo no escribe
         * nada y la flash se queda igual. La primera version de este caso
         * hacia justo eso y "pasaba" sin haber estropeado nada - un banco
         * que no rompe lo que dice romper no comprueba nada-.
         */
        while (donde < za_tope()) {
            spi_flash_read(donde, &b, 1U);
            if (b != 0U) { break; }
            donde++;
        }
        di("hay un byte que se puede estropear", b != 0U, 0);
        b &= (uint8_t)(b - 1U);        /* le baja el bit mas bajo que tenia */
        spi_flash_page_program(donde, &b, 1U);
    }
    mapa_bin_quita();
    {
        mapa_bin_r_t r = mapa_flash_carga(pozo, sizeof pozo);

        di("la carga lo caza", r == MAPA_BIN_SUMA_MAL, mapa_bin_porque_txt(r));
    }
    di("y no queda mapa puesto", !mapa_bin_hay(), 0);

    /* ---------------------------------------------------------------- */
    printf("\n  8. Y TODO OTRA VEZ CON EL CHIP DE 8 MB\n");
    /*
     * EL BANCO SOLO HABIA PROBADO CON 2 MB, Y ESO ES MEDIR UNA SOLA
     * GEOMETRIA. La radio del usuario lleva un W25Q64: la base de la zona
     * alta, el hueco del mapa y el disco caen en direcciones distintas, y
     * todo lo de aqui se calcula a partir de la capacidad.
     */
    s_mb = 8UL;
    disco_limpio();
    printf("     za_base 0x%06X  za_mapa 0x%06X  za_tope 0x%06X\n",
           (unsigned)za_base(), (unsigned)za_mapa(), (unsigned)za_tope());
    di("el hueco del mapa cae dentro de la zona alta",
       za_mapa() > za_base() && za_mapa() < za_tope(), 0);
    di("y en bloque de borrado", (za_mapa() & 0xFFFUL) == 0UL, 0);
    di("el .map se deja en el disco", pon_en_disco("27      ", s_map, s_map_n), 0);
    {
        mapa_flash_r_t r = graba("27      ", pozo, sizeof pozo, 0UL);

        di("se graba", r == MAPA_FLASH_OK, mapa_flash_porque_txt(r));
    }
    di("hay mapa en la flash", mapa_flash_hay(0) != 0U, 0);
    di("y el fichero se ha borrado", !spi_flash_fichero_hay("27      ", "MAP"), 0);
    mapa_bin_quita();
    {
        mapa_bin_r_t r = mapa_flash_carga(pozo, sizeof pozo);

        di("y se carga al arrancar", r == MAPA_BIN_OK, mapa_bin_porque_txt(r));
    }
    di("con sus trazos", mapa_bin_trazos() > 50U, 0);
    s_mb = 2UL;

    printf(fallos ? "\n  HAY FALLOS: %d\n\n" : "\n  TODO CORRECTO\n\n", fallos);
    return fallos ? 1 : 0;
}
