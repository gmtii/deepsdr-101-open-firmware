/* GENERADO por tools/bandas_dump.py desde User/main.c - no editar. */
#ifndef BANDAS_TABLA_H
#define BANDAS_TABLA_H

typedef struct { const char *nombre, *nombre_en, *rango, *modo; unsigned long hz; int familia; } banda_t;

static const banda_t banda[] = {
    { "2200 m", "2200 m", "135,7-137,8 kHz", "USB", 136500UL, 0 },
    { "630 m", "630 m", "472-479 kHz", "USB", 475500UL, 0 },
    { "160 m", "160 m", "1.810-2.000 kHz", "LSB", 1840000UL, 0 },
    { "80 m", "80 m", "3.500-3.800 kHz", "LSB", 3750000UL, 0 },
    { "60 m", "60 m", "5.351-5.367 kHz", "USB", 5357000UL, 0 },
    { "40 m", "40 m", "7.000-7.200 kHz", "LSB", 7150000UL, 0 },
    { "30 m", "30 m", "10.100-10.150 kHz", "USB", 10120000UL, 0 },
    { "20 m", "20 m", "14.000-14.350 kHz", "USB", 14250000UL, 0 },
    { "17 m", "17 m", "18.068-18.168 kHz", "USB", 18130000UL, 0 },
    { "15 m", "15 m", "21.000-21.450 kHz", "USB", 21250000UL, 0 },
    { "12 m", "12 m", "24.890-24.990 kHz", "USB", 24950000UL, 0 },
    { "10 m", "10 m", "28,0-29,7 MHz", "USB", 28400000UL, 0 },
    { "6 m", "6 m", "50-52 MHz", "USB", 50150000UL, 0 },
    { "2 m", "2 m", "144-146 MHz", "NFM", 145500000UL, 0 },
    { "OL", "LW", "148-284 kHz", "AM", 198000UL, 1 },
    { "OM", "MW", "526-1.606 kHz", "AM", 1000000UL, 1 },
    { "120 m", "120 m", "2.300-2.495 kHz", "AM", 2400000UL, 1 },
    { "90 m", "90 m", "3.200-3.400 kHz", "AM", 3300000UL, 1 },
    { "75 m", "75 m", "3.900-4.000 kHz", "AM", 3950000UL, 1 },
    { "60 m", "60 m", "4.750-5.060 kHz", "AM", 4900000UL, 1 },
    { "49 m", "49 m", "5.800-6.200 kHz", "AM", 6000000UL, 1 },
    { "41 m", "41 m", "7.200-7.450 kHz", "AM", 7300000UL, 1 },
    { "31 m", "31 m", "9.400-9.900 kHz", "AM", 9600000UL, 1 },
    { "25 m", "25 m", "11.600-12.100 kHz", "AM", 11800000UL, 1 },
    { "22 m", "22 m", "13.570-13.870 kHz", "AM", 13700000UL, 1 },
    { "19 m", "19 m", "15.100-15.830 kHz", "AM", 15400000UL, 1 },
    { "16 m", "16 m", "17.480-17.900 kHz", "AM", 17650000UL, 1 },
    { "15 m", "15 m", "18.900-19.020 kHz", "AM", 18950000UL, 1 },
    { "13 m", "13 m", "21.450-21.850 kHz", "AM", 21600000UL, 1 },
    { "11 m", "11 m", "25.670-26.100 kHz", "AM", 25800000UL, 1 },
    { "NDB", "NDB", "190-535 kHz", "USB", 350000UL, 2 },
    { "NAVTEX", "NAVTEX", "518 kHz", "USB", 518000UL, 2 },
    { "NAVTEX-N", "NAVTEX-N", "490 kHz", "USB", 490000UL, 2 },
    { "NAVTEX-HF", "NAVTEX-HF", "4.209,5 kHz", "USB", 4209500UL, 2 },
    { "FAX 4", "FAX 4", "DWD 3.855 kHz", "USB", 3855000UL, 2 },
    { "FAX 8", "FAX 8", "DWD 7.880 kHz", "USB", 7880000UL, 2 },
    { "FAX 14", "FAX 14", "DWD 13.882,5 kHz", "USB", 13882500UL, 2 },
    { "SSTV 20", "SSTV 20", "14.230 kHz", "USB", 14230000UL, 2 },
    { "SSTV 40", "SSTV 40", "7.165 kHz", "USB", 7165000UL, 2 },
    { "APRS", "APRS", "144,800 MHz", "NFM", 144800000UL, 3 },
    { "APRS 70", "APRS 70", "432,500 MHz", "NFM", 432500000UL, 3 },
    { "Horaria", "Time", "2,5 / 5 / 10 MHz", "AM", 10000000UL, 2 },
    { "CB", "CB", "26,965-27,405 MHz", "AM", 27185000UL, 2 },
    { "FM OIRT", "FM OIRT", "65,8-74 MHz", "WFM", 66300000UL, 3 },
    { "FM", "FM", "CCIR 87,5-108 MHz", "WFM", 100000000UL, 3 },
    { "Aérea", "Air", "108-137 MHz", "AM", 118000000UL, 3 },
    { "Meteo", "Weather", "137-138 MHz", "WFM", 137500000UL, 3 },
    { "Marina", "Marine", "156-162 MHz", "NFM", 156800000UL, 3 },
    { "VHF alta", "VHF high", "162-174 MHz", "NFM", 165000000UL, 3 },
};
#define N_BANDAS ((int)(sizeof banda / sizeof banda[0]))

static const char *const familia_nombre[] = { "Aficionados", "Radiodifusión", "Utilidades", "VHF/UHF" };
static const char *const familia_nombre_en[] = { "Amateur", "Broadcast", "Utility", "VHF/UHF" };

#endif
