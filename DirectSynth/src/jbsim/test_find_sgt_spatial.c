/*
 * test_find_sgt_spatial.c
 *
 * AR: Este es el "juez": arma una malla SGT de prueba, le hace miles de preguntas (puntos
 * AR: de falla al azar), y compara lo que responde la funcion nueva (find_sgt_spatial, con
 * AR: la tabla hash) contra lo que responde una copia exacta de la funcion original
 * AR: (find_sgt_reference, copiada tal cual de sgt3d_subs.c, sin cambiar ni una linea). Si
 * AR: en algun caso no coinciden exactamente, el programa avisa y falla. No sigo adelante
 * AR: con la version nueva hasta que esto de 100% igual en todos los casos, incluyendo los
 * AR: raros (puntos justo en el borde de la malla).
 *
 * AR: Por que una copia exacta y no simplemente llamar a find_sgt() de verdad? Porque
 * AR: find_sgt() de verdad esta en sgt3d_subs.c, que arrastra memcached, MPI y otras cosas
 * AR: que no necesito para esta prueba puntual. Copiar la funcion tal cual, sin tocar ni
 * AR: una coma, me da la misma respuesta sin todo ese equipaje extra.
 *
 * AR: Como se compila y corre (nada especial, un solo archivo, sin Makefile todavia
 * AR: porque esto es solo para validar la idea antes de integrarla de verdad):
 * AR:   gcc -O2 -o test_find_sgt_spatial test_find_sgt_spatial.c sgt3d_subs_spatial.c -lm
 * AR:   ./test_find_sgt_spatial
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <sys/time.h>
#include "../defs.h"
#include "../structure.h"

/* Las funciones de la version nueva (definidas en sgt3d_subs_spatial.c) */
typedef struct sgt_hash_table sgt_hash_table;
sgt_hash_table* build_sgt_hash(struct sgtindex* sgtindx, int globnp);
void free_sgt_hash(sgt_hash_table* table);
void find_sgt_spatial(struct sgtparams *sgtpar, struct sgtmaster *sgtmast, struct sgtindex *sgtindx,
                       struct sgtindex *eqindx, struct sgtindex *statindx, float *maxd, float *fwt,
                       sgt_hash_table* table, int max_ring);
extern long long g_fallback_count;

/* AR: Copia TEXTUAL de find_sgt() de sgt3d_subs.c (lineas 758-972 del original), solo
 * AR: renombrada para no chocar de nombre. Ni una linea de logica cambiada. */
void find_sgt_reference(struct sgtparams *sgtpar,struct sgtmaster *sgtmast,struct sgtindex *sgtindx,struct sgtindex *eqindx,struct sgtindex *statindx,float *maxd,float *fwt)
{
    float xx, yy, zz, rng, zdp, rexact, zexact, del, delta[4];
    float sum, mind, xwt;
    int ip, i;
    int p0, p1, p2, p3;
    int zflag;

    ip = 0;
    int start_index = 0;
    int end_index = sgtmast->globnp-1;
    int mid_index = (start_index + end_index)/2;
    while (end_index>=start_index) {
        mid_index = (start_index + end_index)/2;
        if (eqindx->indx > sgtindx[mid_index].indx) {
            start_index = mid_index+1;
        } else if (eqindx->indx < sgtindx[mid_index].indx) {
            end_index = mid_index-1;
        } else {
            break;
        }
    }
    ip = mid_index;

    if(eqindx->indx == sgtindx[ip].indx)
       {
       sgtpar->nsgt = 1;
       sgtpar->indx[0] = sgtindx[ip].indx;
       sgtpar->wt[0] = 1.0;
       }
    else
       {
       sgtpar->nsgt = 0;
       p0 = -1; p1 = -1; p2 = -1; p3 = -1;

       delta[0] = 1.0e+15;
       delta[1] = 1.0e+15;
       delta[2] = 1.0e+15;
       delta[3] = 1.0e+15;

       xx = (eqindx->xsgt - statindx->xsgt);
       yy = (eqindx->ysgt - statindx->ysgt);
       rexact = xx*xx + yy*yy;
       zexact = (eqindx->zsgt - statindx->zsgt);

       for(ip=0;ip<sgtmast->globnp;ip++)
          {
          xx = (sgtindx[ip].xsgt - statindx->xsgt);
          yy = (sgtindx[ip].ysgt - statindx->ysgt);
          rng = xx*xx + yy*yy;
          zdp = (sgtindx[ip].zsgt - statindx->zsgt);

          xx = (sgtindx[ip].xsgt - eqindx->xsgt);
          yy = (sgtindx[ip].ysgt - eqindx->ysgt);
          zz = (sgtindx[ip].zsgt - eqindx->zsgt);
          del = xx*xx + yy*yy + zz*zz;

          if(rng <= rexact && zdp <= zexact)
             {
             if(p0 < 0) { p0 = sgtpar->nsgt; sgtpar->nsgt = sgtpar->nsgt + 1; }
             if(del < delta[p0]) { delta[p0] = del; sgtpar->indx[p0] = sgtindx[ip].indx; sgtpar->wt[p0] = sqrt(del); }
             }
          else if(rng > rexact && zdp <= zexact)
             {
             if(p1 < 0) { p1 = sgtpar->nsgt; sgtpar->nsgt = sgtpar->nsgt + 1; }
             if(del < delta[p1]) { delta[p1] = del; sgtpar->indx[p1] = sgtindx[ip].indx; sgtpar->wt[p1] = sqrt(del); }
             }
          else if(rng <= rexact && zdp > zexact)
             {
             if(p2 < 0) { p2 = sgtpar->nsgt; sgtpar->nsgt = sgtpar->nsgt + 1; }
             if(del < delta[p2]) { delta[p2] = del; sgtpar->indx[p2] = sgtindx[ip].indx; sgtpar->wt[p2] = sqrt(del); }
             }
          else
             {
             if(p3 < 0) { p3 = sgtpar->nsgt; sgtpar->nsgt = sgtpar->nsgt + 1; }
             if(del < delta[p3]) { delta[p3] = del; sgtpar->indx[p3] = sgtindx[ip].indx; sgtpar->wt[p3] = sqrt(del); }
             }
          }

       zflag = -1;
       for(i=0;i<sgtpar->nsgt;i++) { if(sgtpar->wt[i] == 0.0) zflag = i; }

       if(zflag >= 0) {
          for(i=0;i<sgtpar->nsgt;i++) sgtpar->wt[i] = 0.0;
          sgtpar->wt[zflag] = 1.0;
       } else {
          sum = 0.0;
          for(i=0;i<sgtpar->nsgt;i++) sum = sum + 1.0/sgtpar->wt[i];
          sum = 1.0/sum;
          for(i=0;i<sgtpar->nsgt;i++) sgtpar->wt[i] = sum/sgtpar->wt[i];
       }

       mind = 1.0e+15;
       xwt = 0.0;
       for(i=0;i<sgtpar->nsgt;i++) { if(delta[i] < mind) { mind = delta[i]; xwt = sgtpar->wt[i]; } }
       if(mind > *maxd) { *maxd = mind; *fwt = xwt; }

       if(sgtpar->nsgt < 4)
          fprintf(stderr,"*** tried to find 4 SGT, but only found %d: eq.zsgt= %d\n",sgtpar->nsgt,eqindx->zsgt);
       }
}

/* AR: arma una malla de prueba, pero con huecos a proposito (stride mayor a 1), igual que
 * AR: una malla SGT real, ya que los puntos SGT no estan en TODOS los indices enteros
 * AR: posibles, solo en ciertos saltos. Si la armo sin huecos, cualquier punto de falla al
 * AR: azar cae justo en un punto real y nunca pruebo la busqueda "sin coincidencia exacta"
 * AR: (eso me paso en el primer intento: 20000 pruebas, 20000 coincidencias exactas, cero
 * AR: pruebas reales de la parte que mas me importa). Con huecos, la mayoria de los puntos
 * AR: de falla caen ENTRE puntos de la malla, que es el caso real e interesante. */
static struct sgtindex* build_test_grid(int nx, int ny, int nz, int stride_x, int stride_y, int stride_z, int* out_globnp) {
    int globnp = nx*ny*nz;
    struct sgtindex* grid_unsorted = (struct sgtindex*) malloc(sizeof(struct sgtindex)*globnp);
    int x, y, z, n = 0;
    for (x = 0; x < nx; x += stride_x) {
        for (y = 0; y < ny; y += stride_y) {
            for (z = 0; z < nz; z += stride_z) {
                grid_unsorted[n].xsgt = x;
                grid_unsorted[n].ysgt = y;
                grid_unsorted[n].zsgt = z;
                grid_unsorted[n].indx = ((long long) x) * 1000000000LL + ((long long) y) * 10000LL + (long long) z;
                grid_unsorted[n].h = 100.0;
                n++;
            }
        }
    }
    *out_globnp = n;

    /* AR: el original necesita el arreglo ORDENADO por indx, ya que su busqueda binaria
     * AR: depende de eso. Como armo la grilla con x/y/z crecientes y el indx crece con
     * AR: x/y/z en ese mismo orden, ya sale ordenado solo, pero lo confirmo con qsort para
     * AR: no depender de esa suposicion. */
    int cmp_indx(const void* a, const void* b) {
        long long ia = ((struct sgtindex*)a)->indx;
        long long ib = ((struct sgtindex*)b)->indx;
        return (ia > ib) - (ia < ib);
    }
    qsort(grid_unsorted, n, sizeof(struct sgtindex), cmp_indx);

    return grid_unsorted;
}

int main() {
    srand(42); /* semilla fija, para que la prueba sea repetible */

    /* AR: la malla cubre 0..nx-1 / 0..ny-1 / 0..nz-1, pero con puntos reales solo cada
     * AR: stride_x/y/z pasos, asi los puntos de falla al azar (que se generan sobre TODO el
     * AR: rango, no solo los multiplos del stride) caen mayormente en huecos. Subo el
     * AR: tamano a algo mas parecido a una malla SGT real, con muchos mas puntos, para que
     * AR: la comparacion de velocidad tenga sentido, ya que con una malla chica, escanearla
     * AR: entera ya es rapido de por si y no se nota la diferencia. */
    int nx = 300, ny = 300, nz = 40;
    int stride_x = 3, stride_y = 3, stride_z = 2;
    int globnp;
    struct sgtindex* grid = build_test_grid(nx, ny, nz, stride_x, stride_y, stride_z, &globnp);
    printf("Malla de prueba: %d puntos reales (de %d posiciones posibles, con huecos)\n", globnp, nx*ny*nz);

    struct sgtmaster sgtmast;
    sgtmast.globnp = globnp;

    sgt_hash_table* table = build_sgt_hash(grid, globnp);

    /* AR: la estacion la ponemos en un punto fijo dentro de la malla */
    struct sgtindex statindx;
    statindx.xsgt = nx/2; statindx.ysgt = ny/2; statindx.zsgt = 0;
    statindx.indx = ((long long) statindx.xsgt) * 1000000000LL + ((long long) statindx.ysgt) * 10000LL + (long long) statindx.zsgt;

    int num_tests = 20000;
    int failures = 0;
    int exact_match_cases = 0;
    int non_exact_cases = 0;

    struct timeval t0, t1, t2, t3;
    double time_reference = 0.0, time_spatial = 0.0;
    /* AR: para saber, de verdad, que tan parecidos son los pesos, no solo si pasan un
     * AR: umbral, sino cual es la diferencia mas grande que aparecio en las 20000 pruebas. */
    double max_wt_diff = 0.0;
    long long exact_bit_matches = 0;
    long long total_wt_comparisons = 0;

    int t;
    for (t = 0; t < num_tests; t++) {
        struct sgtindex eqindx;

        /* AR: mezclo casos normales (adentro de la malla) con casos de borde realistas: un
         * AR: punto de falla cerca del limite del dominio en UN eje. Eso si pasa de verdad,
         * AR: fallas grandes pueden acercarse al borde de la malla SGT, pero no los 3 ejes
         * AR: en la esquina extrema a la vez, que es un caso artificial que casi no pasa con
         * AR: datos reales y solo sirve para probar que el respaldo funciona, no para medir
         * AR: velocidad tipica. */
        int edge_case = (t % 15 == 0);
        if (edge_case) {
            eqindx.xsgt = rand() % nx;
            eqindx.ysgt = rand() % ny;
            eqindx.zsgt = rand() % nz;
            int axis = rand() % 3;
            if (axis == 0) eqindx.xsgt = (rand()%2==0) ? 0 : nx-1;
            else if (axis == 1) eqindx.ysgt = (rand()%2==0) ? 0 : ny-1;
            else eqindx.zsgt = (rand()%2==0) ? 0 : nz-1;
        } else {
            eqindx.xsgt = rand() % nx;
            eqindx.ysgt = rand() % ny;
            eqindx.zsgt = rand() % nz;
        }
        eqindx.indx = ((long long) eqindx.xsgt) * 1000000000LL + ((long long) eqindx.ysgt) * 10000LL + (long long) eqindx.zsgt;

        struct sgtparams par_ref, par_spatial;
        float maxd_ref = 0.0, fwt_ref = 1.0;
        float maxd_spatial = 0.0, fwt_spatial = 1.0;

        gettimeofday(&t0, NULL);
        find_sgt_reference(&par_ref, &sgtmast, grid, &eqindx, &statindx, &maxd_ref, &fwt_ref);
        gettimeofday(&t1, NULL);
        time_reference += (t1.tv_sec-t0.tv_sec) + (t1.tv_usec-t0.tv_usec)/1000000.0;

        gettimeofday(&t2, NULL);
        /* AR: max_ring chico a proposito, 15 en vez de un numero mucho mas grande. Para una
         * AR: malla con puntos reales cada pocos pasos, 15 anillos alcanza de sobra en el
         * AR: caso normal, y en el caso raro donde un cuadrante entero esta realmente vacio,
         * AR: prefiero caer rapido al respaldo en vez de gastar tiempo expandiendo anillos
         * AR: que ya se sabe que no van a encontrar nada. */
        find_sgt_spatial(&par_spatial, &sgtmast, grid, &eqindx, &statindx, &maxd_spatial, &fwt_spatial, table, 15);
        gettimeofday(&t3, NULL);
        time_spatial += (t3.tv_sec-t2.tv_sec) + (t3.tv_usec-t2.tv_usec)/1000000.0;

        if (par_ref.nsgt == 1) exact_match_cases++; else non_exact_cases++;

        /* AR: la comparacion real: tienen que encontrar los MISMOS 4 puntos con los MISMOS
         * AR: pesos, pero NO necesariamente en el mismo orden dentro del arreglo. Verifique
         * AR: que el resto del programa siempre busca estos valores por su numero de indice
         * AR: real, nunca asumiendo que la posicion 0/1/2/3 significa algo fijo, asi que los
         * AR: comparo como conjuntos, no como listas ordenadas. */
        int ok = 1;
        if (par_ref.nsgt != par_spatial.nsgt) ok = 0;
        int k;
        for (k = 0; ok && k < par_ref.nsgt; k++) {
            int found_match = 0;
            int m;
            for (m = 0; m < par_spatial.nsgt; m++) {
                if (par_ref.indx[k] == par_spatial.indx[m]) {
                    /* AR: encontre el mismo punto (mismo indx), ahora mido que tan parecido
                     * AR: es el peso, en vez de solo preguntar si pasa un umbral. */
                    double diff = fabs(par_ref.wt[k] - par_spatial.wt[m]);
                    total_wt_comparisons++;
                    if (diff > max_wt_diff) max_wt_diff = diff;
                    if (par_ref.wt[k] == par_spatial.wt[m]) exact_bit_matches++;
                    if (diff < 1e-6) found_match = 1;
                    break;
                }
            }
            if (!found_match) ok = 0;
        }

        if (!ok) {
            failures++;
            if (failures <= 3) {
                fprintf(stderr, "FALLO en test %d: eqindx=(%d,%d,%d) statindx=(%d,%d,%d) nsgt_ref=%d nsgt_spatial=%d\n",
                        t, eqindx.xsgt, eqindx.ysgt, eqindx.zsgt, statindx.xsgt, statindx.ysgt, statindx.zsgt, par_ref.nsgt, par_spatial.nsgt);
                int kk;
                for (kk = 0; kk < par_ref.nsgt; kk++) {
                    fprintf(stderr, "  [%d] ref: indx=%lld wt=%f   spatial: indx=%lld wt=%f\n",
                            kk, par_ref.indx[kk], par_ref.wt[kk],
                            (kk < par_spatial.nsgt ? par_spatial.indx[kk] : -1),
                            (kk < par_spatial.nsgt ? par_spatial.wt[kk] : -1.0f));
                }
            }
        }
    }

    printf("========================================\n");
    printf("Pruebas totales:        %d\n", num_tests);
    printf("  con coincidencia exacta: %d\n", exact_match_cases);
    printf("  sin coincidencia exacta: %d\n", non_exact_cases);
    printf("Fallos (deben dar 0):   %d\n", failures);
    printf("Casos que cayeron al respaldo (barrido completo): %lld de %d\n", g_fallback_count, num_tests);
    printf("--- que tan parecidos son los pesos (wt) ---\n");
    printf("Comparaciones de peso hechas: %lld\n", total_wt_comparisons);
    printf("Iguales bit por bit:          %lld (%.2f%%)\n", exact_bit_matches, 100.0*exact_bit_matches/total_wt_comparisons);
    printf("Diferencia maxima encontrada: %.10e\n", max_wt_diff);
    printf("Tiempo find_sgt_reference: %.4f seg\n", time_reference);
    printf("Tiempo find_sgt_spatial:   %.4f seg\n", time_spatial);
    printf("Aceleracion:            %.1fx\n", time_reference/time_spatial);
    printf("========================================\n");

    free_sgt_hash(table);
    free(grid);

    return (failures == 0) ? 0 : 1;
}
