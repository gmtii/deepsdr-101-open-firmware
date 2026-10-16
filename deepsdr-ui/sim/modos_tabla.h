/* GENERADO por tools/gen_modos.py desde User/main.c - no editar. */
#define MODO_TABLA_FAMS 4
#define MODO_TABLA_IDIOMAS 2   /* 0 = espanol, 1 = ingles */
static const char *const k_modo_fam_nombres[MODO_TABLA_FAMS][MODO_TABLA_IDIOMAS] = {
    { "Analógico", "Analogue" },
    { "Digital", "Digital" },
    { "Imágenes", "Images" },
    { "Aeronáutica", "Aeronautical" },
};

#define MODO_TABLA_N 28
static const struct { const char *label;
                     const char *desc[MODO_TABLA_IDIOMAS]; int fam;
                     const char *demod; }
    k_modo_tabla[MODO_TABLA_N] = {
    { "AM", { "Amplitud modulada", "Amplitude mod." }, 0, "DEMOD_MODE_AM" },
    { "SAM", { "AM síncrona", "Synchronous AM" }, 0, "DEMOD_MODE_SAM" },
    { "SAM-U", { "Sólo la banda alta", "SAM, upper only" }, 0, "DEMOD_MODE_SAM" },
    { "SAM-L", { "Sólo la banda baja", "SAM, lower only" }, 0, "DEMOD_MODE_SAM" },
    { "USB", { "Banda lateral alta", "Upper sideband" }, 0, "DEMOD_MODE_USB" },
    { "LSB", { "Banda lateral baja", "Lower sideband" }, 0, "DEMOD_MODE_LSB" },
    { "NFM", { "FM estrecha", "Narrow FM" }, 0, "DEMOD_MODE_NFM" },
    { "WFM", { "FM ancha", "Wide FM" }, 0, "DEMOD_MODE_WFM" },
    { "CW", { "Telegrafía Morse", "Morse telegraphy" }, 1, "DEMOD_MODE_USB" },
    { "RTTY-L", { "Teletipo en LSB", "Teletype on LSB" }, 1, "DEMOD_MODE_LSB" },
    { "RTTY-U", { "Teletipo en USB", "Teletype on USB" }, 1, "DEMOD_MODE_USB" },
    { "NAVTEX", { "Avisos marítimos", "Marine warnings" }, 1, "DEMOD_MODE_USB" },
    { "WEFAX", { "Mapas del tiempo", "Weather charts" }, 2, "DEMOD_MODE_USB" },
    { "SSTV", { "Fotos por radio", "Pictures over radio" }, 2, "DEMOD_MODE_USB" },
    { "SSTV-FM", { "Fotos por radio (FM)", "Pictures, narrow FM" }, 2, "DEMOD_MODE_NFM" },
    { "HELL", { "Texto dibujado", "Drawn text" }, 2, "DEMOD_MODE_USB" },
    { "APRS", { "Paquete AX.25", "AX.25 packet" }, 1, "DEMOD_MODE_NFM" },
    { "PKT300", { "Paquete de HF", "HF packet" }, 1, "DEMOD_MODE_LSB" },
    { "DSC", { "Llamada marítima", "Maritime DSC" }, 1, "DEMOD_MODE_USB" },
    { "FT8", { "Digital de HF", "HF digital" }, 1, "DEMOD_MODE_USB" },
    { "HFDL", { "Datos de aviones", "Aircraft data" }, 3, "DEMOD_MODE_USB" },
    { "WSPR", { "Balizas de HF", "HF beacons" }, 1, "DEMOD_MODE_USB" },
    { "AIS", { "Barcos", "Ships" }, 1, "DEMOD_MODE_NFM" },
    { "ALE", { "Sondeos de HF", "HF soundings" }, 1, "DEMOD_MODE_USB" },
    { "PSK31", { "Charla en fase", "Phase-shift chat" }, 1, "DEMOD_MODE_USB" },
    { "JTTY", { "Teletipo digital", "Digital teletype" }, 1, "DEMOD_MODE_USB" },
    { "STANAG", { "Módem naval OTAN", "NATO naval modem" }, 1, "DEMOD_MODE_USB" },
    { "IDENT", { "¿Qué señal es?", "What signal is this?" }, 1, "DEMOD_MODE_USB" },
};
