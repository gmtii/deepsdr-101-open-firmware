#ifndef FMC_SIM_H
#define FMC_SIM_H
#include <stdint.h>
#include "cargador.h"

void     fmc_sim_reset(void);            /* todo a 0xFF, como recien borrada */
uint8_t *fmc_sim_mem(void);              /* 0x08000000 en adelante */
uint32_t fmc_sim_borrados(void);
uint32_t fmc_sim_grabados(void);
uint32_t fmc_sim_fallos(void);           /* escrituras que suben bits */
const carga_fmc_t *fmc_sim_iface(void);

#define FMC_SIM_BASE  0x08000000UL
#define FMC_SIM_TAM   0x00080000UL       /* 512 kB, los del VET6 */

#endif
