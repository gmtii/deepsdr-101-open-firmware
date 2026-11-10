/*
 * DONDE SE VAN LOS OCHO MILLONES DEL DETECTOR DE STANAG.
 *
 * POR QUE EXISTE - 08/11/2026.
 *
 * m4_arbitro.c dejo el numero cerrado y dejo tambien la pregunta: una
 * ventana del detector cuesta 8,3 millones de instrucciones con la banda
 * VACIA y 3,0 con una FSK limpia encima. Casi tres veces mas por no
 * encontrar nada, que es ademas el estado en el que esta la radio casi
 * todo el tiempo. Un coste fijo -las dos transformadas- no explica eso:
 * una FFT de 2048 son once etapas de 1024 mariposas, las dos juntas no
 * llegan al millon y cuestan lo mismo haya señal o no.
 *
 * O sea que el gasto esta en algo que MIRA LOS DATOS. Este banco lo parte
 * en piezas y las cronometra una a una, con el mismo estado que tendrian
 * dentro de stanag_det_paso().
 *
 * COMO LO HACE, Y QUE TIENE DE MALO
 * ---------------------------------
 * Se trae el fichero entero con un #include, no con el enlazador: asi se
 * ven las funciones internas -fft(), mide_espectro(), realce()- que es lo
 * unico que se quiere medir. A cambio, el preambulo de stanag_det_paso()
 * -copiar el anillo, rellenar de ceros, transformar y elevar al cuadrado-
 * esta COPIADO aqui, y una copia se queda atras sola el dia que la de
 * verdad cambie.
 *
 * Contra eso hay una red: antes de desmontar nada se mide UNA VENTANA DE
 * VERDAD llamando a stanag_det_paso(), y al final se comprueba que las
 * piezas suman eso. Si la copia deja de parecerse al original, los numeros
 * dejan de cuadrar y el banco se pone rojo en vez de mentir.
 *
 * Esto NO mide la radio: mide el reparto. El numero bueno, el de cuanto se
 * come el detector de cada segundo, lo da m4_arbitro.c.
 */
#include <stdint.h>
#include "semi.h"

int g_fallos;
void pila_di(unsigned long maximo);   /* vive en start.c */

#define SCB_CCR (*(volatile uint32_t*)0xE000ED14UL)
static void desalineado_vale(void) { SCB_CCR &= ~(1UL << 3); }

/* EL DETECTOR ENTERO, POR DENTRO. Ver la cabecera. */
#include "stanag_det.c"

#define FS        12000U
#define BLOQUE    32U

static float s_trabajo[STANAG_DET_FLOATS];
static float s_audio[FS];

static uint32_t s_rnd;
static float ruido(void)
{
    s_rnd = s_rnd * 1664525u + 1013904223u;
    return (float)((int32_t)((s_rnd >> 9) & 0x7FFFF) - 262144) * (1.0f / 262144.0f);
}

/* Las mismas dos señales que el arbitro, para que los numeros se puedan
 * poner al lado de los suyos sin notas al pie. */
static const float k_sen64[64] = {
    0.0000f, 0.0980f, 0.1951f, 0.2903f, 0.3827f, 0.4714f, 0.5556f, 0.6344f,
    0.7071f, 0.7730f, 0.8315f, 0.8819f, 0.9239f, 0.9569f, 0.9808f, 0.9952f,
    1.0000f, 0.9952f, 0.9808f, 0.9569f, 0.9239f, 0.8819f, 0.8315f, 0.7730f,
    0.7071f, 0.6344f, 0.5556f, 0.4714f, 0.3827f, 0.2903f, 0.1951f, 0.0980f,
    0.0000f,-0.0980f,-0.1951f,-0.2903f,-0.3827f,-0.4714f,-0.5556f,-0.6344f,
   -0.7071f,-0.7730f,-0.8315f,-0.8819f,-0.9239f,-0.9569f,-0.9808f,-0.9952f,
   -1.0000f,-0.9952f,-0.9808f,-0.9569f,-0.9239f,-0.8819f,-0.8315f,-0.7730f,
   -0.7071f,-0.6344f,-0.5556f,-0.4714f,-0.3827f,-0.2903f,-0.1951f,-0.0980f
};

static void llena_ruido(void)
{
    uint32_t i;
    s_rnd = 1234567u;
    for (i = 0U; i < FS; i++) { s_audio[i] = ruido() * 0.3f; }
}

static void llena_fsk(void)
{
    uint32_t i, fase = 0u, bits = 0xB5A3C719u;
    s_rnd = 1234567u;
    for (i = 0U; i < FS; i++) {
        uint32_t sim = i / 240U;
        uint32_t uno = (bits >> (sim & 31U)) & 1U;
        uint32_t paso = uno ? (uint32_t)((2125.0f / 12000.0f) * 64.0f * 65536.0f)
                            : (uint32_t)((1275.0f / 12000.0f) * 64.0f * 65536.0f);
        fase += paso;
        s_audio[i] = k_sen64[(fase >> 16) & 63U] * 0.5f + ruido() * 0.05f;
    }
}

/* ---------------------------------------------------------------------- */

static uint32_t s_ai;   /* por donde va leyendo el audio, dando la vuelta */

static void mete_hasta_ventana(void)
{
    while (!s_listo) {
        stanag_det_mete(&s_audio[s_ai], BLOQUE);
        s_ai += BLOQUE;
        if (s_ai + BLOQUE > FS) { s_ai = 0U; }
    }
}

/* Una ventana DE VERDAD, por la puerta de siempre. */
static unsigned long ventana_entera(void)
{
    stanag_det_t r;
    unsigned long a, b;

    mete_hasta_ventana();
    a = tic();
    (void)stanag_det_paso(&r);
    b = tic();
    return ((a - b) & 0xFFFFFFUL) * 40UL;
}

static void linea(const char *que, unsigned long ins, unsigned long total)
{
    pr("      "); pr(que); pr("  ");
    prn(ins); pr(" ins");
    if (total > 0UL) {
        unsigned long pc = (ins / 100UL) * 10000UL / (total / 100UL + 1UL);
        pr("   ");
        prn(pc / 100UL); pr(","); prn((pc / 10UL) % 10UL); pr(" %");
    }
    pr("\n");
}

/*
 * EL PREAMBULO DE stanag_det_paso(), COPIADO. Ver la cabecera del fichero:
 * esta es la parte fragil, y la cuadratura del final es quien la vigila.
 */
static void preambulo(void)
{
    uint32_t i;
    for (i = 0U; i < STANAG_DET_N; i++) {
        uint32_t k = s_w + i; if (k >= STANAG_DET_N) { k -= STANAG_DET_N; }
        s_fft[2U*i]    = s_anillo[2U*k];
        s_fft[2U*i+1U] = s_anillo[2U*k+1U];
    }
    for (i = STANAG_DET_N; i < NFFT; i++) {
        s_fft[2U*i] = 0.0f; s_fft[2U*i+1U] = 0.0f;
    }
}

static void modulo2(void)
{
    uint32_t i;
    for (i = 0U; i < NFFT; i++) {
        float xr = s_fft[2U*i], xi = s_fft[2U*i+1U];
        s_fft[2U*i]    = xr*xr + xi*xi;
        s_fft[2U*i+1U] = 0.0f;
    }
}

/*
 * Y LA CUENTA DE CANDIDATOS: cuantas casillas del espectro pasan las tres
 * puertas baratas de mide_espectro() y acaban llamando a realce(), que es
 * la cara. La condicion esta copiada de alli tal cual; si alli cambia,
 * aqui sale otro numero y no pasa nada: esto informa, no decide.
 */
static void candidatos(unsigned long *cuantos, unsigned long *coste)
{
    uint32_t i;
    float mx = 0.0f;
    unsigned long n = 0UL, a, b;

    for (i = 0U; i < NFFT; i++) { if (s_fft[2U*i] > mx) { mx = s_fft[2U*i]; } }

    a = tic();
    for (i = 1U; i + 1U < NFFT; i++) {
        float v = s_fft[2U*i];
        float f = casilla_a_audio(i);
        if (v <= s_fft[2U*(i-1U)] || v < s_fft[2U*(i+1U)]) { continue; }
        if (v * (float)TONO_CAIDA < mx) { continue; }
        if (f < 100.0f || f > 3800.0f) { continue; }
        (void)realce(f);
        n++;
    }
    b = tic();
    *cuantos = n;
    *coste   = ((a - b) & 0xFFFFFFUL) * 40UL;
}

/* ---------------------------------------------------------------------- */

static void escenario(const char *nombre, void (*llena)(void))
{
    unsigned long entera, suma = 0UL;
    unsigned long t_pre, t_simb, t_fft1, t_mod, t_esp, t_sim, t_rac, t_fft2;
    unsigned long n_cand, t_cand;
    unsigned long a, b;
    int k;

    pr("\n  "); pr(nombre); pr("\n");

    llena();
    s_ai = 0U;
    if (!stanag_det_init(s_trabajo, STANAG_DET_FLOATS)) {
        pr("    *** el detector no arranca ***\n"); g_fallos++; return;
    }

    /* Que se asiente: PROMEDIOS pasadas y de sobra. El detector arrastra
     * estado de una ventana a la siguiente y medir la primera seria medir
     * el arranque. */
    for (k = 0; k < 12; k++) { (void)ventana_entera(); }

    entera = ventana_entera();
    linea("una ventana entera, por la puerta de siempre", entera, 0UL);
    pr("\n");

    /* Y ahora la siguiente, desmontada. */
    mete_hasta_ventana();

    a = tic(); preambulo();        b = tic(); t_pre  = ((a-b)&0xFFFFFFUL)*40UL;
    a = tic(); simb_acumula();     b = tic(); t_simb = ((a-b)&0xFFFFFFUL)*40UL;
    a = tic(); fft(s_fft, 0U);     b = tic(); t_fft1 = ((a-b)&0xFFFFFFUL)*40UL;
    a = tic(); modulo2();          b = tic(); t_mod  = ((a-b)&0xFFFFFFUL)*40UL;

    /* Aqui, y solo aqui, existe el espectro: lo mismo que dice el original. */
    candidatos(&n_cand, &t_cand);

    a = tic(); mide_espectro();    b = tic(); t_esp  = ((a-b)&0xFFFFFFUL)*40UL;
    a = tic(); mide_simbolos();    b = tic(); t_sim  = ((a-b)&0xFFFFFFUL)*40UL;
    a = tic(); mide_rachas();      b = tic(); t_rac  = ((a-b)&0xFFFFFFUL)*40UL;
    a = tic(); fft(s_fft, 1U);     b = tic(); t_fft2 = ((a-b)&0xFFFFFFUL)*40UL;

    suma = t_pre + t_simb + t_fft1 + t_mod + t_esp + t_sim + t_rac + t_fft2;

    linea("copiar el anillo y rellenar  ", t_pre,  entera);
    linea("simb_acumula()               ", t_simb, entera);
    linea("fft() directa                ", t_fft1, entera);
    linea("el modulo al cuadrado        ", t_mod,  entera);
    linea("mide_espectro()              ", t_esp,  entera);
    linea("mide_simbolos()              ", t_sim,  entera);
    linea("mide_rachas()                ", t_rac,  entera);
    linea("fft() inversa                ", t_fft2, entera);
    pr("\n");
    linea("las ocho piezas              ", suma,   entera);
    /*
     * LA RED, Y CON CUANTA HOLGURA. La ventana entera y la desmontada son
     * DOS ventanas distintas -una detras de otra- asi que no tienen por
     * que costar lo mismo al bit: con ruido, dos ventanas seguidas se
     * llevan un uno por ciento. Lo que se vigila aqui es otra cosa: que
     * las piezas no se queden MUY cortas -seria que la copia del preambulo
     * se dejo trabajo fuera- ni se pasen -seria que mide algo dos veces-.
     * Un 10 % a cada lado es de sobra para el ruido entre ventanas y muy
     * poco para un trozo de verdad.
     */
    if (entera > suma) {
        linea("lo que queda: la ACF y su analisis", entera - suma, entera);
    }
    if (suma > entera + entera / 10UL || suma + entera / 10UL < entera) {
        pr("      *** LAS PIEZAS NO SUMAN LA VENTANA ENTERA ***\n");
        pr("      *** el preambulo copiado ya no se parece al de verdad ***\n");
        g_fallos++;
    }

    pr("\n      dentro de mide_espectro():\n");
    pr("        casillas que llegan a realce()  "); prn(n_cand); pr("\n");
    pr("        y lo que cuestan esas llamadas  "); prn(t_cand);
    pr(" ins   de los "); prn(t_esp); pr(" de mide_espectro()\n");
}

int main(void)
{
    desalineado_vale();
    reloj_on();

    pr("\nEL DETECTOR DE STANAG, POR PIEZAS\n");
    pr("=================================\n");
    pr("\n  Una ventana son 1024 muestras de audio: se cierran 11 o 12 por\n");
    pr("  segundo. El arbitro dice lo que cuesta el segundo entero; esto\n");
    pr("  dice en que se va cada ventana.\n");

    escenario("BANDA VACIA (solo ruido)", llena_ruido);
    escenario("CON UNA FSK DE 850 Hz A 50 BAUDIOS", llena_fsk);

    pr("\n");
    pila_di(0);
    pr("PIEZAS: hecho\n");
    salir();
    return g_fallos ? 1 : 0;
}
