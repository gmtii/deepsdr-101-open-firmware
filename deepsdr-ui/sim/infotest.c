/*
 * Anchos de la pantalla de Informacion.
 *
 * POR QUE EXISTE
 * --------------
 * La etapa 33 le metio cinco celdas nuevas -el chip, el sistema de
 * ficheros, el mapa de la zona alta, la firma de update4 y el almacen de
 * imagenes- y todas enseñan textos que main.c CONSTRUYE al vuelo. Una
 * celda cuyo valor se sale no avisa de nada: se pinta encima de la de al
 * lado, o se corta, y parece que el numero es otro.
 *
 * El ancho se midio a mano al añadirlas y cabian todas. Eso no sirve de
 * nada dentro de un mes: los peores casos se declaran aqui, como los de
 * los ajustes en peores.h, y se miden en cada "make comprueba". La razon
 * de declararlos a mano es la misma que alli - el firmware no los tiene
 * escritos en ningun sitio porque los arma al vuelo-.
 *
 * Los textos fijos (los veredictos del almacen) SI estan en el firmware,
 * en los veredictos del almacen de imagenes (que ya no existe), y de esos
 * se compila el fichero de verdad en
 * vez de copiarlos: una copia a mano de algo que existe es una copia que
 * un dia deja de coincidir.
 */
#include "gfx2.h"
#include "palette.h"
#include "ui_grid.h"
#include "volmsg_tabla.h"
#include "font_ui_14b.h"
#include "font_ui_18b.h"
#include <stdio.h>

const palette_t *g_pal = &k_pal_oscura;
static int fallos = 0;

/* El hueco util de una celda: su ancho menos el margen que deja
 * gfx2_text_in() a los dos lados. */
#define HUECO (UIG_CELL_W - 8)

static void mide(const char *que, const char *txt, const gfx2_font_t *f)
{
    int w = gfx2_text_w(txt, f);
    int ok = (w <= HUECO);
    printf("  %-16s %4d px en %4d  %-26s %s\n", que, w, HUECO, txt,
           ok ? "OK" : "SE SALE");
    if (!ok) { fallos++; }
}

/*
 * El peor caso de cada celda: el texto mas largo que puede llegar a
 * enseñar. Los de la flash salen de los formatos de info_valor():
 *   chip      medido y, si no cuadra, tambien el declarado
 *   ficheros  un tamaño en MB
 *   zona      ocho caracteres de mapa y dos cuentas de hasta 4 cifras
 *   firma     una direccion de 8 digitos hexadecimales
 *   imagenes  la cuenta y lo libre, o el veredicto (que se mide aparte)
 */
static const struct { const char *nombre; const char *peor; } k_peor[] = {
    { "Versión",        "V19.99"                },
    /* Del tercer toque en adelante la fila de la version lleva pegada la
     * cuenta atras para destapar la pagina 2 (ver INFO_TOQUES en main.c).
     * Es el texto mas largo que puede enseñar esa celda. */
    { "Versión",        "V19.99 (3)"            },
    { "Micro",          "GD32F450VET6"          },
    { "Flash",          "255,9 / 256 kB"        },
    /* El desvan, los 64 kB que hay por encima de la firma que mira el
     * gestor de arranque. Ver INFO_FLASH2_TOPE en main.c y la seccion
     * .arriba del enlazador. */
    { "Flash alta",     "63,9 / 64 kB"          },
    { "SRAM",           "191,9 / 192 kB"        },
    { "TCM",            "63,9 / 64 kB"          },
    { "Temp. del chip", "-40 °C"                },
    { "Batería",        "4,20 V"                },
    { "Reloj",          "4294967295 perdidos"   },
    /* Las cuatro cosas que puede decir la fila del RTC. */
    { "RTC",            "cristal KO"            },
    { "RTC",            "en hora: nunca"        },
    { "RTC",            "en hora hace 59 m"     },
    { "RTC",            "en hora hace 999 d"    },
    { "Flash externa",  "16,0 MB ! dice 0,0 MB" },
    /* LAS DOS FILAS DEL BUS DE LA PANTALLA - 20/10/2026.
     *
     * No estaban declaradas aqui, y una de ellas acaba de cambiar de
     * texto: "Bus pantalla" ya no enseña solo un numero, enseña "auto" o
     * el valor a mano seguido de los nanosegundos. El peor caso que puede
     * armar el automatico son doce ciclos (60 ns) y el de a mano son los
     * 15 de la tabla; se declara 1000 ns, que no se alcanza con ningun
     * ASET razonable, para que esto siga valiendo si manana el gestor de
     * arranque deja otro. Ver INFO_XBUS en main.c. */
    { "Bus pantalla",   "auto \u2192 60 ns"      },
    { "Bus pantalla",   "15 \u2192 1000 ns"      },
    { "Pendiente del bus", "2, 50 MHz"          },
    /* Los dos textos de la fila del numero de serie: el identificador
     * entero -16 cifras, que es lo mas largo que puede salir- y lo que se
     * dice cuando el chip no conoce el comando 0x4B. */
    { "Nº de serie",    "E66038B713AF5C21"      },
    { "Nº de serie",    "el chip no lo dice"    },
    /* El reparto por etapas: el peor caso es que las cuatro se vayan a dos
     * cifras, que es cuando la celda mas mide. */
    { "Reparto del audio", "rf99 ex99 au99 ag99" },
    { "Sist. ficheros", "16,0 MB"               },
    /* Los ocho veredictos de la geometria del volumen. */
    { "Formato",        "se puede escribir"     },
    { "Formato",        "no es un FAT12"        },
    { "Formato",        "sector no es de 512"   },
    { "Formato",        "cluster que no vale"   },
    { "Formato",        "no hay 2 copias FAT"   },
    { "Formato",        "FAT demasiado grande"  },
    { "Formato",        "la FAT se queda corta" },
    { "Formato",        "datos sin alinear"     },
    /* Y la fila del DFU, que desde el 02/10/2026 esta en la pagina de
     * siempre y no en la oculta. */
    { "Modo DFU",       "Reiniciar en DFU"      },
    { "Modo DFU",       "Tocar otra vez"        },
    /* La celda de grabar trozos: el estado normal, el tope, y el peor de
     * los motivos de fallo (ver spi_flash_anade_porque_txt). */
    { "Grabar trozo",   "tocar: primer trozo"   },
    { "Grabar trozo",   "trozo 200, 800 kB (199)" },
    { "Grabar trozo",   "directorio lleno"      },
    { "Grabar trozo",   "el fichero es de otro" },
    { "Imágenes",       "sin montar"            },
    /*
     * CUADRATURA I/Q - 06/10/2026, fila nueva. Se mide AQUI Y AHORA y no
     * cuando el usuario vea el rotulo cortado en la pantalla, que es como se
     * pillo lo de "Formatear disco" tres filas mas abajo. Los cuatro
     * textos que puede poner, en los dos idiomas: la zona con su fVCO -el
     * peor caso es zona 10 con el VCO a 907 MHz-, el registro de fase por
     * encima de 4,8 MHz, y el generador del GD32 por debajo de 300 kHz.
     */
    { "Cuadratura I/Q", "z. 10 - 907 MHz"       },
    { "Cuadratura I/Q", "registro de fase"      },
    { "Cuadratura I/Q", "GD32, PA6/PA7"         },
    /* Y lo que pone por debajo de 300 kHz desde el 09/10/2026: la
     * frecuencia que sale de verdad del generador del GD32 y su desvio
     * contra la pedida. El peor caso es el tope de su rango con el
     * escalon mas grande. */
    { "Cuadratura I/Q", "299999 Hz (-1785)"     },
    { "Cuadratura I/Q", "GD32, parado"          },
    { "I/Q quadrature", "GD32, stopped"         },
    { "I/Q quadrature", "phase register"        },
    { "I/Q quadrature", "GD32, PA6/PA7"         },
    /*
     * MULT. ZONA 7 - 09/10/2026, fila nueva. Los textos
     * que puede poner: el multiplicador con su fVCO cuando el dial esta en
     * la zona 7 -el peor caso es 189, fVCO de 907 MHz-, y el multiplicador
     * solo cuando no lo esta. Y el pie, que lleva el rango.
     */
    { "Mult. zona 7",   "189 - 907 MHz"         },
    { "Mult. zona 7",   "189 - sin usar"        },
    { "Mult. zona 7",   "168..189 \u00b7 un toque" },
    { "Zone-7 mult.",   "189 - unused"          },
    { "Zone-7 mult.",   "168..189 \u00b7 one tap" },
    /*
     * ESPEJO I/Q - 07/10/2026, fila nueva, por el video del encoder: "todas
     * las señales al girar el encoder disminuyendo la frecuencia avanzan de
     * izquierda a derecha pero hay una señal que lo hace de derecha a
     * izquierda". Los textos que puede poner: mientras mide, y los dos de
     * despues -encendido y apagado-, con el peor caso de ancho, que es
     * rechazo de tres cifras y fase negativa con decimal.
     */
    { "Espejo I/Q",     "midiendo..."           },
    { "Espejo I/Q",     "112/112 dB -12,3\u00b0"  },
    { "Espejo I/Q",     "off 112/-- dB -12,3\u00b0" },
    { "Espejo I/Q",     "off 112/112 dB -12,3\u00b0" },
    { "Espejo I/Q",     "A/B: sin corregir"     },
    { "Espejo I/Q",     "A/B: corrigiendo"      },
    { "Espejo I/Q",     "A/B 112\u2192112 dB"     },
    { "I/Q mirror",     "measuring..."          },
    { "I/Q mirror",     "A/B: uncorrected"      },
    { "I/Q mirror",     "A/B: corrected"        },
    { "I/Q mirror",     "off 112/112 dB -12,3\u00b0" },
    /*
     * ARMONICOS - 07/10/2026, fila nueva. Los textos que puede poner: sin
     * LO, ninguno, y el peor caso de ancho, que es el armonico mas alto con
     * el nombre de banda mas largo de las 49 -"49 m difusión"- y mas de un
     * encontrado.
     */
    { "Armónicos",      "ninguno"               },
    { "Armónicos",      "sin LO"                },
    { "Armónicos",      "x7 49 m difusión -48k" },
    { "Armónicos",      "x7 49 m difusión +3"   },
    { "Harmonics",      "none"                  },
    { "Harmonics",      "x7 49 m broadcast -48k" },
    { "Harmonics",      "x7 49 m broadcast +3"  },
    /*
     * FORMATEAR DISCO - 06/10/2026. La primera version decia
     * "4096 B/cluster, 1788 clusters" y en la radio salio cortada:
     * "...1788 clus". No lo pillo este banco porque la fila era nueva y a
     * nadie se le ocurrio venir a medirla; lo pillo el usuario mirando la
     * pantalla. Aqui estan los cuatro textos que puede poner.
     */
    /*
     * CARGAR DATOS. El estado normal despues de una carga correcta es
     * "cargada · N frec.", no "no encuentro el .BIN": ver el comentario de
     * INFO_XAVIONES en main.c. El peor caso es el numero mas largo que
     * puede salir del fichero de emisoras.
     */
    { "Cargar datos",   "cargada · 99999 frec." },
    { "Load data",      "loaded · 99999 freq." },
    { "Formatear disco", "4 kB · 1788 clusters" },
    { "Formatear disco", "BORRA TODO: confirma" },
    { "Formatear disco", "hecho: lee y graba" },
    { "Formatear disco", "la prueba no cuadra" },
    { "Formatear disco", "escrito, no lo reconozco" },
    { "Formatear disco", "lo leo, no lo grabo" },
    { "Formatear disco", "el disco es pequeño" },
    { "Formatear disco", "no se ve la flash" },
    { "Formatear disco", "no se pudo escribir" },
    { "Format disk",     "ERASES ALL: tap again" },
    { "Format disk",     "done: reads and writes" },
    { "Format disk",     "test file does not match" },
    { "Format disk",     "written, not recognised" },
    { "Format disk",     "reads but cannot write" },
};

int main(void)
{
    unsigned i;

    printf("anchos de la pantalla de Informacion (celda de %d px)\n\n", UIG_CELL_W);

    printf("  los nombres:\n");
    for (i = 0; i < sizeof k_peor / sizeof k_peor[0]; i++) {
        mide("", k_peor[i].nombre, &font_ui_18b);
    }

    printf("\n  los valores, en su peor caso:\n");
    for (i = 0; i < sizeof k_peor / sizeof k_peor[0]; i++) {
        mide(k_peor[i].nombre, k_peor[i].peor, &font_ui_14b);
    }

    /*
     * Y los seis motivos por los que el volcado puede no arrancar, leidos
     * de spi_flash.c recorriendo los codigos: uno nuevo se mide solo sin
     * que nadie se acuerde de venir a añadirlo aqui.
     */
    printf("\n  los motivos del volcado (generados de spi_flash.c):\n");
    {
        unsigned v;
        for (v = 0U; v < VOLMSG_N; v++) {
            mide("Volcar", k_volmsg[v], &font_ui_14b);
        }
    }

    printf("\n%s (%d fallos)\n", fallos ? "HAY FALLOS" : "TODO CORRECTO", fallos);
    return fallos ? 1 : 0;
}
