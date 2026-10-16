/*
 * sgt3d_subs_spatial.c
 *
 * AR: Explico aqui, de forma simple, por que hice esto y como funciona.
 *
 * AR: EL PROBLEMA:
 * AR: Midiendo los logs reales de una corrida, encontre que la funcion find_sgt() (la
 * AR: original, en sgt3d_subs.c) se estaba comiendo el 90% del tiempo de cada tarea de
 * AR: DirectSynth. Para cada punto de la falla, recorre TODA la malla de puntos SGT (puede
 * AR: haber cientos de miles) buscando los 4 mas cercanos, uno por "cuadrante" relativo a
 * AR: la estacion.
 *
 * AR: PRIMER INTENTO, que no sirvio del todo. Armar una tabla hash por (x,y,z) y buscar
 * AR: "anillos" 3D crecientes alrededor del punto buscado. Funciono perfecto en una malla
 * AR: de prueba pareja e inventada, pero al probarlo con la malla de una tarea de verdad
 * AR: la cosa cambio. Esa malla tiene X e Y bastante dispersos, pero ademas Z tiene
 * AR: saltos MUY irregulares (para un mismo x,y, los z disponibles pueden ser 1, 2, 6, 11,
 * AR: 16, 21, 26... saltos de 1 hasta 25). Como el anillo 3D necesita que los 3 ejes esten
 * AR: cerca AL MISMO TIEMPO, un salto grande en Z obligaba a agrandar el anillo entero, y
 * AR: buscar en X/Y de nuevo aunque ya estuvieran bien, asi que terminaba siendo MAS LENTO
 * AR: que el original en el 99% de los casos reales.
 *
 * AR: LA IDEA BUENA, la de este archivo ahora, es separar los ejes en vez de tratarlos igual.
 * AR: Me di cuenta de algo simple mirando la formula del original. El "cuadrante"
 * AR: horizontal (rng<=rexact, que compara la distancia en X/Y a la estacion) es EL MISMO
 * AR: para todos los puntos de una misma columna (x,y), no depende de Z para nada. Y el
 * AR: cuadrante de profundidad (zdp<=zexact) resulta ser, haciendo la cuenta, simplemente
 * AR: "es este punto mas somero o mas profundo que el punto de falla buscado", un simple
 * AR: umbral sobre Z. Entonces agrupo los puntos SGT por columna (x,y), y DENTRO de cada
 * AR: columna guardo los Z ordenados. Para buscar, expando anillos SOLO en X/Y (2D, mucho
 * AR: mas barato que 3D), y en cada columna que encuentro hago una busqueda binaria en Z
 * AR: (que no le importan los saltos irregulares, los saltos raros de la malla ya no
 * AR: cuestan nada extra). Esto SI se ajusta a la forma que tienen los datos.
 *
 * AR: IMPORTANTE, este archivo es UNA COPIA PARALELA, no reemplaza a find_sgt():
 * AR: find_sgt_spatial() tiene que dar EXACTAMENTE los mismos resultados que la funcion
 * AR: original (mismos indices, mismos pesos, salvo el ultimo bit de redondeo por sumar en
 * AR: otro orden, eso ya lo medi y es el error de redondeo mas chico posible en un numero
 * AR: de este tipo). No toco sgt3d_subs.c para nada.
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "../defs.h"
#include "../structure.h"

/* AR: un punto guardado, listo para ordenar, con su columna (x,y empaquetados en un solo
 * AR: numero), su Z, y en que posicion del arreglo sgtindx[] original vive (para poder
 * AR: devolver despues su indx/h). */
typedef struct {
    long long xy_key;
    int z;
    int pos;
} sgt_entry;

/* AR: la estructura completa. Tiene el arreglo de puntos YA ORDENADO por (columna, z), mas una
 * AR: tabla hash chica que dice "la columna (x,y) empieza en tal posicion del arreglo
 * AR: ordenado, y tiene tantos puntos". Con esto, entrar a una columna y buscar el Z mas
 * AR: cercano es rapido (hash + busqueda binaria), sin importar que tan raros sean los
 * AR: saltos de Z ahi adentro. */
typedef struct sgt_hash_table {
    sgt_entry* sorted;
    int globnp;

    long long* col_keys;   /* -1 si el cajon esta vacio */
    int* col_start;        /* donde empieza esta columna en `sorted` */
    int* col_count;        /* cuantos puntos tiene esta columna */
    int num_buckets;
} sgt_hash_table;

/* AR: empaqueta (x,y) en un solo numero de 64 bits, sin ambiguedad posible (cada uno usa su
 * AR: propia mitad de los bits, no como antes que asumiamos que y no se pasaba de cierto
 * AR: tamano). */
static long long xy_key(int x, int y) {
    return (((long long)(unsigned int) x) << 32) | ((long long)(unsigned int) y);
}

static unsigned long long hash64(long long v, int num_buckets) {
    unsigned long long h = (unsigned long long) v;
    h = h ^ (h >> 33);
    h = h * 0xff51afd7ed558ccdULL;
    h = h ^ (h >> 33);
    return h % (unsigned long long) num_buckets;
}

static int cmp_sgt_entry(const void* a, const void* b) {
    const sgt_entry* ea = (const sgt_entry*) a;
    const sgt_entry* eb = (const sgt_entry*) b;
    if (ea->xy_key != eb->xy_key) return (ea->xy_key > eb->xy_key) - (ea->xy_key < eb->xy_key);
    return (ea->z > eb->z) - (ea->z < eb->z);
}

/* AR: arma todo. Ordena los puntos por (columna, z) y despues arma la tabla hash de
 * AR: columnas mirando donde cambia el xy_key en el arreglo ya ordenado. Se hace UNA vez
 * AR: por tarea (no una vez por punto de falla). */
sgt_hash_table* build_sgt_hash(struct sgtindex* sgtindx, int globnp) {
    sgt_hash_table* table = (sgt_hash_table*) malloc(sizeof(sgt_hash_table));
    table->globnp = globnp;
    table->sorted = (sgt_entry*) malloc(sizeof(sgt_entry) * globnp);

    int i;
    for (i = 0; i < globnp; i++) {
        table->sorted[i].xy_key = xy_key(sgtindx[i].xsgt, sgtindx[i].ysgt);
        table->sorted[i].z = sgtindx[i].zsgt;
        table->sorted[i].pos = i;
    }

    qsort(table->sorted, globnp, sizeof(sgt_entry), cmp_sgt_entry);

    /* AR: le damos el doble de cajones que de PUNTOS, no de columnas, ya que no sabemos
     * AR: cuantas columnas distintas hay todavia sin recorrer todo, y de sobrar no pasa
     * AR: nada, solo usa un poco mas de memoria. */
    table->num_buckets = globnp * 2;
    if (table->num_buckets < 16) table->num_buckets = 16;
    table->col_keys = (long long*) malloc(sizeof(long long) * table->num_buckets);
    table->col_start = (int*) malloc(sizeof(int) * table->num_buckets);
    table->col_count = (int*) malloc(sizeof(int) * table->num_buckets);
    for (i = 0; i < table->num_buckets; i++) table->col_keys[i] = -1;

    /* AR: recorro el arreglo ya ordenado una sola vez, y cada vez que cambia el xy_key
     * AR: es el arranque de una columna nueva. */
    i = 0;
    while (i < globnp) {
        long long key = table->sorted[i].xy_key;
        int start = i;
        int count = 0;
        while (i < globnp && table->sorted[i].xy_key == key) { i++; count++; }

        unsigned long long slot = hash64(key, table->num_buckets);
        while (table->col_keys[slot] != -1) slot = (slot + 1) % table->num_buckets;
        table->col_keys[slot] = key;
        table->col_start[slot] = start;
        table->col_count[slot] = count;
    }

    return table;
}

void free_sgt_hash(sgt_hash_table* table) {
    free(table->sorted);
    free(table->col_keys);
    free(table->col_start);
    free(table->col_count);
    free(table);
}

/* AR: busca una columna (x,y). Si existe, devuelve donde empieza y cuantos puntos tiene
 * AR: (en el arreglo `sorted`, ya ordenado por Z adentro de esa columna). Si no existe,
 * AR: devuelve 0 (nada que mirar ahi). */
static int find_column(sgt_hash_table* table, long long key, int* out_start, int* out_count) {
    unsigned long long slot = hash64(key, table->num_buckets);
    int start_slot = (int) slot;
    while (table->col_keys[slot] != -1) {
        if (table->col_keys[slot] == key) {
            *out_start = table->col_start[slot];
            *out_count = table->col_count[slot];
            return 1;
        }
        slot = (slot + 1) % table->num_buckets;
        if ((int) slot == start_slot) break;
    }
    return 0;
}

/* AR: este es el "modo seguro", el mismo barrido completo del original (recorre TODA la
 * AR: malla). Se usa solo como red de seguridad para el puñado de casos raros donde la
 * AR: busqueda rapida no llega a estar segura de haber encontrado lo mejor (por ejemplo,
 * AR: un punto de falla muy cerca del borde del dominio). */
static void find_sgt_linear_fallback(struct sgtparams *sgtpar, struct sgtmaster *sgtmast, struct sgtindex *sgtindx,
                                      struct sgtindex *eqindx, struct sgtindex *statindx, float *maxd, float *fwt) {
    float xx, yy, zz, rng, zdp, rexact, zexact, del, delta[4];
    int ip, i, p0, p1, p2, p3;

    sgtpar->nsgt = 0;
    p0 = -1; p1 = -1; p2 = -1; p3 = -1;
    delta[0] = 1.0e+15; delta[1] = 1.0e+15; delta[2] = 1.0e+15; delta[3] = 1.0e+15;

    xx = (float) (eqindx->xsgt - statindx->xsgt);
    yy = (float) (eqindx->ysgt - statindx->ysgt);
    rexact = xx*xx + yy*yy;
    zexact = (float) (eqindx->zsgt - statindx->zsgt);

    for (ip = 0; ip < sgtmast->globnp; ip++) {
        xx = (float) (sgtindx[ip].xsgt - statindx->xsgt);
        yy = (float) (sgtindx[ip].ysgt - statindx->ysgt);
        rng = xx*xx + yy*yy;
        zdp = (float) (sgtindx[ip].zsgt - statindx->zsgt);

        xx = (float) (sgtindx[ip].xsgt - eqindx->xsgt);
        yy = (float) (sgtindx[ip].ysgt - eqindx->ysgt);
        zz = (float) (sgtindx[ip].zsgt - eqindx->zsgt);
        del = xx*xx + yy*yy + zz*zz;

        int* pq;
        if (rng <= rexact && zdp <= zexact) pq = &p0;
        else if (rng > rexact && zdp <= zexact) pq = &p1;
        else if (rng <= rexact && zdp > zexact) pq = &p2;
        else pq = &p3;

        if (*pq < 0) { *pq = sgtpar->nsgt; sgtpar->nsgt++; }
        if (del < delta[*pq] || (del == delta[*pq] && sgtindx[ip].indx < sgtpar->indx[*pq])) {
            delta[*pq] = del;
            sgtpar->indx[*pq] = sgtindx[ip].indx;
            sgtpar->wt[*pq] = sqrt(del);
        }
    }

    int zflag = -1;
    for (i = 0; i < sgtpar->nsgt; i++) { if (sgtpar->wt[i] == 0.0) zflag = i; }
    if (zflag >= 0) {
        for (i = 0; i < sgtpar->nsgt; i++) sgtpar->wt[i] = 0.0;
        sgtpar->wt[zflag] = 1.0;
    } else {
        float sum = 0.0;
        for (i = 0; i < sgtpar->nsgt; i++) sum = sum + 1.0/sgtpar->wt[i];
        sum = 1.0/sum;
        for (i = 0; i < sgtpar->nsgt; i++) sgtpar->wt[i] = sum/sgtpar->wt[i];
    }

    float mind = 1.0e+15, xwt = 0.0;
    for (i = 0; i < sgtpar->nsgt; i++) {
        if (delta[i] < mind) { mind = delta[i]; xwt = sgtpar->wt[i]; }
    }
    if (mind > *maxd) { *maxd = mind; *fwt = xwt; }
}

/* AR: contador para poder medir, en las pruebas, cuantas consultas cayeron al respaldo. */
long long g_fallback_count = 0;

/* AR: revisa UNA columna, que ya sabemos que existe, contra el punto buscado. Como el
 * AR: cuadrante horizontal (rng<=rexact) es el MISMO para toda la columna (no depende de Z),
 * AR: alcanza con mirar, dentro de esta columna ordenada por Z, el punto con mayor Z entre
 * AR: los <= al Z buscado (el mejor candidato para el lado "zdp<=zexact"), y el punto con
 * AR: menor Z entre los > al Z buscado (el mejor candidato para el lado "zdp>zexact"). Una
 * AR: busqueda binaria nos da los dos de una, sin importar los saltos raros de Z. */
static void check_column(sgt_hash_table* table, struct sgtindex* sgtindx, int start, int count,
                          struct sgtindex* eqindx, struct sgtindex* statindx, float rexact, float zexact,
                          struct sgtparams* sgtpar, float* delta, int* p0, int* p1, int* p2, int* p3) {
    /* AR: rng es el mismo para todos los puntos de esta columna, lo calculo una vez con
     * AR: cualquiera de ellos (uso el primero). */
    float xx = (float) (sgtindx[table->sorted[start].pos].xsgt - statindx->xsgt);
    float yy = (float) (sgtindx[table->sorted[start].pos].ysgt - statindx->ysgt);
    float rng = xx*xx + yy*yy;
    int horizontal_near = (rng <= rexact);

    /* AR: busqueda binaria del Z buscado adentro de esta columna (ordenada por Z). */
    int lo = start, hi = start + count - 1;
    int first_greater = start + count; /* si no encontramos ninguno mayor, queda "fuera" */
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (table->sorted[mid].z > eqindx->zsgt) {
            first_greater = mid;
            hi = mid - 1;
        } else {
            lo = mid + 1;
        }
    }
    /* AR: first_greater es el primer punto con z > eqindx.z. El de z <= eqindx.z mas
     * AR: cercano es el de justo antes (first_greater-1), si existe dentro de esta columna. */
    int idx_le = first_greater - 1;   /* candidato con z <= eqindx.z (el mayor de esos) */
    int idx_gt = first_greater;       /* candidato con z >  eqindx.z (el menor de esos) */

    int candidates[2];
    int num_candidates = 0;
    if (idx_le >= start) candidates[num_candidates++] = idx_le;
    if (idx_gt < start + count) candidates[num_candidates++] = idx_gt;

    int c;
    for (c = 0; c < num_candidates; c++) {
        int pos = table->sorted[candidates[c]].pos;
        float zdp = (float) (sgtindx[pos].zsgt - statindx->zsgt);
        int vertical_near = (zdp <= zexact);

        float dxx = (float) (sgtindx[pos].xsgt - eqindx->xsgt);
        float dyy = (float) (sgtindx[pos].ysgt - eqindx->ysgt);
        float dzz = (float) (sgtindx[pos].zsgt - eqindx->zsgt);
        float del = dxx*dxx + dyy*dyy + dzz*dzz;

        int* pq;
        if (horizontal_near && vertical_near) pq = p0;
        else if (!horizontal_near && vertical_near) pq = p1;
        else if (horizontal_near && !vertical_near) pq = p2;
        else pq = p3;

        if (*pq < 0) { *pq = sgtpar->nsgt; sgtpar->nsgt++; }
        if (del < delta[*pq] || (del == delta[*pq] && sgtindx[pos].indx < sgtpar->indx[*pq])) {
            delta[*pq] = del;
            sgtpar->indx[*pq] = sgtindx[pos].indx;
            sgtpar->wt[*pq] = sqrt(del);
        }
    }
}

/* AR: find_sgt_spatial tiene la misma firma que antes, asi synth_spatial.c no necesita
 * AR: cambiar nada, pero por adentro ahora busca por columnas (x,y) mas Z ordenado, no por
 * AR: anillos 3D. max_ring ahora es sobre anillos 2D, mucho mas baratos de recorrer. */
void find_sgt_spatial(struct sgtparams *sgtpar, struct sgtmaster *sgtmast, struct sgtindex *sgtindx,
                       struct sgtindex *eqindx, struct sgtindex *statindx, float *maxd, float *fwt,
                       sgt_hash_table* table, int max_ring) {

    float xx = (float) (eqindx->xsgt - statindx->xsgt);
    float yy = (float) (eqindx->ysgt - statindx->ysgt);
    float rexact = xx*xx + yy*yy;
    float zexact = (float) (eqindx->zsgt - statindx->zsgt);

    /* AR: coincidencia exacta. Buscamos la columna del punto pedido, y adentro buscamos el
     * AR: Z exacto (misma busqueda binaria, solo miramos si el candidato mas cercano es
     * AR: exactamente igual). */
    int col_start, col_count;
    if (find_column(table, xy_key(eqindx->xsgt, eqindx->ysgt), &col_start, &col_count)) {
        int lo = col_start, hi = col_start + col_count - 1, found = -1;
        while (lo <= hi) {
            int mid = (lo + hi) / 2;
            if (table->sorted[mid].z == eqindx->zsgt) { found = mid; break; }
            else if (table->sorted[mid].z < eqindx->zsgt) lo = mid + 1;
            else hi = mid - 1;
        }
        if (found >= 0) {
            sgtpar->nsgt = 1;
            sgtpar->indx[0] = sgtindx[table->sorted[found].pos].indx;
            sgtpar->wt[0] = 1.0;
            return;
        }
    }

    float delta[4];
    int p0 = -1, p1 = -1, p2 = -1, p3 = -1;
    delta[0] = 1.0e+15; delta[1] = 1.0e+15; delta[2] = 1.0e+15; delta[3] = 1.0e+15;
    sgtpar->nsgt = 0;

    int confirmed_complete = 0;
    int r;
    for (r = 0; r <= max_ring; r++) {

        /* AR: misma condicion de parada segura de antes, solo que ahora r es distancia en
         * AR: X/Y nomas (2D). Sigue siendo una cota valida porque dz^2 nunca puede ser
         * AR: negativo, asi que r*r sigue siendo el minimo posible para cualquier candidato
         * AR: de esta distancia en adelante. */
        if (p0 >= 0 && p1 >= 0 && p2 >= 0 && p3 >= 0) {
            float min_possible_dist2 = (float) r * (float) r;
            if (min_possible_dist2 > delta[0] && min_possible_dist2 > delta[1] &&
                min_possible_dist2 > delta[2] && min_possible_dist2 > delta[3]) {
                confirmed_complete = 1;
                break;
            }
        }

        #define CHECK_COLUMN_AT(DX, DY) do { \
            int cs, cc; \
            if (find_column(table, xy_key(eqindx->xsgt + (DX), eqindx->ysgt + (DY)), &cs, &cc)) { \
                check_column(table, sgtindx, cs, cc, eqindx, statindx, rexact, zexact, sgtpar, delta, &p0, &p1, &p2, &p3); \
            } \
        } while(0)

        if (r == 0) {
            CHECK_COLUMN_AT(0, 0);
        } else {
            /* AR: perimetro 2D del anillo r (un cuadrado hueco), mucho mas barato de
             * AR: recorrer que el cascaron 3D de antes. */
            int a;
            for (a = -r; a <= r; a++) {
                CHECK_COLUMN_AT(r, a);
                CHECK_COLUMN_AT(-r, a);
            }
            for (a = -r+1; a <= r-1; a++) {
                CHECK_COLUMN_AT(a, r);
                CHECK_COLUMN_AT(a, -r);
            }
        }
        #undef CHECK_COLUMN_AT
    }

    if (!confirmed_complete) {
        g_fallback_count++;
        find_sgt_linear_fallback(sgtpar, sgtmast, sgtindx, eqindx, statindx, maxd, fwt);
        return;
    }

    /* AR: de aca para abajo es igual que el original, convierte distancias en pesos de
     * AR: interpolacion. */
    int zflag = -1;
    int i;
    for (i = 0; i < sgtpar->nsgt; i++) {
        if (sgtpar->wt[i] == 0.0) zflag = i;
    }

    if (zflag >= 0) {
        for (i = 0; i < sgtpar->nsgt; i++) sgtpar->wt[i] = 0.0;
        sgtpar->wt[zflag] = 1.0;
    } else {
        float sum = 0.0;
        for (i = 0; i < sgtpar->nsgt; i++) sum = sum + 1.0/sgtpar->wt[i];
        sum = 1.0/sum;
        for (i = 0; i < sgtpar->nsgt; i++) sgtpar->wt[i] = sum/sgtpar->wt[i];
    }

    float mind = 1.0e+15;
    float xwt = 0.0;
    for (i = 0; i < sgtpar->nsgt; i++) {
        if (delta[i] < mind) {
            mind = delta[i];
            xwt = sgtpar->wt[i];
        }
    }
    if (mind > *maxd) {
        *maxd = mind;
        *fwt = xwt;
    }

    if (sgtpar->nsgt < 4) {
        fprintf(stderr, "*** find_sgt_spatial: solo encontro %d de 4 puntos (eq.zsgt=%d)\n",
                sgtpar->nsgt, eqindx->zsgt);
    }
}
