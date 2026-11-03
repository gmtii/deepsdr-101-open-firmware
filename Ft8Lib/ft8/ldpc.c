//
// LDPC decoder for FT8.
//
// given a 174-bit codeword as an array of log-likelihood of zero,
// return a 174-bit corrected codeword, or zero-length array.
// last 87 bits are the (systematic) plain-text.
// this is an implementation of the sum-product algorithm
// from Sarah Johnson's Iterative Error Correction book.
// codeword[i] = log ( P(x=0) / P(x=1) )
//

#include "ft8_shared_ram.h"
#include "ldpc.h"
#include "constants.h"

#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <stdbool.h>

static int ldpc_check(uint8_t codeword[]);
static float fast_tanh(float x);
static float fast_atanh(float x);

// codeword is 174 log-likelihoods.
// plain is a return value, 174 ints, to be 0 or 1.
// max_iters is how hard to try.
// ok == 87 means success.
/*
 * ===========================================================================
 * ldpc_decode() SE HA IDO, Y NO ES UNA LIMPIEZA - 02/11/2026, banco del M4.
 * ===========================================================================
 *
 * Aqui habia una segunda version del decodificador, la de la biblioteca
 * original, que NADIE llamaba: decode.c usa bp_decode() y tenia la llamada
 * a esta comentada al lado. El enlazador ya la tiraba con --gc-sections, o
 * sea que en la radio no ocupaba ni un byte.
 *
 * Pero declaraba DOS matrices de float[83][174] COMO LOCALES: unos 115 kB
 * de pila. Y la pila de esta radio son 5.632 bytes (_Min_Stack_Size =
 * 0x1600 en GD32F450VE_FLASH.ld), arriba del todo de los 64 kB de TCM. O
 * sea que el dia que alguien descomentara esa linea -que esta ahi, a mano,
 * invitando- la pila se saldria por VEINTE VECES su tamaño, pisando los
 * buffers del DSP que viven en esa misma TCM, y sin un HardFault que lo
 * dijera: por debajo de 0x10000000 simplemente no hay nada.
 *
 * Lo midio el banco del Cortex-M4 pintando la pila y mirando hasta donde
 * llegaba de verdad: 116.388 bytes. Ver deepsdr-ui/m4/start.c.
 *
 * Se borra la funcion Y su declaracion de ldpc.h, para que descomentar la
 * llamada de decode.c ya no compile en vez de reventar en marcha. Lo que
 * se usa, bp_decode(), hace lo mismo con sus matrices en la RAM
 * compartida: ver el comentario de ahi abajo.
 */


//
// does a 174-bit codeword pass the FT8's LDPC parity checks?
// returns the number of parity errors.
// 0 means total success.
//
static int ldpc_check(uint8_t codeword[])
{
    int errors = 0;

    for (int m = 0; m < FTX_LDPC_M; ++m)
    {
        uint8_t x = 0;
        for (int i = 0; i < kFTX_LDPC_Num_rows[m]; ++i)
        {
            x ^= codeword[kFTX_LDPC_Nm[m][i] - 1];
        }
        if (x != 0)
        {
            ++errors;
        }
    }
    return errors;
}

/* ---------------------------------------------------------------------
 * LOS DOS BORRADORES DEL LDPC, FUERA DE LA PILA. 24/09/2026.
 * ---------------------------------------------------------------------
 * Aqui habia "float tov[174][3]" y "float toc[83][7]" declarados dentro de
 * la funcion, o sea en la pila: 4.412 bytes de golpe. En el proyecto de
 * donde viene esto cabian. En esta radio no: la pila entera mide 4.456
 * bytes (ver el comentario de los borradores en User/spi_flash.c y
 * tools/pila.py), y el camino completo hasta aqui -proceso de la ranura,
 * candidato, decodificador- pasa de 7 kB. Se saldria por arriba y pisaria
 * las variables del TCM sin dar un solo aviso.
 *
 * Pasan a .bss. Se puede porque bp_decode() se llama desde un solo sitio,
 * desde el bucle principal, nunca desde una interrupcion y nunca dentro de
 * si misma: no hay dos usos a la vez que puedan pisarse. Cuesta 4.412 bytes
 * de RAM fija, y de la SRAM principal quedan 6.348.
 *
 * Es de las pocas cosas que se tocan de Ft8Lib, que es codigo de otro. Se
 * hace porque no hay alternativa: con esto en la pila, FT8 no puede correr
 * aqui.
 */
/*
 * EN LA MEMORIA COMPARTIDA DESDE EL 10/10/2026, no en .bss propio.
 *
 * Estos dos sumaban 4.412 bytes de SRAM que el enlazador ponia ADEMAS de la
 * cascada. Solo viven mientras FT8 esta puesto -bp_decode() se llama desde
 * ft8_decoder_slot_paso() y de ningun otro sitio-, asi que su sitio es la
 * union, igual que mag[] y las demas. Ver ft8_shared_ram.h.
 */
#define s_tov (g_ft8_shared_ram.ft8.ldpc_tov)
#define s_toc (g_ft8_shared_ram.ft8.ldpc_toc)

void bp_decode(float codeword[], int max_iters, uint8_t plain[], int* ok)
{
#define tov s_tov
#define toc s_toc

    int min_errors = FTX_LDPC_M;

    // initialize message data
    for (int n = 0; n < FTX_LDPC_N; ++n)
    {
        tov[n][0] = tov[n][1] = tov[n][2] = 0;
    }

    for (int iter = 0; iter < max_iters; ++iter)
    {
        // Do a hard decision guess (tov=0 in iter 0)
        int plain_sum = 0;
        for (int n = 0; n < FTX_LDPC_N; ++n)
        {
            plain[n] = ((codeword[n] + tov[n][0] + tov[n][1] + tov[n][2]) > 0) ? 1 : 0;
            plain_sum += plain[n];
        }

        if (plain_sum == 0)
        {
            // message converged to all-zeros, which is prohibited
            break;
        }

        // Check to see if we have a codeword (check before we do any iter)
        int errors = ldpc_check(plain);

        if (errors < min_errors)
        {
            // we have a better guess - update the result
            min_errors = errors;

            if (errors == 0)
            {
                break; // Found a perfect answer
            }
        }

        // Send messages from bits to check nodes
        for (int m = 0; m < FTX_LDPC_M; ++m)
        {
            for (int n_idx = 0; n_idx < kFTX_LDPC_Num_rows[m]; ++n_idx)
            {
                int n = kFTX_LDPC_Nm[m][n_idx] - 1;
                // for each (n, m)
                float Tnm = codeword[n];
                for (int m_idx = 0; m_idx < 3; ++m_idx)
                {
                    if ((kFTX_LDPC_Mn[n][m_idx] - 1) != m)
                    {
                        Tnm += tov[n][m_idx];
                    }
                }
                toc[m][n_idx] = fast_tanh(-Tnm / 2);
            }
        }

        // send messages from check nodes to variable nodes
        for (int n = 0; n < FTX_LDPC_N; ++n)
        {
            for (int m_idx = 0; m_idx < 3; ++m_idx)
            {
                int m = kFTX_LDPC_Mn[n][m_idx] - 1;
                // for each (n, m)
                float Tmn = 1.0f;
                for (int n_idx = 0; n_idx < kFTX_LDPC_Num_rows[m]; ++n_idx)
                {
                    if ((kFTX_LDPC_Nm[m][n_idx] - 1) != n)
                    {
                        Tmn *= toc[m][n_idx];
                    }
                }
                tov[n][m_idx] = -2 * fast_atanh(Tmn);
            }
        }
    }

    *ok = min_errors;
}
#undef tov
#undef toc


// Ideas for approximating tanh/atanh:
// * https://varietyofsound.wordpress.com/2011/02/14/efficient-tanh-computation-using-lamberts-continued-fraction/
// * http://functions.wolfram.com/ElementaryFunctions/ArcTanh/10/0001/
// * https://mathr.co.uk/blog/2017-09-06_approximating_hyperbolic_tangent.html
// * https://math.stackexchange.com/a/446411

static float fast_tanh(float x)
{
    if (x < -4.97f)
    {
        return -1.0f;
    }
    if (x > 4.97f)
    {
        return 1.0f;
    }
    float x2 = x * x;
    // float a = x * (135135.0f + x2 * (17325.0f + x2 * (378.0f + x2)));
    // float b = 135135.0f + x2 * (62370.0f + x2 * (3150.0f + x2 * 28.0f));
    // float a = x * (10395.0f + x2 * (1260.0f + x2 * 21.0f));
    // float b = 10395.0f + x2 * (4725.0f + x2 * (210.0f + x2));
    float a = x * (945.0f + x2 * (105.0f + x2));
    float b = 945.0f + x2 * (420.0f + x2 * 15.0f);
    return a / b;
}

static float fast_atanh(float x)
{
    float x2 = x * x;
    // float a = x * (-15015.0f + x2 * (19250.0f + x2 * (-5943.0f + x2 * 256.0f)));
    // float b = (-15015.0f + x2 * (24255.0f + x2 * (-11025.0f + x2 * 1225.0f)));
    // float a = x * (-1155.0f + x2 * (1190.0f + x2 * -231.0f));
    // float b = (-1155.0f + x2 * (1575.0f + x2 * (-525.0f + x2 * 25.0f)));
    float a = x * (945.0f + x2 * (-735.0f + x2 * 64.0f));
    float b = (945.0f + x2 * (-1050.0f + x2 * 225.0f));
    return a / b;
}

/* Que la union siga teniendo el tamaño que estos dos necesitan: si alguien
 * cambia FTX_LDPC_N o FTX_LDPC_M, esto no compila en vez de desbordar. */
_Static_assert(sizeof(g_ft8_shared_ram.ft8.ldpc_tov) == FTX_LDPC_N * 3 * sizeof(float),
               "ldpc_tov[] de ft8_shared_ram.h ya no cuadra con FTX_LDPC_N");
_Static_assert(sizeof(g_ft8_shared_ram.ft8.ldpc_toc) == FTX_LDPC_M * 7 * sizeof(float),
               "ldpc_toc[] de ft8_shared_ram.h ya no cuadra con FTX_LDPC_M");
