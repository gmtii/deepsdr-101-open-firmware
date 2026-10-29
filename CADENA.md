# La cadena, de la antena al altavoz — y quién se engancha dónde

*Puesto el 29/10/2026 a petición del usuario, y con un motivo concreto
detrás:*

> «cualquier cambio tiene que llevar consigo un estudio de a qué componentes
> de toda la radio puede afectar de principio a fin, porque no puede ser que
> metas un filtro de no sé qué y joda toda la recepción de HFDL»

Y tenía razón. El corrector de imagen de banda ancha entró en la V5.11 para
arreglar el espejo que se veía **en el espectro**. Nadie escribió que corre
dentro de la interrupción del ADC, **sobre todas las muestras de todos los
modos**. Esa frase sola habría bastado para mirar HFDL antes de entregarlo;
sin ella costó dos días, dos radios y un segundo usuario en otro país.

Este fichero existe para que ese recorrido **se pueda leer entero de una
vez**. Hoy está repartido en comentarios de cinco ficheros, y repartido no
se lee.

---

## El recorrido

Cada escalón dice **dónde corre** y **qué hay por debajo**. Tocar un escalón
afecta a TODO lo que hay debajo de él, sin excepciones.

```
  ANTENA
    │
    │  hardware: filtro de entrada, mezclador QSD, oscilador
    │  (ms5351.c o el temporizador del GD32 bajo 300 kHz — lo_gen_gd32.c)
    ▼
  ADC, 96 o 192 kHz, I/Q  ─────────────────────── sdr_rx.c, INTERRUPCIÓN DMA
    │
    ├─ iqbal_bloque()        corrector de espejo: plano + FIR de 4 tomas
    │                        *** TODAS las muestras de TODOS los modos ***
    │                        <- aquí estaba el fallo de HFDL de octubre
    │
    ├─ gancho de bloque      demod_wfm_process_raw()  si FM ancha
    │                        demod_am_process_raw()   en todo lo demás
    ▼
  demod_am_process_raw()  ──────────────────────── demod_am.c, MISMA INTERRUPCIÓN
    │
    ├─ giro grueso del NCO (cuartos de vuelta) ── antes del diezmador
    ├─ diezmado 192→96 si hace falta
    ├─ NCO fino
    ├─ FILTRO DE CANAL  (el ancho que eliges: BW)
    │
    ├── rama AM / SAM / NFM ──────────────────────────────────────────┐
    │     envolvente (s_env)                                          │
    │       ├─ ax25_process / aprs_scope_feed      APRS               │
    │       ├─ ais_modo_mete                       AIS                │
    │       ├─ sstv_process                        SSTV en FM         │
    │       ├─ analiz_feed                         analizador         │
    │       └─ nr_ss_process → anotch_process → silenciador → AGC     │
    │                                                                 │
    └── rama banda lateral (USB/LSB) ─────────────────────────────────┤
          diezmado /4 a 12 kHz  →  s_i_dec, s_q_dec                   │
            │                                                         │
            ├─ hfdl_modo_mete(I,Q)      *** HFDL PINCHA AQUÍ ***      │
            │                           I/Q cruda a 12 kHz, ANTES del │
            │                           Hilbert, del NR, del notch y  │
            │                           del silenciador               │
            │                                                         │
            ▼ fases_combina() → s_ssb_dec (audio real, 12 kHz)        │
            ├─ rtty_process_chunks       RTTY                         │
            ├─ cw_process_chunks         CW                           │
            ├─ navtex_process            NAVTEX                       │
            ├─ ax25_process              APRS en banda lateral        │
            ├─ dsc_process               DSC                          │
            ├─ psk31_process             PSK31                       │
            ├─ wefax_process             FAX                          │
            ├─ sstv_process              SSTV                         │
            ├─ hell_process              Hellschreiber                │
            ├─ wspr_modo_mete            WSPR                        │
            ├─ ale_modo_mete             ALE                         │
            ├─ jtty_modo_mete            JTTY                        │
            ├─ stanag_modo_mete          STANAG                      │
            ├─ ident_modo_mete           IDENT                       │
            ├─ rtty_scope_feed           osciloscopio                │
            │                                                         │
            ├─ nr_ss_process_chunks      reducción de ruido           │
            ├─ anotch_process            notch automático             │
            └─ interpolación a la tasa de salida ─────────────────────┘
                     │
                     ├─ silenciador
                     ├─ agc_bloque
                     ├─ bip_mete
                     ▼
                  ALTAVOZ / USB
```

**Y FT8 no sale en ese árbol a propósito:** se alimenta de la salida de AM
a 12 kHz y de ahí pasa por su propio remuestreador a 6.400 Hz
(`ft8_decimator.c`). Es el único que tiene una tasa propia.

---

## Lo que el bucle principal hace por encima

No está en la cadena de audio pero **compite por la CPU** y por la memoria
prestada, así que también cuenta:

- `sdr_spectrum_waterfall_tick()` — el espectro y la cascada. En los ocho
  modos de panel entero (FT8, HFDL, WSPR, AIS, ALE, JTTY, STANAG, IDENT) no
  hay espectro que pintar, y dentro de esa función vive el estimador del
  corrector de imagen. Ése fue el fallo de octubre: el corrector seguía
  corrigiendo donde su estimador no podía medir.

  **Y cuidado con la frase que había aquí hasta el 29/10/2026**, que decía
  que esa función *no se llama* en esos ocho modos. **Es falsa.** El reparto
  del dibujo es `} else if (drawing_now && modo_panel_entero())` … `} else {
  tick(); }`, con `drawing_now = activo && !s_menu_open`: **basta abrir
  cualquier menú para caer al `else` y que el tick corra**, a propósito. Lo
  que se salta por dentro es sólo el dibujo; la FFT cruda y
  `iqbal_resuelve()` quedan por delante de esa guarda.

  Sobre esa frase falsa se construyó el arreglo del 28/10, y por eso tenía
  un agujero: entrar en HFDL borraba el FIR, abrir Ajustes lo volvía a
  instalar a medio converger, y cerrar el menú lo dejaba congelado dentro de
  la interrupción el resto de la sesión. Hoy la llamada al estimador lleva
  además su guarda `!modo_panel_entero()`.

  La consecuencia para todo lo demás de este fichero: **lo único que impide
  que la cascada escriba encima de los 69 kB mientras un modo los tiene es
  el cerrojo de `waterfall.c`**, no que el tick deje de correr.
- `hfdl_modo_poll()` — el Viterbi de HFDL. Corre **en el bucle principal**,
  no en la interrupción; si el bucle se atrasa, la ráfaga se pierde y se
  cuenta como `P` en la chapa.
- `fax_poll()`, `sstv_poll()`, el autoguardado, el mapa, la captura.
- La unión de 69 kB (`g_ft8_shared_ram`): la comparten FT8, HFDL, WSPR, AIS,
  ALE, DSC, HELL, IDENT, JTTY, STANAG, el fax y el SSTV. Soltar el préstamo
  **borra los 69 kB**.

---

## Cómo se usa esto

En cada entrada de `CAMBIOS.md` va una sección **A QUÉ AFECTA** que recorre
este mapa hacia abajo desde el punto que se toca, y nombra lo que queda por
debajo. No es un trámite: el 90 % de las veces sale «nada más» en dos
líneas, y la vez que no sale es la que vale.

Lo vigila `tools/alcance_check.py`, que no deja entregar sin ella.

Las tres preguntas:

1. **¿Dónde corre?** Interrupción del ADC, interrupción del I2S, o bucle
   principal. Si es una interrupción, cuesta a todos los modos.
2. **¿Qué hay aguas abajo?** Se lee del diagrama. Todo lo que cuelgue por
   debajo del punto tocado está afectado hasta que se demuestre lo
   contrario.
3. **¿Toca memoria compartida o CPU del bucle?** La unión de 69 kB, la banda
   de la pantalla, el buffer del mapa, el tiempo de vuelta del bucle.
