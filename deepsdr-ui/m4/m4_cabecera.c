/*
 * LO QUE CUESTA LA CABECERA EN LA RADIO, EN INSTRUCCIONES.
 *
 * Aqui esta la razon de que girar el mando se note: la franja de estado
 * ENTERA son unos 15.600 us de composicion -unos 26 ms en la radio- y la
 * cabecera entera casi el doble. Cualquier cosa que las repinte por
 * detente se come el cuadro.
 *
 * Por eso la lectura de frecuencia se repinta acotada por los DOS lados
 * (ver ui_top_draw_freq): la caja de un digito son 600 us, no 15.000.
 */
#include "semi.h"
void pila_di(unsigned long maximo);
#include "gfx2.h"
#include "ui_top.h"
#include "palette.h"
#include "spec_chrome.h"
#include "spectrum.h"

int g_fallos;
const palette_t *g_pal = &k_pal_oscura;

static void fondo(gfx2_surf_t *s, void *c){ (void)c;
    gfx2_fill(s,0,0,GFX2_W,GFX2_H,gfx2_rgb(PAL_SURF_0)); }
int16_t ui_top_knob_x_dbg(uint8_t);
int16_t ui_top_knob_right_dbg(void);

int main(void)
{
    ui_top_state_t st;
    spec_chrome_t ch;
    unsigned long a,b,v; int r; unsigned i;
    static const char *k[4]={"7.102,00","7.103,00","7.104,00","7.105,00"};
    char *p;

    p=(char*)&st; for(i=0;i<sizeof st;i++) p[i]=0;
    p=(char*)&ch; for(i=0;i<sizeof ch;i++) p[i]=0;

    st.freq="7.102,00"; st.freq_digit=6; st.mode="LSB"; st.band="40 m";
    st.clock="21:40"; st.s_units=7; st.agc="MED"; st.bw="0-4,0k";
    st.knob_name="Sinton."; st.knob_value="1 kHz"; st.batt_pct=66;
    st.nco_on=1; st.nco_off="+33k"; st.att="OFF";

    ch.center_hz=7102000UL; ch.span_hz=48000UL;
    ch.db_min=-120.0f; ch.db_max=-20.0f;
    ch.cmap=spectrum_colormap_lut(); ch.en_banda=1U;
    ch.band=SPC_BAND_LOWER; ch.band_hz=2700UL;

    reloj_on();
    gfx2_render_screen(fondo,0);
    ui_top_draw(&st); ui_top_draw_freq(&st); spec_chrome_draw(&ch);

    pr("\nLA CABECERA, EN UN CORTEX-M4 DE VERDAD\n\n");

    a=tic(); for(r=0;r<20;r++){ st.freq=k[r&3]; ui_top_draw_freq(&st);} b=tic();
    v=mide("la caja de un digito (un clic) ", a,b,20);
    tope("un clic", v, 200000UL);

    a=tic(); for(r=0;r<20;r++){ ui_top_draw_status(&st);} b=tic();
    (void)mide("la franja de estado ENTERA     ", a,b,20);

    /* Lo que de verdad cuesta un detente con un ajuste enganchado al mando:
     * la pastilla y los chips, no la cabecera entera. */
    a=tic(); for(r=0;r<20;r++){ ui_top_draw_mando(&st);} b=tic();
    v=mide("la pastilla del mando + chips  ", a,b,20);
    tope("un detente de ajuste", v, 2400000UL);

    st.nco_off="+34k";
    a=tic(); for(r=0;r<20;r++){ st.nco_off=(r&1)?"+34k":"+33k";
                                ui_top_draw_chips(&st);} b=tic();
    v=mide("solo los chips (chapa del NCO) ", a,b,20);
    tope("chapa del NCO", v, 900000UL);
    st.nco_off="+33k"; ui_top_draw_status(&st);

    { int16_t kx=ui_top_knob_x_dbg(0), kr=ui_top_knob_right_dbg();
      a=tic(); for(r=0;r<20;r++){ gfx2_render(kx,64,(int16_t)(kr+2-kx),40,fondo,0);} b=tic();
      (void)mide("SOLO empujar los pixeles       ", a,b,20); }

    a=tic(); for(r=0;r<20;r++){ ui_top_draw(&st);} b=tic();
    (void)mide("la cabecera ENTERA             ", a,b,20);

    a=tic(); for(r=0;r<20;r++){ ch.center_hz=7102000UL+(unsigned)(r*1000);
                                spec_chrome_draw_ruler(&ch);} b=tic();
    (void)mide("la regla de frecuencias        ", a,b,20);

    a=tic(); for(r=0;r<20;r++){ ui_top_draw_smeter(&st);} b=tic();
    (void)mide("el S-metro (ventana estrecha)  ", a,b,20);

    pr("\n");
    pila_di(4096UL);
    pr("\n");
    if (g_fallos) { pr("  HAY FALLOS\n\n"); }
    else          { pr("  TODO CORRECTO\n\n"); }
    salir(); return 0;
}
