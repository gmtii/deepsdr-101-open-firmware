#include "spec_snap.h"

/* Suelo de ruido de la zona: la media de una ventana ancha.
 *
 * Se podria hacer con una mediana, que resiste mejor una señal muy fuerte
 * dentro de la ventana, pero costaria ordenar. Con 48 bins a cada lado, una
 * o dos señales mueven la media un par de dB: de sobra para decidir si lo
 * que hay bajo el dedo destaca 8 dB o no. */
static float suelo_local(const float *db, uint32_t n, int32_t centro)
{
    int32_t a = centro - SNAP_SUELO_BINS;
    int32_t b = centro + SNAP_SUELO_BINS;
    float suma = 0.0f;
    int32_t i, cuenta = 0;

    if (a < 0) { a = 0; }
    if (b > (int32_t)n - 1) { b = (int32_t)n - 1; }
    for (i = a; i <= b; i++) { suma += db[i]; cuenta++; }
    return (cuenta > 0) ? (suma / (float)cuenta) : 0.0f;
}

float spec_snap_bin(const float *db, uint32_t n, float bin_tap,
                    snap_modo_t modo, uint8_t *encontrado)
{
    int32_t tap = (int32_t)(bin_tap + 0.5f);
    int32_t a, b, i, pico;
    float suelo, umbral;

    if (encontrado) { *encontrado = 0U; }
    if (!db || n < 8U) { return bin_tap; }
    if (tap < 0) { tap = 0; }
    if (tap > (int32_t)n - 1) { tap = (int32_t)n - 1; }

    a = tap - SNAP_VENTANA_BINS;
    b = tap + SNAP_VENTANA_BINS;
    if (a < 0) { a = 0; }
    if (b > (int32_t)n - 1) { b = (int32_t)n - 1; }

    pico = a;
    for (i = a; i <= b; i++) {
        if (db[i] > db[pico]) { pico = i; }
    }

    suelo = suelo_local(db, n, tap);
    if (db[pico] - suelo < SNAP_MIN_DB) {
        /* Bajo el dedo no hay nada: se respeta lo que apunto, que a lo mejor
         * es justo lo que queria (moverse a un hueco vacio). */
        return bin_tap;
    }
    if (encontrado) { *encontrado = 1U; }

    umbral = suelo + SNAP_BORDE_DB;

    if (modo == SNAP_USB || modo == SNAP_LSB) {
        /*
         * El borde por donde empieza la señal. Se anda desde el pico hacia
         * el lado que toque mientras se siga estando por encima del umbral,
         * y se para en el primer bin que ya no lo esta.
         *
         * El limite de SNAP_SUELO_BINS pasos no es por rendimiento: es para
         * que una banda entera llena de señales pegadas no arrastre el borde
         * hasta la otra punta de la pantalla.
         */
        int32_t paso = (modo == SNAP_USB) ? -1 : 1;
        int32_t k = pico;
        int32_t dados = 0;

        while (dados < SNAP_SUELO_BINS) {
            int32_t sig = k + paso;
            if (sig < 0 || sig > (int32_t)n - 1) { break; }
            if (db[sig] < umbral) { break; }
            k = sig;
            dados++;
        }
        /* Medio bin hacia fuera: el borde esta entre el ultimo bin con señal
         * y el primero sin ella, no encima del ultimo. */
        return (float)k + 0.5f * (float)paso;
    }

    /*
     * Pico, con decimales. Los bins miden 375 Hz y una señal de CW es mucho
     * mas estrecha que eso, asi que caer al bin entero deja hasta 187 Hz de
     * error - audible. La parabola por los tres puntos de alrededor del
     * maximo da el vertice de verdad; con datos en dB (que ya son
     * logaritmicos) es la interpolacion habitual y baja el error a unas
     * decenas de hercios.
     */
    if (pico > 0 && pico < (int32_t)n - 1) {
        float y0 = db[pico - 1], y1 = db[pico], y2 = db[pico + 1];
        float den = (y0 - 2.0f * y1 + y2);
        if (den < -1e-6f || den > 1e-6f) {
            float d = 0.5f * (y0 - y2) / den;
            if (d > 0.5f)  { d = 0.5f; }
            if (d < -0.5f) { d = -0.5f; }
            return (float)pico + d;
        }
    }
    return (float)pico;
}
