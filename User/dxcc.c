#include "dxcc.h"
#include "idioma.h"
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
/* Los 153 nombres, en los dos idiomas desde el 29/09/2026. Van sin acentos
 * a proposito, como estaban. Los 329 PREFIJOS de k_dxcc[] no se tocan:
 * son prefijos de indicativo, no texto. */
/*
 * LOS 153 PAISES, EN EL DESVAN - 30/09/2026.
 *
 * Antes era una tabla de 153 pares de punteros mas sus cadenas sueltas:
 * 1.224 bytes de punteros y 2.648 de texto, los 3.872 en la region FLASH
 * de la aplicacion. Y esa region se habia quedado con 829 bytes libres de
 * 262.144 tras meter los dos idiomas y la pantalla de QTH, o sea que lo
 * siguiente que se anadiera no enlazaba.
 *
 * Aqui van como UN SOLO bloque de texto con sus desplazamientos, los dos en
 * secciones con nombre que el script de enlazado coloca en el desvan
 * (.arriba, region FLASH2, donde habia 10.622 bytes sin usar). El desvan ya
 * guardaba los mapas de bits de las fuentes y las paletas de la cascada por
 * la misma razon.
 *
 * POR QUE UN BLOQUE Y NO PUNTEROS. Las cadenas sueltas las agrupa el
 * compilador en su propia seccion de literales y no se pueden mover una a
 * una: se moverian los 1.224 bytes de punteros y se quedarian abajo los
 * 2.648 de texto, que es la parte gorda. Con un bloque y una tabla de
 * desplazamientos se mueve TODO, y ademas los nombres repetidos comparten
 * sitio.
 *
 * El desplazamiento es uint16_t: el bloque mide 2101 bytes, muy por debajo de
 * los 65.535 que caben. El _Static_assert de abajo lo vigila.
 */
static const char k_pais_txt[] __attribute__((section(".rodata.k_pais_txt"))) =
    "Croacia\0" "Croatia\0" "Malta\0" "Portugal\0"
    "Azores\0" "Alemania\0" "Germany\0" "Bosnia\0"
    "Baleares\0" "Balearics\0" "Canarias\0" "Canaries\0"
    "Ceuta-Melilla\0" "Espana\0" "Spain\0" "Irlanda\0"
    "Ireland\0" "Moldavia\0" "Moldova\0" "Estonia\0"
    "Bielorrusia\0" "Belarus\0" "Francia\0" "France\0"
    "Inglaterra\0" "England\0" "Isla de Man\0" "Isle of Man\0"
    "Irlanda N.\0" "N. Ireland\0" "Jersey\0" "Escocia\0"
    "Scotland\0" "Guernsey\0" "Gales\0" "Wales\0"
    "Hungria\0" "Hungary\0" "Liechtenstein\0" "Suiza\0"
    "Switzerland\0" "Vaticano\0" "Vatican\0" "Italia\0"
    "Italy\0" "Cerdena\0" "Sardinia\0" "Sicilia\0"
    "Sicily\0" "Noruega\0" "Norway\0" "Luxemburgo\0"
    "Luxembourg\0" "Lituania\0" "Lithuania\0" "Bulgaria\0"
    "Austria\0" "Aland\0" "Finlandia\0" "Finland\0"
    "Rep. Checa\0" "Czechia\0" "Eslovaquia\0" "Slovakia\0"
    "Belgica\0" "Belgium\0" "Feroe\0" "Faroes\0"
    "Dinamarca\0" "Denmark\0" "Holanda\0" "Netherlands\0"
    "Eslovenia\0" "Slovenia\0" "Suecia\0" "Sweden\0"
    "Polonia\0" "Poland\0" "Dodecaneso\0" "Dodecanese\0"
    "Creta\0" "Crete\0" "Grecia\0" "Greece\0"
    "San Marino\0" "Turquia\0" "Turkey\0" "Islandia\0"
    "Iceland\0" "Corcega\0" "Corsica\0" "Ucrania\0"
    "Ukraine\0" "Letonia\0" "Latvia\0" "Rumania\0"
    "Romania\0" "Serbia\0" "Macedonia\0" "Kosovo\0"
    "Albania\0" "Gibraltar\0" "Monaco\0" "Montenegro\0"
    "Rusia\0" "Russia\0" "Kazajistan\0" "Kazakhstan\0"
    "Kirguistan\0" "Kyrgyzstan\0" "Tayikistan\0" "Tajikistan\0"
    "Turkmenistan\0" "Uzbekistan\0" "Azerbaiyan\0" "Azerbaijan\0"
    "Georgia\0" "Armenia\0" "EE.UU.\0" "USA\0"
    "Hawai\0" "Hawaii\0" "Alaska\0" "Puerto Rico\0"
    "Canada\0" "Mexico\0" "Argentina\0" "Brasil\0"
    "Brazil\0" "Chile\0" "Colombia\0" "Uruguay\0"
    "Venezuela\0" "Peru\0" "Bolivia\0" "Ecuador\0"
    "Paraguay\0" "Cuba\0" "Rep. Dominic.\0" "Dominican Rep.\0"
    "Costa Rica\0" "Panama\0" "Guatemala\0" "Barbados\0"
    "Trinidad\0" "Antillas Hol.\0" "Neth. Antilles\0" "Martinica\0"
    "Martinique\0" "Guadalupe\0" "Guadeloupe\0" "Guayana Fr.\0"
    "Fr. Guiana\0" "Surinam\0" "Suriname\0" "Granada\0"
    "Grenada\0" "Belice\0" "Belize\0" "Marruecos\0"
    "Morocco\0" "Argelia\0" "Algeria\0" "Tunez\0"
    "Tunisia\0" "Libia\0" "Libya\0" "Egipto\0"
    "Egypt\0" "Sudafrica\0" "South Africa\0" "Cabo Verde\0"
    "Cape Verde\0" "Madeira\0" "Kenia\0" "Kenya\0"
    "Tanzania\0" "Zambia\0" "Zimbabue\0" "Zimbabwe\0"
    "Gabon\0" "Costa Marfil\0" "Ivory Coast\0" "Ghana\0"
    "Nigeria\0" "Senegal\0" "Mauricio\0" "Mauritius\0"
    "Reunion\0" "Madagascar\0" "Japon\0" "Japan\0"
    "China\0" "Taiwan\0" "Corea Sur\0" "South Korea\0"
    "India\0" "Singapur\0" "Singapore\0" "Tailandia\0"
    "Thailand\0" "Malasia\0" "Malaysia\0" "Indonesia\0"
    "Filipinas\0" "Philippines\0" "Australia\0" "Nueva Zelanda\0"
    "New Zealand\0" "Israel\0" "Oman\0" "Emiratos\0"
    "UAE\0" "Qatar\0" "Barein\0" "Bahrain\0"
    "Arabia Saudi\0" "Saudi Arabia\0" "Kuwait\0" "Irak\0"
    "Iraq\0" "Iran\0" "Pakistan\0" "Banglades\0"
    "Bangladesh\0" "Laos\0" "Camboya\0" "Cambodia\0"
    "Vietnam\0" "Mongolia\0" "Guam\0" "N. Caledonia\0"
    "New Caledonia\0" "Polinesia Fr.\0" "Fr. Polynesia\0" "Groenlandia\0"
    "Greenland\0" "Pantelaria\0" "Pantelleria\0";

_Static_assert(sizeof(k_pais_txt) <= 65535U,
               "k_pais_off[] es uint16_t: el bloque de nombres no puede pasar de 64 kB");

static const uint16_t k_pais_off[][IDIOMA_N] __attribute__((section(".rodata.k_pais_off"))) = {
    {     0,     8 },   /* Croacia        Croatia */
    {    16,    16 },   /* Malta          Malta */
    {    22,    22 },   /* Portugal       Portugal */
    {    31,    31 },   /* Azores         Azores */
    {    38,    47 },   /* Alemania       Germany */
    {    55,    55 },   /* Bosnia         Bosnia */
    {    62,    71 },   /* Baleares       Balearics */
    {    81,    90 },   /* Canarias       Canaries */
    {    99,    99 },   /* Ceuta-Melilla  Ceuta-Melilla */
    {   113,   120 },   /* Espana         Spain */
    {   126,   134 },   /* Irlanda        Ireland */
    {   142,   151 },   /* Moldavia       Moldova */
    {   159,   159 },   /* Estonia        Estonia */
    {   167,   179 },   /* Bielorrusia    Belarus */
    {   187,   195 },   /* Francia        France */
    {   202,   213 },   /* Inglaterra     England */
    {   221,   233 },   /* Isla de Man    Isle of Man */
    {   245,   256 },   /* Irlanda N.     N. Ireland */
    {   267,   267 },   /* Jersey         Jersey */
    {   274,   282 },   /* Escocia        Scotland */
    {   291,   291 },   /* Guernsey       Guernsey */
    {   300,   306 },   /* Gales          Wales */
    {   312,   320 },   /* Hungria        Hungary */
    {   328,   328 },   /* Liechtenstein  Liechtenstein */
    {   342,   348 },   /* Suiza          Switzerland */
    {   360,   369 },   /* Vaticano       Vatican */
    {   377,   384 },   /* Italia         Italy */
    {   390,   398 },   /* Cerdena        Sardinia */
    {   407,   415 },   /* Sicilia        Sicily */
    {   422,   430 },   /* Noruega        Norway */
    {   437,   448 },   /* Luxemburgo     Luxembourg */
    {   459,   468 },   /* Lituania       Lithuania */
    {   478,   478 },   /* Bulgaria       Bulgaria */
    {   487,   487 },   /* Austria        Austria */
    {   495,   495 },   /* Aland          Aland */
    {   501,   511 },   /* Finlandia      Finland */
    {   519,   530 },   /* Rep. Checa     Czechia */
    {   538,   549 },   /* Eslovaquia     Slovakia */
    {   558,   566 },   /* Belgica        Belgium */
    {   574,   580 },   /* Feroe          Faroes */
    {   587,   597 },   /* Dinamarca      Denmark */
    {   605,   613 },   /* Holanda        Netherlands */
    {   625,   635 },   /* Eslovenia      Slovenia */
    {   644,   651 },   /* Suecia         Sweden */
    {   658,   666 },   /* Polonia        Poland */
    {   673,   684 },   /* Dodecaneso     Dodecanese */
    {   695,   701 },   /* Creta          Crete */
    {   707,   714 },   /* Grecia         Greece */
    {   721,   721 },   /* San Marino     San Marino */
    {   732,   740 },   /* Turquia        Turkey */
    {   747,   756 },   /* Islandia       Iceland */
    {   764,   772 },   /* Corcega        Corsica */
    {   780,   788 },   /* Ucrania        Ukraine */
    {   796,   804 },   /* Letonia        Latvia */
    {   811,   819 },   /* Rumania        Romania */
    {   827,   827 },   /* Serbia         Serbia */
    {   834,   834 },   /* Macedonia      Macedonia */
    {   844,   844 },   /* Kosovo         Kosovo */
    {   851,   851 },   /* Albania        Albania */
    {   859,   859 },   /* Gibraltar      Gibraltar */
    {   869,   869 },   /* Monaco         Monaco */
    {   876,   876 },   /* Montenegro     Montenegro */
    {   887,   893 },   /* Rusia          Russia */
    {   900,   911 },   /* Kazajistan     Kazakhstan */
    {   922,   933 },   /* Kirguistan     Kyrgyzstan */
    {   944,   955 },   /* Tayikistan     Tajikistan */
    {   966,   966 },   /* Turkmenistan   Turkmenistan */
    {   979,   979 },   /* Uzbekistan     Uzbekistan */
    {   990,  1001 },   /* Azerbaiyan     Azerbaijan */
    {  1012,  1012 },   /* Georgia        Georgia */
    {  1020,  1020 },   /* Armenia        Armenia */
    {  1028,  1035 },   /* EE.UU.         USA */
    {  1039,  1045 },   /* Hawai          Hawaii */
    {  1052,  1052 },   /* Alaska         Alaska */
    {  1059,  1059 },   /* Puerto Rico    Puerto Rico */
    {  1071,  1071 },   /* Canada         Canada */
    {  1078,  1078 },   /* Mexico         Mexico */
    {  1085,  1085 },   /* Argentina      Argentina */
    {  1095,  1102 },   /* Brasil         Brazil */
    {  1109,  1109 },   /* Chile          Chile */
    {  1115,  1115 },   /* Colombia       Colombia */
    {  1124,  1124 },   /* Uruguay        Uruguay */
    {  1132,  1132 },   /* Venezuela      Venezuela */
    {  1142,  1142 },   /* Peru           Peru */
    {  1147,  1147 },   /* Bolivia        Bolivia */
    {  1155,  1155 },   /* Ecuador        Ecuador */
    {  1163,  1163 },   /* Paraguay       Paraguay */
    {  1172,  1172 },   /* Cuba           Cuba */
    {  1177,  1191 },   /* Rep. Dominic.  Dominican Rep. */
    {  1206,  1206 },   /* Costa Rica     Costa Rica */
    {  1217,  1217 },   /* Panama         Panama */
    {  1224,  1224 },   /* Guatemala      Guatemala */
    {  1234,  1234 },   /* Barbados       Barbados */
    {  1243,  1243 },   /* Trinidad       Trinidad */
    {  1252,  1266 },   /* Antillas Hol.  Neth. Antilles */
    {  1281,  1291 },   /* Martinica      Martinique */
    {  1302,  1312 },   /* Guadalupe      Guadeloupe */
    {  1323,  1335 },   /* Guayana Fr.    Fr. Guiana */
    {  1346,  1354 },   /* Surinam        Suriname */
    {  1363,  1371 },   /* Granada        Grenada */
    {  1379,  1386 },   /* Belice         Belize */
    {  1393,  1403 },   /* Marruecos      Morocco */
    {  1411,  1419 },   /* Argelia        Algeria */
    {  1427,  1433 },   /* Tunez          Tunisia */
    {  1441,  1447 },   /* Libia          Libya */
    {  1453,  1460 },   /* Egipto         Egypt */
    {  1466,  1476 },   /* Sudafrica      South Africa */
    {  1489,  1500 },   /* Cabo Verde     Cape Verde */
    {  1511,  1511 },   /* Madeira        Madeira */
    {  1519,  1525 },   /* Kenia          Kenya */
    {  1531,  1531 },   /* Tanzania       Tanzania */
    {  1540,  1540 },   /* Zambia         Zambia */
    {  1547,  1556 },   /* Zimbabue       Zimbabwe */
    {  1565,  1565 },   /* Gabon          Gabon */
    {  1571,  1584 },   /* Costa Marfil   Ivory Coast */
    {  1596,  1596 },   /* Ghana          Ghana */
    {  1602,  1602 },   /* Nigeria        Nigeria */
    {  1610,  1610 },   /* Senegal        Senegal */
    {  1618,  1627 },   /* Mauricio       Mauritius */
    {  1637,  1637 },   /* Reunion        Reunion */
    {  1645,  1645 },   /* Madagascar     Madagascar */
    {  1656,  1662 },   /* Japon          Japan */
    {  1668,  1668 },   /* China          China */
    {  1674,  1674 },   /* Taiwan         Taiwan */
    {  1681,  1691 },   /* Corea Sur      South Korea */
    {  1703,  1703 },   /* India          India */
    {  1709,  1718 },   /* Singapur       Singapore */
    {  1728,  1738 },   /* Tailandia      Thailand */
    {  1747,  1755 },   /* Malasia        Malaysia */
    {  1764,  1764 },   /* Indonesia      Indonesia */
    {  1774,  1784 },   /* Filipinas      Philippines */
    {  1796,  1796 },   /* Australia      Australia */
    {  1806,  1820 },   /* Nueva Zelanda  New Zealand */
    {  1832,  1832 },   /* Israel         Israel */
    {  1839,  1839 },   /* Oman           Oman */
    {  1844,  1853 },   /* Emiratos       UAE */
    {  1857,  1857 },   /* Qatar          Qatar */
    {  1863,  1870 },   /* Barein         Bahrain */
    {  1878,  1891 },   /* Arabia Saudi   Saudi Arabia */
    {  1904,  1904 },   /* Kuwait         Kuwait */
    {  1911,  1916 },   /* Irak           Iraq */
    {  1921,  1921 },   /* Iran           Iran */
    {  1926,  1926 },   /* Pakistan       Pakistan */
    {  1935,  1945 },   /* Banglades      Bangladesh */
    {  1956,  1956 },   /* Laos           Laos */
    {  1961,  1969 },   /* Camboya        Cambodia */
    {  1978,  1978 },   /* Vietnam        Vietnam */
    {  1986,  1986 },   /* Mongolia       Mongolia */
    {  1995,  1995 },   /* Guam           Guam */
    {  2000,  2013 },   /* N. Caledonia   New Caledonia */
    {  2027,  2041 },   /* Polinesia Fr.  Fr. Polynesia */
    {  2055,  2067 },   /* Groenlandia    Greenland */
    {  2077,  2088 },   /* Pantelaria     Pantelleria */
};

/* El nombre del pais `i` en el idioma puesto. */
static const char *pais_txt(uint8_t i)
{
    return &k_pais_txt[k_pais_off[i][idioma()]];
}

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

/* Los 329 prefijos, al desvan por lo mismo que los nombres. Este si se
 * mueve tal cual: pre[5] va DENTRO del struct, no es un puntero a una
 * cadena suelta, asi que el array es un bloque cerrado de 1.974 bytes. */
static const dxcc_t k_dxcc[] __attribute__((section(".rodata.k_dxcc"))) = {
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

const char *dxcc_prefijo(unsigned i)
{
    return (i < DXCC_N) ? k_dxcc[i].pre : 0;
}

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
        if (n > mejor_largo) { mejor_largo = n; mejor = pais_txt(k_dxcc[i].pais); }
    }
    return mejor;
}
