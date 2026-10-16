/*
 * LA TABLA DE AVIONES: EL QUINTO MODULO QUE NADIE HABIA PROBADO.
 *
 * POR QUE EXISTE (25/09/2026). En la pantalla de Reykjavik salen filas que
 * dicen "ID 90" y filas que dicen "717C9C". Son lo mismo: HFDL le asigna a
 * cada avion un numero corto al hacer el logon y lo llama asi hasta que se
 * caza su CONFIRM, que es el que trae la direccion OACI de verdad. Entonces
 * hfdl_ac_update() tiene que FUNDIR los dos registros en uno.
 *
 * Esa fusion es la unica pieza del camino que no se ve fallar: si no funde,
 * no sale un error, salen DOS FILAS del mismo avion, una con modelo y otra
 * sin el, y parece trafico. Y si funde de mas, dos aviones distintos se
 * comen las posiciones el uno del otro y la pantalla miente con coordenadas
 * que parecen buenas.
 *
 * Aqui no hay nada fingido: es hfdl_aircraft.c tal cual, alimentado con los
 * mismos eventos que le arma hfdl_modo.c al recibir una trama.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "hfdl_aircraft.h"

static int fallos;

static void exige(int cond, const char *que)
{
    printf("    %-56s %s\n", que, cond ? "bien" : "MAL");
    if (!cond) { fallos++; }
}

/* --- un evento como los que arma hfdl_modo.c --- */
static hfdl_ac_event_t ev_base(uint8_t gs, uint8_t ac)
{
    hfdl_ac_event_t e;
    memset(&e, 0, sizeof e);
    e.ac_id = ac;
    e.gs_id = gs;
    e.lpdu_type = 0x0D;          /* USER DATA */
    e.hh = 16U; e.mm = 1U; e.ss = 0U;
    return e;
}

static hfdl_ac_rec_t *mete(hfdl_ac_table_t *t, const hfdl_ac_event_t *e)
{
    uint8_t pide = 0U;
    return hfdl_ac_update(t, e, &pide);
}

static hfdl_ac_rec_t *busca_icao(hfdl_ac_table_t *t, uint32_t icao)
{
    uint8_t i;
    for (i = 0U; i < t->count; i++) {
        if (((t->rec[i].flags & HFDL_AC_R_ICAO) != 0U) && (t->rec[i].icao == icao)) {
            return &t->rec[i];
        }
    }
    return (hfdl_ac_rec_t *)0;
}

/* ================= la fusion, que es lo que importa ================= */
static void prueba_fusion(void)
{
    hfdl_ac_rec_t mem[8];
    hfdl_ac_table_t t;
    hfdl_ac_event_t e;
    hfdl_ac_rec_t *r;

    printf("\n  ID -> OACI: lo que pasa cuando por fin llega el CONFIRM\n");
    hfdl_ac_init(&t, mem, sizeof mem);

    /* 1. Solo lo conocemos por su ID, como el "ID 90" de la pantalla. */
    e = ev_base(9U, 90U);
    e.flags = HFDL_AC_EV_FLIGHT;
    memcpy(e.flight, "VLG729", 7);
    (void)mete(&t, &e);
    exige(t.count == 1U, "un avion nuevo abre una fila");
    exige((t.rec[0].flags & HFDL_AC_R_ICAO) == 0U, "sin OACI todavia: se le conoce por el ID");

    /* 2. Otra trama suya, todavia sin OACI: la MISMA fila, no una nueva. */
    e = ev_base(9U, 90U);
    e.flags = HFDL_AC_EV_POS;
    e.lat_e4 = 510000; e.lon_e4 = -8000;
    (void)mete(&t, &e);
    exige(t.count == 1U, "otra trama del mismo ID no abre fila nueva");
    exige(t.rec[0].count == 2U, "y le cuenta las dos tramas");

    /* 3. Llega su CONFIRM con la direccion. AQUI se funde. */
    e = ev_base(9U, 90U);
    e.flags = (uint8_t)(HFDL_AC_EV_ICAO | HFDL_AC_EV_ASSIGN);
    e.icao = 0x717C9CU;
    e.lpdu_type = 0x9F;
    r = mete(&t, &e);
    exige(t.count == 1U, "el CONFIRM funde: sigue habiendo UNA fila");
    exige(r != NULL && r->icao == 0x717C9CU, "y ya tiene su OACI");
    exige(r != NULL && (r->flags & HFDL_AC_R_FLIGHT) != 0U
          && strcmp(r->flight, "VLG729") == 0, "sin perder el vuelo que ya sabiamos");
    exige(r != NULL && (r->flags & HFDL_AC_R_POS) != 0U && r->lat_e4 == 510000,
          "ni la posicion");
    exige(r != NULL && r->count == 3U, "ni la cuenta de tramas");

    /* 4. Y a partir de ahora, por ID o por OACI, es la misma fila. */
    e = ev_base(9U, 90U);
    e.flags = 0U;
    (void)mete(&t, &e);
    exige(t.count == 1U, "despues de fundir, el ID sigue llevando a esa fila");
}

/* ============ el ID se reutiliza: no puede quedarse pegado ============ */
static void prueba_robo_de_id(void)
{
    hfdl_ac_rec_t mem[8];
    hfdl_ac_table_t t;
    hfdl_ac_event_t e;

    printf("\n  el ID 90 se lo dan a OTRO avion\n");
    hfdl_ac_init(&t, mem, sizeof mem);

    e = ev_base(9U, 90U);
    e.flags = (uint8_t)(HFDL_AC_EV_ICAO | HFDL_AC_EV_ASSIGN);
    e.icao = 0x717C9CU;
    (void)mete(&t, &e);

    /* La estacion le da el mismo numero a otro. El primero NO puede seguir
     * respondiendo al 90, o se comeria las tramas del segundo. */
    e = ev_base(9U, 90U);
    e.flags = (uint8_t)(HFDL_AC_EV_ICAO | HFDL_AC_EV_ASSIGN);
    e.icao = 0x06A0E5U;
    (void)mete(&t, &e);
    exige(t.count == 2U, "son dos aviones distintos, dos filas");
    {
        hfdl_ac_rec_t *v = busca_icao(&t, 0x717C9CU);
        hfdl_ac_rec_t *n = busca_icao(&t, 0x06A0E5U);
        exige(v != NULL && v->ac_id == HFDL_AC_ID_NONE, "al viejo le quitan el ID 90");
        exige(n != NULL && n->ac_id == 90U, "y lo tiene el nuevo");
    }

    /* Una trama sin OACI dirigida al 90 tiene que ir al nuevo. */
    e = ev_base(9U, 90U);
    e.flags = HFDL_AC_EV_POS;
    e.lat_e4 = 470000; e.lon_e4 = 91000;
    (void)mete(&t, &e);
    exige(t.count == 2U, "una trama al ID 90 no abre una tercera fila");
    {
        hfdl_ac_rec_t *n = busca_icao(&t, 0x06A0E5U);
        hfdl_ac_rec_t *v = busca_icao(&t, 0x717C9CU);
        exige(n != NULL && (n->flags & HFDL_AC_R_POS) != 0U, "la posicion va al nuevo");
        exige(v != NULL && (v->flags & HFDL_AC_R_POS) == 0U, "y NO al viejo");
    }
}

/* ============ el mismo numero en otra estacion es otro avion ============ */
static void prueba_dos_estaciones(void)
{
    hfdl_ac_rec_t mem[8];
    hfdl_ac_table_t t;
    hfdl_ac_event_t e;

    printf("\n  el ID 90 de Reykjavik y el 90 de Canarias\n");
    hfdl_ac_init(&t, mem, sizeof mem);

    e = ev_base(9U, 90U);  e.flags = HFDL_AC_EV_FLIGHT; memcpy(e.flight, "VLG729", 7);
    (void)mete(&t, &e);
    e = ev_base(4U, 90U);  e.flags = HFDL_AC_EV_FLIGHT; memcpy(e.flight, "EDW187", 7);
    (void)mete(&t, &e);
    exige(t.count == 2U, "mismo numero, estacion distinta: dos aviones");
    exige(strcmp(t.rec[0].flight, "EDW187") == 0, "y cada uno con su vuelo");
}

/* ===================== la tabla se llena ===================== */
static void prueba_llena(void)
{
    hfdl_ac_rec_t mem[4];
    hfdl_ac_table_t t;
    hfdl_ac_event_t e;
    uint32_t i;

    printf("\n  cuando no caben mas\n");
    hfdl_ac_init(&t, mem, sizeof mem);
    exige(t.cap == 4U, "cuatro registros de sitio, cuatro de capacidad");

    for (i = 0U; i < 6U; i++) {
        e = ev_base(9U, (uint8_t)(10U + i));
        e.flags = HFDL_AC_EV_ICAO;
        e.icao = 0x400000U + i;
        (void)mete(&t, &e);
    }
    exige(t.count == 4U, "no se pasa de la capacidad");
    exige(t.rec[0].icao == 0x400005U, "el ultimo en hablar, el primero de la lista");
    exige(busca_icao(&t, 0x400000U) == NULL, "y el mas viejo se ha caido");
}

/* ============ lo que decide si se busca el modelo ============ */
static void prueba_pide_modelo(void)
{
    hfdl_ac_rec_t mem[8];
    hfdl_ac_table_t t;
    hfdl_ac_event_t e;
    uint8_t pide;
    hfdl_ac_rec_t *r;

    printf("\n  cuando hay que ir a la base de datos\n");
    hfdl_ac_init(&t, mem, sizeof mem);

    /* Sin OACI no hay nada que buscar: la base va por direccion. */
    e = ev_base(9U, 90U);
    e.flags = HFDL_AC_EV_FLIGHT; memcpy(e.flight, "VLG729", 7);
    pide = 0xAAU;
    (void)hfdl_ac_update(&t, &e, &pide);
    exige(pide == 0U, "sin OACI no se pide el modelo");

    /* Con OACI y sin modelo, si. */
    e = ev_base(9U, 90U);
    e.flags = HFDL_AC_EV_ICAO; e.icao = 0x717C9CU;
    pide = 0U;
    r = hfdl_ac_update(&t, &e, &pide);
    exige(pide == 1U, "con OACI y sin modelo, se pide una vez");

    /* Y una vez puesto, no se vuelve a pedir: son 100.000 aviones en la
     * flash externa y buscar por biseccion en cada trama es tonteria. */
    if (r != NULL) { memcpy(r->model, "A20N", 5); }
    e = ev_base(9U, 90U);
    e.flags = HFDL_AC_EV_ICAO; e.icao = 0x717C9CU;
    pide = 0U;
    (void)hfdl_ac_update(&t, &e, &pide);
    exige(pide == 0U, "y ya no se vuelve a pedir");
}

/* ============ la trama que no sirve no toca nada ============ */
static void prueba_ignora(void)
{
    hfdl_ac_rec_t mem[8];
    hfdl_ac_table_t t;
    hfdl_ac_event_t e;

    printf("\n  una trama que no sirve\n");
    hfdl_ac_init(&t, mem, sizeof mem);
    e = ev_base(9U, 90U);
    e.flags = HFDL_AC_EV_ICAO; e.icao = 0x717C9CU;
    (void)mete(&t, &e);

    e = ev_base(9U, 91U);
    e.flags = HFDL_AC_EV_IGNORE;
    exige(mete(&t, &e) == NULL, "devuelve nada");
    exige(t.count == 1U, "y no abre fila");
    exige(t.rec[0].count == 1U, "ni cuenta la trama");
}

/* ============ la fila, en sus columnas ============ */
static void prueba_fila(void)
{
    hfdl_ac_rec_t mem[8];
    hfdl_ac_table_t t;
    hfdl_ac_event_t e;
    char l[HFDL_AC_ROW_LEN + 1U];

    printf("\n  la fila escrita\n");
    hfdl_ac_init(&t, mem, sizeof mem);

    e = ev_base(9U, 90U);
    e.flags = HFDL_AC_EV_FLIGHT; memcpy(e.flight, "VLG729", 7);
    (void)mete(&t, &e);
    hfdl_ac_format_row(&t.rec[0], l);
    printf("    [%s]\n", l);
    exige(strlen(l) == HFDL_AC_ROW_LEN, "mide lo que dice la cabecera");
    exige(strncmp(l, "ID 90", 5) == 0, "sin OACI pone ID y el numero");

    e = ev_base(9U, 90U);
    e.flags = (uint8_t)(HFDL_AC_EV_ICAO | HFDL_AC_EV_ASSIGN | HFDL_AC_EV_POS);
    e.icao = 0x717C9CU; e.lat_e4 = 510000; e.lon_e4 = -8000;
    (void)mete(&t, &e);
    memcpy(t.rec[0].model, "A20N Airbus A320neo", 20);
    hfdl_ac_format_row(&t.rec[0], l);
    printf("    [%s]\n", l);
    exige(strncmp(l, "717C9C", 6) == 0, "con OACI pone la direccion en hexadecimal");
    exige(strncmp(&l[7], "VLG729", 6) == 0, "el vuelo en su columna");
    exige(strncmp(&l[15], "A20N ", 5) == 0, "y solo el codigo de tipo, no el modelo entero");
}

/* ===== por que hfdl_modo.c filtra las tramas anonimas ===== */
static void prueba_anonimas(void)
{
    hfdl_ac_rec_t mem[8];
    hfdl_ac_table_t t;
    hfdl_ac_event_t e;
    uint32_t i;

    printf("\n  una trama sin OACI y sin ID: por que no puede llegar hasta aqui\n");
    hfdl_ac_init(&t, mem, sizeof mem);

    /* Cinco tramas de la estacion sin destinatario. Ni direccion ni
     * identificador: no hay nada con que emparejarlas. */
    for (i = 0U; i < 5U; i++) {
        e = ev_base(9U, HFDL_AC_ID_NONE);
        e.flags = HFDL_AC_EV_UPLINK;
        (void)mete(&t, &e);
    }
    exige(t.count == 5U, "abre UNA FILA POR TRAMA, y no es culpa suya");
    exige(t.rec[0].ac_id == HFDL_AC_ID_NONE, "todas dicen ID 255, que es 'no se sabe'");

    /*
     * Esto no es un fallo de hfdl_aircraft.c: busca por OACI o por
     * (estacion, identificador) y aqui no hay ninguno de los dos, asi que
     * abrir fila nueva es lo unico que puede hacer. Lo que estaba mal era
     * darselas. Por eso apunta_trama() las descarta antes, y esta prueba
     * esta aqui para que se vea POR QUE existe ese filtro: si alguien lo
     * quita, la tabla se llena de "ID 255" y los aviones de verdad se
     * caen por abajo.
     *
     * Lo vio el usuario en la pantalla antes que ningun banco: "siguen
     * saliendo los mismos datos", con ID 255 dos veces en la foto.
     */
    hfdl_ac_init(&t, mem, sizeof mem);
    for (i = 0U; i < 5U; i++) {
        e = ev_base(9U, HFDL_AC_ID_NONE);
        e.flags = HFDL_AC_EV_UPLINK;
        if ((e.flags & HFDL_AC_EV_ICAO) == 0U && e.ac_id == HFDL_AC_ID_NONE) {
            continue;                /* lo que hace apunta_trama() */
        }
        (void)mete(&t, &e);
    }
    exige(t.count == 0U, "con el filtro de apunta_trama(), la tabla se queda limpia");
}

int main(void)
{
    printf("\n  TABLA DE AVIONES HFDL (User/hfdl_aircraft.c)\n");
    prueba_fusion();
    prueba_robo_de_id();
    prueba_dos_estaciones();
    prueba_llena();
    prueba_pide_modelo();
    prueba_ignora();
    prueba_fila();
    prueba_anonimas();
    printf("\n%s (%d fallos)\n", fallos ? "HAY FALLOS" : "TODO CORRECTO", fallos);
    return fallos ? 1 : 0;
}
