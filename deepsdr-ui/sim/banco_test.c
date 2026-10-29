/*
 * ======================================================================
 * EL BANCO DE MEMORIAS - 26/10/2026
 * ======================================================================
 *
 * Compila el User/banco.c de verdad. Lo que se exige:
 *
 *   - que grabar y leer devuelva EXACTAMENTE lo guardado, los seis campos;
 *   - que el CSV vaya y vuelva sin perder nada, con las sesenta llenas;
 *   - que un fichero editado desde un PC -con finales de linea de Windows,
 *     con la cabecera, con renglones en blanco y con basura- se lea sin
 *     tirar lo que si vale;
 *   - que un fichero corrompido no desborde el buffer ni invente memorias
 *     en bancos que no existen;
 *   - y que el barrido recorra las usadas y de la vuelta.
 */
#include <stdio.h>
#include <string.h>
#include "banco.h"

/* Tapones: este banco no toca la flash. */
uint32_t spi_flash_read_file_by_name(const char n8[8], const char e3[3],
                                     uint8_t *b, uint32_t c)
{ (void)n8; (void)e3; (void)b; (void)c; return 0U; }
int spi_flash_write_or_update_file(const char n8[8], const char e3[3],
                                   const uint8_t *d, uint32_t l)
{ (void)n8; (void)e3; (void)d; (void)l; return 1; }

static char s_buf[4096];
static int fallos = 0;

static void di(const char *q, int ok, const char *det)
{
    printf("    %-52s %s", q, ok ? "ok " : "*** MAL ***");
    if (det) { printf("   %s", det); }
    printf("\n");
    if (!ok) { fallos++; }
}

static const uint32_t k_pasos[] = {
    10UL, 100UL, 500UL, 1000UL, 5000UL, 9000UL,
    10000UL, 12500UL, 25000UL, 100000UL, 1000000UL
};
#define PASOS_N ((uint8_t)(sizeof k_pasos / sizeof k_pasos[0]))

int main(void)
{
    banco_pasos_pon(k_pasos, PASOS_N);
    banco_mem_t m, r;
    uint32_t n;
    uint8_t b, i;

    printf("== EL BANCO DE MEMORIAS ==\n\n");

    printf("  -- (1) grabar y leer --\n");
    banco_init();
    memset(&m, 0, sizeof m);
    m.hz = 7150000UL; m.modo = 4U; m.paso = 3U;
    m.fil_lo = 300U; m.fil_hi = 2700U; m.cw = 0U;
    di("una memoria recien nacida esta vacia", !banco_lee(0U, 0U, &r), 0);
    di("se graba", banco_graba(0U, 0U, &m) != 0U, 0);
    di("y se lee", banco_lee(0U, 0U, &r) != 0U, 0);
    di("con los seis campos intactos",
       r.hz == m.hz && r.modo == m.modo && r.paso == m.paso
       && r.fil_lo == m.fil_lo && r.fil_hi == m.fil_hi && r.cw == m.cw, 0);
    banco_borra(0U, 0U);
    di("y borrarla la vacia", !banco_lee(0U, 0U, &r), 0);

    printf("\n  -- (2) los indices que no valen no tocan nada --\n");
    di("banco 4 (solo hay 0..3)", !banco_graba(4U, 0U, &m), 0);
    di("memoria 15 (solo hay 0..14)", !banco_graba(0U, 15U, &m), 0);
    di("y leer de ahi tampoco", !banco_lee(9U, 9U, &r), 0);

    printf("\n  -- (3) el CSV va y vuelve con las SESENTA llenas --\n");
    banco_init();
    for (b = 0U; b < BANCO_N; b++) {
        for (i = 0U; i < BANCO_MEM_N; i++) {
            memset(&m, 0, sizeof m);
            m.hz = 100000UL + (uint32_t)b * 1000000UL + (uint32_t)i * 12345UL;
            m.modo = (uint8_t)(i % 7U);
            m.paso = (uint8_t)((b + i) % PASOS_N);
            m.fil_lo = (uint16_t)(100U + i);
            m.fil_hi = (uint16_t)(3000U + i * 7U);
            m.cw = (uint16_t)((i == 3U) ? 500U : 0U);
            banco_graba(b, i, &m);
        }
    }
    n = banco_a_csv(s_buf, sizeof s_buf);
    printf("      el fichero mide %u bytes\n", (unsigned)n);
    di("cabe en el buffer de verdad (2.560)", n <= 2560U, 0);
    banco_init();
    di("se relee", banco_de_csv(s_buf, n) != 0U, 0);
    {
        int bien = 1;
        for (b = 0U; b < BANCO_N; b++) {
            for (i = 0U; i < BANCO_MEM_N; i++) {
                uint32_t hz = 100000UL + (uint32_t)b * 1000000UL + (uint32_t)i * 12345UL;
                if (!banco_lee(b, i, &r)) { bien = 0; break; }
                if (r.hz != hz || r.modo != (uint8_t)(i % 7U)
                    || r.paso != (uint8_t)((b + i) % PASOS_N)
                    || r.fil_lo != (uint16_t)(100U + i)
                    || r.fil_hi != (uint16_t)(3000U + i * 7U)
                    || r.cw != (uint16_t)((i == 3U) ? 500U : 0U)) { bien = 0; break; }
            }
        }
        di("y las sesenta vuelven identicas", bien, 0);
    }

    printf("\n  -- (4) las vacias no ocupan sitio en el fichero --\n");
    banco_init();
    memset(&m, 0, sizeof m); m.hz = 198000UL; m.modo = 0U;
    banco_graba(2U, 7U, &m);
    n = banco_a_csv(s_buf, sizeof s_buf);
    s_buf[n] = '\0';
    printf("      con UNA memoria el fichero mide %u bytes:\n", (unsigned)n);
    printf("      |%s|\n", s_buf);
    di("un fichero de una memoria es corto", n < 80U, 0);

    printf("\n  -- (5) un fichero EDITADO EN UN PC --\n");
    {
        /* Finales de linea de Windows, cabecera, renglones en blanco,
         * espacios, un comentario y una linea rota por la mitad. */
        static const char k_pc[] =
            "banco,memoria,hz,modo,paso,fil_lo,fil_hi,cw\r\n"
            "\r\n"
            "1,1,9690000,0,2,100,4000,0\r\n"
            "esto es una nota que alguien escribio\r\n"
            "2,5,77500,0,0,\r\n"
            "4,15,108000000,6,9,0,0,0\r\n"
            "9,9,1,1,1,1,1,1\r\n"          /* banco que no existe */
            "1,99,1,1,1,1,1,1\r\n"          /* memoria que no existe */
            "3,3,0,1,1,1,1,1\r\n";          /* frecuencia cero = no vale */
        banco_init();
        di("se lee algo", banco_de_csv(k_pc, (uint32_t)(sizeof k_pc - 1U)) != 0U, 0);
        di("la primera entra", banco_lee(0U, 0U, &r) && r.hz == 9690000UL, 0);
        di("la de campos a medias entra con ceros",
           banco_lee(1U, 4U, &r) && r.hz == 77500UL && r.fil_hi == 0U, 0);
        di("la ultima casilla del ultimo banco entra",
           banco_lee(3U, 14U, &r) && r.hz == 108000000UL, 0);
        di("el banco 9 NO se inventa", !banco_lee(3U, 8U, &r), 0);
        di("la memoria 99 tampoco", banco_usadas(0U) == 1U, 0);
        di("y una frecuencia 0 no cuenta", !banco_lee(2U, 2U, &r), 0);
    }

    printf("\n  -- (6) basura: que no desborde ni se invente --\n");
    {
        static char k_malo[600];
        uint32_t j;
        for (j = 0U; j < sizeof k_malo - 1U; j++) {
            k_malo[j] = (char)('0' + (j % 13U));   /* cifras y comas sueltas */
            if ((j % 7U) == 6U) { k_malo[j] = ','; }
            if ((j % 31U) == 30U) { k_malo[j] = '\n'; }
        }
        k_malo[sizeof k_malo - 1U] = '\0';
        banco_init();
        (void)banco_de_csv(k_malo, (uint32_t)(sizeof k_malo - 1U));
        di("no se cuelga ni desborda", 1, "(si llega aqui, no lo ha hecho)");
        /* Y lo que haya entrado tiene que estar DENTRO de rango. */
        {
            int dentro = 1;
            for (b = 0U; b < BANCO_N; b++) {
                for (i = 0U; i < BANCO_MEM_N; i++) {
                    if (banco_lee(b, i, &r) && r.hz == 0UL) { dentro = 0; }
                }
            }
            di("y lo que entro esta dentro de rango", dentro, 0);
        }
        banco_init();
        di("un fichero vacio no rompe", banco_de_csv("", 0U) == 0U, 0);
        di("y un puntero nulo tampoco", banco_de_csv(0, 10U) == 0U, 0);
    }

    printf("\n  -- (7) el barrido recorre las usadas y da la vuelta --\n");
    banco_init();
    memset(&m, 0, sizeof m);
    m.hz = 1UL;  banco_graba(1U, 2U, &m);
    m.hz = 2UL;  banco_graba(1U, 9U, &m);
    m.hz = 3UL;  banco_graba(1U, 14U, &m);
    di("cuenta las usadas", banco_usadas(1U) == 3U, 0);
    di("desde el principio da la primera", banco_siguiente(1U, 0xFFU) == 2U, 0);
    di("y despues la siguiente", banco_siguiente(1U, 2U) == 9U, 0);
    di("y la ultima", banco_siguiente(1U, 9U) == 14U, 0);
    di("y da la vuelta", banco_siguiente(1U, 14U) == 2U, 0);
    banco_init();
    di("un banco vacio devuelve 0xFF", banco_siguiente(0U, 0xFFU) == 0xFFU, 0);
    di("y uno con UNA se devuelve a si misma",
       (banco_graba(0U, 5U, &m), m.hz = 7UL, banco_graba(0U, 5U, &m),
        banco_siguiente(0U, 5U) == 5U), 0);

    printf("\n  -- (8) el nombre de cada banco --\n");
    banco_init();
    di("de serie no hay ninguno", banco_nombre(0U)[0] == '\0', 0);
    banco_nombre_pon(0U, "Rusos");
    di("se pone y se lee", strcmp(banco_nombre(0U), "Rusos") == 0, 0);
    banco_nombre_pon(0U, "");
    di("y se quita con uno vacio", banco_nombre(0U)[0] == '\0', 0);
    banco_nombre_pon(0U, (const char *)0);
    di("y con un puntero nulo", banco_nombre(0U)[0] == '\0', 0);
    di("un banco que no existe no escribe nada",
       (banco_nombre_pon(99U, "x"), banco_nombre(99U)[0] == '\0'), 0);
    {
        /* Mas largo que el tope: se corta, y sobre todo NO se sale del
         * array -eso es lo que de verdad hay que comprobar-. */
        char largo[64];
        unsigned k;

        for (k = 0U; k < 60U; k++) { largo[k] = 'W'; }
        largo[60] = '\0';
        banco_nombre_pon(1U, largo);
        di("uno larguisimo se corta al tope",
           strlen(banco_nombre(1U)) == (size_t)(BANCO_NOM_MAX - 1U), 0);
        /* Y uno mas corto detras no deja cola del anterior. */
        banco_nombre_pon(1U, "ab");
        di("y uno corto detras no deja cola",
           strcmp(banco_nombre(1U), "ab") == 0, 0);
    }
    {
        /* Lo que no se puede pintar se queda fuera: un tabulador, un byte
         * de control o media letra acentuada en UTF-8. */
        banco_nombre_pon(2U, "OM\tradio");
        di("un tabulador corta ahi", strcmp(banco_nombre(2U), "OM") == 0, 0);
        banco_nombre_pon(2U, "Espa\xc3\xb1a");
        di("y un byte alto tambien", strcmp(banco_nombre(2U), "Espa") == 0, 0);
    }

    printf("\n  -- (9) los nombres van y vuelven por el fichero --\n");
    {
        static char buf[BANCO_CSV_MIN];
        uint32_t n;

        banco_init();
        memset(&m, 0, sizeof m);
        m.hz = 9690000UL; m.modo = 1U;
        banco_graba(0U, 0U, &m);
        banco_nombre_pon(0U, "Rusos");
        banco_nombre_pon(3U, "Marina");
        n = banco_a_csv(buf, (uint32_t)sizeof buf);
        di("cabe en el buffer", n < (uint32_t)sizeof buf, 0);
        di("el renglon del nombre esta",
           strstr(buf, "nombre,1,Rusos\n") != 0, 0);
        di("y el del cuarto banco tambien",
           strstr(buf, "nombre,4,Marina\n") != 0, 0);
        di("un banco sin nombre no escribe renglon",
           strstr(buf, "nombre,2,") == 0, 0);

        banco_init();
        di("y al releerlo vuelven",
           (banco_de_csv(buf, n),
            strcmp(banco_nombre(0U), "Rusos") == 0
            && strcmp(banco_nombre(3U), "Marina") == 0), 0);
        di("y las memorias siguen ahi", banco_usadas(0U) == 1U, 0);

        /* Un fichero VIEJO, sin renglones de nombre, se lee igual: es lo
         * que tiene en el disco quien actualice. */
        banco_init();
        di("un fichero sin nombres se lee igual",
           (banco_de_csv("banco,memoria,hz,modo,paso,fil_lo,fil_hi,cw\n"
                         "1,1,9690000,1,2,100,4000,0\n", 72U)
            && banco_usadas(0U) == 1U && banco_nombre(0U)[0] == '\0'), 0);

        /* Y uno con el renglon roto no se lleva por delante lo demas. */
        banco_init();
        di("un renglon de nombre roto se salta",
           (banco_de_csv("nombre,\nnombre,9,x\nnombre\n"
                         "1,1,9690000,1,2,100,4000,0\n", 60U)
            && banco_usadas(0U) == 1U), 0);
    }

    /* ------------------------------------------------------------------
     * (10) LOS NOMBRES DE SERIE SALEN EN EL FICHERO, COMENTADOS
     * ------------------------------------------------------------------
     * 28/10/2026, pedido por el usuario: los de serie TIENEN que salir, si
     * no quien abra el fichero no sabe que eso se cambia ahi. Y salen
     * comentados a proposito, porque los de serie estan TRADUCIDOS:
     * escribirlos vivos los congelaria en el idioma del dia que se grabo.
     * ------------------------------------------------------------------ */
    printf("\n  -- (10) los de serie salen comentados --\n");
    {
        static const char *const k_serie[4] = {
            "HF", "Aviones", "Marina", "Repetidores"
        };
        static char buf[BANCO_CSV_MIN];
        uint32_t n;

        banco_init();
        banco_nombres_pon(k_serie, 4U, "Para ponerle nombre a un banco...");
        memset(&m, 0, sizeof m);
        m.hz = 9690000UL; m.modo = 1U;
        banco_graba(0U, 0U, &m);
        banco_nombre_pon(1U, "Rusos");
        n = banco_a_csv(buf, (uint32_t)sizeof buf);

        di("la ayuda sale arriba",
           strstr(buf, "# Para ponerle nombre") != 0, 0);
        di("el de serie del banco 1 sale comentado",
           strstr(buf, "# nombre,1,HF\n") != 0, 0);
        di("y el del 4 tambien",
           strstr(buf, "# nombre,4,Repetidores\n") != 0, 0);
        di("el que puso el usuario sale VIVO",
           strstr(buf, "\nnombre,2,Rusos\n") != 0, 0);
        di("y no comentado",
           strstr(buf, "# nombre,2,") == 0, 0);

        /* Y lo que importa: releerlo NO le pone nombre a los comentados. */
        banco_init();
        banco_de_csv(buf, n);
        di("al releerlo los comentados siguen sin nombre",
           banco_nombre(0U)[0] == '\0' && banco_nombre(3U)[0] == '\0', 0);
        di("y el del usuario vuelve",
           strcmp(banco_nombre(1U), "Rusos") == 0, 0);
        di("y las memorias siguen ahi", banco_usadas(0U) == 1U, 0);
    }

    /* ------------------------------------------------------------------
     * EL PASO VA EN HERCIOS, Y UN FICHERO VIEJO SIGUE VALIENDO
     * ------------------------------------------------------------------
     * Hasta el 27/10/2026 el fichero guardaba el INDICE del paso. El dia
     * que se inserta un paso en medio de la lista -los 9 kHz de onda
     * media van entre los 5 y los 10- todos los indices por encima se
     * corren uno, y las memorias ya grabadas vuelven con OTRO paso: una
     * de marina guardada en 12,5 kHz volveria en 10.
     * ------------------------------------------------------------------ */
    printf("\n  EL PASO SE GUARDA EN HERCIOS\n");
    {
        banco_mem_t m;
        const char *txt;

        banco_init();
        banco_pasos_pon(k_pasos, PASOS_N);
        memset(&m, 0, sizeof m);
        m.hz = 1008000UL;
        m.modo = 2U;
        m.paso = 5U;                 /* el indice de los 9 kHz en k_pasos */
        banco_graba(0U, 0U, &m);
        n = banco_a_csv(s_buf, sizeof s_buf);
        s_buf[n] = '\0';
        di("el fichero dice 9000 y no 5", strstr(s_buf, ",9000,") != 0, 0);
        di("y la cabecera lo llama paso_hz", strstr(s_buf, "paso_hz") != 0, 0);

        /* Un fichero VIEJO, con el paso como indice: un numero pequeño
         * tiene que entenderse como indice y no como hercios. */
        txt = "banco,memoria,hz,modo,paso,fil_lo,fil_hi,cw\n"
              "1,2,7100000,1,3,300,2700,0\n";
        banco_init();
        banco_pasos_pon(k_pasos, PASOS_N);
        di("un fichero viejo se relee",
           banco_de_csv(txt, (uint32_t)strlen(txt)) != 0U, 0);
        di("y su 3 se entiende como indice",
           banco_lee(0U, 1U, &r) && r.paso == 3U, 0);

        /* Y uno nuevo: los hercios ganan. */
        txt = "banco,memoria,hz,modo,paso_hz,fil_lo,fil_hi,cw\n"
              "1,3,1008000,2,9000,0,4000,0\n";
        banco_init();
        banco_pasos_pon(k_pasos, PASOS_N);
        (void)banco_de_csv(txt, (uint32_t)strlen(txt));
        di("y 9000 se entiende como los 9 kHz",
           banco_lee(0U, 2U, &r) && r.paso == 5U, 0);
    }

    printf("\n%s (%d fallos)\n\n", fallos ? "HAY FALLOS" : "TODO CORRECTO", fallos);
    return fallos ? 1 : 0;
}
