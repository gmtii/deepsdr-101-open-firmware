#ifndef DCF77_H
#define DCF77_H

#include <stdint.h>

/*
 * DCF77 - LA HORA OFICIAL POR RADIO, EN 77,5 kHz. 23/09/2026.
 *
 * QUE ES. Un transmisor en Mainflingen (cerca de Frankfurt) emite una
 * portadora en 77,5 kHz y le mete la hora encima, bajando la amplitud al
 * principio de cada segundo. Cubre un radio de unos 2000 km, asi que desde
 * España se recibe -de noche con holgura, de dia justito y con una antena
 * que valga para onda larga-. La hora que da es CET/CEST, que es la MISMA
 * zona horaria que España peninsular: lo que se decodifica es directamente
 * la hora del reloj de pared, sin conversiones.
 *
 * COMO VA LA MODULACION. Al empezar cada segundo la portadora baja al ~15%
 * durante 100 ms (eso es un CERO) o durante 200 ms (un UNO). El segundo 59
 * es la excepcion: NO baja. Ese hueco de dos segundos entre bajadas es lo
 * que marca el cambio de minuto, y es lo unico que hace falta para saber
 * por donde empieza la trama - no hay preambulo ni sincronismo aparte.
 *
 * LOS 59 BITS (0..58):
 *   0       siempre 0
 *   1..14   avisos civiles y datos meteorologicos cifrados (no se usan)
 *   15      R,  antena de reserva en uso
 *   16      A1, aviso de cambio de hora en la proxima hora
 *   17      Z1, esta vigente el horario de verano (CEST)
 *   18      Z2, esta vigente el horario de invierno (CET)
 *   19      A2, aviso de segundo intercalar
 *   20      S,  siempre 1
 *   21..27  minutos en BCD          28 P1, paridad par de 21..28
 *   29..34  horas en BCD            35 P2, paridad par de 29..35
 *   36..41  dia del mes en BCD
 *   42..44  dia de la semana (1=lunes)
 *   45..49  mes en BCD
 *   50..57  año en BCD (dos cifras) 58 P3, paridad par de 36..58
 *
 * EL DETALLE QUE SE OLVIDA SIEMPRE: la trama que se emite durante un minuto
 * describe el minuto SIGUIENTE. O sea que la hora decodificada no vale
 * cuando se termina de recibir, sino en el instante exacto de la bajada que
 * abre el minuto siguiente. Esta implementacion lo respeta: se decodifica
 * justo al detectar ese hueco de dos segundos, y la hora que sale es la de
 * ESE instante.
 *
 * POR QUE PIDE DOS TRAMAS SEGUIDAS. Las tres paridades son PARES y de un
 * solo bit, asi que cada una deja pasar cualquier numero PAR de errores en
 * su grupo. Con desvanecimiento, los errores vienen a rachas y llegan de
 * dos en dos con facilidad. Una sola trama que cuadre no es prueba
 * suficiente para ponerle la hora al reloj de alguien sin preguntar. Se
 * exige una segunda trama que ademas sea exactamente un minuto mayor que la
 * primera - eso ya no se lo salta el ruido. Cuesta un minuto mas y evita la
 * unica forma que tiene esto de fallar mal: poner una hora equivocada
 * diciendo que ha ido bien.
 *
 * ---------------------------------------------------------------------------
 * MSF - LA OTRA, EN 60 kHz DESDE ANTHORN (CUMBRIA, REINO UNIDO).
 *
 * Añadida el 23/09/2026 para tener una segunda oportunidad: si DCF77 no
 * entra desde donde estas, quiza entre esta -son direcciones distintas y
 * condiciones distintas-. Comparte con DCF77 TODA la parte de abajo (filtro,
 * seguidor de portadora, deteccion de flancos) y se diferencia en tres
 * cosas:
 *
 *   1. La portadora se apaga DEL TODO, no baja al 15%.
 *   2. Lleva DOS bits por segundo, A y B. La portadora se apaga siempre los
 *      primeros 100 ms (esa es la marca de segundo); sigue apagada de 100 a
 *      200 ms si A=1, y de 200 a 300 ms si B=1. O sea que con A=0 y B=1 hay
 *      DOS apagones separados dentro del mismo segundo.
 *   3. El minuto se marca con un apagon de 500 ms, no con un hueco.
 *
 * Reparto de los bits (segundo.bit):
 *   17A..24A  año (BCD)             54B paridad impar de 17A..24A
 *   25A..29A  mes                   55B paridad impar de 25A..35A
 *   30A..35A  dia del mes           56B paridad impar de 36A..38A
 *   36A..38A  dia de la semana      57B paridad impar de 39A..51A
 *   39A..44A  hora                  58B horario de verano en vigor
 *   45A..51A  minuto
 *   52A=0, 53A..58A=1, 59A=0   ->  el patron fijo 01111110, que es la mejor
 *                                   comprobacion que tiene esta emisora
 *
 * OJO CON LA HORA: MSF emite UTC, no hora local. DCF77 emite CET/CEST, que
 * ES la hora de España peninsular y se aplica tal cual; con MSF hay que
 * sumar una hora, o dos si el bit 58B dice que esta vigente el horario de
 * verano. Esa suma se hace AQUI, para que quien use este modulo reciba
 * siempre hora de pared española y no tenga que acordarse de cual era cual.
 *
 * Las paridades de MSF son IMPARES, al reves que las de DCF77. Otra razon
 * para que cada formato tenga su funcion y no se mezclen.
 * ---------------------------------------------------------------------------
 *
 * Este fichero no sabe nada del GD32 ni de CMSIS a proposito: se le da una
 * envolvente ya medida y devuelve una hora, asi que el simulador del PC lo
 * compila tal cual y lo mide con señal sintetica (sim/dcf77test.c). Ahi se
 * comprueba lo unico que de verdad importa: que SIN señal no se invente una
 * hora nunca.
 */

typedef enum {
    DCF_BUSCANDO = 0,   /* aun no ha visto el hueco que marca el minuto */
    DCF_LEYENDO,        /* enganchado, acumulando bits de esta trama */
    DCF_CONFIRMANDO,    /* una trama buena; esperando la siguiente para confirmar */
    DCF_LISTO,          /* dos tramas consecutivas y coherentes: la hora es fiable */
    DCF_PERDIDO         /* estaba enganchado y ha perdido el ritmo */
} dcf_estado_t;

typedef struct {
    dcf_estado_t estado;
    uint8_t  hora, minuto;      /* validos solo con estado == DCF_LISTO */
    uint8_t  dia, mes, anno;    /* anno = 0..99 */
    uint8_t  dia_semana;        /* 1 = lunes */
    uint8_t  verano;            /* 1 si esta vigente el horario de verano */
    uint8_t  bits;              /* bits (DCF77) o segundos (MSF) acumulados, 0..59 */
    uint16_t tramas_ok;         /* tramas que han pasado TODAS las comprobaciones */
    uint16_t tramas_malas;      /* tramas completas que NO las han pasado */
    float    nivel;             /* nivel de portadora estimado, en las mismas
                                 * unidades que la envolvente que se le da */
    /* Diagnostico. Sin esto, cuando algo no cuadra solo se puede adivinar:
     * lo que decide todo son estos dos numeros, y tenerlos en pantalla
     * convierte "no sincroniza" en "las marcas miden 900 ms" o "el ciclo no
     * es de un segundo", que ya son cosas distintas y arreglables. */
    uint16_t ms_marca;          /* duracion de la ultima marca medida */
    uint16_t ms_ciclo;          /* tiempo entre el principio de dos segundos */
    uint16_t marcas_raras;      /* marcas de duracion imposible descartadas */
    float    contraste;         /* (alto-bajo)/alto del ultimo segundo, 0..1.
                                 * Es lo que dice si hay señal DE VERDAD: con
                                 * ruido solo se queda cerca de 0. */
} dcf_info_t;

/* ---------------------------------------------------------------------------
 * ALS162 - ALLOUIS, FRANCIA, 162 kHz. Añadida el 23/09/2026.
 *
 * POR QUE ES LA MEJOR APUESTA DESDE ESPAÑA, y con diferencia:
 *   - 800 kW. DCF77 emite con 50 y MSF con 17. Dieciseis veces DCF77.
 *   - Allouis esta en el centro de Francia: mas cerca que Mainflingen y que
 *     Anthorn.
 *   - Y sobre todo, 162 kHz esta en la banda de onda larga de radiodifusion,
 *     mucho mas arriba que los 60 y 77 kHz. Eso importa aqui porque de esta
 *     radio SABEMOS que recibe bien en onda media -se comprobo a 955 kHz,
 *     con la imagen del oscilador enterrada bajo el ruido-, mientras que por
 *     debajo de 100 kHz no tenemos ninguna prueba de que la entrada de RF
 *     deje pasar algo. 162 esta mucho mas cerca de 955 que de 77.
 *
 * COMO VA. Aqui NO hay envolvente: la portadora no se toca, lo que se
 * modula es la FASE. Cada segundo lleva un "elemento": la fase sube +1
 * radian en 25 ms, baja -2 radianes en 50 ms y vuelve a subir +1 radian en
 * otros 25 ms. Un triangulo de 100 ms que empieza y acaba donde estaba.
 *
 *   un elemento   = bit 0
 *   dos elementos = bit 1   (200 ms, dos triangulos seguidos)
 *   ninguno       = el segundo 59, que es la marca de minuto
 *
 * O sea que la estructura del minuto es LA MISMA que la de DCF77 -59 bits y
 * un hueco- y lo unico que cambia es de donde sale cada bit. Y hay mas
 * suerte todavia: el reparto de los bits 20 al 58 (que es todo el contenido
 * de hora y fecha) es IDENTICO al de DCF77, asi que decodifica() vale tal
 * cual. Lo unico que hay que escribir de cero es el frente: sacar la fase y
 * contar elementos.
 *
 * La hora que da es la francesa (CET/CEST), que es la misma que la de
 * España peninsular. Se aplica tal cual, como DCF77 y al contrario que MSF.
 * ---------------------------------------------------------------------------
 */
/* ---------------------------------------------------------------------------
 * RBU Y WWV - LAS DOS DE FUERA DE EUROPA. Añadidas el 23/09/2026.
 *
 * Cada una tiene su fichero porque no comparten nada con las tres de arriba
 * salvo el hueco en la pantalla: ver User/hora_rbu.h y User/hora_wwv.h, que
 * es donde estan explicadas de verdad. En dos lineas:
 *
 *   RBU  66,66 kHz desde Moscu. La fase modulada por un TONO -100 Hz es un
 *        cero y 312,5 Hz es un uno-, diez elementos por segundo de los que
 *        solo dos llevan datos. Ocho paridades, la mejor protegida de las
 *        cinco. Y la mas dificil de recibir con diferencia: 10 kW a 3.450 km.
 *
 *   WWV  onda corta, 10 y 15 MHz desde Colorado. La unica que NO esta en
 *        onda larga, y por eso la mas distinta: si el problema fuera la
 *        entrada de RF por debajo de 200 kHz, esta entraria igual. La hora
 *        va en una subportadora de 100 Hz dentro del audio.
 *
 * Las dos emiten fuera del huso español -RBU da hora de Moscu y WWV da UTC-
 * y las dos se convierten a hora de pared española dentro de su modulo, con
 * la regla europea del horario de verano (User/hora_comun.h). Quien las usa
 * recibe siempre la hora del reloj de la cocina, como con las otras tres.
 * ------------------------------------------------------------------------- */
typedef enum {
    HORA_DCF77 = 0,   /* 77,5 kHz, Mainflingen (Alemania). Da hora española. */
    HORA_MSF,         /* 60 kHz, Anthorn (Reino Unido). Da UTC; se convierte. */
    HORA_ALS162,      /* 162 kHz, Allouis (Francia). Fase. Da hora española. */
    HORA_RBU,         /* 66,66 kHz, Taldom (Rusia). Fase con tonos. Da Moscu. */
    HORA_WWV10,       /* 10 MHz, Fort Collins (EEUU). Onda corta. Da UTC. */
    HORA_WWV15,       /* 15 MHz, la misma de dia. */
    HORA_BPM,         /* 10 MHz, Pucheng (China). 24 horas al dia. Da UTC. */
    /*
     * RDS - 24/09/2026. La oveja negra de esta lista, y por eso va la
     * ultima.
     *
     * Las seis de arriba son emisoras horarias: una frecuencia fija, una
     * portadora que se modula y un formato que decodifica User/dcf77.c. El
     * RDS no es nada de eso - son datos escondidos en la FM comercial, los
     * decodifica User/rds.c y no tiene frecuencia propia: vale CUALQUIER
     * emisora de FM que mande la hora, o sea casi todas.
     *
     * Entonces por que esta aqui: porque desde el punto de vista de quien
     * usa la radio hace exactamente lo mismo -poner el reloj en hora sin
     * internet- y buscarlo en otro sitio no tendria ningun sentido. La
     * pantalla de hora se bifurca en dos sitios concretos para atenderlo, y
     * los dos estan comentados.
     *
     * Su fila de k_emisoras[] lleva frecuencia 0, que significa "no
     * retuneesa nada, quedate en la FM que estes escuchando".
     */
    HORA_RDS,
    HORA_N
} hora_emisora_t;

/*
 * Cuantas entran en el BARRIDO automatico. Todas menos el RDS: el barrido
 * compara el contraste de la marca de segundo entre emisoras horarias, y el
 * RDS no tiene marca de segundo ni frecuencia propia que visitar. Meterlo
 * seria una columna vacia en la tabla comparativa.
 */
#define HORA_BARRIDO_N  ((int)HORA_RDS)

/*
 * 1 si esa emisora se lee de la FASE de la portadora y no de su envolvente.
 * Lo pregunta demod_am.c para saber que tiene que medir en cada bloque, y
 * asi la eleccion de que alimentar vive donde vive la lista de emisoras y no
 * repartida por el resto del firmware.
 */
uint8_t dcf77_usa_fase(hora_emisora_t e);

/*
 * Cuantas muestras por bloque hay que darle a dcf77_feed() para esa emisora.
 * Vale 1, 4 u 8, y sale de la MISMA tabla que la frecuencia y el nombre.
 *
 * Por que no es un numero fijo: cada emisora necesita un ritmo distinto y
 * ninguno es negociable. DCF77 y MSF miden marcas de 100 ms y con 375
 * muestras por segundo sobra. ALS162 desenrolla la fase y necesita 1.500
 * para que un error de oscilador de 190 Hz no le de la vuelta. WWV tiene que
 * ver una subportadora de 100 Hz, o sea que necesita al menos el doble de
 * eso. Y RBU tiene que distinguir un tono de 312,5 Hz, que con 1.500 se
 * quedaria demasiado cerca del limite: va a 3.000.
 *
 * Lo pregunta demod_am.c. Vive aqui para que añadir una emisora sea tocar
 * una fila de una tabla y nada mas.
 */
uint8_t dcf77_submuestras(hora_emisora_t e);

/*
 * 1 si el campo `nivel` de esa emisora es de verdad el nivel de la
 * PORTADORA, y no otra cosa.
 *
 * Solo lo es en DCF77 y MSF, que miden la envolvente directamente. En
 * ALS162 y RBU no se mide la envolvente para nada (van por fase), y en WWV
 * lo que se mide es la amplitud de la subportadora de 100 Hz, que como mucho
 * es el 9% de la portadora y ademas pasa por un filtro estrecho: enseñarla
 * en la misma barra y con la misma escala que el nivel de DCF77 dejaria la
 * barra pegada al cero incluso con WWV entrando bien.
 *
 * Donde esto vale 0, la pantalla saca el nivel del medidor de señal de la
 * radio -el mismo que mueve el S-metro-, que si es comparable entre
 * emisoras. Asi la barra de Nivel contesta siempre a la misma pregunta
 * ("llega portadora?") y la de Marca a la otra ("lleva la hora encima?").
 */
uint8_t dcf77_nivel_propio(hora_emisora_t e);

/* Cual esta puesta ahora mismo. */
hora_emisora_t dcf77_emisora(void);

/* Frecuencia a la que hay que sintonizar cada una, en Hz. Vive aqui y no en
 * main.c para que la emisora y su frecuencia no puedan separarse. */
uint32_t dcf77_freq_hz(hora_emisora_t e);
const char *dcf77_nombre(hora_emisora_t e);

/* fs_hz = a cuantas muestras por segundo se va a llamar a dcf77_feed(). */
void dcf77_start(hora_emisora_t emisora, float fs_hz);
void dcf77_stop(void);
uint8_t dcf77_activo(void);

/*
 * Una muestra por bloque, a ritmo constante fs_hz. Que es esa muestra
 * depende de la emisora: la ENVOLVENTE para DCF77 y MSF, o la FASE de la
 * portadora en radianes para ALS162. Ver dcf77_usa_fase().
 */
void dcf77_feed(float muestra);

/* Estado actual, para pintarlo. */
void dcf77_info(dcf_info_t *out);

#endif /* DCF77_H */
