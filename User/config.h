/*
 * config.h - los numeros que alguien va a querer tocar.
 *
 * Anadido el 07/08/2026, pedido por el usuario. La costumbre de este
 * proyecto es dejar cada constante pegada a lo que configura, con un
 * comentario que dice por que vale eso -ver AM_SETTLE_MUTE_BLOCKS en
 * demod_am.c, o MENU_TILE_GAP en main.c-. Eso esta bien para entender POR
 * QUE un numero es el que es, una vez que lo has encontrado; y esta mal
 * para encontrarlo, repartido por ficheros de varios miles de lineas, si
 * no sabes ya donde mirar. Esa costumbre NO cambia y sigue mandando para
 * casi todo: la geometria de la pantalla, los colores, los coeficientes
 * de los filtros y demas se quedan exactamente donde estaban.
 *
 * Este fichero existe SOLO para el puñado de valores que alguien querria
 * cambiar sin tener que leerse antes un comentario de proceso de señal:
 * con que arranca la radio, y como de bruto es el control automatico de
 * ganancia de radiofrecuencia. Si buscas algo que NO esta aqui, sigue
 * viviendo al lado de su propia explicacion en main.c, demod_am.c o
 * aic3204.c; buscalo por su nombre y el comentario de alli te contara
 * las contrapartidas que una linea de #define no puede contar.
 *
 * Cada #define de abajo sigue teniendo UN solo dueño de verdad: el .c que
 * declara la variable o usa el valor. Aqui esta el NUMERO; el fichero
 * dueño tiene la explicacion de que hace y por que ese numero, con un
 * comentario que apunta aqui o al reves. Busca el nombre CONFIG_* para
 * ver todos los sitios donde se usa.
 */
#ifndef CONFIG_H
#define CONFIG_H

#include "demod_am.h" /* for demod_mode_t - CONFIG_START_MODE below */

/* ===================== CON QUE ARRANCA ================================
 * Lo que la radio tiene puesto antes de que toques nada. Como se aplica
 * cada cosa esta en la declaracion de s_tune_hz/s_tune_step_idx de main.c
 * y en la de s_mode de demod_am.c.
 */

/* Frecuencia de arranque, en Hz. 7.150.000 son los 40 metros. Cambiado el
 * 07/08/2026: antes era MS5351_CAPTURED_LO_HZ (90,8 MHz), que no tenia
 * sentido con CONFIG_START_MODE puesto en AM y no en WFM. Ver el
 * comentario de MS5351_CAPTURED_LO_HZ en ms5351.h: esa constante es ya
 * SOLO la frecuencia con la que se repite la prueba de humo del
 * sintetizador -ms5351_tune_captured() desde main(), con los bytes
 * capturados tal cual, y asi se queda-, NO donde acaba la radio: main()
 * resintoniza a este valor justo despues de esa prueba. */
#define CONFIG_TUNE_START_HZ  7150000UL

/* Paso de sintonia al arrancar. 1 kHz desde el 07/08/2026, que es lo
 * sensato para buscar voz en HF en la banda de arranque de aqui arriba.
 *
 * 23/09/2026: esto era un INDICE CRUDO (un "1U" pelado) con un aviso al
 * lado que decia, literalmente, que nada garantizaba que siguiera
 * apuntando a 1 kHz si alguien tocaba la tabla de main.c. Al anadir los
 * pasos de 10 Hz y 500 Hz habria pasado a valer 100 Hz sin un solo
 * aviso del compilador. Ahora es el nombre, no el numero: main.c genera
 * los BAND_STEP_* de su propia lista de pasos y aqui se nombra el que se
 * quiere. Si ese paso desaparece de la lista, esto NO compila, que es
 * exactamente lo que se buscaba.
 *
 * Si, esto hace que config.h nombre un simbolo de main.c, cosa que el
 * comentario anterior evitaba a proposito. Se cambia de opinion a
 * sabiendas: un nombre que falla al compilar vale mas que una
 * independencia que solo servia para que el numero pudiera mentir. */
#define CONFIG_TUNE_START_STEP_IDX  BAND_STEP_1K

/* Modo de demodulacion al arrancar. Ver demod_mode_t en demod_am.h. */
#define CONFIG_START_MODE  DEMOD_MODE_AM

/* Tope del MIC_PGA al arrancar: la ganancia analogica ANTES del
 * convertidor, en pasos de 0,5 dB (40 = 20,0 dB). Ver s_pga_gain_db_x2 en
 * main.c y el margen 0..95 -0 a 47,5 dB- del campo que escribe
 * aic3204_set_pga_gain_db(). Esto es el TECHO del que baja el automatico
 * de radiofrecuencia -mas abajo-, no necesariamente lo que el codec tiene
 * puesto en un momento dado. */
#define CONFIG_PGA_START_DB_X2  40

/* ============ AUTOMATICO DE GANANCIA DE RADIOFRECUENCIA (PGA) ==========
 * El diseño entero esta en el comentario de s_rf_agc_enabled en main.c
 * -viene apagado, se enciende con la casilla RFAGC- y la parte que
 * detecta, en el de rf_clip_scan() en demod_am.c. La forma es la misma
 * que la del automatico digital: rapido para protegerse del recorte,
 * lento para soltar esa proteccion. Lo unico distinto es la escala -varias
 * octavas mas basto en el paso de ganancia y en los tiempos- porque esto
 * escribe por I2C en el codec de verdad, no son cuentas por muestra.
 */

/* Deteccion del recorte, en rf_clip_scan() de demod_am.c: como de cerca
 * del tope de +/-32768 del convertidor tiene que llegar una muestra, y
 * cuantas muestras del mismo bloque tienen que llegar ahi para que el
 * bloque cuente como recortado. Por que hacen falta los dos numeros esta
 * en el comentario de rf_clip_scan(): una sola muestra casi a fondo de
 * escala puede ser una señal fuerte legitima y no recorte; para estar
 * seguro hacen falta varias. */
#define CONFIG_RF_CLIP_THRESHOLD   32000
#define CONFIG_RF_CLIP_MIN_COUNT   3U

/* Cuanto baja el PGA de una vez -2,0 dB, en pasos de 0,5- y hasta donde
 * puede bajar: 30,0 dB, Y ESO PARA EL Rin QUE HAYA PUESTO. Ver
 * rf_agc_poll() en main.c.
 *
 * ESTO ES UN TOPE, NO EL ALCANCE - 24/10/2026. Decia "30,0 dB por debajo
 * de CONFIG_PGA_START_DB_X2" y eso no existe: el techo son 20,0 dB y por
 * debajo de cero el codec no baja. El alcance de verdad es el MENOR de los
 * dos y lo calcula rf_agc_backoff_max() en main.c, porque el techo lo
 * mueve el usuario desde la casilla PGA. Mientras aqui ponia 30 y el techo
 * eran 20, el lazo se pasaba cinco pasos de 50 ms creyendo que actuaba. */
#define CONFIG_RF_AGC_STEP_X2         4U
#define CONFIG_RF_AGC_BACKOFF_MAX_X2  60U

/* El escalon de Rin -la impedancia de entrada-: 6,0 dB, que es la
 * atenuacion por paso que da la hoja de datos para 10k -> 20k -> 40k.
 * Antes de fiarte de los valores de registro de 20k y 40k que esto
 * escribe, leete el aviso de SUPOSICION SIN COMPROBAR que hay en
 * aic3204_set_input_impedance(). */
#define CONFIG_RF_AGC_RIN_STEP_X2     12U

/* Los tiempos: hueco minimo entre dos bajadas seguidas de ganancia o de
 * Rin -el ataque-, y cuanto tiene que llevar la señal sin recortar antes
 * de subir un escalon -la soltada-. Por que la soltada son segundos y no
 * milesimas esta en el comentario de rf_agc_poll(): asi la ganancia no
 * bombea al ritmo de la modulacion de una emisora fuerte, que se oye. */
#define CONFIG_RF_AGC_ATTACK_COOLDOWN_MS   50UL
#define CONFIG_RF_AGC_RELEASE_COOLDOWN_MS  3000UL

/* ===================== DECODIFICADOR DE RTTY ==========================
 * Ver rtty.h y rtty.c. Primer borrador, 08/08/2026, pedido por el
 * usuario. Solo en USB/LSB: el RTTY son dos tonos dentro del paso de
 * banda de una banda lateral. El enganche exacto esta en el comentario
 * "RTTY INTEGRATION" de demod_am.c.
 *
 * *** LOS HERCIOS DE MARCA Y ESPACIO SON LO PRIMERO QUE HAY QUE
 * COMPROBAR CONTRA UNA SEÑAL DE VERDAD ***. Dependen enteramente de TU
 * convenio de recepcion: LSB o USB, la polaridad del desplazamiento de la
 * emisora, el paso de banda de audio de tu radio. No hay un valor
 * universalmente correcto, solo el que cae donde cae en tu montaje. Lo de
 * aqui abajo es un punto de partida razonable y comun en radioaficion
 * -170 Hz de desplazamiento, marca por debajo de espacio-, no una
 * garantia. Para saber donde pican de verdad tus dos tonos, sintoniza una
 * RTTY conocida y mira rtty_get_last_mark_mag() y
 * rtty_get_last_space_mag().
 */
#define CONFIG_RTTY_MARK_HZ    2525.0f
#define CONFIG_RTTY_SPACE_HZ   2075.0f  /* marca + 450 Hz, el convenio comun */
#define CONFIG_RTTY_BAUD       50.0f   /* la velocidad casi universal en HF */

/* Lo que mueve el mando al ajustar marca y espacio en vivo contra el
 * osciloscopio de sintonia. Ver el comentario de rtty_set_mark_space_hz()
 * en rtty.c y la rama de RTTY de tune_encoder_poll() en main.c. Eran 100
 * Hz: con un paso asi de basto el usuario no conseguia centrar las rayas
 * sobre los picos de verdad. Con 10 Hz hay unos 17 chasquidos para cruzar
 * un desplazamiento de 170 Hz, que ya es fino para caer encima de un pico
 * en vez de pasarse siempre. */
#define CONFIG_RTTY_ENCODER_STEP_HZ 10.0f

/* La casilla SHIFT de la pagina RADIO, ver ENCODER_TARGET_RTTY_SHIFT en
 * main.c: paso y limites para ajustar directamente la SEPARACION entre
 * marca y espacio, al contrario que CONFIG_RTTY_ENCODER_STEP_HZ de
 * arriba, que mueve las dos juntas y deja la separacion como estaba. Con
 * 5 Hz el ajuste es fino; los limites cubren los desplazamientos que se
 * usan de verdad -170 y 200 Hz corrientes en HF, hasta 850 en algunos
 * sistemas de VHF/UHF o de desplazamiento ancho- con holgura a los dos
 * lados. */
#define CONFIG_RTTY_SHIFT_STEP_HZ 5.0f
#define CONFIG_RTTY_SHIFT_MIN_HZ  50.0f
#define CONFIG_RTTY_SHIFT_MAX_HZ  1000.0f

/* El diagnostico hablador, caracter a caracter y bit a bit: ver
 * rtty_state_machine() en rtty.c. Saca el codigo Baudot de 5 bits en
 * crudo y si el bit de parada cayo donde tocaba, para CADA intento de
 * caracter, valga o no. Comprobado que funciona el 08/08/2026 -una
 * emision de verdad decodifico limpia en cuanto marca y espacio se
 * ajustaron contra el osciloscopio-, asi que vuelve a 0 para el uso
 * normal. Se pone a 1 otra vez si algun dia hace falta este nivel de
 * detalle: es una linea cada 22 ms de caracter a 45,45 baudios, mucho
 * ruido en el registro para algo que solo sirve persiguiendo un fallo de
 * sincronismo de bit o de trama. */
#define CONFIG_RTTY_DIAG_ENABLED 0U

/* Ahora que el RTTY dejo de ser una herramienta de compilacion de
 * depuracion y es un modo que se elige -RTTY-L y RTTY-U en k_demod_modes[]
 * de main.c-, esto ya no cuelga de la marca de compilacion RTTY_ENABLED,
 * que se quito. Arranca APAGADO: elegir un AM/USB/LSB/NFM/WFM normal en el
 * selector de modos lo vuelve a apagar -ver menu_mode_preset_callback()- y
 * RTTY-L/RTTY-U lo encienden. Ver quien llama a rtty_get_enabled(). */
#define CONFIG_RTTY_ENABLED_DEFAULT 0U

/* ===================== DECODIFICADOR DE CW ============================
 *
 * Telegrafia Morse, 21/09/2026. Solo en USB/LSB, igual que el RTTY y por
 * la misma razon: el CW se oye como UN tono dentro del paso de banda de
 * una banda lateral. Ver demod_am.c, donde se engancha justo al lado del
 * RTTY y sobre el mismo audio ya diezmado a 12 kHz.
 *
 * El tono es el que se quiere OIR: el operador sintoniza hasta que el
 * pitido suena a esta frecuencia. 700 Hz es lo mas comun; a quien tenga
 * el oido cansado en agudos le ira mejor 600, y a quien tenga ruido de
 * red 800.
 *
 * La velocidad NO es un ajuste que mande: el decodificador la estima y
 * la persigue solo. Esto es solo por donde empieza a buscar, y sirve
 * para enganchar antes. Medido en el simulador: partiendo de 20 PPM
 * decodifica bien de 10 a 45 PPM sin que nadie le diga nada.
 */
#define CONFIG_CW_PITCH_HZ      700.0f
#define CONFIG_CW_WPM_HINT       20.0f

/* ===================== IDENTIDAD DEL FIRMWARE =========================
 * Lo que sale en Ajustes -> Equipo -> Información. La fecha y la hora NO
 * se ponen aqui: salen de __DATE__ y __TIME__, o sea del momento de
 * compilar, y asi no hay forma de que digan una cosa y el binario sea
 * otra. Esto es lo unico que hay que subir a mano al sacar version.
 *
 * NUMERACION. 24/09/2026. Hasta hoy ponia "Rediseño 11", que contaba las
 * entregas del rediseño y no decia nada de la radio. Desde aqui se llama
 * V<mayor>.<menor>, se sigue por donde iba -el 11 es el mismo 11- y sube
 * de uno en uno: V1.11, V1.12 ... V1.99 y la siguiente es V2.00. O sea
 * que el menor NO es un decimal: 1.9 no existe, existe 1.09, y despues
 * del 1.99 no viene 1.100.
 *
 * Por eso las dos cifras van como TEXTO y no como numero: escritas como
 * numero, el cero delante las convertiria en octal -y 08 y 09 ni siquiera
 * son octal valido, o sea que dos de cada diez versiones no compilarian-.
 * El _Static_assert de abajo es el que obliga a que el menor lleve
 * siempre sus dos cifras: "5" no pasa, "05" si. */
#define CONFIG_FW_MAYOR    "6"
#define CONFIG_FW_MENOR "06"   /* SIEMPRE dos cifras: 00..99 */
#define CONFIG_FW_VERSION  "V" CONFIG_FW_MAYOR "." CONFIG_FW_MENOR

_Static_assert(sizeof(CONFIG_FW_MENOR) == 3,
               "CONFIG_FW_MENOR tiene que llevar dos cifras: \"05\", no \"5\"");

#endif /* CONFIG_H */
