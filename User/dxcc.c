#include "dxcc.h"
#include <stdint.h>

/*
 * La tabla. Ver dxcc.h para que es y, sobre todo, para que NO es.
 *
 * Ordenada por prefijo para poder leerla y para que se vea de un vistazo si
 * falta alguno o hay repetidos - lo comprueba el banco. El orden NO lo usa la
 * busqueda: esa va por prefijo mas largo, no por posicion.
 *
 * Los nombres estan recortados a lo que cabe en la columna (unos 14
 * caracteres, medidos con gfx2_text_w contra el ancho de la columna en
 * ui_digi.c). "Estados Unidos" son 117 px de los 117 que hay. Si algun dia se
 * anade uno mas largo, hay que volver a medir.
 */
/*
 * DOS TABLAS Y NO UNA - 25/09/2026.
 *
 * La primera version metia el nombre del pais dentro de cada fila. Con 244
 * prefijos y 151 paises eso repite "Japon" veintiuna veces, "EE.UU." doce y
 * "Alemania" diez: 4.920 bytes de flash cuando quedaban nueve mil.
 *
 * Asi que los nombres van aparte y las filas guardan un indice. 2.769 bytes,
 * 2.151 menos, y de paso desaparece una clase de fallo: escribir el mismo
 * pais de dos formas distintas en dos filas ("EE.UU." y "EEUU") ya no es
 * posible, porque el nombre solo esta escrito una vez.
 */
static const char *const k_pais[] = {
    "Croacia",
    "Malta",
    "Portugal",
    "Azores",
    "Alemania",
    "Bosnia",
    "Baleares",
    "Canarias",
    "Ceuta-Melilla",
    "Espana",
    "Irlanda",
    "Moldavia",
    "Estonia",
    "Bielorrusia",
    "Francia",
    "Inglaterra",
    "Isla de Man",
    "Irlanda N.",
    "Jersey",
    "Escocia",
    "Guernsey",
    "Gales",
    "Hungria",
    "Liechtenstein",
    "Suiza",
    "Vaticano",
    "Italia",
    "Cerdena",
    "Sicilia",
    "Noruega",
    "Luxemburgo",
    "Lituania",
    "Bulgaria",
    "Austria",
    "Aland",
    "Finlandia",
    "Rep. Checa",
    "Eslovaquia",
    "Belgica",
    "Feroe",
    "Dinamarca",
    "Holanda",
    "Eslovenia",
    "Suecia",
    "Polonia",
    "Dodecaneso",
    "Creta",
    "Grecia",
    "San Marino",
    "Turquia",
    "Islandia",
    "Corcega",
    "Ucrania",
    "Letonia",
    "Rumania",
    "Serbia",
    "Macedonia",
    "Kosovo",
    "Albania",
    "Gibraltar",
    "Monaco",
    "Montenegro",
    "Rusia",
    "Kazajistan",
    "Kirguistan",
    "Tayikistan",
    "Turkmenistan",
    "Uzbekistan",
    "Azerbaiyan",
    "Georgia",
    "Armenia",
    "EE.UU.",
    "Hawai",
    "Alaska",
    "Puerto Rico",
    "Canada",
    "Mexico",
    "Argentina",
    "Brasil",
    "Chile",
    "Colombia",
    "Uruguay",
    "Venezuela",
    "Peru",
    "Bolivia",
    "Ecuador",
    "Paraguay",
    "Cuba",
    "Rep. Dominic.",
    "Costa Rica",
    "Panama",
    "Guatemala",
    "Barbados",
    "Trinidad",
    "Antillas Hol.",
    "Martinica",
    "Guadalupe",
    "Guayana Fr.",
    "Surinam",
    "Granada",
    "Belice",
    "Marruecos",
    "Argelia",
    "Tunez",
    "Libia",
    "Egipto",
    "Sudafrica",
    "Cabo Verde",
    "Madeira",
    "Kenia",
    "Tanzania",
    "Zambia",
    "Zimbabue",
    "Gabon",
    "Costa Marfil",
    "Ghana",
    "Nigeria",
    "Senegal",
    "Mauricio",
    "Reunion",
    "Madagascar",
    "Japon",
    "China",
    "Taiwan",
    "Corea Sur",
    "India",
    "Singapur",
    "Tailandia",
    "Malasia",
    "Indonesia",
    "Filipinas",
    "Australia",
    "Nueva Zelanda",
    "Israel",
    "Oman",
    "Emiratos",
    "Qatar",
    "Barein",
    "Arabia Saudi",
    "Kuwait",
    "Irak",
    "Iran",
    "Pakistan",
    "Banglades",
    "Laos",
    "Camboya",
    "Vietnam",
    "Mongolia",
    "Guam",
    "N. Caledonia",
    "Polinesia Fr.",
    "Groenlandia",
    "Pantelaria",
};

#define PAIS_00 0   /* Croacia */
#define PAIS_01 1   /* Malta */
#define PAIS_02 2   /* Portugal */
#define PAIS_03 3   /* Azores */
#define PAIS_04 4   /* Alemania */
#define PAIS_05 5   /* Bosnia */
#define PAIS_06 6   /* Baleares */
#define PAIS_07 7   /* Canarias */
#define PAIS_08 8   /* Ceuta-Melilla */
#define PAIS_09 9   /* Espana */
#define PAIS_10 10   /* Irlanda */
#define PAIS_11 11   /* Moldavia */
#define PAIS_12 12   /* Estonia */
#define PAIS_13 13   /* Bielorrusia */
#define PAIS_14 14   /* Francia */
#define PAIS_15 15   /* Inglaterra */
#define PAIS_16 16   /* Isla de Man */
#define PAIS_17 17   /* Irlanda N. */
#define PAIS_18 18   /* Jersey */
#define PAIS_19 19   /* Escocia */
#define PAIS_20 20   /* Guernsey */
#define PAIS_21 21   /* Gales */
#define PAIS_22 22   /* Hungria */
#define PAIS_23 23   /* Liechtenstein */
#define PAIS_24 24   /* Suiza */
#define PAIS_25 25   /* Vaticano */
#define PAIS_26 26   /* Italia */
#define PAIS_27 27   /* Cerdena */
#define PAIS_28 28   /* Sicilia */
#define PAIS_29 29   /* Noruega */
#define PAIS_30 30   /* Luxemburgo */
#define PAIS_31 31   /* Lituania */
#define PAIS_32 32   /* Bulgaria */
#define PAIS_33 33   /* Austria */
#define PAIS_34 34   /* Aland */
#define PAIS_35 35   /* Finlandia */
#define PAIS_36 36   /* Rep. Checa */
#define PAIS_37 37   /* Eslovaquia */
#define PAIS_38 38   /* Belgica */
#define PAIS_39 39   /* Feroe */
#define PAIS_40 40   /* Dinamarca */
#define PAIS_41 41   /* Holanda */
#define PAIS_42 42   /* Eslovenia */
#define PAIS_43 43   /* Suecia */
#define PAIS_44 44   /* Polonia */
#define PAIS_45 45   /* Dodecaneso */
#define PAIS_46 46   /* Creta */
#define PAIS_47 47   /* Grecia */
#define PAIS_48 48   /* San Marino */
#define PAIS_49 49   /* Turquia */
#define PAIS_50 50   /* Islandia */
#define PAIS_51 51   /* Corcega */
#define PAIS_52 52   /* Ucrania */
#define PAIS_53 53   /* Letonia */
#define PAIS_54 54   /* Rumania */
#define PAIS_55 55   /* Serbia */
#define PAIS_56 56   /* Macedonia */
#define PAIS_57 57   /* Kosovo */
#define PAIS_58 58   /* Albania */
#define PAIS_59 59   /* Gibraltar */
#define PAIS_60 60   /* Monaco */
#define PAIS_61 61   /* Montenegro */
#define PAIS_62 62   /* Rusia */
#define PAIS_63 63   /* Kazajistan */
#define PAIS_64 64   /* Kirguistan */
#define PAIS_65 65   /* Tayikistan */
#define PAIS_66 66   /* Turkmenistan */
#define PAIS_67 67   /* Uzbekistan */
#define PAIS_68 68   /* Azerbaiyan */
#define PAIS_69 69   /* Georgia */
#define PAIS_70 70   /* Armenia */
#define PAIS_71 71   /* EE.UU. */
#define PAIS_72 72   /* Hawai */
#define PAIS_73 73   /* Alaska */
#define PAIS_74 74   /* Puerto Rico */
#define PAIS_75 75   /* Canada */
#define PAIS_76 76   /* Mexico */
#define PAIS_77 77   /* Argentina */
#define PAIS_78 78   /* Brasil */
#define PAIS_79 79   /* Chile */
#define PAIS_80 80   /* Colombia */
#define PAIS_81 81   /* Uruguay */
#define PAIS_82 82   /* Venezuela */
#define PAIS_83 83   /* Peru */
#define PAIS_84 84   /* Bolivia */
#define PAIS_85 85   /* Ecuador */
#define PAIS_86 86   /* Paraguay */
#define PAIS_87 87   /* Cuba */
#define PAIS_88 88   /* Rep. Dominic. */
#define PAIS_89 89   /* Costa Rica */
#define PAIS_90 90   /* Panama */
#define PAIS_91 91   /* Guatemala */
#define PAIS_92 92   /* Barbados */
#define PAIS_93 93   /* Trinidad */
#define PAIS_94 94   /* Antillas Hol. */
#define PAIS_95 95   /* Martinica */
#define PAIS_96 96   /* Guadalupe */
#define PAIS_97 97   /* Guayana Fr. */
#define PAIS_98 98   /* Surinam */
#define PAIS_99 99   /* Granada */
#define PAIS_100 100   /* Belice */
#define PAIS_101 101   /* Marruecos */
#define PAIS_102 102   /* Argelia */
#define PAIS_103 103   /* Tunez */
#define PAIS_104 104   /* Libia */
#define PAIS_105 105   /* Egipto */
#define PAIS_106 106   /* Sudafrica */
#define PAIS_107 107   /* Cabo Verde */
#define PAIS_108 108   /* Madeira */
#define PAIS_109 109   /* Kenia */
#define PAIS_110 110   /* Tanzania */
#define PAIS_111 111   /* Zambia */
#define PAIS_112 112   /* Zimbabue */
#define PAIS_113 113   /* Gabon */
#define PAIS_114 114   /* Costa Marfil */
#define PAIS_115 115   /* Ghana */
#define PAIS_116 116   /* Nigeria */
#define PAIS_117 117   /* Senegal */
#define PAIS_118 118   /* Mauricio */
#define PAIS_119 119   /* Reunion */
#define PAIS_120 120   /* Madagascar */
#define PAIS_121 121   /* Japon */
#define PAIS_122 122   /* China */
#define PAIS_123 123   /* Taiwan */
#define PAIS_124 124   /* Corea Sur */
#define PAIS_125 125   /* India */
#define PAIS_126 126   /* Singapur */
#define PAIS_127 127   /* Tailandia */
#define PAIS_128 128   /* Malasia */
#define PAIS_129 129   /* Indonesia */
#define PAIS_130 130   /* Filipinas */
#define PAIS_131 131   /* Australia */
#define PAIS_132 132   /* Nueva Zelanda */
#define PAIS_133 133   /* Israel */
#define PAIS_134 134   /* Oman */
#define PAIS_135 135   /* Emiratos */
#define PAIS_136 136   /* Qatar */
#define PAIS_137 137   /* Barein */
#define PAIS_138 138   /* Arabia Saudi */
#define PAIS_139 139   /* Kuwait */
#define PAIS_140 140   /* Irak */
#define PAIS_141 141   /* Iran */
#define PAIS_142 142   /* Pakistan */
#define PAIS_143 143   /* Banglades */
#define PAIS_144 144   /* Laos */
#define PAIS_145 145   /* Camboya */
#define PAIS_146 146   /* Vietnam */
#define PAIS_147 147   /* Mongolia */
#define PAIS_148 148   /* Guam */
#define PAIS_149 149   /* N. Caledonia */
#define PAIS_150 150   /* Polinesia Fr. */
#define PAIS_151 151   /* Groenlandia */
#define PAIS_152 152   /* Pantelaria */

typedef struct { char pre[5]; uint8_t pais; } dxcc_t;

static const dxcc_t k_dxcc[] = {
    /* --- Europa, que es de donde viene casi todo lo que se oye aqui ---- */
    { "9A",   PAIS_00 },
    { "9H",   PAIS_01 },
    { "CT",   PAIS_02 },
    { "CU",   PAIS_03 },
    { "DL",   PAIS_04 },
    { "DA",   PAIS_04 },
    { "DB",   PAIS_04 },
    { "DD",   PAIS_04 },
    { "DF",   PAIS_04 },
    { "DG",   PAIS_04 },
    { "DH",   PAIS_04 },
    { "DJ",   PAIS_04 },
    { "DK",   PAIS_04 },
    { "DM",   PAIS_04 },
    { "DO",   PAIS_04 },
    { "E7",   PAIS_05 },
    { "EA6",  PAIS_06 },
    { "EA8",  PAIS_07 },
    { "EA9",  PAIS_08 },
    { "EA",   PAIS_09 },
    { "EB",   PAIS_09 },
    { "EC",   PAIS_09 },
    { "ED",   PAIS_09 },
    { "EE",   PAIS_09 },
    { "EF",   PAIS_09 },
    { "EG",   PAIS_09 },
    { "EH",   PAIS_09 },
    { "EI",   PAIS_10 },
    { "EJ",   PAIS_10 },
    { "ER",   PAIS_11 },
    { "ES",   PAIS_12 },
    { "EU",   PAIS_13 },
    { "EV",   PAIS_13 },
    { "EW",   PAIS_13 },
    { "F",    PAIS_14 },
    { "G",    PAIS_15 },
    { "GD",   PAIS_16 },
    { "GI",   PAIS_17 },
    { "GJ",   PAIS_18 },
    { "GM",   PAIS_19 },
    { "GU",   PAIS_20 },
    { "GW",   PAIS_21 },
    { "HA",   PAIS_22 },
    { "HB0",  PAIS_23 },
    { "HB",   PAIS_24 },
    { "HG",   PAIS_22 },
    { "HV",   PAIS_25 },
    { "I",    PAIS_26 },
    { "IS0",  PAIS_27 },
    { "IT9",  PAIS_28 },
    { "LA",   PAIS_29 },
    { "LX",   PAIS_30 },
    { "LY",   PAIS_31 },
    { "LZ",   PAIS_32 },
    { "M",    PAIS_15 },
    { "OE",   PAIS_33 },
    { "OH0",  PAIS_34 },
    { "OH",   PAIS_35 },
    { "OK",   PAIS_36 },
    { "OM",   PAIS_37 },
    { "ON",   PAIS_38 },
    { "OY",   PAIS_39 },
    { "OZ",   PAIS_40 },
    { "PA",   PAIS_41 },
    { "PB",   PAIS_41 },
    { "PD",   PAIS_41 },
    { "PE",   PAIS_41 },
    { "PI",   PAIS_41 },
    { "S5",   PAIS_42 },
    { "SM",   PAIS_43 },
    { "SA",   PAIS_43 },
    { "SB",   PAIS_43 },
    { "SK",   PAIS_43 },
    { "SL",   PAIS_43 },
    { "SP",   PAIS_44 },
    { "SQ",   PAIS_44 },
    { "SV5",  PAIS_45 },
    { "SV9",  PAIS_46 },
    { "SV",   PAIS_47 },
    { "SZ",   PAIS_47 },
    { "T7",   PAIS_48 },
    { "TA",   PAIS_49 },
    { "TF",   PAIS_50 },
    { "TK",   PAIS_51 },
    { "UR",   PAIS_52 },
    { "US",   PAIS_52 },
    { "UT",   PAIS_52 },
    { "UU",   PAIS_52 },
    { "UX",   PAIS_52 },
    { "UY",   PAIS_52 },
    { "YL",   PAIS_53 },
    { "YO",   PAIS_54 },
    { "YT",   PAIS_55 },
    { "YU",   PAIS_55 },
    { "Z3",   PAIS_56 },
    { "Z6",   PAIS_57 },
    { "ZA",   PAIS_58 },
    { "ZB",   PAIS_59 },
    { "3A",   PAIS_60 },
    { "4O",   PAIS_61 },
    /* --- Rusia y alrededores ------------------------------------------- */
    { "R",    PAIS_62 },
    { "U",    PAIS_62 },
    { "UA",   PAIS_62 },
    { "UN",   PAIS_63 },
    { "UP",   PAIS_63 },
    { "EX",   PAIS_64 },
    { "EY",   PAIS_65 },
    { "EZ",   PAIS_66 },
    { "UJ",   PAIS_67 },
    { "UK",   PAIS_67 },
    { "4J",   PAIS_68 },
    { "4K",   PAIS_68 },
    { "4L",   PAIS_69 },
    { "EK",   PAIS_70 },
    /* --- America -------------------------------------------------------- */
    { "K",    PAIS_71 },
    { "W",    PAIS_71 },
    { "N",    PAIS_71 },
    { "AA",   PAIS_71 },
    { "AB",   PAIS_71 },
    { "AC",   PAIS_71 },
    { "AD",   PAIS_71 },
    { "AE",   PAIS_71 },
    { "AF",   PAIS_71 },
    { "AG",   PAIS_71 },
    { "AI",   PAIS_71 },
    { "AJ",   PAIS_71 },
    { "AK",   PAIS_71 },
    { "KH6",  PAIS_72 },
    { "KL7",  PAIS_73 },
    { "KP4",  PAIS_74 },
    { "VE",   PAIS_75 },
    { "VA",   PAIS_75 },
    { "VO",   PAIS_75 },
    { "VY",   PAIS_75 },
    { "XE",   PAIS_76 },
    { "LU",   PAIS_77 },
    { "PY",   PAIS_78 },
    { "PP",   PAIS_78 },
    { "PT",   PAIS_78 },
    { "PU",   PAIS_78 },
    { "CE",   PAIS_79 },
    { "HK",   PAIS_80 },
    { "CX",   PAIS_81 },
    { "YV",   PAIS_82 },
    { "OA",   PAIS_83 },
    { "CP",   PAIS_84 },
    { "HC",   PAIS_85 },
    { "ZP",   PAIS_86 },
    { "CO",   PAIS_87 },
    { "CM",   PAIS_87 },
    { "HI",   PAIS_88 },
    { "TI",   PAIS_89 },
    { "HP",   PAIS_90 },
    { "TG",   PAIS_91 },
    { "8P",   PAIS_92 },
    { "9Y",   PAIS_93 },
    { "PJ",   PAIS_94 },
    { "FM",   PAIS_95 },
    { "FG",   PAIS_96 },
    { "FY",   PAIS_97 },
    { "PZ",   PAIS_98 },
    { "J3",   PAIS_99 },
    { "V3",   PAIS_100 },
    /* --- Africa --------------------------------------------------------- */
    { "CN",   PAIS_101 },
    { "7X",   PAIS_102 },
    { "3V",   PAIS_103 },
    { "5A",   PAIS_104 },
    { "SU",   PAIS_105 },
    { "ZS",   PAIS_106 },
    { "D4",   PAIS_107 },
    { "CT3",  PAIS_108 },
    { "5Z",   PAIS_109 },
    { "5H",   PAIS_110 },
    { "9J",   PAIS_111 },
    { "Z2",   PAIS_112 },
    { "TR",   PAIS_113 },
    { "TU",   PAIS_114 },
    { "9G",   PAIS_115 },
    { "5N",   PAIS_116 },
    { "6W",   PAIS_117 },
    { "3B8",  PAIS_118 },
    { "FR",   PAIS_119 },
    { "5R",   PAIS_120 },
    /* --- Asia y Oceania ------------------------------------------------- */
    { "JA",   PAIS_121 },
    { "JE",   PAIS_121 },
    { "JF",   PAIS_121 },
    { "JG",   PAIS_121 },
    { "JH",   PAIS_121 },
    { "JI",   PAIS_121 },
    { "JJ",   PAIS_121 },
    { "JK",   PAIS_121 },
    { "JL",   PAIS_121 },
    { "JM",   PAIS_121 },
    { "JN",   PAIS_121 },
    { "JO",   PAIS_121 },
    { "JP",   PAIS_121 },
    { "JQ",   PAIS_121 },
    { "JR",   PAIS_121 },
    { "JS",   PAIS_121 },
    { "7J",   PAIS_121 },
    { "7K",   PAIS_121 },
    { "7L",   PAIS_121 },
    { "7M",   PAIS_121 },
    { "7N",   PAIS_121 },
    { "BA",   PAIS_122 },
    { "BD",   PAIS_122 },
    { "BG",   PAIS_122 },
    { "BH",   PAIS_122 },
    { "BI",   PAIS_122 },
    { "BY",   PAIS_122 },
    { "BV",   PAIS_123 },
    { "HL",   PAIS_124 },
    { "DS",   PAIS_124 },
    { "6K",   PAIS_124 },
    { "VU",   PAIS_125 },
    { "9V",   PAIS_126 },
    { "HS",   PAIS_127 },
    { "E2",   PAIS_127 },
    { "9M",   PAIS_128 },
    { "YB",   PAIS_129 },
    { "YC",   PAIS_129 },
    { "YD",   PAIS_129 },
    { "DU",   PAIS_130 },
    { "VK",   PAIS_131 },
    { "ZL",   PAIS_132 },
    { "4X",   PAIS_133 },
    { "4Z",   PAIS_133 },
    { "A4",   PAIS_134 },
    { "A6",   PAIS_135 },
    { "A7",   PAIS_136 },
    { "A9",   PAIS_137 },
    { "HZ",   PAIS_138 },
    { "9K",   PAIS_139 },
    { "YI",   PAIS_140 },
    { "EP",   PAIS_141 },
    { "AP",   PAIS_142 },
    { "S2",   PAIS_143 },
    { "XW",   PAIS_144 },
    { "XU",   PAIS_145 },
    { "3W",   PAIS_146 },
    { "JT",   PAIS_147 },
    { "KH2",  PAIS_148 },
    { "FK",   PAIS_149 },
    { "FO",   PAIS_150 },

    /* ------------------------------------------------------------------
     * ANADIDOS EL 25/09/2026, por el dueno del proyecto: "lb4pi no sale
     * pais".
     *
     * Y tenia razon en mas de lo que decia. La primera version ponia UN
     * prefijo por pais -"LA" para Noruega- cuando casi todos tienen un
     * BLOQUE entero: Noruega es LA-LN, Suecia SA-SM, Alemania DA-DR. O sea
     * que acertaba con el indicativo que se me ocurrio de ejemplo y fallaba
     * con los demas del mismo pais. Un fallo que no se ve probando: sale
     * cuando aparece en antena el que falta.
     *
     * Y una trampa que solo se ve escribiendola: en el Reino Unido la M y
     * el 2 llevan las MISMAS letras de region que la G, asi que MM es
     * Escocia y no Inglaterra. Sin estas filas el prefijo corto se lo
     * llevaba todo.
     * ------------------------------------------------------------------ */
    { "LB",   PAIS_29 },
    { "LC",   PAIS_29 },
    { "LD",   PAIS_29 },
    { "LE",   PAIS_29 },
    { "LF",   PAIS_29 },
    { "LG",   PAIS_29 },
    { "LH",   PAIS_29 },
    { "LI",   PAIS_29 },
    { "LJ",   PAIS_29 },
    { "LK",   PAIS_29 },
    { "LL",   PAIS_29 },
    { "LM",   PAIS_29 },
    { "LN",   PAIS_29 },
    { "SC",   PAIS_43 },
    { "SD",   PAIS_43 },
    { "SE",   PAIS_43 },
    { "SF",   PAIS_43 },
    { "SG",   PAIS_43 },
    { "SH",   PAIS_43 },
    { "SI",   PAIS_43 },
    { "SJ",   PAIS_43 },
    { "OU",   PAIS_40 },
    { "OV",   PAIS_40 },
    { "OW",   PAIS_40 },
    { "OX",   PAIS_151 },
    { "DC",   PAIS_04 },
    { "DE",   PAIS_04 },
    { "DI",   PAIS_04 },
    { "DN",   PAIS_04 },
    { "DP",   PAIS_04 },
    { "DQ",   PAIS_04 },
    { "DR",   PAIS_04 },
    { "PC",   PAIS_41 },
    { "PF",   PAIS_41 },
    { "PG",   PAIS_41 },
    { "PH",   PAIS_41 },
    { "SN",   PAIS_44 },
    { "SO",   PAIS_44 },
    { "SR",   PAIS_44 },
    { "3Z",   PAIS_44 },
    { "HF",   PAIS_44 },
    { "OO",   PAIS_38 },
    { "OP",   PAIS_38 },
    { "OQ",   PAIS_38 },
    { "OR",   PAIS_38 },
    { "OS",   PAIS_38 },
    { "OT",   PAIS_38 },
    { "OL",   PAIS_36 },
    { "OF",   PAIS_35 },
    { "OG",   PAIS_35 },
    { "OI",   PAIS_35 },
    { "OJ",   PAIS_35 },
    { "UV",   PAIS_52 },
    { "UZ",   PAIS_52 },
    { "EM",   PAIS_52 },
    { "EN",   PAIS_52 },
    { "EO",   PAIS_52 },
    { "YP",   PAIS_54 },
    { "YQ",   PAIS_54 },
    { "YR",   PAIS_54 },
    { "SW",   PAIS_47 },
    { "SX",   PAIS_47 },
    { "SY",   PAIS_47 },
    { "J4",   PAIS_47 },
    { "CQ",   PAIS_02 },
    { "CR",   PAIS_02 },
    { "CS",   PAIS_02 },
    { "AM",   PAIS_09 },
    { "AN",   PAIS_09 },
    { "AO",   PAIS_09 },
    { "GB",   PAIS_15 },
    { "MD",   PAIS_16 },
    { "MI",   PAIS_17 },
    { "MJ",   PAIS_18 },
    { "MM",   PAIS_19 },
    { "MU",   PAIS_20 },
    { "MW",   PAIS_21 },
    { "2",    PAIS_15 },
    { "2D",   PAIS_16 },
    { "2I",   PAIS_17 },
    { "2J",   PAIS_18 },
    { "2M",   PAIS_19 },
    { "2U",   PAIS_20 },
    { "2W",   PAIS_21 },
    { "IH9",  PAIS_152 },
};

#define DXCC_N ((unsigned)(sizeof k_dxcc / sizeof k_dxcc[0]))

unsigned dxcc_cuantos(void) { return DXCC_N; }

const char *dxcc_pais(const char *indicativo)
{
    unsigned i;
    unsigned mejor_largo = 0U;
    const char *mejor = 0;

    if (indicativo == 0 || indicativo[0] == '\0') { return 0; }

    /*
     * El MAS LARGO gana, y por eso se recorre entera en vez de parar en el
     * primero que encaje: "EA8ABC" empieza por "EA" y tambien por "EA8", y la
     * respuesta buena es la segunda. Parar en el primero dejaria las Canarias
     * como Espana o no segun el orden de la tabla - o sea, segun algo que
     * nadie recordaria mantener.
     */
    for (i = 0U; i < DXCC_N; i++) {
        const char *p = k_dxcc[i].pre;
        unsigned n = 0U;

        while (p[n] != '\0' && indicativo[n] == p[n]) { n++; }
        if (p[n] != '\0') { continue; }          /* no encaja entero */
        if (n > mejor_largo) { mejor_largo = n; mejor = k_pais[k_dxcc[i].pais]; }
    }
    return mejor;
}
