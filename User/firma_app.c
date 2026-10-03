/*
 * firma_app.c - los ocho bytes que el gestor de arranque mira antes de
 * saltar a la app.  27/09/2026.
 *
 * El gestor de fabrica ("Bootloader v4.0.3 ... by BH5HNU") hace esto en
 * 0x0800A7CA, en CADA arranque, no solo al actualizar:
 *
 *     memcmp((void *)0x0800be2c, (void *)0x08060000, 8)
 *
 * Si los ocho bytes no coinciden escribe "Running APP---" por el puerto
 * serie y se queda en un bucle infinito.  Si coinciden escribe
 * "Running APP..." y salta.  No comprueba nada mas de la imagen: ni
 * checksum, ni CRC, ni longitud.
 *
 * Hasta hoy esos ocho bytes los pegaba la receta de update4.bin al final
 * del fichero, porque el binario era mas pequeno que 0x40000 y el relleno
 * de ceros los dejaba justo en su sitio.  Ahora la imagen pasa de 0x40000
 * -ver el comentario de MEMORY en GD32F450VE_FLASH.ld- asi que los ocho
 * bytes tienen que ir DENTRO de ella.  De eso va este fichero.
 *
 * NO HAY NI UNA REFERENCIA a esta variable en todo el firmware, y eso es
 * correcto: nadie la lee mas que el gestor, y el la lee por direccion
 * absoluta.  Lo que la mantiene viva es el KEEP() del enlazador; el
 * "used" de aqui es el cinturon por si algun dia se compila sin
 * -fdata-sections y el compilador decide que una constante que nadie usa
 * no hace falta.
 *
 * SI ESTO DESAPARECE, la radio se queda colgada en "Running APP---" y hay
 * que recuperarla metiendo un update4.bin bueno por el USB.  Se recupera
 * -el gestor busca el pendrive antes de llegar a esta comprobacion- pero
 * el susto es gratuito.  Por eso avisos.mk comprueba en cada compilacion
 * que los ocho bytes han acabado en el desplazamiento 0x40000 del .bin.
 */

#include <stdint.h>

const uint8_t g_firma_update4[8]
    __attribute__((used, section(".firma"))) = {
    0x8FU, 0x25U, 0xC8U, 0x65U, 0x59U, 0x9CU, 0x55U, 0x31U
};
