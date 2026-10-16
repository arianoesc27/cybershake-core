#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include "pmcl3d.h"

int writeCHK(char *chkfile, int ntiskp, float dt, float dh, 
      int nxt, int nyt, int nzt,
      int nt, float arbc, int npc, int nve,
      float fl, float fh, float fp, 
      float *vse, float *vpe, float *dde, double qpscale, double qsscale){

  FILE *fchk;

  fchk = fopen(chkfile,"w");
  fprintf(fchk,"STABILITY CRITERIA .5 > CMAX*DT/DX:\t%f\n",vpe[1]*dt/dh);
  fprintf(fchk,"# OF X,Y,Z NODES PER PROC:\t%d, %d, %d\n",nxt,nyt,nzt);
  fprintf(fchk,"# OF TIME STEPS:\t%d\n",nt);
  fprintf(fchk,"DISCRETIZATION IN SPACE:\t%f\n",dh);
  fprintf(fchk,"DISCRETIZATION IN TIME:\t%f\n",dt);
  fprintf(fchk,"PML REFLECTION COEFFICIENT:\t%f\n",arbc);
  fprintf(fchk,"HIGHEST P-VELOCITY ENCOUNTERED:\t%f\n",vpe[1]);
  fprintf(fchk,"LOWEST P-VELOCITY ENCOUNTERED:\t%f\n",vpe[0]);
  fprintf(fchk,"HIGHEST S-VELOCITY ENCOUNTERED:\t%f\n",vse[1]);
  fprintf(fchk,"LOWEST S-VELOCITY ENCOUNTERED:\t%f\n",vse[0]);
  fprintf(fchk,"HIGHEST DENSITY ENCOUNTERED:\t%f\n",dde[1]);
  fprintf(fchk,"LOWEST  DENSITY ENCOUNTERED:\t%f\n",dde[0]);
  fprintf(fchk,"SKIP OF SEISMOGRAMS IN TIME (LOOP COUNTER):\t%d\n",ntiskp);
  fprintf(fchk,"ABC CONDITION, PML=1 OR CERJAN=0:\t%d\n",npc);
  fprintf(fchk,"FD SCHEME, VISCO=1 OR ELASTIC=0:\t%d\n",nve);
  fprintf(fchk,"Q, FL,FP,FH:\t%f, %f, %f\n",fl,fp,fh);
  //AR: (2026-10-14) The attenuation fractions this run used, see def_QSIN in command.c
  fprintf(fchk,"Q interpretation:\tQs=QSIN*Vs, Qp=QPIN*Qs\n");
  fprintf(fchk,"QPIN/QSIN:\t%f,\t%f\n", qpscale, qsscale);
  fclose(fchk);

return 0;
}
