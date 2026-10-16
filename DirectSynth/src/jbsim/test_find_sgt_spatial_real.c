/*
 * test_find_sgt_spatial_real.c
 *
 * AR: Es la misma idea que test_find_sgt_spatial.c, comparar la funcion nueva contra una
 * AR: copia fiel de la original, pero en vez de inventar una malla de prueba, leo la malla
 * AR: SGT REAL de una tarea real, el archivo .sgthead que ya existe en disco de una corrida
 * AR: real. Asi confirmo que no hay ninguna sorpresa rara de la malla real (espaciado
 * AR: distinto, forma irregular del dominio, etc.) que la malla inventada no haya podido
 * AR: mostrar.
 *
 * AR: El formato del archivo .sgthead es simple: primero va el bloque de cabecera tal cual
 * AR: en binario, y despues el arreglo completo de puntos, tambien tal cual en binario. Eso
 * AR: lo confirme leyendo el codigo real que arma este mismo archivo cuando el proceso
 * AR: principal lo lee al arrancar.
 *
 * AR: Como se compila y corre:
 * AR:   gcc -O2 -o test_find_sgt_spatial_real test_find_sgt_spatial_real.c sgt3d_subs_spatial.c -lm
 * AR:   ./test_find_sgt_spatial_real /ruta/al/archivo.sgthead
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <sys/time.h>
#include "../defs.h"
#include "../structure.h"

typedef struct sgt_hash_table sgt_hash_table;
sgt_hash_table* build_sgt_hash(struct sgtindex* sgtindx, int globnp);
void free_sgt_hash(sgt_hash_table* table);
void find_sgt_spatial(struct sgtparams *sgtpar, struct sgtmaster *sgtmast, struct sgtindex *sgtindx,
                       struct sgtindex *eqindx, struct sgtindex *statindx, float *maxd, float *fwt,
                       sgt_hash_table* table, int max_ring);
extern long long g_fallback_count;

/* AR: copia exacta de find_sgt(), identica a la que ya uso en test_find_sgt_spatial.c */
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

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "Uso: %s /ruta/al/archivo.sgthead\n", argv[0]);
        return 1;
    }

    /* AR: leo el archivo real, primero el bloque de cabecera, despues todo el arreglo de
     * AR: puntos, tal cual lo hace el codigo real que los lee normalmente (sin memcached,
     * AR: directo a disco). */
    FILE* fp = fopen(argv[1], "rb");
    if (!fp) {
        fprintf(stderr, "No se pudo abrir %s\n", argv[1]);
        return 1;
    }

    struct sgtmaster sgtmast;
    if (fread(&sgtmast, sizeof(struct sgtmaster), 1, fp) != 1) {
        fprintf(stderr, "Error leyendo el sgtmaster.\n");
        return 1;
    }

    printf("=== Datos reales leidos de %s ===\n", argv[1]);
    printf("globnp (puntos SGT reales): %d\n", sgtmast.globnp);
    printf("nt: %d, geoproj: %d\n", sgtmast.nt, sgtmast.geoproj);

    if (sgtmast.globnp <= 0 || sgtmast.globnp > 500000000) {
        fprintf(stderr, "globnp=%d no tiene pinta de ser real -- el formato del archivo no es el esperado.\n", sgtmast.globnp);
        return 1;
    }

    struct sgtindex* grid = (struct sgtindex*) malloc(sizeof(struct sgtindex) * (size_t) sgtmast.globnp);
    size_t read_count = fread(grid, sizeof(struct sgtindex), sgtmast.globnp, fp);
    if (read_count != (size_t) sgtmast.globnp) {
        fprintf(stderr, "Error leyendo sgtindx: se esperaban %d puntos, se leyeron %zu.\n", sgtmast.globnp, read_count);
        return 1;
    }
    fclose(fp);

    /* AR: un vistazo real a la forma de la malla, el minimo y maximo de cada eje, para saber
     * AR: si de verdad es una grilla regular como asumo. */
    int xmin=grid[0].xsgt, xmax=grid[0].xsgt, ymin=grid[0].ysgt, ymax=grid[0].ysgt, zmin=grid[0].zsgt, zmax=grid[0].zsgt;
    int i;
    for (i = 0; i < sgtmast.globnp; i++) {
        if (grid[i].xsgt < xmin) xmin = grid[i].xsgt;
        if (grid[i].xsgt > xmax) xmax = grid[i].xsgt;
        if (grid[i].ysgt < ymin) ymin = grid[i].ysgt;
        if (grid[i].ysgt > ymax) ymax = grid[i].ysgt;
        if (grid[i].zsgt < zmin) zmin = grid[i].zsgt;
        if (grid[i].zsgt > zmax) zmax = grid[i].zsgt;
    }
    printf("Rango real: x=[%d,%d] y=[%d,%d] z=[%d,%d]\n", xmin, xmax, ymin, ymax, zmin, zmax);
    printf("h (espaciado) del primer punto: %f\n", grid[0].h);

    sgt_hash_table* table = build_sgt_hash(grid, sgtmast.globnp);

    /* AR: pongo la estacion en el centro del dominio real. No tengo el dato real de donde
     * AR: esta la estacion en este archivo, pero para probar que la busqueda anda bien con
     * AR: la FORMA real de la malla, el centro es una eleccion razonable. */
    struct sgtindex statindx;
    statindx.xsgt = (xmin+xmax)/2;
    statindx.ysgt = (ymin+ymax)/2;
    statindx.zsgt = zmin;
    statindx.indx = ((long long) statindx.xsgt) * 100000000LL + ((long long) statindx.ysgt) * 10000LL + (long long) statindx.zsgt;

    srand(42);
    int num_tests = 20000;
    int failures = 0;
    long long total_wt_comparisons = 0, exact_bit_matches = 0;
    double max_wt_diff = 0.0;
    struct timeval t0, t1, t2, t3;
    double time_reference = 0.0, time_spatial = 0.0;

    int t;
    for (t = 0; t < num_tests; t++) {
        struct sgtindex eqindx;
        eqindx.xsgt = xmin + rand() % (xmax-xmin+1);
        eqindx.ysgt = ymin + rand() % (ymax-ymin+1);
        eqindx.zsgt = zmin + rand() % (zmax-zmin+1);
        eqindx.indx = ((long long) eqindx.xsgt) * 100000000LL + ((long long) eqindx.ysgt) * 10000LL + (long long) eqindx.zsgt;

        struct sgtparams par_ref, par_spatial;
        float maxd_ref = 0.0, fwt_ref = 1.0, maxd_spatial = 0.0, fwt_spatial = 1.0;

        gettimeofday(&t0, NULL);
        find_sgt_reference(&par_ref, &sgtmast, grid, &eqindx, &statindx, &maxd_ref, &fwt_ref);
        gettimeofday(&t1, NULL);
        time_reference += (t1.tv_sec-t0.tv_sec) + (t1.tv_usec-t0.tv_usec)/1000000.0;

        gettimeofday(&t2, NULL);
        find_sgt_spatial(&par_spatial, &sgtmast, grid, &eqindx, &statindx, &maxd_spatial, &fwt_spatial, table, 50);
        gettimeofday(&t3, NULL);
        time_spatial += (t3.tv_sec-t2.tv_sec) + (t3.tv_usec-t2.tv_usec)/1000000.0;

        int ok = 1;
        if (par_ref.nsgt != par_spatial.nsgt) ok = 0;
        int k;
        for (k = 0; ok && k < par_ref.nsgt; k++) {
            int found_match = 0, m;
            for (m = 0; m < par_spatial.nsgt; m++) {
                if (par_ref.indx[k] == par_spatial.indx[m]) {
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
            if (failures <= 5) {
                fprintf(stderr, "FALLO en test %d: eqindx=(%d,%d,%d) nsgt_ref=%d nsgt_spatial=%d\n",
                        t, eqindx.xsgt, eqindx.ysgt, eqindx.zsgt, par_ref.nsgt, par_spatial.nsgt);
            }
        }
    }

    printf("========================================\n");
    printf("Pruebas totales (con la malla REAL):   %d\n", num_tests);
    printf("Fallos (deben dar 0):   %d\n", failures);
    printf("Casos con respaldo (barrido completo): %lld de %d\n", g_fallback_count, num_tests);
    printf("Comparaciones de peso: %lld, iguales bit a bit: %lld (%.2f%%), diferencia maxima: %.10e\n",
           total_wt_comparisons, exact_bit_matches, 100.0*exact_bit_matches/total_wt_comparisons, max_wt_diff);
    printf("Tiempo original: %.4f seg | Tiempo nuevo: %.4f seg | Aceleracion: %.1fx\n",
           time_reference, time_spatial, time_reference/time_spatial);
    printf("========================================\n");

    free_sgt_hash(table);
    free(grid);

    return (failures == 0) ? 0 : 1;
}
