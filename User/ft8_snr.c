/* Ver ft8_snr.h: alli esta el porque de cada cuenta de este fichero. */
#include "ft8_snr.h"
#include <math.h>

/* dB -> potencia lineal. La cascada guarda 2*dB+240 y WF_ELEM_MAG lo
 * devuelve como x*0,5-120, o sea dB con un desplazamiento arbitrario. El
 * desplazamiento se cancela en el cociente, que es justo lo que se hace. */
static float lineal(float db)
{
    /* exp(db*ln10/10) y no powf(10, db/10) - 26/10/2026: powf costaba
     * 1.856 bytes de flash interna y era la unica funcion de la biblioteca
     * que no usaba nadie mas. El resultado se usa en un COCIENTE de dos
     * de estos, asi que la diferencia -1e-7 relativo- se cancela ademas. */
    return expf(db * 0.230258509f);
}

/*
 * get_cand_mag() de ft8_lib es static dentro de decode.c, asi que la cuenta
 * se repite aqui. No es una copia por comodidad: es la MISMA aritmetica de
 * indices que usa el decodificador, y si algun dia cambia la estructura de
 * mag[] esto tiene que cambiar con ella. Por eso esta junta y nombrada.
 */
static const WF_ELEM_T *base_del_candidato(const ftx_waterfall_t *wf,
                                           const ftx_candidate_t *cand)
{
    int off = cand->time_offset;

    off = (off * wf->time_osr) + (int)cand->time_sub;
    off = (off * wf->freq_osr) + (int)cand->freq_sub;
    off = (off * wf->num_bins) + cand->freq_offset;
    return wf->mag + off;
}

float ft8_snr_db(const ftx_waterfall_t *wf, const ftx_candidate_t *cand)
{
    const WF_ELEM_T *base;
    uint16_t histo[256];
    uint32_t n_ruido = 0UL;
    uint32_t simbolos = 0UL;
    double suma_sn = 0.0;          /* potencias lineales: se acumulan en doble */
    float  p_ruido, p_sn, p_sig, snr;
    int sym, i, k;

    if (wf == 0 || cand == 0 || wf->mag == 0) { return FT8_SNR_SUELO_DB; }

    /* Los ocho tonos tienen que caber enteros, y los vecinos del ruido se
     * recortan mas abajo contra el borde. Si ni los ocho caben, esto no es
     * un candidato medible. */
    if (cand->freq_offset < 0 || (cand->freq_offset + 8) > wf->num_bins) {
        return FT8_SNR_SUELO_DB;
    }

    for (i = 0; i < 256; i++) { histo[i] = 0U; }
    base = base_del_candidato(wf, cand);

    for (sym = 0; sym < 79; sym++) {
        const WF_ELEM_T *fila;
        int bloque = cand->time_offset + sym;
        int mayor;

        if (bloque < 0 || bloque >= wf->num_blocks) { continue; }
        fila = base + (sym * wf->block_stride);

        /* SEÑAL+RUIDO: el maximo de los ocho tonos. El maximo conmuta con el
         * logaritmo, asi que se busca sobre el byte crudo y se convierte una
         * sola vez. */
        mayor = (int)WF_ELEM_MAG_INT(fila[0]);
        for (k = 1; k < 8; k++) {
            int v = (int)WF_ELEM_MAG_INT(fila[k]);
            if (v > mayor) { mayor = v; }
        }
        suma_sn += (double)lineal((float)mayor * 0.5f - 120.0f);
        simbolos++;

        /*
         * RUIDO: los vecinos, con guarda a los dos lados. Al histograma va el
         * byte crudo; la mediana sale de ahi sin ordenar nada y sin memoria
         * extra, y como la mediana conmuta con el logaritmo, la mediana de
         * los dB ES la mediana de las potencias.
         */
        for (k = -FT8_SNR_VECINOS; k <= FT8_SNR_VECINOS; k++) {
            int bin;

            if (k >= -FT8_SNR_GUARDA && k <= (7 + FT8_SNR_GUARDA)) { continue; }
            bin = cand->freq_offset + k;
            if (bin < 0 || bin >= wf->num_bins) { continue; }
            histo[(int)WF_ELEM_MAG_INT(fila[k])]++;
            n_ruido++;
        }
    }

    if (simbolos == 0UL || n_ruido == 0UL) { return FT8_SNR_SUELO_DB; }

    /* La mediana del histograma. */
    {
        uint32_t mitad = n_ruido / 2UL;
        uint32_t acum = 0UL;
        int med = 0;

        for (i = 0; i < 256; i++) {
            acum += (uint32_t)histo[i];
            if (acum > mitad) { med = i; break; }
        }
        /* mediana -> media, para ruido exponencial. Ver la cabecera. */
        p_ruido = lineal((float)med * 0.5f - 120.0f) * FT8_SNR_MEDIANA_A_MEDIA;
    }

    if (!(p_ruido > 0.0f)) { return FT8_SNR_SUELO_DB; }

    p_sn  = (float)(suma_sn / (double)simbolos);

    /*
     * Y AQUI SE RESTA EL SESGO DEL MAXIMO DE OCHO, QUE NO ES Pn - 13/10/2026.
     *
     * Restar el ruido medio a secas dejaba el extremo flojo del barrido 4 dB
     * alto: -20 dB puestos salian -16. Y no era un desplazamiento, porque el
     * resto del barrido estaba dentro de 1 dB; era el sesgo que la cabecera
     * ya describia y que nadie estaba restando.
     *
     * Con ruido solo, el maximo de ocho potencias exponenciales no vale la
     * media: vale H8 = 1 + 1/2 + ... + 1/8 = 2,7179 veces la media. Asi que
     * lo que hay que quitarle a "señal+ruido" es Pn*H8, no Pn.
     *
     * Y NO ROMPE EL OTRO EXTREMO: con señal fuerte el maximo ES el tono, el
     * termino que sobra es 1,72*Pn y a esas alturas Pn es despreciable frente
     * a la señal. O sea que la correccion desaparece sola donde no hace
     * falta, que es lo que la hace legitima: sale de la distribucion, no de
     * empujar el numero hasta que el banco pase.
     */
    p_sig = p_sn - p_ruido * FT8_SNR_MAX8;

    if (!(p_sig > 0.0f)) { return FT8_SNR_SUELO_DB; }

    snr = 10.0f * log10f(p_sig / p_ruido) - FT8_SNR_REF_DB + FT8_SNR_CAPTURA_DB;

    if (snr < FT8_SNR_SUELO_DB) { snr = FT8_SNR_SUELO_DB; }
    if (snr > FT8_SNR_TECHO_DB) { snr = FT8_SNR_TECHO_DB; }
    return snr;
}
