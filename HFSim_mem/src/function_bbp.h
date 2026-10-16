#include "structure.h"
#include "StandRupFormat/function.h"
#include "WccFormat/Progs/function.h"

float ucvm_vs30(float lon, float lat, char* model);

void hfsim(float** seisC, char* stat, float slon, float slat, char* local_vmod, FILE* output_fp, FILE* raw_output_fp, float vs30, struct seisheader* header, float modelrot, struct slipfile* sfile, int num_comps, int do_site_response, float vref, float vpga, int seed, float rvfrac, int debug, float stress_average_override, float mean_rvfac_override, float range_rvfac_override, float akapp_override, int ispar_adjust_override, float spar_exp_override, int ipdur_model_override, float shal_rvfac_override, float deep_rvfac_override, char* site_response_module, float flowcap_override, float fmin_override, float fmidbot_override, float fhightop_override, float fmax_override);

int add_param_reset(char** param_string, char* param, int reset);
int add_param(char** param_string, char* param);

