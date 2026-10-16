/* Cuanto cuesta el chrome por si solo, para saber si se puede llamar 30
 * veces por segundo (no se llama, pero conviene saber el peor caso). */
#include "lcd_sim.h"
#include "gfx2.h"
#include "palette.h"
#include "wf_cmap.h"
#include "spec_chrome.h"
#include "spectrum.h"
#include <stdio.h>
const palette_t *g_pal = &k_pal_oscura;
int main(void){
    spec_chrome_t st = {0};
    lcd_cost_t c;
    spectrum_init();
    st.db_min=-110; st.db_max=-20; st.center_hz=7200000; st.span_hz=96000;
    st.demod_px=SPC_TRACE_W/4; st.band=SPC_BAND_BOTH; st.band_hz=4000;
    st.cmap=spectrum_colormap_lut();
    lcd_sim_reset(0); lcd_sim_cost_reset();
    spec_chrome_draw(&st);
    c=lcd_sim_cost();
    printf("chrome completo : %llu ventanas, %llu accesos, %.2f ms a 0/2/0/5\n",
        (unsigned long long)c.windows,(unsigned long long)c.bus_writes,
        lcd_sim_est_us(7,168.0)/1000.0);
    lcd_sim_cost_reset(); spec_chrome_draw_ruler(&st); c=lcd_sim_cost();
    printf("solo la regla   : %llu ventanas, %llu accesos, %.2f ms\n",
        (unsigned long long)c.windows,(unsigned long long)c.bus_writes,
        lcd_sim_est_us(7,168.0)/1000.0);
    return 0;
}
