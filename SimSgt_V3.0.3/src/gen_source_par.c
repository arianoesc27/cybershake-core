/*
 * ---------------------------------------------------------------
 *  File: gen_source_par.c  (modified version)
 *
 *  Original File:
 *      This file is a modified version of the original program
 *      "gen_source.c" from SimSgt_V3.0.3
 *
 *  Modification Author:  Andrea C. Riano
 *  Modification Date:    03/12/2025
 *
 *  Summary of Modifications:
 *      - Added runtime parameter support using:
 *            setpar(argc, argv);
 *            getpar("param", "type", &variable);
 *        allowing the user to change simulation parameters
 *        without recompiling the program.
 *
 *      - Removed multiple hard-coded values (dt, nt, flo, fhi, stype,
 *        file names, filter settings, etc.), replacing them with
 *        configurable parameters with default values.
 *
 *      - Ensured that all dependent quantities (tzero, tdelay, etc.)
 *        are computed after reading user-defined parameters.
 *
 *      - Improved the initialization of runparamsP3 and pntsrcs
 *        structures for clarity and flexibility.
 *
 *      - Re-organized the code and modified it to read input parameters 
 *        from command line and .par files.  
 *
 *  Usage:
 *      Example with direct command-line parameters:
 *          ./gen_source_par dxsrc=0 ysrc=0 zsrc=0 fxsrc=1 fysrc=1 fzsrc=1 moment=1e20 dt=0.002 nt=80000 flo=1.5 stype=cos
 *
 *      Example using a parameter file:
 *          ./gen_source_par par=config.par
 *
 *  Notes:
 *      - If a parameter is not provided, its default value is used.
 *      - No recompilation is needed to adjust model parameters anymore.
 * ---------------------------------------------------------------
 */

#include "include.h"

int main(int argc, char** argv) {
    /* ====== VALORES POR DEFECTO ====== */
    int nt = 50000;
    float dt = 0.004;
    int nbounce = 1;
    char stype[256]   = "cos";
    char name[256]    = "source";
    char stfdir[256]  = "Stf";
    char stf_file[256]= "fx_src_v3.0.8";
    int bfilt = 4;
    float flo = 2.0;
    float fhi = 0.0;
    float tdelay = 0.0;
    float tzero;

    struct pntsrcs srcs;
    struct runparamsP3 rpars_p3;
    struct nodeinfo ninfo;

    /* ====== VALORES POR DEFECTO EN rpars_p3 ====== */
    rpars_p3.geoproj = 1;
    rpars_p3.xshift  = -1.0e+15;
    rpars_p3.yshift  = -1.0e+15;

    rpars_p3.modelrot = 0.0;   /* rotation of y-axis from south (clockwise positive) */
    rpars_p3.modellat = 34.0;  /* latitude of model origin */
    rpars_p3.modellon = -118.0;/* longitude of model origin */

    rpars_p3.xmom = 0.0;
    rpars_p3.ymom = 0.0;
    rpars_p3.zmom = 0.0;

    srcs.nsource = 1;
    srcs.eqsrc   = 0;
    srcs.expl    = 0;
    srcs.psrc    = 0;
    srcs.bforce  = 1;
    srcs.dblcpl  = 0;
    srcs.pointmt = 0;
    srcs.ffault  = 0;
    srcs.adjoint = 0;
    srcs.relative_slip = 0;
    srcs.absolute_slip = 0;
    srcs.area    = 1.0;

    /* ====== LEER PARAMETROS DESDE LINE DE COMANDOS ====== */
    setpar(argc, argv);

    /* estos son opcionales, y si no se pasan se quedan los defaults */
    getpar("nt",      "d", &nt);        // o "i" 
    getpar("dt",      "f", &dt);
    getpar("nbounce", "d", &nbounce);
    getpar("stype",   "s", stype);
    getpar("name",    "s", name);
    getpar("stfdir",  "s", stfdir);
    getpar("stf_file","s", stf_file);
    getpar("bfilt",   "d", &bfilt);
    getpar("flo",     "f", &flo);
    getpar("fhi",     "f", &fhi);

    /* En caso de ser necesario, tambié nse puede hacer configurables estos parametros */
    getpar("geoproj", "d", &rpars_p3.geoproj);
    getpar("modelrot","f", &rpars_p3.modelrot);
    getpar("modellat","f", &rpars_p3.modellat);
    getpar("modellon","f", &rpars_p3.modellon);

    /* ====== CALCULOS QUE DEPENDEN DE flo y dt (después de getpar) ====== */
    tzero = 0.1/(flo/2.0);
    float it = 3.0/(dt*flo); //Criterio para preservar Causalidad 
    tdelay = it*dt;

    rpars_p3.dt     = dt;
    rpars_p3.tdelay = tdelay;

    /* esta rutina ya puede usar los valores actualizados */
    get_bforce_parP3(&srcs, &tzero, &rpars_p3);
    // init_bforceP3(&srcs, &rpars_p3);

    int intsrc = 0;
    float* stfunc = check_malloc(nt * sizeof(float));

    getsource(stfunc, &dt, nt, &srcs.rtime[0], nbounce, intsrc,
              stype, name, bfilt, &flo, &fhi, 0, 0, &tdelay,
              stfdir, stf_file);

    endpar();
    return 0;
}
