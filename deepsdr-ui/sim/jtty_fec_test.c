/*
 * JTTY: LA CORRECCION DE ERRORES, CONTRA EL FORTRAN DE VERDAD.
 *
 * Pedido por el usuario.
 *
 * ESTE BANCO NO COMPRUEBA NUESTRO CODIGO CONTRA NUESTRO CODIGO, y esa es
 * toda la razon de que exista asi. Los vectores de abajo los ha sacado
 * `tbcc.f90` DE WSJT-X v3.2.0-rc1, compilado con gfortran y llamado con
 * el perfil de JTTY. Si nuestra version tuviera del reves cualquiera de
 * los convenios -el orden de los bits, la precarga del tail-biting, el
 * mapa Gray de tonos, el polinomio del CRC- aqui NO se cancelaria: los
 * tonos saldrian distintos.
 *
 * Esa leccion salio cara. El localizador de WSPR tenia el mismo error en
 * el codificador y en el decodificador, la ida y vuelta salia limpia, y
 * hicieron falta una emision de verdad y una base de datos de fuera para
 * destaparlo. Con un formato ajeno, comprobar contra la implementacion
 * ajena es lo unico que cuenta.
 *
 * Las cargas son las del documento normativo lib/jtty/jtty_source_encoding.txt.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <stdint.h>
#include "jtty_fec.h"

static int fallos;

static void exige(int cond, const char *que)
{
    printf("    %-58s %s\n", que, cond ? "bien" : "MAL");
    if (!cond) { fallos++; }
}

/* Vectores sacados del Fortran de referencia. Ver la cabecera. */
static const struct { const char *nombre, *carga, *tonos; } k_oro[] = {
#include "jtty_oro.h"
};

static double gauss(void)
{
    double u1 = (rand() + 1.0) / (RAND_MAX + 2.0);
    double u2 = (rand() + 1.0) / (RAND_MAX + 2.0);

    return sqrt(-2.0 * log(u1)) * cos(6.283185307 * u2);
}

int main(void)
{
    static jtty_fec_ram_t ram;
    unsigned i;

    printf("\n  JTTY: LA CORRECCION DE ERRORES\n");

    printf("\n  los tonos, contra los que saca el Fortran de WSJT-X\n");
    for (i = 0; i < sizeof k_oro / sizeof k_oro[0]; i++) {
        uint8_t carga[JTTY_CARGA_BITS], tonos[JTTY_SIMBOLOS];
        char salen[JTTY_SIMBOLOS + 1];
        unsigned k;

        for (k = 0; k < JTTY_CARGA_BITS; k++) {
            carga[k] = (uint8_t)(k_oro[i].carga[k] - '0');
        }
        jtty_fec_codifica(carga, tonos);
        for (k = 0; k < JTTY_SIMBOLOS; k++) { salen[k] = (char)('0' + tonos[k]); }
        salen[JTTY_SIMBOLOS] = '\0';
        if (strcmp(salen, k_oro[i].tonos) != 0) {
            printf("      esperado %s\n      salen    %s\n", k_oro[i].tonos, salen);
        }
        exige(strcmp(salen, k_oro[i].tonos) == 0, k_oro[i].nombre);
    }

    printf("\n  y de vuelta: de los tonos al mensaje\n");
    for (i = 0; i < sizeof k_oro / sizeof k_oro[0]; i++) {
        uint8_t carga[JTTY_CARGA_BITS], tonos[JTTY_SIMBOLOS], sale[JTTY_CARGA_BITS];
        float e[JTTY_SIMBOLOS][4];
        unsigned k, j;

        for (k = 0; k < JTTY_CARGA_BITS; k++) {
            carga[k] = (uint8_t)(k_oro[i].carga[k] - '0');
        }
        jtty_fec_codifica(carga, tonos);
        for (k = 0; k < JTTY_SIMBOLOS; k++) {
            for (j = 0; j < 4; j++) { e[k][j] = (tonos[k] == j) ? 1.0f : 0.0f; }
        }
        memset(sale, 0xFF, sizeof sale);
        exige(jtty_fec_descodifica((const float (*)[4])e, &ram, 4U, sale)
              && memcmp(sale, carga, JTTY_CARGA_BITS) == 0, k_oro[i].nombre);
    }

    printf("\n  cuantos errores aguanta\n");
    {
        /*
         * Se van borrando tonos -poniendoles la misma energia a los
         * cuatro, que es lo que hace el ruido cuando se lleva un simbolo
         * por delante- y se mira hasta cuantos aguanta. Un convolucional
         * K=10 de tasa 1/2 sobre 46 simbolos tiene que aguantar unos
         * cuantos; si aguantara UNO, es que el trellis esta mal cerrado.
         */
        uint8_t carga[JTTY_CARGA_BITS], tonos[JTTY_SIMBOLOS], sale[JTTY_CARGA_BITS];
        unsigned borrados;

        for (i = 0; i < JTTY_CARGA_BITS; i++) {
            carga[i] = (uint8_t)(k_oro[0].carga[i] - '0');
        }
        jtty_fec_codifica(carga, tonos);

        for (borrados = 0; borrados <= 20; borrados += 2) {
            unsigned bien = 0, intento;

            for (intento = 0; intento < 20; intento++) {
                float e[JTTY_SIMBOLOS][4];
                unsigned k, j, b;

                srand(1000 + intento);
                for (k = 0; k < JTTY_SIMBOLOS; k++) {
                    for (j = 0; j < 4; j++) { e[k][j] = (tonos[k] == j) ? 1.0f : 0.0f; }
                }
                for (b = 0; b < borrados; b++) {
                    unsigned k2 = (unsigned)rand() % JTTY_SIMBOLOS;

                    for (j = 0; j < 4; j++) { e[k2][j] = 0.25f; }
                }
                if (jtty_fec_descodifica((const float (*)[4])e, &ram, 4U, sale)
                    && memcmp(sale, carga, JTTY_CARGA_BITS) == 0) { bien++; }
            }
            printf("    %2u simbolos borrados de 46  ->  %2u de 20\n", borrados, bien);
            if (borrados <= 6) {
                exige(bien == 20, "hasta seis borrados no puede fallar ninguno");
            }
        }
    }

    printf("\n  con ruido de verdad en las energias\n");
    {
        uint8_t carga[JTTY_CARGA_BITS], tonos[JTTY_SIMBOLOS], sale[JTTY_CARGA_BITS];
        double sigma;

        for (i = 0; i < JTTY_CARGA_BITS; i++) {
            carga[i] = (uint8_t)(k_oro[0].carga[i] - '0');
        }
        jtty_fec_codifica(carga, tonos);

        for (sigma = 0.1; sigma <= 1.3; sigma += 0.2) {
            unsigned bien = 0, malos = 0, intento;

            for (intento = 0; intento < 30; intento++) {
                float e[JTTY_SIMBOLOS][4];
                unsigned k, j;

                srand(5000 + intento);
                for (k = 0; k < JTTY_SIMBOLOS; k++) {
                    for (j = 0; j < 4; j++) {
                        e[k][j] = (float)(((tonos[k] == j) ? 1.0 : 0.0)
                                          + sigma * gauss());
                    }
                }
                if (jtty_fec_descodifica((const float (*)[4])e, &ram, 4U, sale)) {
                    if (memcmp(sale, carga, JTTY_CARGA_BITS) == 0) { bien++; }
                    else { malos++; }
                }
            }
            printf("    sigma %.1f  ->  %2u buenos, %2u FALSOS de 30\n",
                   sigma, bien, malos);
            if (sigma <= 0.35) { exige(bien >= 28, "con poco ruido casi todos"); }
        }
    }

    printf("\n  y lo que NO puede pasar: inventarse un mensaje\n");
    {
        uint8_t sale[JTTY_CARGA_BITS];
        unsigned inventados = 0, intento;

        for (intento = 0; intento < 200; intento++) {
            float e[JTTY_SIMBOLOS][4];
            unsigned k, j;

            srand(9000 + intento);
            for (k = 0; k < JTTY_SIMBOLOS; k++) {
                for (j = 0; j < 4; j++) { e[k][j] = (float)gauss(); }
            }
            if (jtty_fec_descodifica((const float (*)[4])e, &ram, 4U, sale)) {
                inventados++;
            }
        }
        printf("      %u de 200 tandas de ruido puro dieron mensaje\n", inventados);
        /*
         * El CRC son doce bits y el bit reservado uno mas: trece bits de
         * comprobacion sobre cuatro candidatos, o sea del orden de
         * 4/8192 = 0,05%. En 200 tandas eso es menos de uno. Se admite
         * uno por si acaso, pero no dos.
         */
        exige(inventados <= 1, "el CRC y el bit reservado paran el ruido");
    }

    printf("\n  cuanta memoria pide\n");
    printf("      %u bytes de trabajo (metricas y vuelta atras)\n",
           (unsigned)sizeof(jtty_fec_ram_t));
    exige(sizeof(jtty_fec_ram_t) < 8192U,
          "cabe de sobra en la memoria prestada de la cascada");

    printf("\n%s (%d fallos)\n", fallos ? "HAY FALLOS" : "TODO CORRECTO", fallos);
    return fallos ? 1 : 0;
}
