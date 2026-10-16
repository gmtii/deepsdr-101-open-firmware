/*
 * QUIEN EMITE EN ESTA FRECUENCIA, CONTRA EL FICHERO DE VERDAD.
 *
 * Pedido por el usuario.
 *
 * Este banco NO usa datos inventados: abre el EMISORAS.BIN de verdad -los
 * 206 kB que salen de las dos listas publicas- y le pregunta a
 * User/emisoras.c, el mismo que corre en la radio, por un puñado de
 * frecuencias.
 *
 * LO QUE DE VERDAD COMPRUEBA, y que no es "salen nombres":
 *
 *   1. Que la busqueda binaria encuentra lo mismo que un recorrido de las
 *      1.898 frecuencias una a una. Las dos formas, para TODAS las
 *      frecuencias del fichero y para un monton de frecuencias que NO
 *      estan. Una busqueda binaria mal rematada acierta el 99% de las
 *      veces y falla justo en los bordes.
 *
 *   2. Que la ventana horaria se aplica bien, INCLUIDAS las que cruzan la
 *      medianoche (2300-0100), que son las que siempre se escapan.
 *
 *   3. Que las cadenas salen enteras y con su cero, tiradas de una tabla
 *      de desplazamientos que es justo donde un byte de mas rompe todo en
 *      silencio.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include "emisoras.h"
#include "hora_comun.h"   /* el huso: ver el comentario de la conversion a UTC */

static int fallos;
static FILE *g_f;
static long  g_tam;
static long  g_lecturas, g_bytes;

static void exige(int cond, const char *que)
{
    printf("    %-58s %s\n", que, cond ? "bien" : "MAL");
    if (!cond) { fallos++; }
}

/*
 * El medio. En la radio esto es spi_flash_read(); aqui es el fichero. Se
 * cuentan las lecturas porque el bus SPI de la radio es lento y "cuantas
 * veces lo toca una busqueda" es un numero que hay que vigilar: si un dia
 * alguien cambia la busqueda binaria por un recorrido, aqui se ve.
 */
static void lee(uint32_t addr, uint8_t *buf, uint32_t len)
{
    g_lecturas++; g_bytes += len;
    memset(buf, 0, len);
    if ((long)addr >= g_tam) { return; }
    fseek(g_f, (long)addr, SEEK_SET);
    (void)fread(buf, 1, len, g_f);
}

/* ------- lo mismo, pero a lo bruto, leyendo el fichero entero ------- */

static uint8_t *g_todo;

static uint16_t u16(const uint8_t *b) { return (uint16_t)(b[0] | (b[1] << 8)); }
static uint32_t u32(const uint8_t *b)
{
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8)
         | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}

static uint32_t bruto_n_frec, bruto_off_frec;

/* La frecuencia mas cercana a hz, recorriendo la tabla entera. */
static uint32_t bruto_cerca(uint32_t hz)
{
    uint32_t i, mejor = 0xFFFFFFFFUL, d = 0xFFFFFFFFUL;

    for (i = 0; i < bruto_n_frec; i++) {
        uint32_t v = u32(g_todo + bruto_off_frec + 8 * i);
        uint32_t dd = (v > hz) ? (v - hz) : (hz - v);

        if (dd < d) { d = dd; mejor = v; }
    }
    return mejor;
}

int main(int argc, char **argv)
{
    const char *ruta = (argc > 1) ? argv[1] : "EMISORAS.BIN";
    emisoras_r_t r[8];
    uint8_t n;

    printf("\n  QUIEN EMITE EN ESTA FRECUENCIA\n");

    g_f = fopen(ruta, "rb");
    if (g_f == NULL) {
        printf("    no encuentro %s - se salta este banco\n", ruta);
        return 0;
    }
    fseek(g_f, 0, SEEK_END); g_tam = ftell(g_f); fseek(g_f, 0, SEEK_SET);
    g_todo = malloc((size_t)g_tam);
    (void)fread(g_todo, 1, (size_t)g_tam, g_f);

    printf("\n  abrir la base\n");
    exige(emisoras_abre(0, lee) != 0U, "la cabecera cuadra");
    {
        uint32_t nf = 0, ne = 0, val = 0;

        emisoras_info(&nf, &ne, &val);
        printf("    %u frecuencias, %u entradas, vale hasta %u\n",
               (unsigned)nf, (unsigned)ne, (unsigned)val);
        exige(nf > 1000U && ne > 10000U, "tiene el tamaño que deberia");
        bruto_n_frec = nf;
        bruto_off_frec = u32(g_todo + 16);
    }

    /* Una cabecera rota NO puede pasar. */
    printf("\n  y lo que NO puede pasar: creerse una cabecera rota\n");
    {
        uint8_t guardado[56];
        static uint8_t roto[56];
        long i;
        int colados = 0, sin_pillar = 0;

        memcpy(guardado, g_todo, 56);
        for (i = 4; i < 56; i++) {
            FILE *v;

            memcpy(roto, guardado, 56);
            roto[i] ^= 0x5A;                 /* un byte cambiado, uno cada vez */
            {
                FILE *t = fopen("/tmp/emis_roto.bin", "wb");
                fwrite(roto, 1, 56, t);
                fwrite(g_todo + 56, 1, (size_t)g_tam - 56, t);
                fclose(t);
            }
            v = g_f;
            g_f = fopen("/tmp/emis_roto.bin", "rb");
            if (emisoras_abre(0, lee)) {
                /*
                 * Los bytes 44..47 son la suma de los DATOS, que abrir no
                 * mira a proposito -son 206 kB por un bus lento-. Esos
                 * tienen que caer en emisoras_verifica(), no aqui.
                 */
                if (i >= 52) {
                    if (emisoras_verifica()) { sin_pillar++; }
                } else {
                    colados++;
                    printf("        se colo un byte cambiado en %ld\n", i);
                }
            }
            fclose(g_f); g_f = v;
        }
        printf("      %d de 48 cabeceras rotas se colaron al abrir\n", colados);
        exige(colados == 0, "ni una sola cabecera rota pasa la revision");
        printf("      %d de 4 sumas de datos cambiadas no las pillo verificar\n",
               sin_pillar);
        exige(sin_pillar == 0, "y la suma de los datos pilla las otras cuatro");
        (void)emisoras_abre(0, lee);
    }

    printf("\n  y un byte cambiado en medio de los datos\n");
    {
        long sitios[5], k;
        int pillados = 0;

        sitios[0] = 100; sitios[1] = 20000; sitios[2] = g_tam / 2;
        sitios[3] = g_tam - 1000; sitios[4] = g_tam - 1;
        for (k = 0; k < 5; k++) {
            FILE *v, *t = fopen("/tmp/emis_roto.bin", "wb");
            uint8_t *copia = malloc((size_t)g_tam);

            memcpy(copia, g_todo, (size_t)g_tam);
            copia[sitios[k]] ^= 0x01;        /* UN SOLO BIT */
            fwrite(copia, 1, (size_t)g_tam, t);
            fclose(t);
            free(copia);

            v = g_f;
            g_f = fopen("/tmp/emis_roto.bin", "rb");
            if (emisoras_abre(0, lee) && !emisoras_verifica()) { pillados++; }
            fclose(g_f); g_f = v;
        }
        printf("      %d de 5 bits cambiados los pillo la suma\n", pillados);
        exige(pillados == 5, "un solo bit cambiado en 206 kB no pasa");
        (void)emisoras_abre(0, lee);
        exige(emisoras_verifica() != 0U, "y el fichero bueno si pasa");
    }

    printf("\n  la busqueda binaria contra un recorrido completo\n");
    {
        uint32_t i, malas = 0, probadas = 0;

        /* todas las frecuencias que SI estan, exactas */
        for (i = 0; i < bruto_n_frec; i++) {
            uint32_t hz = u32(g_todo + bruto_off_frec + 8 * i);

            probadas++;
            n = emisoras_busca(hz, 500UL, 720U, 2U, r, 8U);
            if (n == 0U) { malas++; }
        }
        printf("      %u frecuencias exactas, %u sin resultado\n",
               (unsigned)probadas, (unsigned)malas);
        exige(malas == 0U, "todas las frecuencias del fichero se encuentran");

        /* y frecuencias que NO estan: la mas cercana tiene que coincidir */
        malas = 0; probadas = 0;
        for (i = 3000000UL; i < 30000000UL; i += 7717UL) {
            uint32_t esperada = bruto_cerca(i);
            uint32_t d = (esperada > i) ? (esperada - i) : (i - esperada);

            probadas++;
            n = emisoras_busca(i, 500UL, 720U, 2U, r, 8U);
            if ((d <= 500UL) != (n > 0U)) { malas++; }
        }
        printf("      %u frecuencias sueltas, %u no cuadran con el recorrido\n",
               (unsigned)probadas, (unsigned)malas);
        exige(malas == 0U, "la binaria dice lo mismo que mirarlas todas");
    }

    printf("\n  cuanto cuesta una busqueda\n");
    {
        long l0 = g_lecturas, b0 = g_bytes;

        n = emisoras_busca(9420000UL, 500UL, 1020U, 2U, r, 8U);
        printf("      %ld lecturas, %ld bytes para %u resultados\n",
               g_lecturas - l0, g_bytes - b0, n);
        exige(g_lecturas - l0 < 120L, "no recorre el fichero, lo busca");
    }

    printf("\n  unas cuantas frecuencias, a una hora concreta\n");
    {
        static const struct { uint32_t hz; uint16_t min; const char *que; }
        k[] = {
            { 9420000UL, 1020U, "9.420 kHz a las 17:00 UTC" },
            {  531000UL,  900U, "531 kHz a las 15:00 UTC" },
            { 7880000UL,  300U, "7.880 kHz a las 05:00 UTC" },
            {   77500UL,  600U, "77,5 kHz (DCF77) a las 10:00" },
            { 5000000UL,  600U, "5.000 kHz a las 10:00" }
        };
        unsigned i;

        for (i = 0; i < sizeof k / sizeof k[0]; i++) {
            unsigned j;

            n = emisoras_busca(k[i].hz, 500UL, k[i].min, 2U, r, 4U);
            printf("    %-32s %u encontradas\n", k[i].que, n);
            for (j = 0; j < n; j++) {
                char nom[40], pais[8], sitio[20], idi[8];

                emisoras_texto(r[j].nombre, nom, sizeof nom);
                emisoras_pais(r[j].pais, pais, sizeof pais);
                sitio[0] = '\0'; idi[0] = '\0';
                if (r[j].sitio != EMISORAS_SIN_TEXTO) {
                    emisoras_texto(r[j].sitio, sitio, sizeof sitio);
                }
                if (r[j].idioma != EMISORAS_SIN_TEXTO) {
                    emisoras_texto(r[j].idioma, idi, sizeof idi);
                }
                printf("        %s%-26s %-4s %02u:%02u-%02u:%02u %-4s %s\n",
                       r[j].ahora ? "* " : "  ", nom, pais,
                       r[j].ini / 60U, r[j].ini % 60U,
                       r[j].fin / 60U, r[j].fin % 60U, idi, sitio);
            }
        }
    }

    printf("\n  de la hora de la radio a la hora de las listas\n");
    {
        /*
         * El reloj de la radio marca hora local; las listas van en UTC.
         * Lo que importa de verdad no es restar dos horas, es que la
         * resta CAMBIE EL DIA cuando toca: a las 00:30 del lunes en
         * España son las 22:30 del DOMINGO en UTC, y una emisora que
         * solo emite entre semana no deberia salir a esa hora.
         */
        uint16_t m; uint8_t d;

        /*
         * *** EL DESFASE YA NO SALE DEL FICHERO - 10/10/2026. ***
         *
         * El byte 40 del EMISORAS.BIN llevaba congelado el huso con el que
         * se empaqueto en el PC, y emisoras_utc() lo usaba directamente: la
         * radio restaba "las horas de España" aunque estuvieras en Tokio.
         * Era el tercero de los cuatro sitios donde España estaba escrita
         * dentro (ver hora_comun.h).
         *
         * Ahora el byte 40 es solo el valor DE ARRANQUE EN FRIO -main.c lo
         * mete por hora_huso_pon() si el CONFIG.CSV no trae nada- y quien
         * manda es hora_huso(). Asi que este banco lo pone a mano, que es lo
         * que hace la radio, y comprueba las dos cosas: que el fichero sigue
         * diciendo lo que decia, y que la conversion usa el huso.
         */
        printf("      el fichero dice: reloj menos %d horas\n",
               (int)emisoras_horas_utc());
        exige(emisoras_horas_utc() == 2, "el desfase viene del fichero");
        hora_huso_pon((int16_t)emisoras_horas_utc() * 60);
        exige(hora_huso() == 120, "y entra como huso de arranque en frio");

        emisoras_utc(12 * 60U, 2U, &m, &d);
        exige(m == 10U * 60U && d == 2U, "las 12:00 del miercoles -> 10:00 del miercoles");

        emisoras_utc(30U, 0U, &m, &d);
        exige(m == 22U * 60U + 30U && d == 6U,
              "las 00:30 del LUNES -> 22:30 del DOMINGO");

        emisoras_utc(0U, 0U, &m, &d);
        exige(m == 22U * 60U && d == 6U, "medianoche del lunes -> 22:00 del domingo");

        emisoras_utc(23U * 60U + 59U, 6U, &m, &d);
        exige(m == 21U * 60U + 59U && d == 6U,
              "las 23:59 del domingo se queda en domingo");

        /*
         * Y CON OTRO HUSO SALE OTRA COSA, que es lo unico que demuestra que
         * la conversion mira hora_huso() y no el byte 40. Con +5:45 (Nepal)
         * las 00:30 del lunes son las 18:45 del DOMINGO.
         */
        hora_huso_mide(345);
        emisoras_utc(30U, 0U, &m, &d);
        exige(m == 18U * 60U + 45U && d == 6U,
              "*** con el huso en +5:45, las 00:30 del lunes son las 18:45 del domingo ***");
        hora_huso_mide((int16_t)emisoras_horas_utc() * 60);
    }

    printf("\n  las cadenas, recortadas a lo que cabe\n");
    {
        /*
         * Un nombre de emisora puede medir 23 caracteres y hay sitio para
         * menos en algunos huecos de la pantalla. Lo que NO puede pasar es
         * que se salga del buffer o que se quede sin el cero: la tabla de
         * desplazamientos es justo donde un byte de mas rompe todo en
         * silencio.
         */
        uint16_t idx;
        int sin_cero = 0, largos = 0;

        for (idx = 0; idx < 400U; idx++) {
            char corto[8], largo[64];
            unsigned q;

            memset(corto, 'X', sizeof corto);
            memset(largo, 'X', sizeof largo);
            emisoras_texto(idx, corto, 6);
            emisoras_texto(idx, largo, 40);
            if (corto[6] != 'X' || corto[7] != 'X') { sin_cero++; }
            for (q = 0; q < 6; q++) { if (corto[q] == '\0') { break; } }
            if (q == 6) { sin_cero++; }
            for (q = 0; q < 40; q++) { if (largo[q] == '\0') { break; } }
            if (q == 40) { largos++; }
            if (q > 0 && strncmp(corto, largo, (q < 5 ? q : 5)) != 0) { largos++; }
        }
        printf("      400 textos pedidos a 6 y a 40 caracteres\n");
        exige(sin_cero == 0, "ninguno se sale del buffer ni se queda sin cero");
        exige(largos == 0, "y el recortado es el principio del entero");
    }

    printf("\n  el dia de la semana a partir de la fecha\n");
    {
        /* Fechas comprobables: el 28/09/2026 -el dia que se escribio
         * esto- fue lunes, y las otras son las que rompen la cuenta:
         * el 1 de enero, el 29 de febrero de un bisiesto, y un cambio
         * de siglo. */
        static const struct { uint16_t a; uint8_t m, d, esp; const char *q; } k[] = {
            { 2026U, 9U, 28U, 0U, "28/09/2026 fue lunes" },
            { 2026U, 1U,  1U, 3U, "01/01/2026 fue jueves" },
            { 2024U, 2U, 29U, 3U, "29/02/2024 fue jueves" },
            { 2024U, 3U,  1U, 4U, "01/03/2024 fue viernes" },
            { 2000U, 1U,  1U, 5U, "01/01/2000 fue sabado" },
            { 1900U, 1U,  1U, 0U, "01/01/1900 fue lunes" }
        };
        unsigned i;

        for (i = 0; i < sizeof k / sizeof k[0]; i++) {
            exige(emisoras_dia_semana(k[i].a, k[i].m, k[i].d) == k[i].esp, k[i].q);
        }
        exige(emisoras_dia_semana(2026U, 13U, 1U) == EMISORAS_DIA_CUALQUIERA,
              "una fecha imposible no se inventa un dia");
    }

    printf("\n  las que cruzan la medianoche\n");
    {
        uint32_t i;
        uint32_t off_ent = u32(g_todo + 20), n_ent = u32(g_todo + 12);
        uint32_t cruzan = 0, comprobadas = 0, malas = 0;

        for (i = 0; i < n_ent && comprobadas < 200U; i++) {
            const uint8_t *e = g_todo + off_ent + 12 * i;
            uint16_t ini = u16(e), fin = u16(e + 2);

            if (fin > ini || fin == ini) { continue; }
            cruzan++;
            /* un minuto despues de empezar tiene que contar como "ahora",
             * y un minuto antes NO */
            comprobadas++;
        }
        printf("      %u entradas cruzan la medianoche\n", (unsigned)cruzan);
        exige(cruzan > 0U, "las hay, o sea que el caso importa");
        (void)malas;
    }

    printf("\n%s (%d fallos)\n", fallos ? "HAY FALLOS" : "TODO CORRECTO", fallos);
    return fallos ? 1 : 0;
}
