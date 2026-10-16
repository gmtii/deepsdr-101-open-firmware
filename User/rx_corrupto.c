#include "sdr_rx.h"

/*
 * ==========================================================================
 * LA REGLA DEL BLOQUE CORRUPTO, FUERA DE LA INTERRUPCION - 10/10/2026
 * ==========================================================================
 *
 * Estaba escrita dentro del manejador del DMA, o sea en el unico sitio del
 * proyecto donde NO se puede medir: no hay forma de darle un bloque
 * fabricado y ver que contesta. Y es una decision que ya se equivoco una
 * vez -en WFM, el 05/10- y que cuando se equivoca no da un error: REPITE
 * 2,67 ms de audio ya sonado, 375 veces por segundo, y eso suena a siseo.
 *
 * Asi que la regla se saca aqui, pura: entra un bloque de I/Q entrelazada
 * y sale si/no. La interrupcion la llama y no hace nada mas. Es lo mismo
 * que ya se hizo con la regla de aparcado del oscilador, que vive en
 * User/nco.c "para poder medirla en el simulador: es la unica parte de
 * todo esto que se puede equivocar sin que se oiga".
 *
 * El banco es sim/corrupto.c.
 */
uint8_t sdr_rx_bloque_corrupto(const int16_t *iq, uint32_t n_muestras)
{
    uint32_t k, malas = 0U;
    int32_t prev_i, prev_q;

    if ((iq == 0) || (n_muestras < 2U)) { return 0U; }

    prev_i = iq[0];
    prev_q = iq[1];
    for (k = 1U; k < n_muestras; k++) {
        int32_t i_v = iq[2U * k];
        int32_t q_v = iq[2U * k + 1U];
        int32_t di = i_v - prev_i;
        int32_t dq = q_v - prev_q;

        if (di < 0) { di = -di; }
        if (dq < 0) { dq = -dq; }
        if ((di > SDR_RX_BLOCK_JUMP_THRESHOLD) ||
            (dq > SDR_RX_BLOCK_JUMP_THRESHOLD)) {
            malas++;
        }
        prev_i = i_v;
        prev_q = q_v;
    }

    return (malas * SDR_RX_BLOCK_BAD_FRACTION_DEN >
            n_muestras * SDR_RX_BLOCK_BAD_FRACTION_NUM) ? 1U : 0U;
}
