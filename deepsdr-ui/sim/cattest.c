/*
 * EL CAT, MEDIDO CON EL HAMLIB DE VERDAD.
 *
 * Este banco tiene dos mitades y la segunda es la que importa.
 *
 * La PRIMERA le mete ordenes a mano y compara la respuesta byte a byte.
 * Eso comprueba que el codigo hace lo que yo creo que dice el manual de
 * Kenwood, que no es lo mismo que comprobar que funciona.
 *
 * La SEGUNDA abre un puerto serie de mentira -un pseudoterminal- , pone
 * este mismo cat.c al otro lado, y le lanza encima el `rigctl` de hamlib
 * DE VERDAD, con su controlador de TS-2000 y sin saber que hay detras. Si
 * hamlib lee la frecuencia, el modo y el S, es que esto es un TS-2000 para
 * cualquier programa que use hamlib: WSJT-X, JTDX, fldigi, CQRLOG.
 *
 * La diferencia entre las dos no es academica. El bloque IF mide 37
 * caracteres y hamlib lo parsea POR POSICION: un caracter de mas y descarta
 * la respuesta entera, con lo que la radio "no contesta" y no hay ningun
 * error que leer. Mi propio banco de la primera mitad podria estar contando
 * mal y aprobarse a si mismo. Hamlib no.
 */
#define _GNU_SOURCE
#include "cat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <poll.h>
#include <sys/wait.h>
#include <termios.h>

static int fallos = 0;

static void ok(const char *que, int bien, const char *detalle)
{
    printf("  %-46s %s%s%s\n", que, bien ? "ok" : "MAL",
           detalle ? "   " : "", detalle ? detalle : "");
    if (!bien) { fallos++; }
}

/* ---------------- la radio de mentira ---------------- */
static uint32_t s_freq = 7110000UL;
static uint8_t  s_modo = CAT_MODO_LSB;
static uint8_t  s_s    = 5U;
static uint8_t  s_vol  = 66U;
static uint16_t s_bw   = 2300U;

static uint32_t f_lee(void)            { return s_freq; }
static void     f_pon(uint32_t h)      { s_freq = h; }
static uint8_t  m_lee(void)            { return s_modo; }
static void     m_pon(uint8_t m)       { s_modo = m; }
static uint8_t  s_lee(void)            { return s_s; }
static uint8_t  v_lee(void)            { return s_vol; }
static void     v_pon(uint8_t v)       { s_vol = v; }
static uint16_t a_lee(void)            { return s_bw; }

/* La vuelta del bucle: numeros inventados pero con la forma de los de
 * verdad, que es lo que hay que comprobar -el ancho de cada hueco-. */
static void d_lee(cat_diag_t *d)
{
    uint8_t k;

    d->vueltas   = 37U;
    d->detentes  = 12U;
    d->nr        = 1U;
    d->carga     = 49U;
    d->vuelta_us = 28431UL;
    for (k = 0U; k < CAT_DIAG_TRAMOS; k++) {
        d->tramo_us[k] = (uint32_t)k * 100UL;
    }
    d->tramo_us[0] = 123456UL;   /* se tiene que topar a 99999 */
}

/* Y lo de DENTRO del dibujo, igual: numeros inventados con la forma de los
 * de verdad. `saltadas` se topa para comprobar que el hueco aguanta. */
static void dd_lee(cat_dibujo_t *d)
{
    d->cuadros    = 44U;
    d->fft_us     = 1100UL;
    d->chrome_us  = 2200UL;
    d->spec_us    = 3300UL;
    d->push_us    = 4400UL;
    d->wf_us      = 5500UL;
    d->saltadas   = 123456UL;   /* se tiene que topar a 99999 */
    d->bandas     = 52UL;
    d->invalida   = 17UL;
    d->lecturas   = 131UL;
    d->frq_n      = 5UL;
    d->rgl_n      = 2UL;
    d->chr_n      = 7UL;
    d->lot_us     = 1763UL;
    d->lot_n      = 35UL;
    d->chapa_n    = 1UL;
    d->arrastre_n = 23UL;
    d->arr_us     = 1990UL;
    d->wf_lineas  = 9UL;
    d->estado     = 0x15UL;
}

static const cat_radio_t k_radio = {
    f_lee, f_pon, m_lee, m_pon, s_lee, v_lee, v_pon, a_lee, d_lee, dd_lee
};

/* Mete una orden entera y devuelve lo que contesta, terminado en cero. */
static const char *manda(const char *orden)
{
    static char sal[CAT_SALIDA_MAX + 1];
    uint8_t n = 0U;
    const char *p;

    for (p = orden; *p != '\0'; p++) {
        n = cat_byte(*p, sal);
    }
    sal[n] = '\0';
    return sal;
}

static void iguales(const char *que, const char *orden, const char *espera)
{
    const char *r = manda(orden);
    char det[160];

    snprintf(det, sizeof det, "%-16s -> \"%s\"", orden, r);
    ok(que, strcmp(r, espera) == 0, det);
    if (strcmp(r, espera) != 0) {
        printf("        esperaba \"%s\"\n", espera);
    }
}

static void a_mano(void)
{
    printf("\n  las ordenes, una a una\n");

    cat_init(&k_radio);
    s_freq = 7110000UL; s_modo = CAT_MODO_LSB; s_s = 5U; s_vol = 66U;

    iguales("se identifica como un TS-2000", "ID;", "ID019;");
    iguales("dice que esta encendida",       "PS;", "PS1;");
    iguales("la informacion automatica, apagada", "AI;", "AI0;");
    iguales("da la frecuencia con 11 cifras", "FA;", "FA00007110000;");
    /*
     * LA VUELTA DEL BUCLE, HUECO A HUECO. 2 + 4 + 4 + 1 + 3 + 5 + 18 x 5 + 1
     * = 110 caracteres, que caben en CAT_SALIDA_MAX. Si
     * alguien añade un tramo sin tocar CAT_DIAG_TRAMOS, esto se cae aqui
     * y no en la radio.
     */
    iguales("la vuelta del bucle, de ancho fijo", "DG;",
            "DG0037001210492843199999001000020000300004000050000600007000"
            "0800009000100001100012000130001400015000160001700;");
    /*
     * Y LO DE DENTRO DEL DIBUJO. 2 + 4 + 19 x 5 + 1 = 102 caracteres. Va
     * aparte de "DG" porque los tramos de "DG" REPARTEN la vuelta y estos
     * son el desglose de uno de ellos: juntos contarian dos veces.
     */
    iguales("dentro del dibujo, de ancho fijo", "DD;",
            "DD00440110002200033000440005500999990005200017001310000500002"
            "0000701763000350000100023019900000900021;");
    iguales("y acepta que se la cambien",     "FA00014074000;", "");
    iguales("  (y se queda cambiada)",        "FA;", "FA00014074000;");
    iguales("da el modo",                     "MD;", "MD1;");
    iguales("y acepta que se lo cambien",     "MD2;", "");
    iguales("  (y se queda cambiado)",        "MD;", "MD2;");
    iguales("el S, en el rango de Kenwood",   "SM0;", "SM00005;");
    iguales("el volumen",                     "AG0;", "AG0066;");
    iguales("el ancho del filtro",            "FW;", "FW2300;");
    iguales("un solo VFO, y es el A",         "FR;", "FR0;");

    /* El que de verdad importa. */
    {
        const char *r = manda("IF;");
        char det[160];
        size_t n = strlen(r);

        snprintf(det, sizeof det, "%zu caracteres: \"%s\"", n, r);
        ok("el bloque IF mide 38 (37 + el punto y coma)", n == 38U, det);
        ok("  y lleva la frecuencia donde toca",
           (n == 38U) && (strncmp(&r[2], "00014074000", 11) == 0), NULL);
        ok("  y el modo en su sitio",
           (n == 38U) && (r[29] == '2'), NULL);
        ok("  y dice que esta RECIBIENDO",
           (n == 38U) && (r[28] == '0'), NULL);
    }

    printf("\n  lo que NO se contesta\n");
    iguales("transmitir: esto es un receptor", "TX;", "?;");
    iguales("una orden que no existe",         "ZZ;", "?;");
    iguales("una orden a medias",              "F;",  "?;");
    iguales("una frecuencia con letras",       "FA0001407400X;", "?;");

    printf("\n  y la mugre del cable\n");
    s_s = 7U;
    iguales("minusculas",                      "sm0;", "SM00007;");
    iguales("con retorno de carro delante",    "\r\nSM0;", "SM00007;");
    iguales("un punto y coma suelto",          ";", "");
    {
        /* Una orden mas larga que el buffer no puede dejar el analizador
         * atascado: tiene que tirarla y seguir vivo en la siguiente. */
        char largo[200];
        memset(largo, 'A', sizeof largo - 2);
        largo[sizeof largo - 2] = ';';
        largo[sizeof largo - 1] = '\0';
        iguales("una orden absurdamente larga", largo, "?;");
        iguales("  y la siguiente sigue yendo", "FA;", "FA00014074000;");
    }
    /* El S por encima del rango de Kenwood: 255 enseñaria S9+40 con el
     * receptor en silencio. */
    s_s = 200U;
    iguales("el S se acota a 30", "SM0;", "SM00030;");
}

/* ---------------- la mitad que importa: hamlib ---------------- */

static int sirve_pty(int fd, int ms_tope)
{
    char sal[CAT_SALIDA_MAX];
    int  ordenes = 0;

    for (;;) {
        struct pollfd p = { fd, POLLIN, 0 };
        char c;
        ssize_t r;
        uint8_t n;

        if (poll(&p, 1, ms_tope) <= 0) { break; }
        r = read(fd, &c, 1);
        if (r <= 0) {
            if ((r < 0) && ((errno == EAGAIN) || (errno == EINTR))) { continue; }
            break;
        }
        n = cat_byte(c, sal);
        if (n > 0U) {
            if (write(fd, sal, n) != (ssize_t)n) { break; }
            ordenes++;
        }
    }
    return ordenes;
}

static void con_hamlib(void)
{
    int maestro;
    char nombre[128];
    pid_t hijo;
    int tuberia[2];
    char salida[8192];
    ssize_t total = 0;
    int estado = 0, ordenes;

    printf("\n  y ahora el rigctl de hamlib, contra el mismo cat.c\n");

    cat_init(&k_radio);
    s_freq = 14074000UL; s_modo = CAT_MODO_USB; s_s = 9U;

    maestro = posix_openpt(O_RDWR | O_NOCTTY);
    if ((maestro < 0) || (grantpt(maestro) != 0) || (unlockpt(maestro) != 0)
        || (ptsname_r(maestro, nombre, sizeof nombre) != 0)) {
        ok("abrir un puerto serie de mentira", 0, strerror(errno));
        return;
    }
    printf("    puerto: %s\n", nombre);

    if (pipe(tuberia) != 0) { ok("tuberia", 0, NULL); return; }

    /* Sin hamlib instalado no hay segunda mitad, y eso no es un fallo del
     * firmware: se dice y se sigue. */
    if (system("command -v rigctl >/dev/null 2>&1") != 0) {
        printf("    no hay rigctl instalado: me salto esta mitad\n");
        close(tuberia[0]); close(tuberia[1]); close(maestro);
        return;
    }

    hijo = fork();
    if (hijo == 0) {
        dup2(tuberia[1], 1); dup2(tuberia[1], 2);
        close(tuberia[0]); close(tuberia[1]); close(maestro);
        execlp("rigctl", "rigctl", "-m", "2014", "-r", nombre,
               "-s", "9600", "f", "m", "l", "STRENGTH", "t", "v", (char *)NULL);
        _exit(127);
    }
    close(tuberia[1]);

    ordenes = sirve_pty(maestro, 4000);

    {
        ssize_t r;

        fcntl(tuberia[0], F_SETFL, O_NONBLOCK);
        while ((r = read(tuberia[0], &salida[total],
                         (size_t)(sizeof salida - 1 - (size_t)total))) > 0) {
            total += r;
        }
    }
    salida[total] = '\0';
    close(tuberia[0]);
    waitpid(hijo, &estado, 0);
    close(maestro);

    {
        char det[80];

        snprintf(det, sizeof det, "%d ordenes CAT contestadas", ordenes);
        ok("hamlib habla con la radio", ordenes > 0, det);
    }
    /*
     * TODO SE MIRA SOBRE `salida` INTACTA, Y SE IMPRIME SOBRE UNA COPIA.
     *
     * Esto ya ha fallado DOS veces aqui, de la misma forma las dos: el
     * bucle que imprime parte la cadena metiendo ceros en los saltos de
     * linea, y strstr se para en el primer cero. La primera version daba
     * por malo que hamlib leyera el modo con el modo impreso tres lineas
     * mas abajo; al arreglarlo, la comprobacion siguiente se escribio
     * DESPUES del bucle y volvio a caer igual.
     *
     * Por eso ahora el orden es fijo y esta escrito: primero se saca todo
     * de la cadena original, despues se imprime sobre una copia, y el
     * bucle que destroza no toca nunca lo que se mide.
     */
    {
        int vi_f   = (strstr(salida, "14074000") != NULL);
        int vi_m   = (strstr(salida, "USB") != NULL);
        int vi_v   = (strstr(salida, "VFOA") != NULL);
        int vi_err = (strstr(salida, "error") != NULL);
        int vi_s   = 0;
        char copia[sizeof salida];
        char *l, *s2;

        /* El S sale como decibelios respecto a S9: un numero con signo en
         * su propia linea. */
        memcpy(copia, salida, sizeof copia);
        l = copia;
        while ((l != NULL) && (*l != '\0')) {
            s2 = strchr(l, '\n');
            if (s2 != NULL) { *s2 = '\0'; }
            if ((*l == '-') || ((*l >= '0') && (*l <= '9'))) {
                char *f2;
                long v = strtol(l, &f2, 10);

                if ((*f2 == '\0') && (v > -60L) && (v < 60L)) { vi_s = 1; }
            }
            l = (s2 != NULL) ? (s2 + 1) : NULL;
        }

        printf("    rigctl dijo:\n");
        memcpy(copia, salida, sizeof copia);
        l = copia;
        while ((l != NULL) && (*l != '\0')) {
            s2 = strchr(l, '\n');
            if (s2 != NULL) { *s2 = '\0'; }
            if (*l != '\0') { printf("      %s\n", l); }
            l = (s2 != NULL) ? (s2 + 1) : NULL;
        }

        ok("hamlib lee la frecuencia que puso la radio", vi_f, NULL);
        ok("hamlib lee el modo", vi_m, NULL);
        ok("hamlib lee el S, en dB respecto a S9", vi_s, NULL);
        /*
         * ESTA ES LA QUE MIDE EL BLOQUE IF, Y HACE FALTA UNA APARTE.
         *
         * `f` y `m` usan FA; y MD;, asi que pasan aunque el IF este mal.
         * Las dos ultimas ordenes que se le piden al rigctl -`t` (¿estas
         * transmitiendo?) y `v` (¿que VFO?)- las saca hamlib DEL BLOQUE IF,
         * y las parsea por posicion.
         *
         * VISTO FALLAR: con un espacio de mas en el IF -39 caracteres en
         * vez de 38- las tres de arriba siguen en verde y esta se pone
         * roja, con hamlib soltando "get_ptt: error = tcflush" y sin llegar
         * siquiera a la orden siguiente. Sin esta comprobacion, el banco
         * aprobaria una radio que la mitad de los programas no sabe abrir.
         */
        ok("y saca el PTT y el VFO DEL BLOQUE IF", vi_v && !vi_err, NULL);
    }
}


int main(void)
{
    printf("\nEL CAT DE KENWOOD, Y EL HAMLIB DE VERDAD POR DELANTE\n");
    a_mano();
    con_hamlib();
    printf("\n%s (%d fallos)\n", fallos ? "HAY FALLOS" : "TODO CORRECTO",
           fallos);
    return fallos ? 1 : 0;
}
