#ifndef DEMOD_AM_H
#define DEMOD_AM_H

#include <stdint.h>

/*
 * El demodulador de AM por envolvente, desde el I/Q en banda base hasta el
 * altavoz.
 *
 * EL CAMINO DE LA SEÑAL. El detector en cuadratura mezcla lo que entra por
 * la antena con el oscilador local en cuadratura, asi que una emisora
 * sintonizada en el VFO llega aqui como una señal compleja en banda base
 * centrada en continua, o cerca. En AM la informacion que se transmite ES
 * la envolvente, y la envolvente de la banda base compleja es simplemente
 * |I + jQ|, que tiene la propiedad clasica de ser INSENSIBLE a un error
 * pequeño de sintonia: un desplazamiento de la portadora solo gira el
 * fasor, y |z| no cambia. No hace falta batir a cero con la emisora:
 * suena igual en cualquier punto al alcance del filtro de audio.
 *
 * La cadena, bloque a bloque, a los 192 kHz enteros y sin diezmar -el
 * convertidor de salida va a 192 kHz por el mismo bus I2S de todas
 * formas-. Desde el 30/07/2026 las dos etapas mas caras van por CMSIS-DSP
 * (arm_biquad_cascade_df1_f32 y arm_cmplx_mag_f32) en vez de por un bucle
 * escalar escrito a mano muestra a muestra; por que eso necesito un
 * pequeño apaño encima del CMSIS-Core anterior al 5.0 de este proyecto
 * esta en CMSIS/DSP/Include/cmsis_compiler.h. Lo demas -el bloqueador de
 * continua y el automatico de ganancia- es lo bastante barato por muestra
 * como para que una llamada de bloque de CMSIS no se pagara sola, asi que
 * se queda escalar:
 *
 *   0a. BAJADA EN FRECUENCIA: correr digitalmente el I/Q crudo
 *      DEMOD_IF_OFFSET_HZ -el por que esta en la nota de SINTONIA POR
 *      FRECUENCIA INTERMEDIA BAJA, mas abajo-, y solo mientras s_mix_hz
 *      no sea cero. Saltandose esto, todo lo que viene detras se queda
 *      igual que antes.
 *   0b. FILTRO DE CANAL: un paso bajo Butterworth de cuarto orden -dos
 *      biquads DF1 de CMSIS en cascada- aplicado IGUAL a I y a Q con
 *      arm_biquad_cascade_df1_f32(), que es un paso bajo complejo
 *      centrado en el VFO: -3 dB a unos 4 kHz y unos -57 dB a +/-20 kHz.
 *      Esto es lo que hace que el receptor demodule LA LINEA DEL CENTRO
 *      del panadaptador en vez de oir a la emisora mas fuerte de toda la
 *      ventana de +/-96 kHz. Tiene que ir ANTES del detector de
 *      envolvente: |z| no es lineal e intermodularia todo lo que viera.
 *      Los coeficientes de CHF_COEFFS se diseñaron aparte, por
 *      transformada bilineal con precompensacion -ver el guion de diseño
 *      que citan las notas del proyecto-: para retocar el filtro se
 *      regenera el array de diez valores entero, no se editan numeros
 *      sueltos a mano.
 *   1. |z| con arm_cmplx_mag_f32() sobre el I/Q ya filtrado e
 *      intercalado: el modulo de verdad, usando la raiz cuadrada por
 *      hardware VSQRT.F32 del Cortex-M4F, en lugar de la aproximacion
 *      alfa-max-beta-min de antes. Ya no hace falta cambiar precision por
 *      velocidad cuando la raiz es una sola instruccion de la unidad de
 *      coma flotante y no una llamada a libm.
 *   2. Bloqueador de continua, un paso alto de un polo a unos 15 Hz:
 *      quita el termino de continua de la portadora para que el
 *      automatico de ganancia y el convertidor solo vean audio. Se deja
 *      como bucle escalar muestra a muestra: es una sola multiplicacion
 *      con acumulacion y no compensa una llamada de bloque de CMSIS.
 *   3. Paso bajo de audio: otro Butterworth de cuarto orden -dos biquads
 *      DF1 mas de CMSIS, con instancia y estado propios, aparte de los
 *      del filtro de canal-, -3 dB a unos 6 kHz. Cae mas rapido -unos 24
 *      dB por octava- que las dos secciones de un polo a las que
 *      sustituye -unos 12-, con el mismo codo.
 *   4. Automatico de ganancia: seguidor de pico de ataque instantaneo y
 *      soltada lenta, que normaliza a un nivel de salida comodo, con la
 *      ganancia acotada para que el ruido puro no se amplifique a fondo
 *      de escala. Escalar: es un lazo de control lleno de bifurcaciones,
 *      no un filtro de los que CMSIS tiene resueltos por bloques.
 *   5. La misma muestra a izquierda y derecha, en entero de 16 bits, al
 *      buffer de transmision del I2S.
 *
 * LO QUE OCUPA EN RAM -todo estatico, en .bss y apto para interrupcion-,
 * contando lo que añadio el paso a CMSIS-DSP: i_buf y q_buf (512*4*2 =
 * 4.096 B) + iq_cplx (512*2*4 = 4.096 B) + env (512*4 = 2.048 B) + el
 * estado de los biquads (3 instancias * 2 etapas * 4 floats * 4 B = 96 B)
 * = unos 10,2 kB. La cadena de banda lateral diezmada, del 30/07/2026,
 * añade: el estado de los dos diezmadores ((161+512-1)*4*2 = 5.376 B) +
 * el del Hilbert ((63+32-1)*4 = 376 B) + el del interpolador
 * ((10+32-1)*4 = 164 B) + los buffers de trabajo a 12 kHz (4*32 floats
 * mas el historico y el trabajo del retardo, 31+63 floats = unos 890 B) =
 * unos 6,8 kB mas, unos 17 kB en total. El WFM, del 31/07/2026, añade su
 * propio estado de biquad para el paso bajo de audio (2 etapas * 4 floats
 * * 4 B = 32 B) mas los escalares del discriminador y el desenfasis (3
 * floats, 12 B): despreciable, los 17 kB siguen siendo buenos salvo
 * redondeo. El NFM -tambien del 31/07/2026, justo despues del WFM- añade
 * su propio estado de filtro de canal, del mismo tamaño que el del paso
 * bajo del WFM (2 instancias * 2 etapas * 4 floats * 4 B = 64 B) y ningun
 * escalar nuevo, porque comparte el estado del discriminador con el WFM;
 * otra vez despreciable. Con el buffer s_audio_out ya existente y todo lo
 * de fft.c y waterfall.c, sigue cabiendo de sobra en la SRAM del
 * GD32F450VET6.
 *
 * SINTONIA POR FRECUENCIA INTERMEDIA BAJA: la historia (30/07/2026), por
 * que existe y por que se intento dos veces. Esto es un detector en
 * cuadratura de conversion directa, o sea de frecuencia intermedia cero,
 * asi que sintonizar el oscilador local justo encima de la emisora pone
 * TANTO la portadora que se quiere COMO el artefacto que genera el propio
 * oscilador al mezclarse consigo mismo exactamente en 0 Hz de la banda
 * base compleja. En AM no se puede quitar esa fuga con un filtro de
 * muesca en continua: la portadora ES lo que necesita la deteccion de
 * envolvente, asi que quitarla se lleva la señal por delante. Ese es el
 * borron de distorsion que siempre se sienta en la linea del centro: es
 * la arquitectura de frecuencia intermedia cero, no esta cadena de
 * filtros.
 *
 *   PRIMER INTENTO (Fs/8 = 24 kHz, con tabla de seno y coseno): se hizo,
 *   luego dio un suelo de ruido de banda ancha y un espectro saltarin
 *   sobre la placa, y se deshizo entero. El usuario confirmo que las
 *   señales del I2S y el oscilador local del MS5351 estaban bien cada uno
 *   por su lado, lo que descarto "la resintonia ha roto el oscilador"
 *   como explicacion.
 *
 *   LA CAUSA DE VERDAD, encontrada despues y sin relacion con la
 *   sintonia: EL MISMO DIA la compilacion del proyecto entero paso de -O0
 *   a -O2 para arreglar la tasa de cuadros. delay_i2c() de i2c_bitbang.c
 *   -un bucle de contar NOP sin calibrar, hay que reconocerlo- salio mas
 *   apretado con -O2, y su retardo real en reloj de pared se encogio lo
 *   justo para comerse los tiempos de flanco del I2C a pedales en el bus
 *   que comparten el codec AIC3204 Y el MS5351. O sea: una escritura de
 *   I2C que salio mal durante la puesta en marcha del codec o del
 *   oscilador, y no el desplazamiento de sintonia. Arreglado clavando
 *   delay_i2c() -y el delay_us_approx() de touch.c, igual de sin
 *   calibrar- a -O0 con __attribute__((optimize("O0"))), independiente de
 *   los CFLAGS del proyecto desde entonces.
 *
 *   SEGUNDO INTENTO (este, Fs/4 = 48 kHz, girando por cambios de signo):
 *   una vez encontrado y arreglado el fallo de verdad, se volvio a
 *   intentar la sintonia por frecuencia intermedia baja con otro
 *   algoritmo, mas simple, que el usuario aporto porque ya lo tenia
 *   funcionando en otro sitio: justo en Fs/4 el giro e^(+j*2*pi*n/4)
 *   recorre 1, j, -1, -j, asi que NO hace falta ninguna multiplicacion,
 *   solo cambios de signo y un intercambio de I con Q. La correspondencia
 *   exacta muestra a muestra esta en el comentario de bloque que hay
 *   encima de s_mix_hz en demod_am.c.
 *
 * COMO FUNCIONA, valga el desplazamiento que valga: el oscilador local se
 * sintoniza DEMOD_IF_OFFSET_HZ por debajo de la emisora elegida -ver
 * tune_encoder_poll() en main.c-, asi que la señal que se quiere llega
 * lejos de 0 Hz en el I/Q crudo mientras el artefacto de la fuga del
 * oscilador -que se mezcla consigo mismo a 0 Hz se sintonice donde se
 * sintonice- se queda donde estaba. Despues demod_am_process_raw() mezcla
 * el bloque digitalmente de vuelta para que la señal buena caiga en
 * continua, justo donde CHF_COEFFS ya la espera y sin rediseñar ningun
 * filtro, y eso deja el artefacto de la fuga fuera del paso de banda de
 * +/-4 kHz del filtro de canal. s_mix_hz -ver demod_am.c- mantiene la
 * bajada digital de acuerdo con lo que de verdad esta programado en el
 * oscilador: empieza en 0, porque la sintonia de arranque sigue siendo la
 * repeticion byte a byte de lo capturado, sin desplazamiento -ver main.c-,
 * y solo tune_encoder_poll() la enciende, una vez que el oscilador esta
 * de verdad sintonizado con desplazamiento. Que esos dos se
 * desincronicen saca la señal buena FUERA del paso de banda del filtro de
 * canal en vez de meterla dentro.
 *
 * EFECTO SECUNDARIO EN EL PANADAPTADOR: la FFT de la cascada y del
 * espectro sigue leyendo el I/Q crudo, sin desplazar, de
 * sdr_rx_poll_block_iq(), y su linea central esta definida como "donde
 * este el oscilador local". O sea que con esta sintonia desplazada, una
 * emisora en la que te pongas aparecera DEMOD_IF_OFFSET_HZ fuera del
 * centro exacto en vez de justo encima de la linea. Es lo esperado y es
 * como se ven normalmente los receptores reales que usan este truco; es
 * solo estetico, el audio demodulado esta bien de las dos maneras. Si se
 * quiere que la linea central del panadaptador siga a la emisora ELEGIDA
 * en vez de al oscilador crudo, eso es otro cambio, en spectrum.c y
 * main.c -correr el eje que se dibuja, o aplicar la misma bajada antes de
 * la FFT-, y queda fuera de lo que cuenta este fichero.
 *
 * BANDA LATERAL (USB/LSB): añadida el 30/07/2026, por el metodo de
 * desfase y con arquitectura DIEZMADA -el mismo dia, en una segunda
 * vuelta-. Comparte con el AM los pasos 0a a 0c sin cambios -bajada en
 * frecuencia y filtro de canal- y luego se separa: diezma I y Q por 16
 * hasta 12 kHz (arm_fir_decimate_f32), desplaza Q con el Hilbert y
 * retrasa I para que casen a esa velocidad, los combina en audio de una
 * sola banda lateral, e interpola de vuelta hasta 192 kHz
 * (arm_fir_interpolate_f32) directamente dentro del buffer compartido
 * s_env[]. Todo lo que viene detras -bloqueador de continua, paso bajo de
 * audio, automatico de ganancia y salida- es identico al de AM. Los
 * numeros del diseño estan en el comentario de CADENA que hay encima de
 * DECIM_COEFFS en demod_am.c.
 *
 * POR QUE DIEZMADA: la primera vuelta corria un Hilbert FIR a los 192 kHz
 * enteros, y las cuentas de ciclos de verdad con el DWT enseñaron el
 * problema. La interrupcion cabia en su propio presupuesto de bloque -un
 * 79 % con 63 tomas- pero con prioridad 6 en el NVIC ese ciclo de trabajo
 * mataba de hambre al bucle principal y estiraba los tiempos de cuadro
 * entre 3,6 y 4,8 veces: el espectro, el tactil y el mando iban a rastras,
 * y eso se informo como "el sistema se cuelga al cambiar de modo". A 12
 * kHz el Hilbert cubre de 300 Hz a 6 kHz MUCHO mejor de lo que la version
 * a velocidad entera podria haber cubierto nunca -|H| = 0,913 a 300 Hz
 * contra 0,164- y la cadena de banda lateral entera cuesta una fraccion
 * de la vieja: el diezmador solo evalua a la velocidad de salida, asi que
 * hasta sus 161 tomas de antialias salen baratas. Se comprueba sobre la
 * placa con demod_am_get_last_cycles(): se espera que caiga cerca de los
 * 136.000 ciclos del camino de AM, lejos de los 422.000 de antes.
 *
 * DONDE SE EJECUTA, Y ESTO IMPORTA: demod_am_process_raw() esta pensada
 * para correr desde la interrupcion de DMA de recepcion -se registra con
 * sdr_rx_set_block_hook()-, NO desde el bucle principal. El bucle
 * principal se bloquea mas de lo que dura un bloque de 2,67 ms mientras
 * pinta un cuadro de espectro, asi que el audio alimentado desde alli se
 * cortaria unas treinta veces por segundo. Dentro de la interrupcion es
 * inmune a lo que este haciendo la pantalla. La unidad de coma flotante
 * se puede usar aqui sin problema: el apilado perezoso del Cortex-M4F se
 * encarga de los floats en contexto de interrupcion.
 */

/* Fixed output gain trim applied after the AGC (1.0 = none). */
#define DEMOD_AM_GAIN 1.0f

/* Demodulation mode. AM is the default (matches all behavior before
 * 30/07/2026). USB/LSB share the same down-mix + channel filter
 * front-end as AM - see the SSB note above for how they diverge
 * afterwards, and its known low-frequency limitation. NFM ALSO shares
 * that down-mix front-end (unlike WFM) but with its OWN, wider
 * channel filter - see the NFM note below. WFM shares neither - see
 * its own note below that. */
typedef enum {
    DEMOD_MODE_AM = 0,
    DEMOD_MODE_USB,
    DEMOD_MODE_LSB,
    DEMOD_MODE_NFM,
    DEMOD_MODE_WFM,
    DEMOD_MODE_SAM /* synchronous AM (both sidebands) - 21/08/2026, see sam.h */
} demod_mode_t;

/*
 * NFM -FM de banda estrecha, los canales de voz tipo radioaficion o PMR-,
 * añadido el 31/07/2026 justo despues del WFM, y hecho a proposito para
 * compartir con el WFM todo lo que tenga sentido compartir y separar lo
 * que de verdad es distinto:
 *
 * COMPARTE CON EL WFM: el discriminador en si -retrasar, conjugar,
 * multiplicar y sacar el angulo con atan2f(); la deduccion entera esta
 * en la nota del WFM, mas abajo-, sacado a una sola funcion auxiliar
 * fm_discriminate() en demod_am.c a la que llaman los dos modos en vez
 * de duplicar el bucle, y el estado de la muestra anterior que comparten
 * (s_fm_i_prev y s_fm_q_prev). Compartirlo no da problemas porque el WFM
 * y el NFM nunca estan activos a la vez; es la misma contrapartida de
 * "una muestra rancia al entrar en el modo" que ya se acepto para ese
 * estado.
 *
 * SE DIFERENCIA DEL WFM EN: el NFM se queda con la bajada en frecuencia y
 * el filtro de canal de delante -como AM, USB y LSB-, solo que usa SU
 * PROPIO filtro de canal (NFM_CHF_COEFFS en demod_am.c) en vez de
 * CHF_COEFFS. Un canal de FM de banda estrecha necesita el apaño de la
 * fuga de continua tanto como el AM o la banda lateral -es una emisora
 * sola, de poco ancho, no hay razon para gastar los +/-96 kHz que pide el
 * WFM-, pero el codo de unos 4 kHz de CHF_COEFFS esta dimensionado para
 * el ancho de audio pelado de AM y banda lateral, y recortaria las
 * BANDAS LATERALES moduladas en frecuencia de una señal de FM estrecha
 * antes de que llegaran al discriminador: no filtraria el audio
 * resultante, destrozaria la informacion de FM en si. NFM_CHF_COEFFS esta
 * dimensionado en cambio al ancho que da la regla de Carson para un canal
 * estrecho con separacion de 12,5 kHz -+/-2,5 kHz de desviacion maxima,
 * unos 3 kHz de voz-; los numeros exactos estan en su propio comentario
 * en demod_am.c. Ni apply_lo_tune() de main.c ni el ciclo del boton MODE
 * necesitan aqui ningun caso especial para NFM -al contrario que para
 * WFM-: cae en el grupo de AM/USB/LSB, el de "manten el desplazamiento de
 * frecuencia intermedia", simplemente POR NO SER WFM, que es el unico
 * modo que apply_lo_tune() señala aparte.
 *
 * SIN DESENFASIS: al contrario que el WFM, el NFM NO aplica el desenfasis
 * de 50 us, ni ningun otro. Las radios de voz de FM estrecha de
 * radioaficion o PMR no siguen el mismo convenio normalizado de preenfasis
 * en el que se apoya el WFM, asi que adivinar aqui una constante de tiempo
 * tendria tantas papeletas de EMPEORAR las cosas -un equilibrio tonal
 * audiblemente mal- como de mejorarlas. Si el audio de NFM sale fino o
 * apagado con señales de verdad, eso es lo primero que hay que revisar, y
 * no una señal de que haya nada roto.
 *
 * PASO BAJO DE AUDIO: reaprovecha los ALPF_COEFFS y s_alpf_inst que YA
 * existen -unos 6 kHz, los mismos que AM y banda lateral- en vez de los
 * WFM_ALPF_COEFFS, mas anchos, del WFM: el audio de voz en FM estrecha
 * anda por el mismo ancho que el de AM o banda lateral, y no hay razon
 * para una instancia de filtro aparte.
 *
 * UNA SUPOSICION QUE CONVIENE DEJAR DICHA: NFM_CHF_COEFFS esta
 * dimensionado para separacion de canal de 12,5 kHz, que es lo corriente
 * en FM estrecha de radioaficion y PMR en Europa. Si algun dia hace falta
 * recibir canales separados 25 kHz -+/-5 kHz de desviacion-, el codo de
 * NFM_CHF_COEFFS se queda estrecho y habria que ensancharlo: no es un
 * cambio de estructura del codigo, son coeficientes nuevos y ya.
 */

/*
 * WFM -la FM comercial de banda ancha, en mono-, añadido el 31/07/2026.
 *
 * POR QUE NO LLEVA NI BAJADA EN FRECUENCIA NI FILTRO DE CANAL, pedido por
 * el usuario: AM, USB y LSB son de banda estrecha, asi que compensa
 * gastar los +/-24 kHz enteros de la banda base compleja -eran +/-96 kHz
 * a 192 kHz antes del 04/08/2026, ver el comentario de
 * SDR_RX_BLOCK_SAMPLES en sdr_rx.h- solo para llegar a una emisora, y el
 * filtro de canal CHF_COEFFS -3 dB a unos 4 kHz- es lo que lo hace
 * posible. Un canal de FM comercial son unos 200 kHz de ancho con hasta
 * +/-75 kHz de desviacion de pico: CHF_COEFFS lo destrozaria hasta
 * hacerlo irreconocible, y la bajada de DEMOD_IF_OFFSET_HZ -12 kHz, eran
 * 48- empujaria un buen trozo de esos 200 kHz fuera de la ventana de
 * Nyquist en vez de centrarlo. Asi que el WFM se salta los DOS pasos
 * enteros -ver la comprobacion de modo alrededor de 0b y 0c en
 * demod_am_process_raw()- y corre el discriminador directamente sobre el
 * I/Q crudo, de ancho completo y ya desintercalado. Esto solo funciona
 * porque la emisora esta sintonizada justo en el centro: el oscilador
 * local exactamente en frecuencia, sin desplazamiento.
 *
 * *** 04/08/2026: este planteamiento entero ya no es que sea imperfecto,
 * es que se queda CORTO para el WFM ***. Una ventana de Nyquist de
 * +/-24 kHz -bajando desde +/-96- sencillamente no puede contener un
 * canal de FM comercial de unos 200 kHz, este centrado o no; esto ya no
 * es un problema de filtro ni de sintonia desplazada, es un limite fisico
 * de ancho de banda. El WFM se queda conectado y se puede elegir -decision
 * del usuario: aceptar de momento que suene peor antes que bloquear el
 * trabajo de los 48 kHz por esto, o esconder el modo-, pero hay que
 * esperar que suene mal, con solapamiento y distorsion, y no solo "un
 * poco estrecho", hasta que un diseño futuro de doble velocidad le
 * devuelva al WFM su propia captura de banda ancha; ese plan aplazado
 * esta en las notas de la migracion a 48 kHz de este proyecto.
 *
 * Tanto tune_encoder_poll() de main.c como el que atiende al boton MODE
 * tienen que saber esto: siempre que el modo sea WFM tienen que programar
 * el oscilador en la frecuencia elegida a secas -no en freq menos
 * DEMOD_IF_OFFSET_HZ- y llamar a demod_am_set_mix_hz(0). Que eso se
 * desincronice centraria el discriminador en espectro vacio, 12 kHz fuera
 * de la emisora de verdad. El margen de sintonia de esta placa -de 4,8 a
 * 180 MHz, ver TUNE_MIN_HZ y TUNE_MAX_HZ en main.c- ya cubre la banda
 * comercial de 88 a 108 MHz; de hecho la sintonia de arranque capturada
 * byte a byte (MS5351_CAPTURED_LO_HZ, ver ms5351.h) cae en 90,8 MHz,
 * justo dentro.
 *
 * LO QUE DA EL ANCHO DE BANDA: +/-96 kHz de Nyquist complejo contra una
 * señal comercial de unos 200 kHz de ancho y +/-75 kHz de desviacion va
 * justo, pero se puede trabajar en MONO. No queda sitio para decodificar
 * ademas el piloto de 19 kHz y la subportadora de L-R en 38 kHz: el
 * estereo necesitaria banda base limpia mas alla de 53 kHz, que no entra
 * en +/-96 kHz dejando algun margen de antialias. Esta implementacion es
 * solo mono, y el paso bajo de audio del WFM que hay mas abajo es a
 * proposito lo bastante empinado para hundir el piloto -unos -9 dB a 19
 * kHz, -16 a 23 y -36 a 38, comprobado con numeros- en vez de dejar que
 * bata audiblemente contra la banda de audio.
 *
 * EL DISCRIMINADOR: retrasar, conjugar y multiplicar -o, dicho de otra
 * forma, derivar y multiplicar cruzado-, que es el discriminador de FM de
 * siempre en radio definida por software; asi se evita derivar o restar
 * de verdad una fase instantanea llena de ruido. Para dos muestras
 * consecutivas de banda base compleja z[n] = I[n] + jQ[n]:
 *
 *   z[n] * conj(z[n-1]) = (I[n]*I[n-1] + Q[n]*Q[n-1])
 *                       + j*(Q[n]*I[n-1] - I[n]*Q[n-1])
 *
 * ...y el ANGULO de ese producto es exactamente el salto de fase entre
 * dos muestras seguidas, o sea la frecuencia instantanea, que es lo que
 * la demodulacion de FM quiere. Se calcula muestra a muestra con el
 * atan2f() de newlib, NO con el arm_atan2_f32() de CMSIS: el arbol de
 * CMSIS-DSP de este proyecto solo trae FilteringFunctions,
 * ComplexMathFunctions y alguna mas -ver el comentario de C_SOURCES del
 * Makefile-, y la carpeta donde vive arm_atan2_f32.c,
 * Source/FastMathFunctions/, no esta, asi que traerse esa unica funcion
 * significaria sacar ficheros nuevos del CMSIS-DSP original en vez de
 * añadir una linea al Makefile. Con el nucleo a 200 MHz y los 192 kHz de
 * muestreo a los que corre esto, eso son unos 1.041 ciclos de presupuesto
 * por muestra SIN nada mas corriendo en este modo -ni filtro de canal ni
 * bajada en frecuencia-: el atan2f de libm deberia ir sobrado. Pero esto
 * esta razonado desde los numeros de la hoja de datos, no medido sobre la
 * placa. COMPRUEBALO con demod_am_get_last_cycles() despues de grabar,
 * igual que se comprobo el camino de banda lateral -ver su nota mas
 * arriba-: si alguna vez vuelve cerca de los 534.000 ciclos del
 * presupuesto entero de bloque, esa es la señal para replantearlo, por
 * ejemplo trayendo arm_atan2_f32.c de verdad o una aproximacion
 * polinomica mas barata del atan2.
 *
 * No hay riesgo de ambiguedad ni de vuelta: con +/-75 kHz de desviacion
 * el salto de fase por muestra se queda como mucho en unos 2,454 radianes
 * (2*pi*75000/192000), comodamente dentro del margen de +/-pi del atan2.
 *
 * LA GANANCIA: WFM_DISC_GAIN, en demod_am.c, escala la salida en radianes
 * por muestra para que una desviacion de +/-75 kHz caiga mas o menos
 * donde cae el nivel de AM o banda lateral antes del automatico de
 * ganancia. Tampoco es que importe mucho, porque el automatico normaliza
 * al pico de todas formas mas adelante -ver el paso 4 en demod_am.c-;
 * simplemente deja la excursion de ganancia del automatico en algo
 * razonable en vez de arrancar desde una señal casi nula o recortada a lo
 * bestia.
 *
 * DESENFASIS Y PASO BAJO DE AUDIO: la FM comercial preenfatiza los agudos
 * en el transmisor con una constante de tiempo fija -50 us en Europa, por
 * el CCIR, que es lo que este receptor supone porque esta hecho y
 * ajustado en España; las emisiones de Estados Unidos usan 75 us, ver el
 * comentario de WFM_DEEMPH_ALPHA en demod_am.c por si algun dia hay que
 * poder elegirlo-, asi que el receptor tiene que desenfatizar con un paso
 * bajo de un polo que le case, -3 dB a unos 3,18 kHz, o el audio sale
 * cargadisimo de agudos. Se aplica DESPUES del bloqueador de continua
 * compartido -el paso 2, sin cambios; aqui lo que se come sobre todo es
 * el termino de continua de un error pequeño de sintonia o de oscilador,
 * no una portadora- y ANTES de un paso bajo de audio propio del WFM
 * (WFM_ALPF_COEFFS, -3 dB a unos 15 kHz, que sustituye al codo de 6 kHz
 * de ALPF_COEFFS solo para este modo: el paso bajo mas estrecho de voz de
 * AM y banda lateral se llevaria por delante casi toda la fidelidad para
 * la que existe la FM comercial). Todo lo que viene despues -automatico
 * de ganancia y salida a izquierda y derecha- es el mismo codigo
 * compartido que ya usan AM y banda lateral.
 */

/* Elige el modo de demodulacion. Se puede llamar en cualquier momento,
 * incluso desde el bucle principal con la interrupcion corriendo: ver
 * demod_am.c, la variable del modo es un uint8_t alineado y lo peor que
 * puede pasar es que una lectura rancia demodule UN bloque en el modo
 * anterior. No merece una seccion critica. */
void demod_am_set_mode(demod_mode_t mode);

/* Query the current mode - used by main.c for the on-screen mode
 * label. */
demod_mode_t demod_am_get_mode(void);

/* Live SAM carrier-frequency-offset reading, Hz (21/08/2026) - see
 * sam.h's own top comment and demod_am.c's s_sam declaration. Only
 * meaningful while DEMOD_MODE_SAM is active and has had time to
 * settle (several hundred ms to a few seconds after retuning) -
 * reads 0 (or a stale value) otherwise. For MS5351 PPM calibration:
 * tune to a station with a precisely known carrier frequency, wait
 * for this to settle, then ppm = 1e6 * this_value / tuned_freq_hz. */
float demod_am_get_sam_carrier_hz(void);

/* Si el PLL de SAM esta cogido a una portadora de verdad (1) o el numero de
 * arriba no significa nada (0), y la medida continua 0..1 de la que sale.
 * Ver demod_am_sam_enganchado() en demod_am.c. */
uint8_t demod_am_sam_enganchado(void);
/*
 * Y si el lazo se ha quedado CLAVADO contra el tope del margen: 0 no,
 * +1 por arriba, -1 por abajo. Con portadora fuera de alcance se queda
 * pegado; con solo ruido se pasea. Medido en sim/sam_debil.c, y el
 * porque entero esta encima de sam_tope() en sam.h.
 */
int8_t demod_am_sam_tope(void);
float demod_am_sam_enganche(void);

/*
 * EL ANCHO DEL FILTRO DE AUDIO DE AM Y BANDA LATERAL. Añadido el
 * 02/08/2026 y ampliado a un selector de tres posiciones ese mismo dia,
 * pedido por el usuario: la propia chapa de BW de la pantalla principal
 * se convirtio en el mando, asi que un simple ANCHO/ESTRECHO gano un
 * tercer paso. Elige entre tres pasos bajos de audio dedicados:
 * AUDIO_BW_4K0 -unos 4,0 kHz, ALPF_4K0_COEFFS-, AUDIO_BW_2K3 -unos 2,3
 * kHz, ALPF_2K3_COEFFS- y AUDIO_BW_1K8 -unos 1,8 kHz, ALPF_1K8_COEFFS-.
 * Es la misma idea que el selector ANCHO/ESTRECHO/MAS ESTRECHO de un
 * transceptor de HF corriente para trabajar banda lateral o AM en bandas
 * llenas. El diseño y la comprobacion de cada uno estan en el comentario
 * de su array *_COEFFS en demod_am.c.
 *
 * NINGUNO de los tres es el filtro que usa el NFM (ALPF_COEFFS,
 * s_alpf_inst, que siguen en unos 6 kHz y a los que este selector no
 * toca). Estan separados a proposito, para que apretar el ancho de audio
 * de AM y banda lateral no pueda estrechar tambien el del NFM sin
 * querer. Cambiar esto estando en NFM no hace daño: no se oye nada hasta
 * que pasas a AM, USB o LSB, porque el estado se guarda siempre y lo que
 * depende del modo es solo su APLICACION dentro de
 * demod_am_process_raw().
 *
 * *** 01/09/2026: el WFM es DISTINTO del NFM en esto; este selector SI
 * tiene efecto, audible e inmediato, tambien en WFM ***. Ver la eleccion
 * entre WFM_ALPF_WIDE/NORM/NARROW_COEFFS que hace
 * demod_wfm_process_raw() en demod_am.c. Los mismos tres valores de la
 * enumeracion, el mismo s_audio_bw guardado debajo y la misma casilla BW,
 * pero un juego de tres filtros completamente distinto -15 kHz, 8 kHz y
 * 4 kHz, no 4,0 / 2,3 / 1,8-: el valor solo significa un filtro concreto
 * una vez combinado con el modo ACTUAL, nunca por si solo. badges_draw()
 * y menu_tile_bw_refresh() de main.c cambian el texto del rotulo entre
 * los dos significados segun lo que diga demod_am_get_mode(), asi que la
 * pantalla nunca pone "BW 4K0" mientras el WFM esta estrechado de verdad
 * a 4 kHz, ni al reves.
 *
 * El mismo razonamiento de "una enumeracion del tamaño de un uint8_t
 * alineado, sin seccion critica" que el comentario de
 * demod_am_set_mode() de mas arriba: se puede llamar desde el bucle
 * principal con la interrupcion corriendo.
 */
typedef enum {
    AUDIO_BW_4K0 = 0,
    AUDIO_BW_2K3,
    AUDIO_BW_1K8
} audio_bw_t;

void demod_am_set_audio_bw(audio_bw_t bw);

/*
 * EL FILTRO DE DOS CORTES - etapa 34, 24/09/2026.
 *
 * Sustituye a los tres anchos fijos de audio_bw_t de arriba, que eran paso
 * bajo y nada mas. Ahora hay un corte por abajo y otro por arriba, cada
 * uno por su lado, y CADA FAMILIA DE MODO RECUERDA LOS SUYOS: el filtro
 * que quieres en banda lateral no es el que quieres en AM, y tener que
 * reajustarlo al cambiar de modo convierte un ajuste fino en una
 * molestia.
 *
 * CW no esta en la lista a proposito: tiene su propio paso banda, que se
 * mueve con el tono (demod_am_set_cw_filter_hz) y manda sobre esto.
 * WFM tampoco: su audio va por otro camino entero, a 192 kHz, con sus
 * propios WFM_ALPF_*.
 *
 * El diseño del filtro vive en audiofil.c, que no depende de nada del
 * GD32 para poder medirlo en el simulador - ver su cabecera.
 */
typedef enum {
    FIL_FAM_SSB = 0,   /* USB, LSB y todo lo que cuelga de ellos */
    FIL_FAM_AM,
    FIL_FAM_SAM,
    FIL_FAM_NFM,
    FIL_FAM_N
} fil_fam_t;

/* La familia del modo que hay puesto ahora mismo. */
uint8_t demod_am_filtro_familia(void);

/* Los cortes de una familia, en Hz. `lo` a 0 quiere decir "sin paso
 * alto". Se recortan a lo posible (ver audiofil.h), y lo que queda
 * guardado es lo RECORTADO, no lo pedido - asi lo que enseña la pantalla
 * es lo que suena. */
void demod_am_set_filtro(uint8_t fam, uint16_t lo, uint16_t hi);
void demod_am_get_filtro(uint8_t fam, uint16_t *lo, uint16_t *hi);
audio_bw_t demod_am_get_audio_bw(void);

/*
 * Filtro de CW: paso banda de unos 500 Hz centrado en `centre_hz`,
 * usado en lugar del selector de ancho mientras el decodificador de CW
 * esta encendido. Es PARA EL OIDO: cw.c toma el audio antes de este
 * filtro, asi que esto no afecta a lo que decodifica - ver el comentario
 * completo junto a ALPF_CW_STAGES en demod_am.c.
 *
 * Llamarlo al cambiar el tono de CW. Al cambiar de frecuencia de
 * muestreo se rehace solo.
 */
void  demod_am_set_cw_filter_hz(float centre_hz);
float demod_am_get_cw_filter_hz(void);

/*
 * Ancho a -3 dB del filtro de CW, en Hz (22/09/2026). La falda la fija
 * ALPF_CW_STAGES; ver su comentario en demod_am.c para por que cuatro
 * etapas y de donde sale la Q.
 */
void  demod_am_set_cw_bw_hz(float bw_hz);
float demod_am_get_cw_bw_hz(void);

/* La tasa a la que corre ahora mismo el camino de AM/SSB/NFM: 48 o 96 kHz
 * segun el ajuste de tasa. La necesita el decodificador de APRS, que se
 * engancha al discriminador de FM y por tanto trabaja a ESTA tasa y no a los
 * 12 kHz del camino de banda lateral. */
float demod_am_get_active_fs_hz(void);

/*
 * WFM pre-discriminator channel filter width - added 01/09/2026, per
 * the project owner: a control over the RAW COMPLEX BASEBAND
 * bandwidth reaching the FM discriminator, completely separate from
 * audio_bw_t/WFM_ALPF_* above (which shape the DEMODULATED audio,
 * after the discriminator has already run). See WFM_IFBW_NARROW_
 * COEFFS' comment in demod_am.c for the full filter design/
 * verification and why 80kHz specifically. WIDE (default) is a
 * genuine no-op - the discriminator sees completely unfiltered I/Q,
 * exactly as it always has - so nobody who never touches this control
 * gets a different signal path than before this was added. NARROW
 * trades a small amount of margin right at full +/-75kHz deviation
 * (~0.4dB, ~22.5us group delay - both verified negligible) for real
 * rejection of anything sitting further out in the +/-96kHz capture -
 * useful on a crowded band, or a weak station near a strong neighbor.
 *
 * Same "plain uint8_t-sized enum, no critical section" reasoning as
 * demod_am_set_mode()'s comment above - safe to call from the main
 * loop while the ISR runs.
 */
typedef enum {
    WFM_IFBW_WIDE = 0,
    WFM_IFBW_NARROW
} wfm_ifbw_t;

/*
 * LA BANDA DEL DETECTOR SINCRONO - 11/10/2026.
 *
 * Y para eso es: con una emisora pisando por un lado, quedarse con el otro
 * recupera la escucha. En AM normal no se puede -el detector de envolvente
 * ve las dos bandas sumadas- y por eso esto es de SAM.
 *
 * AMBAS es lo de siempre y es lo que hay por omision.
 */
typedef enum {
    SAM_BANDA_AMBAS = 0,
    SAM_BANDA_USB,
    SAM_BANDA_LSB
} sam_banda_t;

void demod_am_set_sam_banda(sam_banda_t b);
sam_banda_t demod_am_get_sam_banda(void);

/*
 * QUE LADO DE LA PORTADORA SE ESTA OYENDO: +1 solo por encima, -1 solo por
 * debajo, 0 las dos. Lo usa el sombreado del espectro, que antes lo
 * decidia con una cadena de if sobre el modo y se dejaba fuera SAM-U y
 * SAM-L. El porque entero esta en el .c.
 */
int8_t demod_am_banda_lado(void);

/*
 * EL MARGEN DE ENGANCHE DEL SAM, EN Hz A CADA LADO DE LA SINTONIA.
 *
 * El porque esta entero en sam.h, encima de sam_margen_pon(): en dos
 * lineas, con los 4.000 Hz del original una portadora intrusa dentro del
 * filtro de canal se lleva el lazo y ademas lo deja diciendo que esta
 * enganchado. Con 250 Hz solo puede si esta MUY cerca; ver el bloque de
 * SAM_MARGEN_DEF_HZ, justo debajo, para el porque de ese numero y su
 * precio.
 *
 * Las opciones son estas cuatro y no un numero libre: lo que se elige aqui
 * no es un gusto, es cuanto error de sintonia se admite, y esos cuatro
 * cubren los casos que existen -calibrado, sin calibrar, y el ancho con el
 * que se calibra-.
 *
 * Se puede cambiar con la radio andando: el cambio lo aplica el bloque
 * siguiente dentro de la interrupcion (una comparacion por bloque), sin
 * tocar el estado del lazo ni reiniciar el modo.
 */
/*
 * Y POR OMISION 250 Hz, NO 100 - 19/10/2026.
 *
 * Los 100 Hz se eligieron el 12/10 contra la intrusa, y para eso siguen
 * siendo lo correcto. Lo que no se midio entonces es el otro lado: una
 * portadora de onda corta de verdad NO esta quieta. En la cascada de ese
 * video la traza serpentea unos tres pixeles de los 68 Hz que mide cada uno,
 * o sea del orden de +-150 Hz, y con 100 de margen el lazo se queda contra
 * el tope el 21 % del tiempo.
 *
 * Lo mide sim/sam_captura.c, y las dos caras:
 *
 *   portadora con vaiven de +-150 Hz     100 Hz: engancha el 79 % del tiempo
 *                                        250 Hz: el 100 %
 *
 *   una intrusa 20 dB mas fuerte a...    100 Hz      250 Hz
 *                              +150 Hz   no la pilla SE LA LLEVA
 *                              +200 Hz   no la pilla SE LA LLEVA
 *                              +300 Hz   no la pilla no la pilla
 *                              +500 Hz   no la pilla no la pilla
 *
 * O sea que esto NO es gratis y conviene tenerlo escrito: con 250 una
 * intrusa a menos de 250 Hz puede llevarse el lazo, cosa que con 100 no
 * pasaba. A cambio, la radio engancha emisoras reales que antes soltaba.
 * Se elige lo segundo porque lo primero -dos portadoras a menos de 250 Hz
 * dentro del mismo filtro- ya suena a batido y tiene su salida propia, que
 * es SAM-U/SAM-L o estrechar el filtro; y porque el caso que rompia de
 * verdad, el medido, estaba a 500 y 1.000 Hz y sigue cubierto.
 *
 * Quien quiera el comportamiento de antes lo tiene en la lista: 100 sigue
 * siendo una de las cuatro opciones.
 */
#define SAM_MARGEN_OPCIONES 4U
#define SAM_MARGEN_DEF_HZ   250U
extern const uint16_t k_sam_margen_hz[SAM_MARGEN_OPCIONES];

void     demod_am_sam_margen_set(uint16_t hz);
uint16_t demod_am_sam_margen_get(void);
/* El indice dentro de k_sam_margen_hz[] del margen que hay puesto; si el
 * valor guardado no es ninguno de los cuatro, devuelve el del mas cercano
 * por arriba, que es el que no quita alcance. */
uint8_t  demod_am_sam_margen_idx(void);

void demod_am_set_wfm_ifbw(wfm_ifbw_t bw);
wfm_ifbw_t demod_am_get_wfm_ifbw(void);

/*
 * --- AGC profile (release speed / manual bypass) --------------------
 *
 * Added 31/07/2026, per the project owner: the classic MAN/SLW/MED/FST
 * selector found on most communications receivers, applied to the
 * SAME shared AGC peak-detector every mode already uses (step 4 of
 * demod_am_process_raw() - see AGC_TARGET's comment in demod_am.c).
 * Attack stays instant in every profile (a real signal should always
 * grab the gain down immediately, no reason to make that adjustable)
 * - only the RELEASE time constant changes:
 *
 *   AGC_PROFILE_SLOW   - ~700ms release. Rides through fading/flutter
 *                         and inter-word gaps in speech without the
 *                         gain visibly "pumping" - the classic AM/SSB
 *                         broadcast-listening choice.
 *   AGC_PROFILE_MEDIUM - ~175ms release (DEFAULT - this is the exact
 *                         same release rate the AGC has always used,
 *                         see AGC_RELEASE_MEDIUM's comment in
 *                         demod_am.c - so leaving the profile at its
 *                         default changes nothing about existing
 *                         behavior).
 *   AGC_PROFILE_FAST    - ~60ms release. Recovers quickly between
 *                         syllables/CW elements at the cost of some
 *                         audible pumping on a fluttery signal - handy
 *                         for weak, choppy contacts.
 *   AGC_PROFILE_MANUAL  - bypasses the peak-tracking loop ENTIRELY:
 *                         fixed unity gain, no automatic leveling at
 *                         all. Loudness becomes the job of the
 *                         existing VOLUME control (main.c's
 *                         ENCODER_TARGET_VOLUME) instead of this loop
 *                         - same tradeoff as flipping a real radio's
 *                         AGC to MANUAL and riding the volume knob by
 *                         hand: a strong signal can clip if VOLUME is
 *                         set too hot, and a weak one can vanish into
 *                         the noise floor if it's set too low. This is
 *                         NOT an adjustable manual GAIN LEVEL of its
 *                         own (that would need its own encoder target
 *                         and state) - just an AGC on/off switch,
 *                         which is what was actually asked for; a real
 *                         manual RF/IF gain control would be a natural
 *                         follow-up if ever needed.
 */
typedef enum {
    AGC_PROFILE_MANUAL = 0,
    AGC_PROFILE_SLOW,
    AGC_PROFILE_MEDIUM,
    AGC_PROFILE_FAST
} agc_profile_t;

/* Se puede llamar en cualquier momento -el mismo razonamiento que
 * demod_am_set_mode(), ver su comentario-, incluso desde el bucle
 * principal con la interrupcion corriendo. */
void demod_am_set_agc_profile(agc_profile_t profile);

/* Query the current profile - used by main.c for the badge. */
agc_profile_t demod_am_get_agc_profile(void);

/*
 * --- El silenciador (AM y NFM) -------------------------------------------
 *
 * Añadido el 31/07/2026 para el NFM y AMPLIADO al AM ese mismo dia,
 * pedido por el usuario: un silenciador clasico, por nivel de
 * radiofrecuencia. AL CONTRARIO que un silenciador por banda de ruido
 * -que filtra la salida de AUDIO del discriminador por encima de la banda
 * de voz y mide alli la energia del siseo, que es la otra tecnica
 * corriente-, este mide el modulo medio de la propia BANDA BASE COMPLEJA
 * ya bajada en frecuencia y filtrada de canal -CHF_COEFFS en AM,
 * NFM_CHF_COEFFS en NFM; el filtro de canal de cada modo, ver sus notas
 * respectivas de mas arriba-, o sea, como de fuerte es la señal de
 * radiofrecuencia que hay DENTRO del canal, ANTES de la etapa que extrae
 * el audio. Ver el bloque compartido del silenciador en demod_am.c, que
 * corre justo despues de la bifurcacion AM/WFM/NFM/banda lateral del paso
 * 1, y no dentro de ninguna de ellas: es el mismo codigo para AM y para
 * NFM. Se eligio asi, y no por banda de ruido, porque no depende de lo
 * que este haciendo la señal: en NFM concretamente, un pico de voz fuerte
 * y sostenido no puede engañarlo para que lea "no hay portadora" ni un
 * instante, porque la portadora de FM es de envolvente constante y se
 * queda mas o menos a la misma amplitud este muy modulada o nada. En AM
 * la misma medida es, si acaso, todavia mas natural: alli que haya
 * portadora ES amplitud, por definicion, asi que esto se parece mucho a
 * como funciona un silenciador de AM de toda la vida.
 *
 * El WFM y la banda lateral no lo tienen. La entrada del WFM se salta el
 * filtro de canal entero -ni bajada en frecuencia ni filtro, ver su
 * nota-, asi que no hay un nivel de radiofrecuencia dentro del canal
 * equivalente que medir de la misma forma. La banda lateral comparte
 * exactamente la entrada del AM -bajada mas CHF_COEFFS- y podria
 * reaprovechar esto tal cual, pero nadie lo ha pedido y no esta
 * conectado; añadirlo igual seria sencillo si alguna vez se quiere.
 *
 * SIN CALIBRAR, con el mismo aviso que la escala de dB del espectro -ver
 * el comentario original de SDR_DB_MIN y SDR_DB_MAX en main.c-: el umbral
 * va en dB (20*log10 del modulo lineal, y esta vez con un log10f() de
 * verdad, no con la aproximacion por manipulacion de bits de fft.c), pero
 * no hay ningun nivel de referencia que lo ate a nada absoluto. Hay que
 * ajustarlo mirando demod_am_get_squelch_level_db() -o simplemente
 * escuchando- contra señales de verdad y ruido de banda de verdad sobre
 * la placa, igual que AGC_PEAK_MIN y las ganancias de los discriminadores
 * de WFM y NFM se ajustaron razonando y apuntando a un orden de magnitud
 * y no contra una referencia de laboratorio. Los filtros de canal de AM y
 * de NFM tienen ganancias de paso de banda y anchos de ruido distintos
 * -unos 4 kHz CHF_COEFFS contra unos 6,25 kHz NFM_CHF_COEFFS-, asi que el
 * MISMO valor de umbral puede no leerse como la misma "distancia por
 * encima del suelo de ruido" en los dos modos. Si eso resulta importar en
 * la practica, partirlo en dos umbrales independientes -uno por modo- es
 * un cambio pequeño; de momento se comparten a proposito, primero lo
 * simple.
 *
 * POR DEFECTO: -100 dB, o sea APAGADO en la practica, porque nada real
 * lee tan bajo. Asi AM y NFM se comportan EXACTAMENTE igual que antes de
 * que el silenciador existiera, hasta que alguien suba el umbral a
 * proposito. Subirlo hacia el suelo de ruido -el que se lea sobre la
 * placa- es lo que de verdad hace que silencie.
 *
 * HISTERESIS: abrir y cerrar usan puntos de disparo distintos, +/-1,5 dB
 * alrededor del umbral, ver SQUELCH_HYSTERESIS_DB en demod_am.c. Asi una
 * señal que se quede justo en el umbral no castañetea abriendo y cerrando
 * en cada bloque.
 */

/* Sets the squelch threshold in dB - see the block comment above for
 * what "dB" means here. Safe to call at any time (same reasoning as
 * demod_am_set_mode()). Has no audible effect outside AM/NFM mode,
 * but is remembered regardless (no need to be in one of those modes
 * to adjust it ahead of switching into it). */
void demod_am_set_squelch_db(float threshold_db);

/* Query the current threshold - used by main.c for the readout. */
float demod_am_get_squelch_db(void);

/* Pregunta por el nivel de radiofrecuencia suavizado del ULTIMO BLOQUE,
 * en las mismas unidades de dB que el umbral. Asi main.c -o alguna vista
 * de depuracion futura- puede enseñar donde esta de verdad la señal o el
 * suelo de ruido, que es justo lo que hay que ver para poner el umbral
 * con algun criterio. */
float demod_am_get_squelch_level_db(void);

/*
 * LA POTENCIA DEL CANAL EN dB, PARA EL BARRIDO DE MEMORIAS - 26/10/2026.
 *
 * Y es OTRA COSA que la de aqui arriba, a proposito:
 *
 *   - la del silenciador solo se calcula en AM y en FM estrecha, que es
 *     donde hay silenciador. En banda lateral -que es donde esta casi todo
 *     lo que se barre en onda corta- se queda CONGELADA en la ultima
 *     lectura que tomo, y preguntarle devuelve siempre lo mismo. Eso hacia
 *     que el barrido se parase en la segunda memoria con solo ruido;
 *
 *   - esta se calcula en todos los modos que pasan por el mezclador y no
 *     cuesta ni una raiz: es la suma de I²+Q² que el medidor de señal ya
 *     hace, en decibelios.
 *
 * Es POTENCIA, o sea otra escala que la del silenciador. Sirve para
 * comparar una frecuencia con otra, no contra un numero absoluto. Lo mide
 * sim/scan_nivel.c.
 */
/* Enciende o apaga la medida de demod_am_get_nivel_db(). Apagada de
 * fabrica: cuesta 256 multiplicaciones y un logaritmo por bloque y solo la
 * usa el barrido de memorias, que la enciende mientras corre. Ver el
 * comentario de s_nivel_on en demod_am.c. */
void demod_am_set_nivel_medir(uint8_t on);
float demod_am_get_nivel_db(void);

/* Query whether the squelch is currently PASSING audio (1) or MUTING
 * it (0). Only meaningful in AM/NFM mode - see demod_am_set_mode(). */
uint8_t demod_am_get_squelch_open(void);

/* Cycles spent in the LAST call to demod_am_process_raw() (the whole
 * ISR body, including the I2S stream write), measured via
 * DWT->CYCCNT. Added 30/07/2026 to diagnose a system hang reported
 * when switching into USB/LSB - see demod_am.c's comment above
 * HILBERT_COEFFS. Compare against SystemCoreClock * 512 / 192000 (one
 * block's real-time budget in cycles) - if this is consistently close
 * to or over that, the ISR doesn't fit in real time. Safe to poll
 * from the main loop (plain volatile uint32_t, single aligned word). */
uint32_t demod_am_get_last_cycles(void);

/*
 * El reparto de ciclos por etapa de la ULTIMA llamada a
 * demod_am_process_raw(). Añadido el 31/07/2026 para clavar donde se van
 * exactamente los ciclos de un bloque de la interrupcion, despues de que
 * el total de la interrupcion entera -demod_am_get_last_cycles(), mas
 * arriba- volviera mas alto de lo esperado en USB/LSB en cuanto se añadio
 * el WFM al lado. El mismo patron de leer el CYCCNT del DWT y restar, y
 * el mismo razonamiento de "lo bastante barato como para no importar, y
 * aqui no hay puerto serie": ver el comentario de
 * demod_am_get_last_cycles(). Las etapas:
 *   frontend: pasos 0a a 0c -desintercalar, bajar en frecuencia y filtro
 *             de canal-. En WFM se salta entera, ver demod_am.h.
 *   extract:  paso 1, lo propio de cada modo: el modulo en AM, el
 *             discriminador en WFM o la cadena diezmada con Hilbert en
 *             banda lateral. Es la que con mas papeletas explica un
 *             cambio que solo se note en USB o LSB.
 *   audio:    pasos 2 y 3 -bloqueador de continua y paso bajo de audio,
 *             o desenfasis mas paso bajo en WFM-.
 *   nr:       lo que cuesta la reduccion de ruido por sustraccion
 *             espectral. Añadido el 03/08/2026 y revisado el 04/08/2026,
 *             cuando cambio el camino de la banda lateral -ver el
 *             comentario de INTEGRACION DE LA REDUCCION DE RUIDO en
 *             demod_am.c-: en AM es el diezmado, nr_ss_process() e
 *             interpolado propios del paso 3b; en USB y LSB es en cambio
 *             lo que cuesta el paso 2d, que corre la reduccion encima del
 *             buffer que la cadena de banda lateral ya tiene a 12 kHz, y
 *             sale mas barato porque no hace falta un par de diezmado e
 *             interpolado aparte. Siempre 0 en WFM y NFM -la etapa se
 *             salta entera en esos modos, no es que salga barata- y
 *             siempre 0 con la reduccion apagada. ES el numero que hay
 *             que mirar al comprobar si esto cabe en el presupuesto de
 *             tiempo real de la interrupcion; ver la cabecera de nr_ss.c.
 *   agc_out:  pasos 4 y 5 -automatico de ganancia, recorte y escritura al
 *             I2S-.
 * La suma de las cinco deberia caer cerca del total de
 * demod_am_get_last_cycles(); que baile unos pocos ciclos es normal,
 * porque las propias lecturas no son gratis. Se puede consultar desde el
 * bucle principal, igual que el total. */
typedef struct {
    uint32_t frontend;
    uint32_t extract;
    uint32_t audio;
    uint32_t nr;
    uint32_t agc_out;
} demod_am_cycles_breakdown_t;


demod_am_cycles_breakdown_t demod_am_get_last_cycles_breakdown(void);

/* El pico de envolvente ANTES del automatico de ganancia, en unidades de
 * fondo de escala de entero de 16 bits, con ataque instantaneo y soltada
 * lenta: de aqui sale el medidor de S de la interfaz. Pasarlo a dB o a
 * unidades S se hace en el bucle principal, no aqui. Se puede consultar
 * en cualquier momento. */
float demod_am_get_signal_peak(void);
/*
 * La bandera de recorte en la entrada de radiofrecuencia. Añadida el
 * 07/08/2026, pedida por el usuario, para el automatico de ganancia de
 * radiofrecuencia -el del PGA analogico- de main.c, o sea rf_agc_poll() y
 * s_rf_agc_enabled. Se pone DESDE DENTRO de la interrupcion de DMA de
 * recepcion: tanto demod_am_process_raw() como demod_wfm_process_raw()
 * recorren cada bloque de raw_interleaved buscando muestras pegadas al
 * tope del convertidor -ver rf_clip_scan() en demod_am.c-, porque ese es
 * el unico sitio donde existen las muestras crudas, antes del proceso de
 * señal. Para cuando algo de mas adelante -la envolvente, el automatico
 * de ganancia, s_env[]- ve un valor, una muestra que de verdad se recorto
 * ya perdio informacion que ningun programa puede devolver: que las
 * cuentas de ganancia del automatico digital sean matematicamente
 * correctas no sirve de nada si la ENTRADA ya venia con la cabeza plana.
 * Esta bandera es la salida de emergencia: la interrupcion lo puede
 * detectar barato -un recorrido pelado, de la misma clase de coste que el
 * seguimiento de minimo y maximo que ya hay-, pero cambiar de verdad la
 * ganancia del PGA significa una transaccion de I2C a pedales
 * (aic3204_set_pga_gain_db()) que NUNCA debe correr dentro de una
 * interrupcion -ver el aviso de "corre en la interrupcion de DMA de
 * recepcion" de demod_am_process_raw()-. Asi que esto solo pone una
 * bandera y se va; quien actua de verdad es rf_agc_poll() de main.c, al
 * que se llama una vez por vuelta del bucle principal, igual que a
 * tune_encoder_poll().
 *
 * Leer y borrar a la vez -esta funcion devuelve la bandera Y la borra en
 * la misma llamada-. La interrupcion solo la pone, nunca la borra, asi
 * que no hay ninguna carrera de verdad mas alla de la misma clase de
 * "volatile pelado compartido con una interrupcion" que ya aceptan todos
 * los demas campos de este fichero que cruzan de contexto -por ejemplo
 * s_last_cycles_*-: una bandera que se ponga entre la lectura y el
 * borrado simplemente se recoge en la consulta SIGUIENTE, lo que no hace
 * daño en algo tan grueso como esto, donde los pasos de bajada de
 * ganancia van con una espera de muchas milesimas y no al ritmo de la
 * muestra.
 */
uint8_t demod_am_get_and_clear_rf_clip_flag(void);

/* El desplazamiento del oscilador local para la sintonia por frecuencia
 * intermedia baja: el oscilador de verdad = frecuencia elegida menos
 * DEMOD_IF_OFFSET_HZ, ver la nota de SINTONIA POR FRECUENCIA INTERMEDIA
 * BAJA de mas arriba. Ahora mismo es Fs/4 (96000/4 = 24000; eran
 * 48000/4 = 12000 antes de que AM, banda lateral y NFM pasaran de 48 a 96
 * kHz, y 192000/4 = 48000 antes de eso, ver el comentario de
 * SDR_RX_BLOCK_SAMPLES en sdr_rx.h), para que case con el giro por
 * cambios de signo y sin multiplicaciones de demod_am.c. Si esto cambia
 * alguna vez, el algoritmo del giro de demod_am.c tiene que cambiar con
 * el: NO es un oscilador generico, solo funciona exactamente en Fs/4.
 *
 * *** 01/09/2026: ahora son DOS valores, no uno; ver el comentario de
 * demod_am_set_active_rate() mas abajo ***. AM, USB, LSB y NFM pueden
 * correr a 96 kHz o a 48 kHz -la casilla RATE de main.c- y, como este
 * desplazamiento tiene que ser SIEMPRE el Fs/4 de la velocidad que este
 * corriendo de verdad -el giro de la bajada no sabe hacer otra cosa-,
 * main.c tiene que elegir cual de estas dos constantes toca segun
 * s_nonwfm_use_48k, y no usar DEMOD_IF_OFFSET_HZ a pelo como hacia el
 * codigo viejo. Ver la funcion auxiliar demod_if_offset_hz() de main.c,
 * que es el UNICO sitio que deberia seguir leyendo directamente
 * cualquiera de estas dos macros.
 */
#define DEMOD_IF_OFFSET_HZ 24000UL /* Fs/4 @ 96kHz - the default/96kHz-rate value */
#define DEMOD_IF_OFFSET_HZ_48K 12000UL /* Fs/4 @ 48kHz */

/*
 * LA TASA DEL CAMINO DECIMADO, UNA SOLA VEZ - 07/10/2026.
 *
 * Todo lo que escucha banda lateral -RTTY, CW, NAVTEX, PSK31, WEFAX, SSTV,
 * Feld-Hell, FT8, el paquete de HF, el notch automatico- trabaja sobre
 * s_ssb_dec, que sale del decimador de este fichero a 12 kHz. Ese numero
 * estaba escrito A MANO en una docena de sitios repartidos por ocho
 * ficheros, y el decimador lo calculaba con OTRA copia del mismo literal.
 *
 * Es exactamente la forma de averia que costo la tabla de zonas del
 * oscilador: dos numeros que TIENEN que ser el mismo, escritos por
 * separado, y nada que los compare. Alli uno decia 300 kHz y el otro 100, y
 * la tabla se derivo para un tramo que no existia.
 *
 * Aqui vive el bueno. Los consumidores lo usan; el que no pueda incluir
 * esta cabecera se ata con un _Static_assert donde se vean los dos (ver
 * main.c y PKT_HF_FS_HZ en ax25.h).
 */
#define DEMOD_DEC_FS_HZ 12000.0f

/* Speaker enable pin: PB7, driven high to unmute the speaker amp. */

/* Configure PB7 (speaker enable, set high), initialize the CMSIS-DSP
 * biquad instances (channel filter x2, audio LPF), and reset all
 * filter/AGC state. Call once before registering the block hook. */
void demod_am_init(void);

/*
 * ============================================================================
 * EL DESPLAZAMIENTO DIGITAL, EN HERCIOS - 23/09/2026 (etapa del NCO)
 * ============================================================================
 * ESTO ERA UN SI/NO. Habia un demod_am_set_if_offset_active(uint8_t) porque
 * solo existia un desplazamiento posible, Fs/4, y lo unico que quedaba por
 * decir era si estaba puesto o no. Al llegar el NCO de sintonia -el
 * oscilador se queda aparcado y lo que se mueve es el punto de demodulacion-
 * el desplazamiento pasa a ser una distancia cualquiera, asi que el si/no ya
 * no alcanza.
 *
 * Y NO se ha anadido un segundo ajuste al lado del primero, que era lo
 * facil. Dos variables que tienen que estar de acuerdo -"activo" y "cuantos
 * hercios"- son exactamente el fallo que en este proyecto ya ha mordido seis
 * veces: se ponen de acuerdo hasta el dia que no, y no hay error de
 * compilacion que avise. Aqui hay UN dato:
 *
 *     0     no hay desplazamiento (WFM, o el arranque con los bytes
 *           capturados, que no lleva ninguno)
 *     != 0  los hercios que hay que desplazar, con su signo
 *
 * Todo lo demas se deduce de el, incluido el "esta activo" que main.c
 * necesita para saber donde cae el punto de demodulacion en el panadaptador.
 *
 * TIENE QUE SEGUIR AL OSCILADOR DE VERDAD. La regla no cambia: esto vale
 * exactamente (frecuencia sintonizada - oscilador local programado).
 * Desajustarlo saca la señal FUERA del filtro de canal en vez de meterla
 * dentro, que es lo mismo que pasaba antes, solo que ahora se puede
 * desajustar de infinitas maneras en vez de dos. Por eso lo pone un solo
 * sitio: apply_lo_tune() de main.c, que es tambien quien programa el
 * oscilador, en la misma funcion y sin nada en medio.
 *
 * COMO SE APLICA. Si los hercios son EXACTAMENTE Fs/4 se usa el camino de
 * siempre -la rotacion 1, -j, -1, +j, sin una sola multiplicacion-, asi que
 * el modo normal de la radio cuesta hoy lo mismo que costaba ayer. Para
 * cualquier otro valor entra el NCO de User/nco.c. Que los dos caminos dan
 * lo mismo a Fs/4 no es una suposicion: lo comprueba sim/ncotest.c muestra a
 * muestra.
 */
void demod_am_set_mix_hz(float hz);

/* Los hercios que hay puestos. main.c lo usa para saber a que distancia del
 * centro del panadaptador cae la señal que se esta demodulando de verdad
 * (ver el parametro center_mark_offset_px de spectrum_draw()), y para
 * recentrar el zoom antes de diezmar. */
float demod_am_get_mix_hz(void);

/*
 * demod_am_set_active_rate(), añadida el 01/09/2026 y pedida por el
 * usuario: AM, USB, LSB y NFM pueden correr ya a 96 kHz -lo normal- o a
 * 48 kHz -la casilla RATE de main.c; el motivo entero, "mejor relacion
 * señal a ruido y mas holgura de procesador para el HFDL", esta en el
 * comentario donde se declara s_nonwfm_use_48k-. HAY QUE llamarla -desde
 * apply_demod_mode() de main.c, siempre que se aplique de verdad al codec
 * una velocidad que no sea la del WFM- ANTES de que el gancho de bloque
 * empiece a soltar muestras de verdad a la velocidad nueva. Que esto no
 * case con lo que el codec esta corriendo DE VERDAD significa que todos
 * los filtros de la cadena aplican sus coeficientes a muestras que llegan
 * a otra velocidad real, y eso corre en silencio todas las frecuencias de
 * corte y todas las constantes de tiempo en la misma proporcion en que se
 * diferencien las dos velocidades. Esta NO es la misma bandera que
 * demod_am_set_if_offset_active() de mas arriba: aquella dice SI la
 * bajada en frecuencia esta activa -siempre lo esta, salvo en la sintonia
 * de arranque capturada-; esta dice CUAL de dos juegos completos de
 * coeficientes -filtros, constantes de tiempo del automatico de ganancia
 * y del bloqueador de continua, diezmador de banda lateral- tiene que
 * usar la cadena, independientemente de si la bajada esta activa o no.
 *
 * El WFM NO SE ENTERA, pase lo que pase: siempre ha corrido su propio
 * camino aparte a 192 kHz -demod_wfm_process_raw(), sus propios filtros
 * WFM_ALPF_ y WFM_IFBW_, su propio estado de automatico de ganancia y de
 * bloqueador de continua- diga lo que diga esta bandera. Esto solo elige
 * entre los juegos de coeficientes de AM, USB, LSB y NFM que usa
 * demod_am_process_raw().
 */
/*
 * LA RADIOFRECUENCIA AL DOBLE: 192 kHz en vez de 96 - 23/10/2026.
 *
 * Pedido por el usuario: ver el doble de espectro. El panadaptador enseña
 * EXACTAMENTE la tasa a la que se muestrea -el ancho es la tasa, no el
 * tamaño de la FFT-, asi que para ver 192 kHz hay que muestrear a 192.
 *
 * Con esto a 1 el DMA entrega 512 muestras por bloque en vez de 256, en el
 * mismo tiempo de siempre (2,667 ms). Lo que llega a ESTA cadena no cambia:
 * un diezmador de medio banda (User/semibanda.h) las deja en 256 a 96 kHz
 * justo detras del quitapulsos, antes de la bajada en frecuencia. De ahi en
 * adelante no hay ni una constante distinta.
 *
 * O SEA QUE NO VA CON demod_am_set_active_rate(): esa dice a que tasa
 * TRABAJA la cadena -48 o 96- y con esto puesto sigue siendo 96. Las dos
 * conviven, y hay que llamar a las dos.
 *
 * SE VE EL DOBLE Y SE SINTONIZA EN TODO LO QUE SE VE -tocar el espectro
 * mueve el oscilador local-, pero el ancho INSTANTANEO que llega al
 * demodulador sigue siendo de 96 kHz. Para un modo que decodifica una sola
 * señal da igual; no daria igual el dia que algo quisiera mirar dos trozos
 * lejanos a la vez.
 */
void demod_am_set_rf_doble(uint8_t doble);
uint8_t demod_am_get_rf_doble(void);

void demod_am_set_active_rate(uint8_t is_48k);
uint8_t demod_am_get_active_rate_is_48k(void);

/*
 * Conmuta el filtro de canal de NFM entre sus coeficientes de siempre y los
 * anchos de AIS. Lo llama main.c al entrar y salir del modo AIS; ver el
 * comentario de AIS_CHF_96K_COEFFS en demod_am.c para por que hace falta un
 * filtro mas ancho y por que comparten instancia.
 */
/*
 * EL FILTRO DE CANAL ANCHO DE NFM: 9 kHz en vez de 6,25.
 *
 * Se llamaba demod_am_ais_chf() hasta el 13/10/2026, cuando dejo de ser
 * cosa solo del AIS: la SSTV en FM lo pide por el mismo motivo -la señal
 * ocupa mas de lo que el filtro de voz deja pasar- y llamar desde SSTV a
 * algo que se llama "ais" seria un nombre que miente. La cosa que hace no
 * ha cambiado ni un coeficiente.
 */
void demod_am_chf_ancho(uint8_t si);

/*
 * El SEMIANCHO en hercios de lo que deja pasar el filtro de canal de FM
 * estrecha: 6.250 el de siempre, 9.000 el ancho que se carga con AIS y con
 * la SSTV en FM. Lo usa el espectro para sombrear la banda de paso, que en
 * este modo no la pone el filtro de audio. Ver demod_am.c.
 */
uint16_t demod_am_nfm_ancho_hz(void);

/* Procesa una mitad de recepcion en crudo: SDR_RX_BLOCK_SAMPLES tramas de
 * izquierda y derecha -o sea I y Q- intercaladas, las demodula y empuja
 * el bloque de audio que sale al flujo de transmision del I2S
 * (gd32_i2s_stream_write_half()). Pensada para ser el gancho de bloque de
 * sdr_rx: corre en contexto de interrupcion de DMA. */
void demod_am_process_raw(const int16_t *raw_interleaved);

/*
 * El gancho de bloque PROPIO del WFM, aparte, del 05/08/2026: ver su
 * propio comentario de cabecera en demod_am.c. Son SDR_RX_BLOCK_SAMPLES_WFM
 * (512) tramas de izquierda y derecha -I y Q- intercaladas, a los 192 kHz
 * nativos del WFM, y completamente independiente del estado de 128
 * muestras a 48 kHz de demod_am_process_raw(). main.c registra ESTE -con
 * sdr_rx_set_block_hook()- en vez de demod_am_process_raw() al entrar EN
 * el WFM, y lo cambia de vuelta al salir; ver alli la secuencia de cambio
 * de modo. Corre tambien en contexto de interrupcion de DMA, igual que
 * demod_am_process_raw().
 */
void demod_wfm_process_raw(const int16_t *raw_interleaved);

/* Diagnostic-only: resets the call counter that gates
 * demod_wfm_process_raw()'s per-block debug logging - call this
 * right when switching INTO WFM (see main.c's apply_demod_mode()) so
 * each WFM entry gets its own fresh set of logged calls, the same way
 * gd32_i2s.c's write_half() diagnostic already does for its own
 * counter. As of 05/08/2026 this ALSO (re-)arms the settle-mute
 * window (WFM_SETTLE_MUTE_BLOCKS in demod_am.c) - both need to reset
 * together on every WFM entry, so they share this one call rather
 * than main.c needing to know about two separate counters. */
void demod_wfm_reset_diag(void);

/* Lo mismo, pero con el silencio corto de AM_SETTLE_MUTE_BLOCKS en vez de
 * los 200 bloques de la entrada en el modo. Para los escalones de
 * ganancia del AGC de radiofrecuencia, que no cambian la tasa del codec y
 * no necesitan medio segundo. Ver demod_am.c. */
void demod_wfm_mute_tras_ganancia(void);

/* Y solo el silencio, sin el olvido: para el hueco de los pocos bloques en
 * que el gancho esta suelto cuando una orden de modo redundante barre las
 * colas de la cadena estando ya en FM ancha. No acorta un silencio ya
 * armado. Ver demod_am.c. */
void demod_wfm_mute_corto(void);

/* La fuga de continua que la cadena de FM ancha se ha medido a si misma,
 * en cuentas del convertidor. Es el SUELO del recorte del quitador: ver
 * demod_am.c. Cero = este aparato no tiene fuga y el suelo no hace nada. */
float demod_wfm_get_fuga(void);
/* El silenciador de FM ancha: cuanto ruido de ARRIBA hay en el multiplex,
 * en decibelios, y si esta abierto. Ver WFM_SQL_B0 en demod_am.c. */
float   demod_wfm_get_sql_db(void);
uint8_t demod_wfm_get_sql_abierto(void);
/* Y SU UMBRAL, que NO es el de AM: ver s_wfm_sql_umbral en demod_am.c, que
 * lleva la tabla de por que compartirlo dejaba la FM comercial muda. El
 * suelo es "apagado". */
void    demod_am_set_wfm_sql_db(float db);
float   demod_am_get_wfm_sql_db(void);
float   demod_am_wfm_sql_min(void);
float   demod_am_wfm_sql_max(void);

/*
 * La continua que el quitador de I/Q esta restando ahora mismo, en cuentas
 * del convertidor (ver su comentario largo en demod_am.c, delante de
 * fm_discriminate()). Cualquiera de los dos punteros puede ser NULL.
 *
 * Este numero NO es un diagnostico de curiosidad: es el SUELO DE
 * SENSIBILIDAD de WFM antes de que existiera el quitador. Ninguna emisora
 * cuya portadora quedara por debajo de este valor se podia demodular, y el
 * corte era total -medido en sim/wfm_dc.c-, no una degradacion suave.
 */
void demod_wfm_get_dc(float *dc_i, float *dc_q);

/* Lo mismo SIN el tope de WFM_DC_TOPE: lo que el lazo mide de verdad. Si
 * las dos coinciden, el tope no esta actuando; si no, la diferencia es la
 * fuga que se queda sin corregir. Ver demod_am.c. */
void demod_wfm_get_dc_crudo(float *dc_i, float *dc_q);

/* Multiplica la estimacion de continua de FM ancha por `g`. La llama
 * rf_agc_apply_pga() cuando cambia la ganancia de entrada: la fuga escala
 * con la ganancia, asi que escalar la estimacion es exacto y evita el
 * transitorio de volver a converger. Ver demod_am.c. */
void demod_wfm_dc_escala(float g);

/* La continua que la cadena de AM/banda lateral/NFM le esta quitando a la I
 * y a la Q antes de la bajada en frecuencia: la fuga del oscilador local.
 * Ver s_rf_dc_i en demod_am.c y el banco sim/rf_dc.c. */
void demod_am_get_rf_dc(float *dc_i, float *dc_q);

/* Diagnostic-only: resets the call counter that gates
 * demod_am_process_raw()'s per-block debug logging - call this right
 * when switching OUT of WFM (into AM/USB/LSB/NFM), mirroring
 * demod_wfm_reset_diag()'s own call on the way in. See
 * am_should_log_diag()'s comment in demod_am.c for why this was
 * added. */
void demod_am_reset_diag(void);

/* WFM's own ISR cycle counter, mirrors demod_am_get_last_cycles() -
 * was missing from this header (implicit-declaration warning fixed
 * 05/08/2026 alongside the R27/R30 clock-start reordering). */
uint32_t demod_wfm_get_last_cycles(void);

#endif /* DEMOD_AM_H */
