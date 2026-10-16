/* Replica la pantalla 4 (timings del EXMC) con los datos REALES medidos en la
 * placa, para revisar el layout antes de grabar. */
#include "lcd_sim.h"
#include "gfx2.h"
#include "palette.h"
#include "font_ui_14.h"
#include "font_ui_14b.h"
#include "font_ui_18b.h"
#include <stdio.h>

const palette_t *g_pal = &k_pal_oscura;

typedef struct { unsigned buslat, aset, ahld, dset, us; } bench_t;
static const bench_t B[6] = {
    {0,2,0, 5, 25000}, {3,3,3,15, 46000}, {2,2,2, 8, 30700},
    {1,1,1, 5, 23000}, {0,1,1, 3, 21100}, {0,0,1, 2, 21200},
};
#define SCC 200000000u
#define SNT 0x00000502u
#define VTOR 0x08020000u

static char *u2s(char *b, unsigned v){ char t[12]; int n=0,i=0;
    do{t[n++]=(char)('0'+v%10);v/=10;}while(v); while(n)b[i++]=t[--n]; b[i]=0; return b; }
static char *ms2s(char *b, unsigned us){ char t[12]; int i=0; u2s(t,us/1000);
    while(t[i]){b[i]=t[i];i++;} b[i++]=','; b[i++]=(char)('0'+(us%1000)/100); b[i]=0; return b; }
static char *ratio2s(char *b, unsigned n, unsigned d){ unsigned h=d?(n*100/d):0; char t[12]; int i=0;
    u2s(t,h/100); while(t[i]){b[i]=t[i];i++;} b[i++]=','; b[i++]=(char)('0'+(h/10)%10);
    b[i++]=(char)('0'+h%10); b[i]=0; return b; }
static char *x2s(char *b, unsigned v){ static const char hx[]="0123456789ABCDEF"; int i;
    b[0]='0';b[1]='x'; for(i=0;i<8;i++) b[2+i]=hx[(v>>(28-i*4))&0xF]; b[10]=0; return b; }
static char *hz2mhz(char *b, unsigned hz){ unsigned m=(hz+50000)/100000; char t[12]; int i=0,j=0;
    u2s(t,m/10); while(t[j])b[i++]=t[j++]; b[i++]=','; b[i++]=(char)('0'+m%10);
    b[i++]=' ';b[i++]='M';b[i++]='H';b[i++]='z'; b[i]=0; return b; }

static void draw(gfx2_surf_t *s, void *ctx)
{
    char buf[80]; int i; (void)ctx;
    gfx2_fill(s,0,0,GFX2_W,GFX2_H,gfx2_rgb(PAL_SURF_0));
    gfx2_vgrad(s,0,0,GFX2_W,56,gfx2_rgb(PAL_HDR_TOP),gfx2_rgb(PAL_SURF_1));
    gfx2_text(s,16,14,"Timings del EXMC — medido en esta placa",&font_ui_18b,gfx2_rgb(PAL_INK));
    gfx2_text(s,16,66,"Volcado a pantalla completa (800x480). La fila 1 son los timings que deja el",&font_ui_14,gfx2_rgb(PAL_INK_DIM));
    gfx2_text(s,16,84,"bootloader. Ya han sido RESTAURADOS: lo que ves está dibujado con ellos.",&font_ui_14,gfx2_rgb(PAL_INK_DIM));
    gfx2_fill(s,16,110,768,26,gfx2_rgb(PAL_SURF_2));
    gfx2_text(s, 26,115,"buslat",&font_ui_14b,gfx2_rgb(PAL_INK_DIM));
    gfx2_text(s,106,115,"aset",  &font_ui_14b,gfx2_rgb(PAL_INK_DIM));
    gfx2_text(s,176,115,"ahld",  &font_ui_14b,gfx2_rgb(PAL_INK_DIM));
    gfx2_text(s,246,115,"dset",  &font_ui_14b,gfx2_rgb(PAL_INK_DIM));
    gfx2_text(s,340,115,"ms / pantalla",&font_ui_14b,gfx2_rgb(PAL_INK_DIM));
    gfx2_text(s,500,115,"fps máx",&font_ui_14b,gfx2_rgb(PAL_INK_DIM));
    gfx2_text(s,610,115,"coste por píxel",&font_ui_14b,gfx2_rgb(PAL_INK_DIM));

    for(i=0;i<6;i++){
        int16_t ry=(int16_t)(142+i*34); unsigned us=B[i].us?B[i].us:1; int on=(i==0);
        if(on) gfx2_rrect(s,16,ry,768,30,6,gfx2_rgba(PAL_ACCENT,34));
        else if(i&1) gfx2_fill(s,16,ry,768,30,gfx2_rgba(0xFFFFFF,6));
        u2s(buf,B[i].buslat); gfx2_text(s, 26,(int16_t)(ry+6),buf,&font_ui_14b,gfx2_rgb(PAL_INK));
        u2s(buf,B[i].aset);   gfx2_text(s,106,(int16_t)(ry+6),buf,&font_ui_14b,gfx2_rgb(PAL_INK));
        u2s(buf,B[i].ahld);   gfx2_text(s,176,(int16_t)(ry+6),buf,&font_ui_14b,gfx2_rgb(PAL_INK));
        u2s(buf,B[i].dset);   gfx2_text(s,246,(int16_t)(ry+6),buf,&font_ui_14b,gfx2_rgb(PAL_INK));
        ms2s(buf,us);         gfx2_text(s,340,(int16_t)(ry+6),buf,&font_ui_14b,gfx2_rgb(on?PAL_ACCENT:PAL_INK));
        u2s(buf,1000000u/us); gfx2_text(s,500,(int16_t)(ry+6),buf,&font_ui_14b,gfx2_rgb(on?PAL_ACCENT:PAL_INK));
        { unsigned fm=SCC/1000000u, c100=(us*fm*100u)/384000u; char cb[16]; int16_t tx;
          ms2s(cb,c100*10u);
          tx=gfx2_text(s,610,(int16_t)(ry+6),cb,&font_ui_14b,gfx2_rgb(on?PAL_ACCENT:PAL_INK));
          gfx2_text(s,(int16_t)(610+tx+5),(int16_t)(ry+6),"ciclos/escr",&font_ui_14,gfx2_rgb(PAL_INK_MUTE)); }
        if(i>0){ ratio2s(buf,B[0].us,us);
          gfx2_text_in(s,700,(int16_t)(ry+6),76,buf,&font_ui_14b,gfx2_rgb(PAL_INK_DIM),GFX2_ALIGN_R); }
    }
    gfx2_text(s,16,424,"Si alguna fila salió con basura mientras se medía, ese candidato es demasiado",&font_ui_14,gfx2_rgb(PAL_WARN));
    gfx2_text(s,16,442,"agresivo para este panel: quédate con el anterior. Toca para seguir.",&font_ui_14,gfx2_rgb(PAL_WARN));
    { int16_t tx; char b2[80];
      gfx2_hline(s,16,362,768,gfx2_rgb(PAL_LINE));
      tx =gfx2_text(s,16,370,"reloj ",&font_ui_14,gfx2_rgb(PAL_INK_MUTE));
      tx=(int16_t)(tx+gfx2_text(s,(int16_t)(16+tx),370,hz2mhz(b2,SCC),&font_ui_14b,gfx2_rgb(PAL_INK)));
      tx=(int16_t)(tx+gfx2_text(s,(int16_t)(16+tx),370,"   ·   VTOR ",&font_ui_14,gfx2_rgb(PAL_INK_MUTE)));
      tx=(int16_t)(tx+gfx2_text(s,(int16_t)(16+tx),370,x2s(b2,VTOR),&font_ui_14b,gfx2_rgb(PAL_INK)));
      tx=(int16_t)(tx+gfx2_text(s,(int16_t)(16+tx),370,"   ·   SNTCFG del bootloader ",&font_ui_14,gfx2_rgb(PAL_INK_MUTE)));
      gfx2_text(s,(int16_t)(16+tx),370,x2s(b2,SNT),&font_ui_14b,gfx2_rgb(PAL_INK)); }
}

int main(void){ lcd_sim_reset(0); gfx2_render_screen(draw,0);
    lcd_sim_write_ppm("out/07_bench.ppm"); printf("ok\n"); return 0; }
