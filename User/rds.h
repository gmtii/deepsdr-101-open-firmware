#ifndef RDS_H_INCLUDED
#define RDS_H_INCLUDED

#include <stdint.h>

/*
 * ============================================================================
 * RDS - los datos que viajan escondidos en la FM comercial
 * ============================================================================
 * 23/09/2026.
 *
 * QUE ES. Toda emisora de FM que se precie manda, ademas del audio, un
 * chorrito de datos a 1187,5 bits por segundo montado en una subportadora de
 * 57 kHz. Ahi va el nombre de la emisora, el texto que se ve en la radio del
 * coche, el identificador de la cadena... y LA HORA, que es lo que nos
 * interesa aqui: la hora de un reloj atomico, sin esperar cuatro minutos a
 * que DCF77 se digne, y con señal de sobra a plena luz del dia.
 *
 * POR QUE SE PUEDE HACER EN ESTA RADIO. El camino de FM ancha corre a 192
 * kHz, o sea que la ventana llega a +/-96 kHz: los 57 kHz caben con holgura.
 * Y el discriminador ya produce la señal multiplex ENTERA - audio, piloto de
 * 19 kHz, estereo y RDS-; lo que pasa es que justo despues hay un filtro de
 * audio de 15 kHz que se lo carga todo. Por eso el RDS se pincha ANTES de la
 * deenfasis y del filtro de audio, con el discriminador todavia crudo.
 *
 * COMO ESTA HECHO, de arriba abajo:
 *
 *   1. Mezclar por 57 kHz con el NCO de User/nco.c y bajar a banda base
 *      compleja. Esto no habria sido posible hace dos dias: el unico
 *      desplazamiento que sabia hacer esta radio era Fs/4.
 *
 *   2. Filtrar a +/-2,6 kHz y diezmar por 16, de 192 kHz a 12 kHz. Los
 *      coeficientes los genera deepsdr-ui/tools/gen_rds_fir.py; alli esta el
 *      diseno y lo que se plieguesobre la señal (-63 dB).
 *
 *   3. Recuperar la fase de la portadora. La subportadora va con portadora
 *      SUPRIMIDA -es BPSK-, asi que no hay nada a lo que engancharse
 *      directamente: se usa un lazo de Costas.
 *
 *   4. Recuperar el reloj de bit. El RDS va codificado en BIFASE, que tiene
 *      una transicion garantizada en mitad de cada simbolo pase lo que pase
 *      con los datos. Esa transicion es el reloj, y engancharse a ella no
 *      depende de lo que se este transmitiendo.
 *
 *   5. Integrar media y media: el valor del simbolo es la primera mitad
 *      menos la segunda. Eso ES el filtro adaptado de una señal bifase, sin
 *      necesidad de un coseno alzado aparte.
 *
 *   6. Decodificacion diferencial, que de paso resuelve la ambiguedad de 180
 *      grados que el lazo de Costas no puede resolver por si mismo.
 *
 *   7. Sincronismo de bloque por SINDROME. Cada bloque son 16 bits de datos
 *      y 10 de comprobacion, y esos 10 llevan sumada una "palabra de
 *      desplazamiento" distinta segun la posicion del bloque en el grupo
 *      (A, B, C, C' o D). Al dividir el bloque recibido por el polinomio
 *      generador, el resto TIENE que ser esa palabra. Asi se sabe a la vez
 *      si el bloque esta bien y en que posicion del grupo estamos, sin
 *      ninguna marca de sincronismo.
 *
 *   8. Y los grupos: 0A/0B para el nombre, 2A/2B para el radiotexto y 4A
 *      para la hora.
 */

typedef struct {
    uint8_t  enganchado;    /* 1 = hay sincronismo de bloque */
    float    calidad;       /* fraccion de bloques con el resto correcto, 0..1 */
    uint32_t bloques_ok;
    uint32_t bloques_mal;

    uint8_t  tiene_pi;
    uint16_t pi;            /* identificador de programa */

    uint8_t  tiene_ps;
    char     ps[9];         /* nombre de emisora, 8 caracteres */

    uint8_t  tiene_rt;
    char     rt[65];        /* radiotexto, 64 caracteres */

    /* --- la hora (grupo 4A) --- */
    uint8_t  tiene_hora;
    /* Sube uno cada vez que llega un grupo 4A que pasa TODAS las
     * comprobaciones. main.c lo vigila para exigir que dos horas seguidas
     * sean coherentes entre si antes de tocar el reloj - ver alli. */
    uint32_t hora_seq;
    /* 1 = los cuatro bloques de ese grupo llegaron BIEN, sin que la
     * correccion tuviera que tocar ninguno. Es el nivel de confianza que
     * tenia el decodificador antes de que existiera la correccion, y por eso
     * una hora asi se aplica sin esperar confirmacion. */
    uint8_t  hora_limpia;
    uint8_t  hora;          /* UTC */
    uint8_t  minuto;
    uint8_t  dia;
    uint8_t  mes;
    uint8_t  anno2;         /* dos cifras */
    int8_t   desp_medias;   /* desplazamiento local en MEDIAS horas, con signo */
    /* En que muestra de banda base (12 kHz) acabo ese grupo 4A. Sirve para
     * el SEGUNDO, no para el minuto: ver rds_hora_atraso_ms(). */
    uint32_t hora_muestra;
} rds_info_t;

/* Arranca el decodificador. fs_hz es la tasa del MULTIPLEX que le va a
 * llegar por rds_feed(), o sea 192000 en esta radio. */
void rds_start(float fs_hz);
void rds_stop(void);
uint8_t rds_activo(void);

/* Un bloque de muestras del discriminador, crudas (antes de la deenfasis y
 * del filtro de audio). Se llama desde la interrupcion. */
void rds_feed(const float *mpx, uint32_t n);

/* Lo que se sepa hasta ahora. */
void rds_info(rds_info_t *out);

/*
 * EL SEGUNDO, NO SOLO EL MINUTO. 24/09/2026.
 *
 * La hora del grupo 4A trae horas y minutos, pero no segundos - y no le hacen
 * falta, porque el segundo esta en CUANDO llega: la norma exige que el borde
 * de minuto caiga a menos de 0,1 s del FINAL del grupo. O sea que el propio
 * instante en que acaba de llegar el grupo ES el cambio de minuto.
 *
 * Esto devuelve cuanto hace de eso, en milisegundos. Quien lo llame resta ese
 * atraso al reloj de la radio y ya sabe donde cae el borde de minuto con la
 * precision que da la norma, que son esos 0,1 s.
 *
 * Devuelve 0 mientras no haya llegado ninguna hora (mirar tiene_hora).
 */
uint32_t rds_hora_atraso_ms(void);

/* La tasa (Hz) en la que esta contada hora_muestra. */
float rds_fs_bb(void);

/* ----------------------------------------------------------------------
 * Lo de abajo se expone SOLO para el banco del simulador (sim/rdstest.c),
 * que necesita fabricar una señal de RDS de verdad para medir el
 * decodificador. Construir los bloques en el banco con su propia cuenta del
 * CRC seria comprobar que dos copias de la misma formula coinciden, que no
 * demuestra nada.
 * ---------------------------------------------------------------------- */

/* El resto de dividir un bloque de 26 bits por el polinomio generador del
 * RDS. En un bloque correcto vale exactamente la palabra de desplazamiento
 * que le toca por su posicion en el grupo. */
uint16_t rds_resto(uint32_t bloque26);

/* El estado interno de los dos lazos, para que el banco pueda ver si se
 * estan yendo en vez de deducirlo de que los bloques empeoran. Al mirar esto
 * se encontro que la primera version del lazo de reloj era un doble
 * integrador. */
void rds_lazos_dbg(float *cos_frec_hz, float *sim_err_ppm, float *nivel);
void rds_cuentas_dbg(uint32_t *enganches, uint32_t *perdidas);

/* La muestra de banda base por la que va el decodificador. Con esto y con
 * hora_muestra, el banco mide el retardo propio del decodificador en vez de
 * estimarlo. */
uint32_t rds_muestras_dbg(void);

/*
 * Intenta ARREGLAR un bloque de 26 bits suponiendo que el error es una
 * rafaga de como mucho `max_raf` bits seguidos, sabiendo que palabra de
 * desplazamiento le toca.
 *
 * Devuelve 1 y deja el bloque corregido en *bloque26 si encuentra UNA rafaga
 * que lo explique; 0 si no.
 *
 * Esta expuesto porque el banco tiene que medir las DOS caras: cuanto sube
 * el rendimiento y cuanto sube el riesgo de aceptar basura. Lo segundo no es
 * teorico - corregir amplia el conjunto de palabras que se dan por buenas, y
 * una hora "corregida" mal es peor que no tener hora.
 */
uint8_t rds_corrige(uint32_t *bloque26, uint16_t off, uint8_t max_raf);

/* Las cinco palabras de desplazamiento, por si el banco necesita nombrarlas. */
#define RDS_OFF_A   0x0FCU
#define RDS_OFF_B   0x198U
#define RDS_OFF_C   0x168U
#define RDS_OFF_CP  0x350U
#define RDS_OFF_D   0x1B4U

#endif /* RDS_H_INCLUDED */
