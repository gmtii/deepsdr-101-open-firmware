# Novedades · What's new

Lo que cambia en cada versión, en corto. El porqué, las medidas y los fallos
que hubo por el camino están en **`CAMBIOS.md`**.

What changes in each release, briefly. The reasoning, the measurements and the
bugs found along the way are in **`CAMBIOS.md`**.

> Una entrada por versión, la más nueva arriba. La versión tiene que ser la
> misma que `CONFIG_FW_VERSION` de `User/config.h`, y eso lo comprueba
> `tools/novedades_check.py` antes de entregar.
>
> One entry per release, newest first. The version must match
> `CONFIG_FW_VERSION` in `User/config.h`; `tools/novedades_check.py` checks
> that before any release goes out.

---

## V3.43 · 2026-10-05

### Qué cambia

- El **RTTY comercial de 50 baudios** ya sale en Ident **sin el `(?)`**:
  comprobado en antena con un boletín marítimo del DWD alemán, 446 Hz de
  salto y 49,7 baudios medidos. Es la segunda firma confirmada de las 26.
- Fuera del firmware, en `tools/`: el empaquetador de emisoras puede ordenar
  cada frecuencia **por cercanía a tu QTH** (`--qth JN97`), así que en una
  frecuencia compartida sale la emisora que de verdad estás oyendo y no la
  primera por orden alfabético. Y las herramientas avisan de que **`BD.BIN`
  no puede pasar de 1 MB**, se ponga el chip de flash que se ponga.

### What's new

- **Commercial 50 Bd RTTY** is now shown in Ident **without the `(?)`**:
  confirmed on air against a German DWD marine weather bulletin, measured at
  446 Hz shift and 49.7 baud. Second confirmed signature out of 26.
- Outside the firmware, in `tools/`: the station packer can sort each
  frequency **by distance from your QTH** (`--qth JN97`), so on a shared
  frequency you get the station you are actually hearing instead of whichever
  comes first alphabetically. The tools also now warn that **`BD.BIN` cannot
  exceed 1 MB**, whatever flash chip is fitted.

## V3.42 · 2026-10-05

### Qué cambia

- Ident **mide la velocidad de símbolo** de una señal aunque no vea tonos
  sueltos, así que ya nombra el MFSK multitono (Olivia y parecidos) en vez de
  encogerse de hombros.

### What's new

- Ident now **measures the symbol rate** even when it cannot see individual
  tones, so it names multi-tone MFSK (Olivia and friends) instead of giving up.

## V3.41 · 2026-10-05

### Qué cambia

- Ident aprende a **medir la cadencia** de una señal: cada cuánto arranca y
  qué parte del ciclo ocupa. Con eso reconoció el **FT8 en antena** por
  primera vez (15 s de ciclo, 86 % ocupado).

### What's new

- Ident learns to **measure a signal's cadence**: how often it starts and how
  much of the cycle it fills. That got **FT8 recognised on air** for the first
  time (15 s cycle, 86 % duty).

## V3.40 · 2026-10-05

### Qué cambia

- Arreglado que Ident **no reconociera el FT8** en una banda real: miraba el
  ancho de una señal sola y en 7.074 hay cuarenta a la vez.

### What's new

- Fixed Ident **failing to recognise FT8** on a real band: it keyed on the
  width of a single signal, and on 7.074 there are forty at once.

## V3.39 · 2026-10-05

### Qué cambia

- **Modo Ident nuevo**: apúntale una señal y te dice qué puede ser, con
  veintitantas firmas conocidas y sus medidas en pantalla.

### What's new

- **New Ident mode**: point it at a signal and it tells you what it might be,
  with twenty-odd known signatures and the measurements on screen.

## V3.38 · 2026-10-05

### Qué cambia

- Los valores largos **ya no se salen de su celda** en Información.
- La barra de "Guardando" **deja de parpadear**.

### What's new

- Long values **no longer spill out of their cell** on the Information screen.
- The "Saving" progress bar **stops flickering**.

## V3.37 · 2026-10-05

### Qué cambia

- Arreglado el **"NO CABE: directorio lleno"** con el pendrive casi vacío: se
  leía sólo el primer sector del directorio raíz, y Windows formatea con 224
  o 512 entradas.

### What's new

- Fixed **"directory full"** with a nearly empty USB stick: only the first
  sector of the FAT12 root directory was being read, while Windows formats
  with 224 or 512 entries.

## V3.36 · 2026-10-05

### Qué cambia

- El cartel de "Guardando" **ya no desaparece en un segundo**: el espectro lo
  repintaba encima cuarenta veces por segundo.

### What's new

- The "Saving" notice **no longer vanishes after a second**: the spectrum was
  repainting over it forty times a second.

## V3.35 · 2026-10-05

### Qué cambia

- Las **capturas de pantalla funcionan también** en los modos digitales y en
  las pantallas de opciones, no sólo en la principal.

### What's new

- **Screenshots now work** in the digital modes and the settings screens too,
  not just on the main one.

## V3.34 · 2026-10-05

### Qué cambia

- Fuera la **línea blanca** del final de las capturas.
- Los **pitidos** suenan en todas las capturas, no sólo en la primera.
- Guardar una captura tarda **ocho segundos menos**: se borra por bloques de
  64 kB en vez de por sectores de 4 kB.

### What's new

- The **white line** at the bottom of screenshots is gone.
- The **beeps** sound on every capture, not just the first.
- Saving a screenshot is **eight seconds faster**: erasing in 64 kB blocks
  instead of 4 kB sectors.

## V3.33 · 2026-10-05

### Qué cambia

- La **captura de pantalla** sale bien y tarda 2,5 veces menos.

### What's new

- **Screen capture** comes out correct and is 2.5× faster.

## V3.32 · 2026-10-05

### Qué cambia

- **Capturas de pantalla al pendrive**, en BMP de 800×480.

### What's new

- **Screenshots to the USB stick**, as 800×480 BMP.

---

Todo lo anterior a V3.32 está en `CAMBIOS.md`, que va por fecha y cuenta el
porqué de cada cosa.

Everything before V3.32 is in `CAMBIOS.md`, which is ordered by date and
explains the reasoning behind each change.
