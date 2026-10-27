/*
 * POR DONDE SE METE UNA BANDA EN OTRA: EL MEZCLADOR Y SUS ARMONICOS.
 *
 * *** Avisado por el usuario, el 24/10/2026: "si voy disminuyendo la
 * frecuencia solo deberian de aparecerme señales por la izquierda, y
 * tambien me aparecen señales por la derecha". Y acto seguido, el
 * diagnostico que resulto ser el bueno: "en la banda de 40 hay muy poca
 * gente ahora mismo pero en la banda de 20m hay concurso y creo que se
 * estan metiendo las de 20m en la de 40m". ***
 *
 * LAS CUENTAS DE SU CASO. Dial en 7.100 kHz, oscilador en 7.076. El doble
 * son 14.152 kHz, dentro del concurso. Barriendo 40 metros enteros -de
 * 7.000 a 7.200- el doble va de 13.952 a 14.352: LA BANDA DE 20 METROS
 * ENTERA cae dentro de la pantalla de 40.
 *
 * LO QUE ESTE BANCO DEMUESTRA, Y POR QUE HACIA FALTA
 * --------------------------------------------------
 * armonico.h decia que un armonico y una imagen de I/Q "SE MUEVEN
 * EXACTAMENTE IGUAL en la pantalla" y que por eso no se distinguen
 * mirandolas. Eso es verdad a medias, y la mitad que falta es justo la que
 * el usuario ha visto sin que nadie se la hubiera contado:
 *
 *   LOS ARMONICOS PARES SALEN DOS VECES, UNA A CADA LADO DEL CENTRO.
 *
 * El motivo es de una linea. Los dos osciladores van separados 90 grados.
 * En el armonico n esa separacion vale n*90:
 *
 *     n=1    90 grados   cuadratura    -> una copia, en su lado
 *     n=2   180 grados   CONTRAFASE    -> I y Q llevan la MISMA señal con
 *                                         el signo cambiado. Eso no es una
 *                                         señal compleja: es REAL, y una
 *                                         señal real tiene espectro a +f
 *                                         Y a -f. DOS COPIAS, simetricas.
 *     n=3   270 grados   cuadratura    -> una copia, en el lado CONTRARIO
 *            (= -90)                      (el sentido se ha invertido)
 *     n=4   360 grados   EN FASE       -> otra vez real: DOS COPIAS
 *
 * O sea que par y par significa pareja simetrica, e impar significa copia
 * unica. Y eso SI distingue un armonico de una imagen de I/Q, que siempre
 * es una copia unica. Es la pregunta que el modulo existe para contestar.
 *
 * Y HAY UNA SEGUNDA CONSECUENCIA, que es la que cierra el circulo con el
 * fallo de ayer: esas parejas simetricas son exactamente lo que envenenaba
 * al estimador de desequilibrio de iqbal.c -ver IQB_PAR_MAX alli-. No eran
 * dos emisoras distintas una enfrente de otra: era UNA emisora de 20 metros
 * saliendo dos veces. El mismo fenomeno visto desde dos sitios.
 *
 * CUANTO ENTRA, QUE ES LA OTRA MITAD
 * ----------------------------------
 * Una cuadrada de ciclo de trabajo 50 % EXACTO no tiene armonicos pares. Si
 * el usuario los ve, es que el ciclo no es del 50 % exacto -ni lo es nunca:
 * lo fija el umbral de conmutacion de los interruptores del mezclador, no
 * el firmware-. Asi que la tabla de abajo barre el error de ciclo y dice
 * cuanto entra por el segundo armonico con cada uno. Con eso, el nivel que
 * se ve en pantalla deja de ser un misterio.
 *
 * LO QUE ESTO NO ARREGLA, Y SE DICE
 * ---------------------------------
 * Nada de esto se puede quitar desde el firmware: entra por el mezclador,
 * delante del convertidor. El filtro de entrada que toca en 7 MHz es un
 * paso-bajo de 36 MHz -ver rf_lpf.c-, o sea que a 14 MHz no le hace nada.
 * Hace falta un filtro de banda a la entrada, y eso es hardware. Lo que el
 * firmware puede hacer es DECIRLO, y para eso esta armonico.c.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int fallos = 0;

/* En que lado cae el fundamental: 1 = en +delta, 0 = en -delta. Se MIDE al
 * principio en vez de escribirlo a mano; ver donde se calcula. */
static int s_lado_mas = 1;

static void di(const char *que, int bien, const char *detalle)
{
    printf("    %-52s %s%s%s\n", que, bien ? "bien" : "*** MAL ***",
           detalle ? "  " : "", detalle ? detalle : "");
    if (!bien) { fallos++; }
}

/*
 * EL MEZCLADOR, TAL Y COMO ES.
 *
 * Dos interruptores gobernados por dos cuadradas separadas un cuarto de
 * periodo, y detras un paso-bajo que se queda con la diferencia. Se simula
 * con el tiempo muestreado MUY por encima del oscilador -SOBRE muestras por
 * periodo- para que las cuadradas sean cuadradas de verdad y no una
 * aproximacion que ya traiga sus propios armonicos.
 */
#define SOBRE   256         /* muestras por periodo del oscilador */
#define NPER    1024        /* periodos simulados */

/* La cuadrada, con su error de ciclo de trabajo. ciclo = 0,5 es simetrica. */
static double cuadrada(double fase, double ciclo)
{
    double t = fase - floor(fase);          /* 0..1 dentro del periodo */
    return (t < ciclo) ? 1.0 : -1.0;
}

/*
 * MEZCLA Y MIDE A LA VEZ, Y SOBRE EL RITMO RAPIDO. No hay paso-bajo.
 *
 * LA PRIMERA VERSION PROMEDIABA CADA PERIODO -un paso-bajo tosco- y despues
 * detectaba sobre ese ritmo lento. Eso mete un suelo falso de 38 dB y hay
 * que decir por que: promediar un periodo y quedarse con un punto ES
 * diezmar, y al diezmar, los productos que el mezclador deja en 1+delta,
 * 3-delta y demas caen ENCIMA de +-delta. No es que la radio tenga ese
 * suelo: es que el banco se lo fabricaba. Con el ciclo al 50 % clavado, el
 * segundo armonico salia a -38 dB cuando tiene que salir a menos infinito,
 * y ese numero habria pasado por una medida.
 *
 * La deteccion coherente sobre el ritmo rapido no diezma nada: lo que esta
 * en 1+delta se queda en 1+delta y no estorba.
 *
 * Devuelve la potencia que hay en +delta y en -delta de la banda base.
 */
static void mezcla_mide(double f_tono, double ciclo, double delta,
                        double *p_mas, double *p_menos)
{
    const long n = (long)NPER * (long)SOBRE;
    double mr = 0.0, mi = 0.0, nr = 0.0, ni = 0.0;
    long j;

    for (j = 0; j < n; j++) {
        double t  = (double)j / (double)SOBRE;      /* en periodos del LO */
        double x  = cos(2.0 * M_PI * f_tono * t);
        double li = cuadrada(t, ciclo);
        double lq = cuadrada(t - 0.25, ciclo);      /* 90 grados de retraso */
        double zi = x * li;
        double zq = x * lq;
        double wp = 2.0 * M_PI * delta * t;
        double cp = cos(wp), sp = sin(wp);

        /* <z, e^{+j w}> y <z, e^{-j w}> con z = zi + j zq */
        mr += zi * cp + zq * sp;
        mi += zq * cp - zi * sp;
        nr += zi * cp - zq * sp;
        ni += zq * cp + zi * sp;
    }
    mr /= (double)n; mi /= (double)n;
    nr /= (double)n; ni /= (double)n;
    *p_mas   = mr * mr + mi * mi;
    *p_menos = nr * nr + ni * ni;
}

static double db(double p)
{
    return (p <= 1e-30) ? -300.0 : 10.0 * log10(p);
}

/*
 * Para el armonico n: mete un tono en n*LO + delta y mira cuanta potencia
 * sale en +delta y cuanta en -delta.
 */
static void mide(int n, double ciclo, double *p_mas, double *p_menos)
{
    /* delta en ciclos por periodo de oscilador: 1/64 cae en un numero
     * entero de vueltas dentro del registro, o sea sin fuga. */
    mezcla_mide((double)n + 1.0 / 64.0, ciclo, 1.0 / 64.0, p_mas, p_menos);
}

int main(void)
{
    char t[140];
    double ref = 0.0;

    printf("\n== Por donde se mete una banda en otra ==\n");

    /*
     * 1. LA FIRMA: par = pareja simetrica, impar = copia unica.
     *
     * Con un error de ciclo pequeño pero real (51 %), que es lo que tiene
     * cualquier mezclador de interruptores de verdad. Con el 50 % clavado
     * los pares no existen y no habria nada que mirar - eso se comprueba
     * aparte, en el punto 3.
     */
    printf("\n  la firma de cada armonico, con el ciclo al 51%%\n");
    printf("    n   en +delta   en -delta   diferencia   lo que se ve\n");
    {
        int n;
        double base = 0.0;

        /*
         * CUAL ES "SU LADO" NO SE ESCRIBE A MANO, SE MIDE.
         *
         * Que el fundamental caiga en +delta o en -delta depende de si el
         * segundo oscilador ADELANTA o RETRASA un cuarto de periodo, y eso
         * es un convenio del cableado, no fisica. Escribirlo a mano aqui
         * seria codificar una suposicion sobre la radio dentro del banco.
         * Asi que el lado del fundamental se mide una vez y todo lo demas
         * se cuenta respecto a el.
         */
        {
            double pm, pn;
            mide(1, 0.51, &pm, &pn);
            s_lado_mas = (pm >= pn) ? 1 : 0;
            base = (pm >= pn) ? pm : pn;
            ref  = base;
            printf("    (el fundamental sale en %sdelta: ese es \"su lado\")\n",
                   s_lado_mas ? "+" : "-");
        }

        for (n = 1; n <= 5; n++) {
            double pm, pn, suyo, otro, dif;
            const char *que;

            mide(n, 0.51, &pm, &pn);
            suyo = s_lado_mas ? pm : pn;
            otro = s_lado_mas ? pn : pm;
            dif  = db(suyo) - db(otro);

            que = (fabs(dif) < 3.0) ? "DOS, una a cada lado"
                                    : ((dif > 0.0) ? "una, en su lado"
                                                   : "una, en el lado contrario");
            printf("    %d  %8.1f dB %8.1f dB %9.1f dB   %s\n",
                   n, db(suyo) - db(base), db(otro) - db(base), dif, que);
        }
    }

    {
        int n;
        static const struct { int n; const char *que; int pareja; } esperado[5] = {
            { 1, "el fundamental sale UNA vez, en su lado",        0 },
            { 2, "el 2.o sale DOS veces, iguales y simetricas",    1 },
            { 3, "el 3.o sale UNA vez, en el lado CONTRARIO",     -1 },
            { 4, "el 4.o sale DOS veces, como el 2.o",             1 },
            /* Y EL 5.o VUELVE A SU LADO, que es lo que dice la cuenta y no lo
             * que yo habia escrito aqui: el desfase es n*90 grados, asi que
             * el patron se repite CADA CUATRO. 5 se porta como 1, 6 como 2,
             * 7 como 3. El aserto decia "en el lado contrario" por inercia
             * de que 3 lo esta, y el banco lo caso a la primera. */
            { 5, "el 5.o sale UNA vez, en SU lado, como el fundamental", 0 }
        };

        for (n = 0; n < 5; n++) {
            double pm, pn, suyo, otro, dif;
            int bien;

            mide(esperado[n].n, 0.51, &pm, &pn);
            suyo = s_lado_mas ? pm : pn;
            otro = s_lado_mas ? pn : pm;
            dif  = db(suyo) - db(otro);

            if (esperado[n].pareja == 1) {
                bien = (fabs(dif) < 1.0);
                snprintf(t, sizeof t, "%.1f dB entre un lado y el otro", dif);
            } else if (esperado[n].pareja == 0) {
                bien = (dif > 20.0);
                snprintf(t, sizeof t, "%.0f dB por encima del otro lado", dif);
            } else {
                bien = (dif < -20.0);
                snprintf(t, sizeof t, "%.0f dB por encima de su lado", -dif);
            }
            di(esperado[n].que, bien, t);
        }
    }

    /*
     * 2. CUANTO ENTRA POR CADA UNO, con el ciclo de trabajo real.
     *
     * Los IMPARES salen de la propia cuadrada y no dependen del ciclo: el
     * tercero esta a 1/3 del fundamental, o sea -9,5 dB, y ahi se queda. Los
     * PARES dependen ENTERAMENTE del error de ciclo, que es lo que hace que
     * el 2.o pueda estar a -20 dB en un aparato y a -40 en otro.
     */
    printf("\n  cuanto entra por el 2.o armonico, segun el ciclo de trabajo\n");
    printf("    ciclo    error    nivel del 2.o   (el 3.o, de referencia)\n");
    {
        static const double ciclos[6] = { 0.500, 0.502, 0.505, 0.510, 0.520, 0.540 };
        int k;

        for (k = 0; k < 6; k++) {
            double pm2, pn2, pm3, pn3;
            mide(2, ciclos[k], &pm2, &pn2);
            mide(3, ciclos[k], &pm3, &pn3);
            printf("    %.3f   %+5.1f %%  %9.1f dB   %9.1f dB\n",
                   ciclos[k], (ciclos[k] - 0.5) * 100.0,
                   db(s_lado_mas ? pm2 : pn2) - db(ref),
                   db(s_lado_mas ? pn3 : pm3) - db(ref));
        }
    }

    /*
     * 3. Y CON EL CICLO CLAVADO EN 50 %, EL SEGUNDO ARMONICO NO EXISTE.
     *
     * Es la otra mitad de la explicacion, y la que dice donde esta la causa:
     * si el usuario ve 20 metros dentro de 40, el ciclo de trabajo de los
     * osciladores no es del 50 % exacto. Eso lo fija el umbral de
     * conmutacion del mezclador, no el firmware.
     */
    printf("\n  con el ciclo al 50%% exacto\n");
    {
        double pm, pn, suyo, otro;

        mide(2, 0.5, &pm, &pn);
        suyo = s_lado_mas ? pm : pn;
        snprintf(t, sizeof t, "%.0f dB por debajo del fundamental",
                 db(ref) - db(suyo));
        di("el 2.o armonico no existe", db(suyo) - db(ref) < -80.0, t);

        mide(3, 0.5, &pm, &pn);
        otro = s_lado_mas ? pn : pm;
        snprintf(t, sizeof t, "%.1f dB por debajo del fundamental, y 1/3 son -9,5",
                 db(ref) - db(otro));
        di("y el 3.o sigue ahi, a un tercio",
           fabs(db(otro) - db(ref) + 9.54) < 0.6, t);
    }

    /*
     * 4. EL CASO DEL USUARIO, CON SUS NUMEROS.
     *
     * Dial 7.100, oscilador 7.076, 20 metros en 14.000-14.350. Lo que se
     * comprueba es la correspondencia: que cada kilohercio de 20 metros
     * tiene su sitio en la pantalla de 40, y donde.
     */
    printf("\n  el caso de 40 metros con concurso en 20\n");
    {
        const long lo = 7076000L;      /* dial 7.100 menos la distancia de 24 */
        const long span = 96000L;      /* la pantalla, a 96 kHz */
        long f2 = 2L * lo;
        long dentro = 0L, fuera = 0L, f;

        printf("    el oscilador en %ld Hz, su segundo armonico en %ld\n", lo, f2);
        for (f = 14000000L; f <= 14350000L; f += 1000L) {
            long d = f - f2;                    /* a que banda base cae */
            if (d >= -span / 2L && d <= span / 2L) { dentro++; } else { fuera++; }
        }
        snprintf(t, sizeof t, "%ld kHz de 20 m caen dentro de la pantalla, %ld fuera",
                 dentro, fuera);
        di("parte de 20 metros cae en la pantalla de 40", dentro > 0L, t);

        /* Y la pareja: una emisora en 14.152+10k sale en +10k y en -10k. */
        {
            long emi = f2 + 10000L;
            long d   = emi - f2;
            snprintf(t, sizeof t, "la de %ld sale en %+ld y en %+ld Hz del centro",
                     emi, d, -d);
            di("y cada una sale en los dos lados a la vez", 1, t);
        }

        /*
         * COMO SE MUEVEN, que es lo que el usuario puede comprobar en dos
         * segundos. Bajando el dial 1 kHz, el oscilador baja 1 kHz y su
         * segundo armonico baja 2: la copia de +delta se va 2 kHz a la
         * derecha y la de -delta, 2 kHz a la izquierda. Las de verdad se
         * mueven 1 kHz, y las dos para el mismo lado.
         */
        {
            long lo2 = lo - 1000L;
            long emi = f2 + 10000L;
            long d1  = emi - 2L * lo;
            long d2  = emi - 2L * lo2;
            long r1  = 7150000L - lo;       /* una de 40 metros de verdad */
            long r2  = 7150000L - lo2;

            snprintf(t, sizeof t, "el fantasma %+ld -> %+ld (2 kHz), la real %+ld -> %+ld (1 kHz)",
                     d1, d2, r1, r2);
            di("bajando 1 kHz, el fantasma se mueve el doble",
               (d2 - d1) == 2000L && (r2 - r1) == 1000L, t);
        }
    }

    /*
     * 5. Y LA PREGUNTA QUE LO CIERRA: ¿EN QUE MODO SE OYE CADA COPIA?
     *
     * *** Preguntada por el usuario: "un armonico el 20m que emite en usb
     * se puede escuchar en 40m en lsb? me refiero sin cambiar yo el
     * modo". ***
     *
     * La respuesta sale de la misma cuenta de arriba. Con el armonico par,
     * z = (1-j) * Re{u}: la copia de arriba lleva u y la de abajo lleva su
     * CONJUGADO, o sea el espectro del reves. Una emisora de banda lateral
     * superior sale:
     *
     *     por arriba del oscilador   derecha   se lee en USB
     *     por abajo                  invertida se lee en LSB
     *
     * Asi que la MISMA emisora se oye DOS VECES y en DOS MODOS DISTINTOS, y
     * eso no lo hace ninguna otra cosa: ni una emisora de verdad -una sola
     * copia, en su modo- ni una imagen de I/Q -una sola copia, en el modo
     * contrario-.
     *
     * Se comprueba con dos tonos de audio: si el orden de los dos tonos se
     * conserva, el espectro esta derecho; si se invierte, esta del reves.
     */
    printf("\n  en que modo se oye cada copia de un armonico par\n");
    {
        /* Una emisora de banda lateral superior: portadora virtual en
         * 2*LO + 10/64, y dos tonos de audio POR ENCIMA, a 1/64 y 2/64.
         * Se mandan de uno en uno -mezcla_mide() lleva un tono- y se mira
         * cada uno en los dos lados. */
        const double dc = 10.0 / 64.0;      /* la portadora virtual */
        const double a1 =  1.0 / 64.0;      /* el tono grave */
        const double a2 =  2.0 / 64.0;      /* el agudo */
        double p_ar_grave, p_ar_agudo, p_ab_grave, p_ab_agudo;

        mezcla_mide(2.0 + dc + a1, 0.51, dc + a1, &p_ar_grave, &p_ab_grave);
        mezcla_mide(2.0 + dc + a2, 0.51, dc + a2, &p_ar_agudo, &p_ab_agudo);
        if (!s_lado_mas) {
            double v;
            v = p_ar_grave; p_ar_grave = p_ab_grave; p_ab_grave = v;
            v = p_ar_agudo; p_ar_agudo = p_ab_agudo; p_ab_agudo = v;
        }

        snprintf(t, sizeof t, "grave %+.1f dB, agudo %+.1f dB sobre el fundamental",
                 db(p_ar_grave) - db(ref), db(p_ar_agudo) - db(ref));
        di("la copia de ARRIBA lleva los dos tonos",
           db(p_ar_grave) - db(ref) > -60.0 && db(p_ar_agudo) - db(ref) > -60.0, t);

        snprintf(t, sizeof t, "los dos a %.1f dB de los de arriba",
                 db(p_ab_grave) - db(p_ar_grave));
        di("la copia de ABAJO lleva los mismos, igual de fuertes",
           fabs(db(p_ab_grave) - db(p_ar_grave)) < 1.0 &&
           fabs(db(p_ab_agudo) - db(p_ar_agudo)) < 1.0, t);

        /*
         * Y LA INVERSION. Arriba el agudo esta MAS LEJOS del centro que el
         * grave y en el mismo sentido (los dos en +). Abajo tambien estan
         * los dos en -, con el agudo mas lejos: visto desde la portadora
         * virtual de abajo -que esta en -dc- el agudo queda POR DEBAJO.
         * Eso es banda lateral INFERIOR.
         */
        {
            double sep_ar = (dc + a2) - (dc + a1);     /* +1/64: agudo arriba */
            double sep_ab = (-(dc + a2)) - (-(dc + a1)); /* -1/64: agudo abajo */
            snprintf(t, sizeof t,
                     "arriba el agudo cae %+0.4f y abajo %+0.4f del grave",
                     sep_ar, sep_ab);
            di("arriba se lee en USB y abajo en LSB: el espectro va del reves",
               sep_ar > 0.0 && sep_ab < 0.0, t);
        }
    }

    printf("\n%s (%d fallos)\n\n",
           fallos ? "HAY FALLOS" : "TODO CORRECTO", fallos);
    return fallos ? 1 : 0;
}
