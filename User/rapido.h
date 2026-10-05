#ifndef RAPIDO_H
#define RAPIDO_H

/*
 * CUENTAS RAPIDAS: lo que la biblioteca hace bien pero demasiado caro.
 * 04/10/2026.
 *
 * *** El dueño: "al elegir modo sam la radio se ralentiza un monton" ...
 * "revisa todos los posibles puntos y no me tengas dando vueltas". ***
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
 * Error maximo medido en 100.000 puntos repartidos por los cuatro
 * cuadrantes: 0,0102 radianes, o sea seis decimas de grado. Cuesta una
 * division, tres multiplicaciones y dos sumas, frente a los ~200 ciclos de
 * atan2f().
 *
 * DONDE VALE Y DONDE NO. Vale para alimentar lazos y discriminadores, que es
 * donde se usa: un PLL se come un sesgo de seis decimas de grado en el
 * integrador, y un discriminador de FM lo convierte en una distorsion muy
 * por debajo del ruido del canal. NO vale para dar un angulo que alguien vaya
 * a leer o a comparar contra una norma; para eso esta atan2f() y no pasa nada
 * por llamarla una vez.
 */
float rapido_atan2(float y, float x);

#endif /* RAPIDO_H */
