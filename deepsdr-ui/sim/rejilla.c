/*
 * VOLVER A LA REJILLA DEL PASO - 22/09/2026.
 *
 * El usuario del proyecto se quedo clavado en 7.112,03 kHz: girase lo que
 * girase el mando, los 30 Hz de mas no se iban. Lo destapo el enganche al
 * pico de la etapa 21, que deja el VFO en la frecuencia exacta de la señal -
 * o sea, fuera de la rejilla del paso de sintonia.
 *
 * Esta es la misma cuenta que hace main.c, con el caso de verdad y con los
 * bordes que suelen fallar: paso negativo, frecuencia justo en la rejilla,
 * saltos de varios pasos, y el paso mas fino de todos.
 */
#include <stdio.h>
#include <stdint.h>

/* COPIA de tune_mueve_pasos() de main.c, escrita aparte a proposito: si el
 * banco llamara a la del firmware, comprobaria que hace lo que hace, no lo
 * que debe. Aqui se escribe la REGLA y se compara. */
static int64_t espera(int64_t f, int32_t pasos, int64_t paso)
{
    int64_t g;
    /* La regla, dicha en palabras: el mando lleva al siguiente multiplo del
     * paso en el sentido que gires, y de ahi sigue de paso en paso. */
    if (paso <= 0 || pasos == 0) { return f; }
    if (pasos > 0) {
        g = ((f / paso) + 1) * paso;              /* el multiplo de encima */
        return g + (int64_t)(pasos - 1) * paso;
    }
    g = f % paso ? (f / paso) * paso : f - paso;   /* el de debajo */
    return g + (int64_t)(pasos + 1) * paso;
}

static int64_t bajo_prueba(int64_t f, int32_t pasos, int64_t paso_hz)
{
    int64_t resto;
    if (paso_hz <= 0 || pasos == 0) { return f; }
    resto = f % paso_hz;
    if (resto < 0) { resto += paso_hz; }
    if (resto != 0) {
        f -= resto;
        if (pasos > 0) { f += paso_hz; pasos--; }
        else           { pasos++; }
    }
    return f + (int64_t)pasos * paso_hz;
}

static int fallos = 0;

static void caso(const char *que, int64_t f, int32_t pasos, int64_t paso)
{
    int64_t r = bajo_prueba(f, pasos, paso);
    int64_t e = espera(f, pasos, paso);
    printf("  %-34s %9lld %+3d x %5lld -> %9lld %s\n", que,
           (long long)f, pasos, (long long)paso, (long long)r,
           (r == e) ? "" : "   MAL, se esperaba distinto");
    if (r != e) { fallos++; }
}

int main(void)
{
    printf("mando de sintonia: volver a la rejilla del paso\n");

    /* El caso que lo destapo. */
    caso("7.112,03 kHz, un clic arriba", 7112030, +1, 1000);
    caso("7.112,03 kHz, un clic abajo",  7112030, -1, 1000);

    /* Y que a partir de ahi ya se quede en numeros redondos. */
    {
        int64_t f = 7112030;
        int i;
        for (i = 0; i < 3; i++) { f = bajo_prueba(f, +1, 1000); }
        printf("  %-34s tres clics -> %lld %s\n", "desde 7.112,03", (long long)f,
               (f % 1000 == 0) ? "(redondo)" : "   MAL, sigue fuera de la rejilla");
        if (f % 1000 != 0) { fallos++; }
    }

    /* Ya en la rejilla: un clic tiene que mover un paso entero, ni mas ni
     * menos - si no, girar sobre una frecuencia redonda se quedaria quieto. */
    caso("ya redonda, un clic arriba",   7112000, +1, 1000);
    caso("ya redonda, un clic abajo",    7112000, -1, 1000);
    caso("ya redonda, cinco clics",      7112000, +5, 1000);

    /* Varios pasos desde fuera de la rejilla. */
    caso("fuera, tres clics arriba",     7112030, +3, 1000);
    caso("fuera, tres clics abajo",      7112030, -3, 1000);

    /* Pasos finos y gruesos. */
    caso("paso de 10 Hz",                7112030, +1, 10);
    caso("paso de 1 Hz",                 7112030, +1, 1);
    caso("paso de 100 kHz",              7112030, +1, 100000);

    /* Cero clics no mueve. */
    caso("cero clics",                   7112030,  0, 1000);

    printf("  %s\n", fallos ? "HAY FALLOS" : "el mando vuelve a la rejilla y se queda en ella");
    return fallos ? 1 : 0;
}
