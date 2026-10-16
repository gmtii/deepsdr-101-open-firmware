/*
 * EL DIRECTORIO DE LA ZONA ALTA, CONTRA UN BD.BIN DE VERDAD.
 *
 * Pedido por el usuario.
 *
 * Este banco abre el BD.BIN que sale de tools/datos_pack.py -con las
 * dos bases de verdad dentro- y le pregunta a User/zona_alta.c, el mismo
 * que corre en la radio.
 *
 * LO QUE COMPRUEBA, Y POR QUE CADA COSA:
 *
 *   1. Que encuentra las dos secciones y que en la direccion que dice
 *      cada una empiezan de verdad sus cuatro letras. Un desplazamiento
 *      mal leido da un directorio que valida y secciones corridas, y eso
 *      en la radio sale como modelos de avion equivocados.
 *
 *   2. Que un byte cambiado EN EL DIRECTORIO no se cuela. Esto es lo que
 *      impide que unos bytes cualesquiera que hubiera en la zona se lean
 *      como desplazamientos y manden a leer al area del gestor de
 *      arranque.
 *
 *   3. Que un bit cambiado en los DATOS lo pilla la verificacion.
 *
 *   4. Y que SIN directorio se cae a la distribucion vieja, que es lo que
 *      permite actualizar el firmware sin volver a cargar los datos.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include "zona_alta.h"

static int fallos;
static FILE *g_f;
static long  g_tam;

static void exige(int cond, const char *que)
{
    printf("    %-58s %s\n", que, cond ? "bien" : "MAL");
    if (!cond) { fallos++; }
}

/* En la radio esto es spi_flash_read; aqui, el fichero puesto en ZA_BASE. */
static void lee(uint32_t addr, uint8_t *buf, uint32_t len)
{
    long off = (long)addr - (long)ZA_BASE;

    memset(buf, 0xFF, len);
    if (off < 0 || off >= g_tam) { return; }
    fseek(g_f, off, SEEK_SET);
    (void)fread(buf, 1, len, g_f);
}

int main(int argc, char **argv)
{
    const char *ruta = (argc > 1) ? argv[1] : "BD.BIN";
    uint8_t *todo;

    printf("\n  EL DIRECTORIO DE LA ZONA ALTA\n");

    g_f = fopen(ruta, "rb");
    if (g_f == NULL) {
        printf("    no encuentro %s - se salta este banco\n", ruta);
        return 0;
    }
    fseek(g_f, 0, SEEK_END); g_tam = ftell(g_f); fseek(g_f, 0, SEEK_SET);
    todo = malloc((size_t)g_tam);
    (void)fread(todo, 1, (size_t)g_tam, g_f);

    printf("\n  abrir el directorio\n");
    exige(zona_alta_abre(lee) != 0U, "el directorio cuadra");
    printf("    %u secciones en %ld bytes\n",
           (unsigned)zona_alta_secciones(), g_tam);
    exige(zona_alta_secciones() >= 2U, "hay al menos dos bases dentro");

    printf("\n  donde dice que esta cada una\n");
    {
        uint8_t i;

        for (i = 0; i < zona_alta_secciones(); i++) {
            uint32_t tipo = 0, addr = 0, tam = 0;
            char n[5];
            char q[80];

            exige(zona_alta_seccion(i, &tipo, &addr, &tam), "la seccion se lee");
            n[0] = (char)(tipo & 0xFF); n[1] = (char)((tipo >> 8) & 0xFF);
            n[2] = (char)((tipo >> 16) & 0xFF); n[3] = (char)((tipo >> 24) & 0xFF);
            n[4] = '\0';
            printf("    %-6s 0x%06X  %8u bytes\n", n, (unsigned)addr, (unsigned)tam);
            /*
             * Y QUE AHI EMPIECEN DE VERDAD SUS CUATRO LETRAS. El tipo de
             * la seccion son las mismas cuatro letras con las que empieza
             * su fichero, asi que esto comprueba de una vez que el
             * desplazamiento es el bueno y que no hay dos listas de tipos
             * que cuadrar.
             */
            snprintf(q, sizeof q, "en 0x%06X empieza '%s' de verdad",
                     (unsigned)addr, n);
            exige(memcmp(&todo[addr - ZA_BASE], n, 4) == 0, q);
        }
    }

    printf("\n  buscar por tipo\n");
    {
        uint32_t a = 0, t = 0;

        exige(zona_alta_donde(ZA_TIPO_AVIONES, &a, &t) && t > 1000U,
              "los aviones estan y miden algo");
        exige(zona_alta_donde(ZA_TIPO_EMISORAS, &a, &t) && t > 1000U,
              "las emisoras tambien");
        exige(!zona_alta_donde(0x41414141UL, &a, &t),
              "y un tipo que no esta NO se inventa una direccion");
    }

    printf("\n  las sumas\n");
    exige(zona_alta_verifica() != 0U, "las secciones cuadran con sus sumas");

    printf("\n  y lo que NO puede pasar: creerse un directorio roto\n");
    {
        /*
         * SOLO LOS BYTES QUE SON DEL DIRECTORIO. Del 16 + 16*n en adelante
         * es relleno del bloque de 4 kB y no pertenece a nada, asi que
         * cambiarlo NO tiene que romper nada - eso se comprueba aparte,
         * mas abajo-. La primera version de este banco estropeaba tambien
         * el relleno y contaba 96 "colados" que eran justo lo contrario:
         * bytes que se ignoran bien.
         */
        long fin = 16 + 16 * (long)zona_alta_secciones();
        long i;
        int colados = 0;

        for (i = 4; i < fin; i++) {
            FILE *v, *t;
            uint8_t *copia = malloc((size_t)g_tam);

            memcpy(copia, todo, (size_t)g_tam);
            copia[i] ^= 0x5A;
            t = fopen("/tmp/za_roto.bin", "wb");
            fwrite(copia, 1, (size_t)g_tam, t);
            fclose(t);
            free(copia);

            v = g_f;
            g_f = fopen("/tmp/za_roto.bin", "rb");
            if (zona_alta_abre(lee)) { colados++; }
            fclose(g_f); g_f = v;
        }
        printf("      %d de %ld bytes cambiados en el directorio se colaron\n",
               colados, fin - 4);
        exige(colados == 0, "ni uno solo pasa la suma del directorio");
        (void)zona_alta_abre(lee);
    }

    printf("\n  y el relleno del directorio, que NO es de nadie\n");
    {
        /* Los bytes que van desde el final de las secciones hasta los
         * 4.096 del bloque son relleno. Cambiarlos no puede romper nada:
         * si rompiera, una version futura que escribiera algo ahi dejaria
         * de poder leerse con esta. */
        long fin = 16 + 16 * (long)zona_alta_secciones();
        int rotos = 0;
        long i;

        for (i = fin; i < 4096; i += 137) {
            FILE *v, *t;
            uint8_t *copia = malloc((size_t)g_tam);

            memcpy(copia, todo, (size_t)g_tam);
            copia[i] ^= 0x5A;
            t = fopen("/tmp/za_roto.bin", "wb");
            fwrite(copia, 1, (size_t)g_tam, t);
            fclose(t);
            free(copia);

            v = g_f;
            g_f = fopen("/tmp/za_roto.bin", "rb");
            if (!zona_alta_abre(lee)) { rotos++; }
            fclose(g_f); g_f = v;
        }
        printf("      %d bytes de relleno cambiados rompieron el directorio\n",
               rotos);
        exige(rotos == 0, "el relleno se ignora, como tiene que ser");
        (void)zona_alta_abre(lee);
    }

    printf("\n  y un bit cambiado en medio de los datos\n");
    {
        long sitios[4], k;
        int pillados = 0;

        /*
         * DENTRO DE UNA SECCION, no en el relleno del final. Entre el
         * final de la ultima seccion y el final del fichero hay ceros de
         * relleno hasta el bloque de 4 kB, y esos no son de nadie: la
         * suma no los cubre y cambiarlos no tiene por que notarse.
         */
        uint32_t a0 = 0, t0 = 0, a1 = 0, t1 = 0;

        (void)zona_alta_donde(ZA_TIPO_AVIONES, &a0, &t0);
        (void)zona_alta_donde(ZA_TIPO_EMISORAS, &a1, &t1);
        sitios[0] = (long)(a0 - ZA_BASE);
        sitios[1] = (long)(a0 - ZA_BASE + t0 - 1);
        sitios[2] = (long)(a1 - ZA_BASE + t1 / 2);
        sitios[3] = (long)(a1 - ZA_BASE + t1 - 1);
        for (k = 0; k < 4; k++) {
            FILE *v, *t;
            uint8_t *copia = malloc((size_t)g_tam);

            memcpy(copia, todo, (size_t)g_tam);
            copia[sitios[k]] ^= 0x01;
            t = fopen("/tmp/za_roto.bin", "wb");
            fwrite(copia, 1, (size_t)g_tam, t);
            fclose(t);
            free(copia);

            v = g_f;
            g_f = fopen("/tmp/za_roto.bin", "rb");
            if (zona_alta_abre(lee) && !zona_alta_verifica()) { pillados++; }
            fclose(g_f); g_f = v;
        }
        printf("      %d de 4 bits cambiados los pillo la verificacion\n",
               pillados);
        exige(pillados == 4, "un solo bit cambiado no pasa");
        (void)zona_alta_abre(lee);
    }

    printf("\n  sin directorio: la distribucion vieja\n");
    {
        /*
         * Una radio que ya tenia las dos bases cargadas a la manera vieja
         * tiene que seguir funcionando despues de actualizar el firmware.
         * Por eso, cuando en 0x101000 no hay directorio, las direcciones
         * son las de antes.
         */
        uint32_t a = 0, t = 1;
        FILE *v = g_f, *z = fopen("/tmp/za_vacio.bin", "wb+");
        uint8_t ff[64];

        memset(ff, 0xFF, sizeof ff);
        fwrite(ff, 1, sizeof ff, z);
        fflush(z);
        g_f = z;
        exige(zona_alta_abre(lee) == 0U, "sin directorio, se sabe que no lo hay");
        exige(zona_alta_donde(ZA_TIPO_AVIONES, &a, &t) && a == 0x101000UL,
              "los aviones caen en 0x101000, como siempre");
        exige(zona_alta_donde(ZA_TIPO_EMISORAS, &a, &t) && a == 0x140000UL,
              "y las emisoras en 0x140000");
        exige(zona_alta_secciones() == 0U, "y no dice tener secciones");
        fclose(z); g_f = v;
    }

    printf("\n%s (%d fallos)\n", fallos ? "HAY FALLOS" : "TODO CORRECTO", fallos);
    return fallos ? 1 : 0;
}
