/*
 * FAT32 de solo lectura. Ver fat32.h para por que aqui no hay cache de FAT
 * y para lo que hace y lo que no.
 */
#include "fat32.h"

#define SEC  FAT32_TAM_SECTOR

static uint16_t le16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int lee1(const bloques_t *d, uint32_t sector, uint8_t *dst)
{
    return d->lee(d->ctx, sector, 1U, dst);
}

/*
 * ES UN ARRANQUE DE FAT32, O NO LO ES.
 *
 * No vale mirar la firma 0x55AA y creersela: ese par de bytes esta tambien
 * en el MBR, en un FAT12 y en medio disco con basura. Lo que distingue a un
 * FAT32 es que el numero de sectores por FAT de 16 bits valga CERO y el de
 * 32 bits no, mas una geometria que cuadre.
 */
static int mira_bpb(const uint8_t *s, uint16_t *tam_sec, uint8_t *spc,
                    uint16_t *reserv, uint8_t *nfats, uint32_t *spf,
                    uint32_t *tot, uint32_t *raiz)
{
    uint16_t spf16;

    if ((s[510] != 0x55U) || (s[511] != 0xAAU)) { return FAT32_E_NO_FAT32; }

    *tam_sec = le16(&s[11]);
    *spc     = s[13];
    *reserv  = le16(&s[14]);
    *nfats   = s[16];
    spf16    = le16(&s[22]);
    *tot     = le32(&s[32]);
    *spf     = le32(&s[36]);
    *raiz    = le32(&s[44]);

    /* FAT12/16 ponen el de 16 bits; FAT32 pone el de 32 y el otro a cero. */
    if ((spf16 != 0U) || (*spf == 0U)) { return FAT32_E_NO_FAT32; }
    if (le16(&s[17]) != 0U)            { return FAT32_E_NO_FAT32; } /* raiz fija */
    return FAT32_OK;
}

int fat32_monta(fat32_t *fs, const bloques_t *dev)
{
    uint8_t *s = fs->buf;
    uint16_t tam_sec, reserv;
    uint8_t  spc, nfats;
    uint32_t spf, tot, raiz, vol0 = 0UL;
    int      r;

    fs->montado = 0U;
    fs->dev = dev;

    if (dev->tam_sector != SEC) { return FAT32_E_SECTOR; }
    if (lee1(dev, 0UL, s) != 0) { return FAT32_E_LEER; }

    r = mira_bpb(s, &tam_sec, &spc, &reserv, &nfats, &spf, &tot, &raiz);
    if (r != FAT32_OK) {
        /*
         * NO ERA EL ARRANQUE: SERA EL MBR.
         *
         * Un pincho de fabrica viene particionado, asi que en el sector 0
         * hay tabla de particiones y el volumen empieza mas adelante -2048
         * es lo normal-. Los formateados a lo bruto (mkfs sobre el disco
         * entero) no la llevan y el arranque esta en el 0. Hay que admitir
         * los dos: el primero es lo que compras, el segundo es lo que sale
         * si alguien lo formatea a mano.
         *
         * Se miran las cuatro entradas y se coge la PRIMERA que sea FAT32,
         * por tipo (0x0B o 0x0C) o porque su primer sector lo parezca. No
         * se exige el tipo: hay utilidades que ponen 0x0C en un FAT16 y al
         * reves, y quien manda es el sector de arranque.
         */
        uint8_t i;

        if ((s[510] != 0x55U) || (s[511] != 0xAAU)) { return r; }
        for (i = 0U; i < 4U; i++) {
            const uint8_t *e = &s[446U + (16U * i)];
            uint32_t ini = le32(&e[8]);
            uint32_t n   = le32(&e[12]);
            uint8_t  t   = e[4];
            uint8_t *b = fs->fat_cache;   /* todavia no hay cache que perder */

            if ((ini == 0UL) || (n == 0UL))   { continue; }
            if (ini >= dev->sectores)         { continue; }
            if ((t != 0x0BU) && (t != 0x0CU) && (t != 0x07U)) { continue; }
            if (lee1(dev, ini, b) != 0)       { return FAT32_E_LEER; }
            if (mira_bpb(b, &tam_sec, &spc, &reserv, &nfats, &spf, &tot,
                         &raiz) != FAT32_OK)  { continue; }
            vol0 = ini;
            break;
        }
        if (vol0 == 0UL) { return FAT32_E_NO_FAT32; }
    }

    /*
     * Y AHORA QUE LOS NUMEROS CUADREN, ANTES DE FIARSE DE NINGUNO.
     *
     * Todo lo que viene de aqui sale de un medio que ha enchufado el
     * usuario y que puede traer cualquier cosa: un volumen a medio
     * formatear, uno de otro sistema, o basura. Un `spc` a cero divide por
     * cero mas abajo, y en esta radio una excepcion CUELGA -no hay perro
     * guardian y el HardFault_Handler es un bucle infinito-. O sea que esto
     * no es higiene: es lo que separa "el pincho no vale" de "la radio se
     * ha quedado tiesa".
     */
    if (tam_sec != SEC)                      { return FAT32_E_SECTOR; }
    if ((spc == 0U) || ((spc & (spc - 1U)) != 0U)) { return FAT32_E_GEOMETRIA; }
    if ((nfats == 0U) || (nfats > 2U))       { return FAT32_E_GEOMETRIA; }
    if (reserv == 0U)                        { return FAT32_E_GEOMETRIA; }
    if (raiz < 2UL)                          { return FAT32_E_GEOMETRIA; }

    {
        uint32_t meta = (uint32_t)reserv + (spf * (uint32_t)nfats);

        if (tot <= meta)                     { return FAT32_E_GEOMETRIA; }
        fs->clusters = (tot - meta) / (uint32_t)spc;
        if (fs->clusters < 2UL)              { return FAT32_E_GEOMETRIA; }
        /* Un FAT32 de verdad tiene mas de 65524 clusters; por debajo de eso
         * la norma dice que es FAT16 aunque el arranque ponga otra cosa.
         * No se rechaza -hay utilidades que los hacen mas pequeños- pero se
         * exige que la FAT alcance para los clusters que dice tener. */
        if (((uint64_t)spf * (uint64_t)SEC) < ((uint64_t)(fs->clusters + 2UL) * 4ULL)) {
            return FAT32_E_GEOMETRIA;
        }
        if (raiz >= (fs->clusters + 2UL))    { return FAT32_E_GEOMETRIA; }

        fs->vol0   = vol0;
        fs->fat0   = vol0 + (uint32_t)reserv;
        fs->datos0 = vol0 + meta;
        if ((fs->datos0 + ((uint64_t)fs->clusters * spc)) > dev->sectores) {
            return FAT32_E_GEOMETRIA;
        }
    }

    fs->sec_por_fat     = spf;
    fs->sec_por_cluster = spc;
    fs->n_fats          = nfats;
    fs->raiz_cluster    = raiz;
    fs->fat_cache_sec   = 0xFFFFFFFFUL;
    fs->montado         = 1U;
    return FAT32_OK;
}

/* La entrada de la FAT del cluster `cl`, con el sector cacheado. */
static int fat_sig(fat32_t *fs, uint32_t cl, uint32_t *sig)
{
    uint32_t off = cl * 4UL;
    uint32_t sec = fs->fat0 + (off / SEC);

    if (cl < 2UL)                       { return FAT32_E_CADENA; }
    if (cl >= (fs->clusters + 2UL))     { return FAT32_E_CADENA; }

    if (sec != fs->fat_cache_sec) {
        if (lee1(fs->dev, sec, fs->fat_cache) != 0) { return FAT32_E_LEER; }
        fs->fat_cache_sec = sec;
    }
    /* Los 4 bits de arriba estan reservados y hay volumenes que los traen
     * sucios: la norma manda ignorarlos. */
    *sig = le32(&fs->fat_cache[off % SEC]) & 0x0FFFFFFFUL;
    return FAT32_OK;
}

static uint32_t cluster_a_sector(const fat32_t *fs, uint32_t cl)
{
    return fs->datos0 + ((cl - 2UL) * (uint32_t)fs->sec_por_cluster);
}

static int fin_de_cadena(uint32_t cl)
{
    return (cl >= 0x0FFFFFF8UL) ? 1 : 0;
}

/* Pasa el 8.3 crudo del directorio a "NOMBRE~1.TXT". */
static void nombre83(const uint8_t *d, char *out)
{
    uint8_t i, j = 0U;

    for (i = 0U; i < 8U; i++) {
        if (d[i] == ' ') { break; }
        out[j++] = (char)d[i];
    }
    if (d[8] != ' ') {
        out[j++] = '.';
        for (i = 8U; i < 11U; i++) {
            if (d[i] == ' ') { break; }
            out[j++] = (char)d[i];
        }
    }
    out[j] = '\0';
}

int fat32_entrada(fat32_t *fs, uint32_t cl, uint32_t i, fat32_entrada_t *e)
{
    uint8_t *s = fs->buf;
    uint32_t vistas = 0UL;

    if (fs->montado == 0U) { return FAT32_E_LEER; }
    if (cl < 2UL)          { cl = fs->raiz_cluster; }

    while (fin_de_cadena(cl) == 0) {
        uint32_t base = cluster_a_sector(fs, cl);
        uint8_t  k;

        for (k = 0U; k < fs->sec_por_cluster; k++) {
            uint16_t o;

            if (lee1(fs->dev, base + k, s) != 0) { return FAT32_E_LEER; }
            for (o = 0U; o < SEC; o += 32U) {
                const uint8_t *d = &s[o];

                if (d[0] == 0x00U) { return FAT32_E_FIN; }  /* no hay mas */
                if (d[0] == 0xE5U) { continue; }            /* borrada */
                /* 0x0F es un trozo de nombre largo: no es un fichero, es
                 * decoracion de la entrada 8.3 que viene detras. */
                if ((d[11] & 0x0FU) == 0x0FU) { continue; }
                if ((d[11] & 0x08U) != 0U)    { continue; } /* etiqueta */

                if (vistas == i) {
                    nombre83(d, e->nombre);
                    e->cluster = ((uint32_t)le16(&d[20]) << 16)
                               |  (uint32_t)le16(&d[26]);
                    e->tam    = le32(&d[28]);
                    e->es_dir = ((d[11] & 0x10U) != 0U) ? 1U : 0U;
                    return FAT32_OK;
                }
                vistas++;
            }
        }
        {
            uint32_t sig;
            int r = fat_sig(fs, cl, &sig);

            if (r != FAT32_OK) { return r; }
            cl = sig;
        }
    }
    return FAT32_E_FIN;
}

static int igual83(const char *a, const char *b)
{
    while ((*a != '\0') && (*b != '\0')) {
        char x = *a, y = *b;

        if ((x >= 'a') && (x <= 'z')) { x = (char)(x - 32); }
        if ((y >= 'a') && (y <= 'z')) { y = (char)(y - 32); }
        if (x != y) { return 0; }
        a++; b++;
    }
    return ((*a == '\0') && (*b == '\0')) ? 1 : 0;
}

int fat32_abre(fat32_t *fs, uint32_t dir_cl, const char *nombre,
               fat32_fich_t *f)
{
    fat32_entrada_t e;
    uint32_t i = 0UL;

    for (;;) {
        int r = fat32_entrada(fs, dir_cl, i, &e);

        if (r != FAT32_OK) { return r; }
        if ((e.es_dir == 0U) && (igual83(e.nombre, nombre) != 0)) {
            f->fs          = fs;
            f->cluster_ini = e.cluster;
            f->tam         = e.tam;
            f->pos         = 0UL;
            f->cluster     = e.cluster;
            f->cluster_idx = 0UL;
            return FAT32_OK;
        }
        i++;
    }
}

int32_t fat32_lee(fat32_fich_t *f, void *dst, uint32_t n)
{
    fat32_t *fs = f->fs;
    uint8_t *p  = (uint8_t *)dst;
    uint32_t hechos = 0UL;
    uint32_t tam_cl;

    if (fs->montado == 0U)   { return FAT32_E_LEER; }
    if (f->pos >= f->tam)    { return 0; }
    if (n > (f->tam - f->pos)) { n = f->tam - f->pos; }

    tam_cl = (uint32_t)fs->sec_por_cluster * SEC;

    while (hechos < n) {
        uint32_t idx = f->pos / tam_cl;        /* en que cluster cae */
        uint32_t off = f->pos % tam_cl;        /* y donde dentro de el */
        uint32_t sec, dentro, trozo;
        uint8_t *s = fs->buf;

        /*
         * EL SALTO DE CLUSTER, SIN VOLVER A EMPEZAR CADA VEZ.
         *
         * Si lo que toca es el cluster siguiente al que ya tenemos, se da
         * UN paso. Solo si alguien ha retrocedido -o si es la primera
         * lectura- se recorre la cadena desde el principio.
         *
         * Esto importa de verdad: sin el, leer un fichero de 3 MB seguido
         * con clusters de 4 kB serian 768 recorridos de media 384 saltos
         * cada uno, o sea unas 300.000 lecturas de FAT para un fichero que
         * necesita 768.
         */
        if (idx < f->cluster_idx) {
            f->cluster     = f->cluster_ini;
            f->cluster_idx = 0UL;
        }
        while (f->cluster_idx < idx) {
            uint32_t sig;
            int r = fat_sig(fs, f->cluster, &sig);

            if (r != FAT32_OK)        { return r; }
            if (fin_de_cadena(sig))   { return FAT32_E_CADENA; }
            f->cluster = sig;
            f->cluster_idx++;
        }

        sec    = cluster_a_sector(fs, f->cluster) + (off / SEC);
        dentro = off % SEC;
        trozo  = SEC - dentro;
        if (trozo > (n - hechos)) { trozo = n - hechos; }

        if (lee1(fs->dev, sec, s) != 0) { return FAT32_E_LEER; }
        {
            uint32_t j;

            for (j = 0UL; j < trozo; j++) { p[hechos + j] = s[dentro + j]; }
        }
        hechos  += trozo;
        f->pos  += trozo;
    }
    return (int32_t)hechos;
}

uint64_t fat32_libres(fat32_t *fs)
{
    uint32_t cl, libres = 0UL;

    if (fs->montado == 0U) { return 0ULL; }
    for (cl = 2UL; cl < (fs->clusters + 2UL); cl++) {
        uint32_t v;

        if (fat_sig(fs, cl, &v) != FAT32_OK) { return 0ULL; }
        if (v == 0UL) { libres++; }
    }
    return (uint64_t)libres * (uint64_t)fs->sec_por_cluster * (uint64_t)SEC;
}
