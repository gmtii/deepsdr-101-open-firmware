/*
 * Sustitutos de las llamadas al sistema que arrastra newlib.
 *
 * El enlazador avisaba cuatro veces de "_close/_lseek/_read/_write is not
 * implemented and will always fail". No es un fallo: newlib-nano arrastra su
 * maquinaria de stdio a traves de exit()/atexit(), y los sustitutos de
 * nosys.specs que la satisfacen se limitan a devolver error. Este firmware no
 * usa stdio - no hay printf, ni malloc, ni ficheros - asi que esas funciones
 * no se llaman nunca.
 *
 * Definirlas aqui, escuetas, quita los cuatro avisos sin cambiar el
 * comportamiento: siguen devolviendo error, pero ya no son los sustitutos
 * "sin implementar" de la libreria.
 */

#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>

int _close(int fd)                          { (void)fd; errno = EBADF; return -1; }
int _read(int fd, char *buf, int len)       { (void)fd; (void)buf; (void)len; errno = EBADF; return -1; }
int _write(int fd, const char *buf, int len){ (void)fd; (void)buf; (void)len; errno = EBADF; return -1; }
int _lseek(int fd, int off, int whence)     { (void)fd; (void)off; (void)whence; errno = EBADF; return -1; }
int _fstat(int fd, struct stat *st)         { (void)fd; if (st) { st->st_mode = S_IFCHR; } return 0; }
int _isatty(int fd)                         { (void)fd; return 1; }
