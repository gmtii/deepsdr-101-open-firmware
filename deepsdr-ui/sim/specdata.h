#ifndef SPECDATA_H
#define SPECDATA_H

/*
 * Espectro sintetico compartido por los renders "antes" y "despues",
 * para que la comparacion sea honesta: los dos dibujan EXACTAMENTE los
 * mismos datos.
 *
 * Escenario: 41m (7.2 MHz), AM, span +/-96kHz. Suelo de ruido con
 * textura, una emisora fuerte en la portadora (centro), dos vecinas
 * a distinta distancia y un birdie estrecho - el tipo de imagen que
 * se ve de verdad en banda de radiodifusion.
 */

#include <math.h>

#define SPEC_BINS 796

/* devuelve dBFS por bin, aprox -110..-20 */
static void specdata_fill(float *out, int n, unsigned seed)
{
    int i;
    unsigned s = seed ? seed : 1u;
    for (i = 0; i < n; i++) {
        float x = (float)i / (float)n;          /* 0..1 */
        float f = (x - 0.5f) * 192.0f;          /* kHz respecto al VFO */
        float db;

        /* suelo de ruido con una ligera pendiente y ondulacion */
        db = -98.0f + 3.0f * sinf(x * 11.0f) + 2.0f * sinf(x * 37.0f + 1.3f);

        /* ruido pseudoaleatorio reproducible (LCG) */
        s = s * 1103515245u + 12345u;
        db += ((float)((s >> 16) & 0x7FFF) / 32767.0f - 0.5f) * 7.0f;

        /* portadora AM fuerte en el centro + bandas laterales +/-5kHz */
        db += 74.0f * expf(-(f * f) / (2.0f * 0.55f * 0.55f));
        db += 26.0f * expf(-((f - 4.2f) * (f - 4.2f)) / (2.0f * 2.2f * 2.2f));
        db += 26.0f * expf(-((f + 4.2f) * (f + 4.2f)) / (2.0f * 2.2f * 2.2f));

        /* vecina fuerte a -35 kHz */
        db += 52.0f * expf(-((f + 35.0f) * (f + 35.0f)) / (2.0f * 0.7f * 0.7f));
        db += 18.0f * expf(-((f + 35.0f) * (f + 35.0f)) / (2.0f * 3.5f * 3.5f));

        /* vecina debil a +58 kHz */
        db += 22.0f * expf(-((f - 58.0f) * (f - 58.0f)) / (2.0f * 0.8f * 0.8f));

        /* otra apenas asomando a +21 kHz */
        db += 12.0f * expf(-((f - 21.0f) * (f - 21.0f)) / (2.0f * 0.6f * 0.6f));

        /* birdie muy estrecho a -72 kHz (armonico del reloj) */
        db += 30.0f * expf(-((f + 72.0f) * (f + 72.0f)) / (2.0f * 0.25f * 0.25f));

        out[i] = db;
    }
}

#endif /* SPECDATA_H */
