#ifndef RAPIDO_H
#define RAPIDO_H

/*
 * CUENTAS RAPIDAS: lo que la biblioteca hace bien pero demasiado caro.
 * 04/10/2026.
 *
 * En este Cortex-M4 el coprocesador hace sumas, multiplicaciones, divisiones
 * y raices cuadradas en hardware. Lo demas -senos, cosenos, arcotangentes,
 * exponenciales- son RUTINAS DE SOFTWARE de newlib, de un par de cientos de
 * ciclos cada una. Eso no importa si se llaman una vez por bloque; importa, y
 * mucho, si se llaman una vez por MUESTRA dentro de la interrupcion de audio.
 *
 * A 96 kHz y 256 muestras por bloque, el presupuesto de un bloque son 533.333
 * ciclos. Una sola rutina de 200 ciclos por muestra se lleva 51.200, casi el
 * 10%. Tres se llevan el 29%, y cuando la interrupcion no cabe en su tiempo
 * lo que se queda sin CPU es el bucle principal: la pantalla, el tactil y los
 * menus. Por eso se nota como "la radio va lenta" y no como "suena mal".
 *
 * Aqui van los sustitutos. La regla es la de siempre en este proyecto: el
 * error se MIDE y se escribe, y el banco comprueba que el atajo da el mismo
 * resultado que la cuenta cara en el sitio donde se usa -no solo que el
 * numero se parezca-.
 *
 * El seno y el coseno no estan aqui: ya los da la tabla del NCO con
 * nco_sen_cos_rad(). Esto es para lo que no encaja en una tabla.
 */

/*
 * Arcotangente de dos argumentos, cuatro cuadrantes.
 *
 * Error maximo medido sobre 2.000.001 puntos repartidos por los cuatro
 * cuadrantes: **0,00062 radianes, o sea 0,036 grados**, con pendiente en
 * cero 0,995355 y una distorsion del discriminador del 0,037 % con una
 * desviacion de +-5 kHz. Cuesta una division, tres multiplicaciones y dos
 * sumas, frente a los ~200 ciclos de atan2f().
 *
 * (Aqui ponia "0,0102 radianes, seis decimas de grado". Ese era el error
 * del estimador ANTERIOR, el que se sustituyo el 06/10/2026, y es
 * DIECISEIS VECES peor que el que hay. Lo malo de un numero viejo en una
 * cabecera no es que este mal: es que quien la lea para decidir si el
 * atajo vale en un sitio nuevo lo va a descartar teniendo margen de
 * sobra. Corregido el 27/10/2026, remedido.)
 *
 * DONDE VALE Y DONDE NO. Vale para alimentar lazos y discriminadores, que es
 * donde se usa: un PLL se come ese sesgo en el integrador sin enterarse, y
 * un discriminador de FM lo convierte en una distorsion muy por debajo del
 * ruido del canal. NO vale para dar un angulo que alguien vaya
 * a leer o a comparar contra una norma; para eso esta atan2f() y no pasa nada
 * por llamarla una vez.
 */
float rapido_atan2(float y, float x);

#endif /* RAPIDO_H */
