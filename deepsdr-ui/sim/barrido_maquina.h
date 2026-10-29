/* GENERADO por tools/barrido_dump.py desde User/main.c. No se
 * edita: lo que valga se cambia alli. */

#include "barrido_consts.h"

static float scan_lee(void)
{
    demod_mode_t md = demod_am_get_mode();

    if (md == DEMOD_MODE_WFM) { return demod_wfm_get_sql_db(); }
    if (md == DEMOD_MODE_AM || md == DEMOD_MODE_NFM) {
        return demod_am_get_squelch_level_db();
    }
    return demod_am_get_nivel_db();
}

static void scan_mira(void)
{
    float v = scan_lee();

    if (demod_am_get_mode() == DEMOD_MODE_WFM) {
        if (v < s_scan_pico) { s_scan_pico = v; }   /* el ruido MAS bajo */
    } else {
        if (v > s_scan_pico)  { s_scan_pico  = v; } /* el nivel MAS alto */
        if (v < s_scan_valle) { s_scan_valle = v; } /* y el MAS callado */
    }
}

static uint8_t mem_hay_senal(uint8_t c, uint8_t sube_ok)
{
    float sube;

    if (demod_am_get_mode() == DEMOD_MODE_WFM) {
        return (uint8_t)(s_scan_pico < SCAN_WFM_RUIDO_DB);
    }
    if (c >= BANCO_MEM_N) { return 0U; }

    /* Baja de golpe y sube a cuentagotas, los dos con el mismo minimo:
     * el suelo no puede quedar por encima de lo mas callado que se acaba
     * de oir aqui. */
    sube = sube_ok ? (s_scan_piso[c] + SCAN_PISO_SUBE_DB) : s_scan_piso[c];
    s_scan_piso[c] = (s_scan_valle < sube) ? s_scan_valle : sube;

    return (uint8_t)(s_scan_pico >= (s_scan_piso[c] + SCAN_MARGEN_DB));
}

static void scan_ventana(uint32_t espera_ms)
{
    s_scan_ms    = g_msticks;
    s_scan_espera = espera_ms;
    s_scan_pico  = (demod_am_get_mode() == DEMOD_MODE_WFM) ? 9999.0f : -9999.0f;
    s_scan_valle = 9999.0f;
}

static void mem_scan_para(void)
{
    if (!s_scan_on) { return; }
    s_scan_on      = 0U;
    s_scan_para_en = 0U;
    /* Y se apaga la medida del nivel, que solo existe para esto: ver
     * demod_am_set_nivel_medir(), y por que importa apagarla. */
    demod_am_set_nivel_medir(0U);
    mem_boton_repinta();
    mem_rejilla_repinta();
}

static void mem_scan_salta(void)
{
    uint8_t sig = banco_siguiente(s_scan_banco, s_scan_mem);
    uint8_t rehace;

    if (sig == 0xFFU) { mem_scan_para(); return; }
    s_scan_mem     = sig;
    s_scan_para_en = 0U;
    /* Primero se pone la memoria y DESPUES se abre la ventana, que asi
     * empieza a contar cuando la cadena ya ha cambiado y no antes. Ver
     * scan_ventana(), que ademas necesita saber el modo que ha quedado. */
    rehace = mem_recupera(s_scan_banco, sig);
    /* Lo que tarde en poder contestar depende de si la cadena se ha
     * rehecho, y de si lo que ha entrado es FM ancha: ver SCAN_MIRA_MS. */
    scan_ventana(!rehace ? SCAN_ESPERA_MS
                 : ((demod_am_get_mode() == DEMOD_MODE_WFM)
                    ? SCAN_ESPERA_WFM_MS : SCAN_ESPERA_MODO_MS));
    /* El numero del boton cambia en CADA salto, asi que esto si va por
     * salto. Es la ventana del boton y nada mas -72x32 px-, o sea del
     * orden de 2.400 accesos al bus frente a los 89.771 de la cabecera
     * entera. Ver ui_top_draw_mem(). */
    mem_boton_repinta();
    mem_rejilla_repinta();
}

static void mem_scan_conmuta(uint8_t bk)
{
    if (s_scan_on && s_scan_banco == bk) { mem_scan_para(); return; }
    if (banco_usadas(bk) == 0U) {
        /* Un banco vacio no se barre, y se dice con el pip de "no ha
         * podido ser" en vez de no hacer nada: un boton que no responde se
         * vive como que no ha entrado el toque. */
        bip_error();
        return;
    }
    s_scan_on      = 1U;
    /*
     * Y SE ENCIENDE LA MEDIDA DEL NIVEL, que fuera del barrido no se mide.
     *
     * Son 256 multiplicaciones y un logaritmo por bloque, y dejarlas
     * corriendo siempre le quitaba el margen que le queda a STANAG cuando
     * esta enganchado a una señal: la radio se saturaba y dejaba de
     * responder. Lo vio el usuario. Ver demod_am_set_nivel_medir().
     */
    demod_am_set_nivel_medir(1U);
    s_scan_banco   = bk;
    s_scan_mem     = 0xFFU;
    /* Cada barrido aprende sus propios suelos: los de la vez anterior eran
     * de otra hora y otro automatico de ganancia. 9999 = sin visitar, que
     * en la primera visita deja el suelo en el valle de esa visita. */
    {
        uint8_t i;

        for (i = 0U; i < BANCO_MEM_N; i++) { s_scan_piso[i] = 9999.0f; }
    }
    mem_scan_salta();
    /*
     * Y SE VUELVE A LA VENTANA PRINCIPAL - 26/10/2026, pedido por el
     * usuario. Mientras busca, lo que se quiere delante es el espectro y
     * la cascada, no la rejilla del banco: ver el salto y lo que cuesta
     * en mem_recupera(). Se cierra DESPUES del primer salto para no
     * repintar la rejilla que esta a punto de desaparecer.
     *
     * Por donde va lo dice el boton MEM de la cabecera, que pone el
     * numero de la memoria; y tocarlo para el barrido. Ver
     * ui_top_state_t.mem_scan.
     */
    menu_screen_close();
}

static void mem_scan_poll(void)
{
    uint8_t hay;

    if (!s_scan_on) { return; }

    /*
     * Y SE PARA SOLO EN LOS MODOS DE PANEL ENTERO - 29/10/2026, tarde,
     * revision a fondo de HFDL.
     *
     * No habia ni una llamada a mem_scan_para() en ningun camino de
     * cambio de modo: los unicos frenos eran tocar MEM, reconmutar el
     * mismo banco y quedarse el banco sin memorias. Asi que arrancabas el
     * barrido, elegias HFDL en la rejilla de modos y el barrido seguia
     * vivo, saltando de memoria cada SCAN_ESPERA_MS + SCAN_MIRA_MS. El
     * dial se iba de la frecuencia DOS VECES Y MEDIA POR SEGUNDO, y una
     * rafaga de HFDL dura un segundo. Lo mismo al reves: desde HFDL se
     * puede abrir el banco y arrancarlo desde dentro.
     *
     * La guarda va AQUI y no en los sitios que cambian de modo, y es
     * deliberado: en el latido no hay forma de rodearla. Poniendola en
     * los cambios de modo habria que acordarse en cada camino nuevo, que
     * es exactamente como se escapo esto.
     *
     * Barrer no tiene sentido en estos ocho: todos viven en un canal
     * fijo, el panel ocupa la pantalla entera y el nivel que mira el
     * barrido -demod_am_set_nivel_medir()- cuesta 256 multiplicaciones y
     * un logaritmo por bloque, que es lo que ya ahogo a la radio en
     * STANAG.
     */
    if (modo_panel_entero()) { mem_scan_para(); return; }
    /* Un rato de gracia para que el silenciador decida: la cadena acaba de
     * cambiar de frecuencia y de modo, y el silencio de entrada de FM ancha
     * son doscientos bloques. Preguntar antes es preguntar al transitorio. */
    {
        uint32_t t = (uint32_t)(g_msticks - s_scan_ms);

        if (t < s_scan_espera) { return; }

        /* El cuarto de segundo de mirar: se apunta lo mejor que se vea, en
         * cada vuelta del bucle. Una voz tiene pausas, asi que preguntar
         * una sola vez al final se perderia media emisora. */
        scan_mira();
        if (t < (uint32_t)(s_scan_espera + SCAN_MIRA_MS)) { return; }
    }

    /* Vigilando, el suelo no puede subir: ver mem_hay_senal(). */
    hay = mem_hay_senal(s_scan_mem, (uint8_t)(s_scan_para_en ? 0U : 1U));

    if (hay) {
        if (!s_scan_para_en) {
            /* Recien encontrada: se queda y lo dice. El rotulo de la
             * rejilla pasa de "barriendo" a "escuchando", y por eso se
             * repinta aqui: la rejilla solo se repintaba en los saltos, y
             * quedarse ya no es un salto. */
            s_scan_para_en = 1U;
            bip_listo();
            mem_rejilla_repinta();
        }
        s_scan_oida_ms = g_msticks;
        /* Y se sigue vigilando el mismo canal: otra ventana aqui mismo,
         * sin espera, que la cadena no se ha movido. */
        scan_ventana(0U);
        return;
    }

    if (s_scan_para_en) {
        /*
         * Callado, pero no por eso libre todavia: un hueco entre dos
         * frases dura menos que SCAN_VUELVE_MS, y lo que se mide es
         * cuanto lleva callado DE SEGUIDO. Mientras no llegue, se sigue
         * vigilando aqui.
         */
        if ((uint32_t)(g_msticks - s_scan_oida_ms) < SCAN_VUELVE_MS) {
            scan_ventana(0U);
            return;
        }
        /* Ya esta libre: el barrido sigue por donde iba. Sin pip, que
         * esto no es un hallazgo. */
    }
    /* El suelo de esta memoria ya lo ha puesto al dia mem_hay_senal()
     * antes de decidir: ver alli por que va en ese orden. */
    mem_scan_salta();
}

