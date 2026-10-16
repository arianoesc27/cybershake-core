//AR: New file (2026-09-06). It is a copy of jbsim3d.c and the only change is how the SGT data
//AR: is fetched from the handlers. Everything else (generating ruptures, matching each fault
//AR: point to its SGT data, computing the seismogram, writing the output) is byte for byte the
//AR: same code as jbsim3d.c. I did that on purpose, so any risk stays in the fetching part.
//AR:
//AR: Why. I measured the logs of a run that already had the first speedup, and fetching SGT
//AR: data from the handlers had become the biggest cost, about 65 to 68% of the time of each
//AR: task. Generating the rupture and the physics take much less. A task usually needs data
//AR: from several SGT handlers (I measured 7 to 11), and the original code asks them one at a
//AR: time, waiting for the full answer before asking the next one. Every handler is its own
//AR: process, so they could work at the same time, and the original code never used that.
//AR:
//AR: What I do. I send the request to every handler that has data for this task at once,
//AR: without waiting, and then collect the answers as they come in. Since each handler works
//AR: on its own, the wait becomes as long as the SLOWEST handler instead of the SUM of all of
//AR: them. MPI has a non-blocking way of doing exactly this, MPI_Isend and MPI_Irecv ("I" for
//AR: immediate, they send or receive and come back right away). This file uses them instead of
//AR: the usual blocking send and receive.
//AR:
//AR: What it costs. Instead of reusing ONE buffer for whichever handler is being asked, this
//AR: needs one buffer PER handler, because all of them are filled at about the same time. I
//AR: measured 45 to 55% of the memory free on these nodes, so the extra memory is affordable.
//AR:
//AR: What it does NOT change. When the share of one handler is too big for one request
//AR: (bigger than max_buf_mb), I still ask that handler again for the rest the old way, one
//AR: request at a time. That has not happened in any run so far, so rewriting that path too
//AR: was not worth the risk.
//AR:
//AR: This builds a separate binary, direct_synth_spatial_async, and direct_synth_spatial is
//AR: untouched. I tested it on the full TEST site (2026-09-07). The results are correct but
//AR: it is not faster, see the comment in Makefile.bsc-mn5.
#include "include.h"
#include "defs.h"
#include "structure.h"
#include "rupgen_api.h"
#include "function.h"

const int X_COMP_FLAG = 1;
const int Y_COMP_FLAG = 2;
const int Z_COMP_FLAG = 4;

int find_index(int source_id, int rupture_id, int rv_id, int num_rv_infos, rv_info* rvinfo_array) {
	//Should be sorted by (source id, rupture id, rv id)
	//Binary search
	int start_index = 0;
    int end_index = num_rv_infos-1;
    int mid_index = (start_index + end_index) / 2;
    while(end_index>=start_index) {
        mid_index = (start_index + end_index) / 2;
        if (source_id>rvinfo_array[mid_index].source_id || (source_id==rvinfo_array[mid_index].source_id && rupture_id>rvinfo_array[mid_index].rupture_id) || (source_id==rvinfo_array[mid_index].source_id && rupture_id==rvinfo_array[mid_index].rupture_id && rv_id>rvinfo_array[mid_index].rup_var_id)) {
            start_index = mid_index+1;
        } else if (source_id<rvinfo_array[mid_index].source_id || (source_id==rvinfo_array[mid_index].source_id && rupture_id<rvinfo_array[mid_index].rupture_id) || (source_id==rvinfo_array[mid_index].source_id && rupture_id==rvinfo_array[mid_index].rupture_id && rv_id<rvinfo_array[mid_index].rup_var_id)) {
            end_index = mid_index-1;
        } else {
			//Found it!
			return mid_index;
        }
    }
}

float find_rvfrac(int source_id, int rupture_id, int rv_id, int num_rv_infos, rv_info* rvinfo_array) {
    int index = find_index(source_id, rupture_id, rv_id, num_rv_infos, rvinfo_array);
    return rvinfo_array[index].rvfrac;
}

int find_seed(int source_id, int rupture_id, int rv_id, int num_rv_infos, rv_info* rvinfo_array) {
	int index = find_index(source_id, rupture_id, rv_id, num_rv_infos, rvinfo_array);
	return rvinfo_array[index].seed;
}

//AR: New helper (2026-09-06), fetches SGT data from every handler that has points for this
//AR: task at the same time, instead of one at a time. Fills one sgthead/sgtbuf pair PER
//AR: handler (handler_ids[k]/req_start[k]/req_end[k] describe which slice of that handler's
//AR: points this covers). This function allocates all of them, and the caller is responsible for
//AR: freeing sgthead_out[k]/sgtbuf_out[k] for every k once done using them. Aborts the same
//AR: way the original request_sgt() did if a handler's reported dt doesn't match dtout.
void request_sgt_all(struct sgtheader** sgthead_out, float** sgtbuf_out, int num_comps,
		int num_active, int* handler_ids, int* req_start, int* req_end,
		long long** sgts_by_handler, int nt, float dtout, int my_id) {
	int k, c, j;
	MPI_Datatype sgtheader_type, sgtdata_type;
	construct_sgtheader_datatype(&sgtheader_type);
	construct_sgtdata_datatype(&sgtdata_type, nt);

	int* req_count = check_malloc(sizeof(int)*num_active);
	handler_msg* msgs = check_malloc(sizeof(handler_msg)*num_active);
	float** raw = check_malloc(sizeof(float*)*num_active);

	MPI_Request* msg_send_req = check_malloc(sizeof(MPI_Request)*num_active);
	MPI_Request* idx_send_req = check_malloc(sizeof(MPI_Request)*num_active);
	MPI_Request* head_recv_req = check_malloc(sizeof(MPI_Request)*num_active);
	MPI_Request* data_recv_req = check_malloc(sizeof(MPI_Request)*num_active*num_comps);

	//Kick off every request first, without waiting for any of them
	for (k=0; k<num_active; k++) {
		int h = handler_ids[k];
		req_count[k] = req_end[k] - req_start[k];

		sgthead_out[k] = check_malloc(req_count[k]*sizeof(struct sgtheader));
		sgtbuf_out[k] = check_malloc((long long)18*req_count[k]*nt*sizeof(float));
		raw[k] = check_malloc((long long)num_comps*req_count[k]*6*nt*sizeof(float));

		msgs[k].msg_src = my_id;
		msgs[k].msg_type = SGT_REQUEST;
		msgs[k].num_sgts_requested = req_count[k];

		if (debug) {
			char buf[256];
			sprintf(buf, "Requesting %d SGTs from handler %d (async, %d of %d).", req_count[k], h, k+1, num_active);
			write_log(buf);
		}
		MPI_Isend(&msgs[k], 3, MPI_INT, h, SGT_REQUEST_TAG, MPI_COMM_WORLD, &msg_send_req[k]);
		MPI_Isend(sgts_by_handler[h]+req_start[k], req_count[k], MPI_LONG_LONG, h, SGT_REQUEST_TAG, MPI_COMM_WORLD, &idx_send_req[k]);
		MPI_Irecv(sgthead_out[k], req_count[k], sgtheader_type, h, SGT_HEADER_DATA_TAG, MPI_COMM_WORLD, &head_recv_req[k]);
		for (c=0; c<num_comps; c++) {
			MPI_Irecv(raw[k]+(long long)c*req_count[k]*6*nt, req_count[k], sgtdata_type, h, SGT_RAW_DATA_TAG, MPI_COMM_WORLD, &data_recv_req[k*num_comps+c]);
		}
	}

	//Now wait for everything. This is where the time is saved, since it waits once for the
	//SLOWEST handler instead of waiting for each one in turn.
	MPI_Waitall(num_active, msg_send_req, MPI_STATUSES_IGNORE);
	MPI_Waitall(num_active, idx_send_req, MPI_STATUSES_IGNORE);
	MPI_Waitall(num_active, head_recv_req, MPI_STATUSES_IGNORE);
	MPI_Waitall(num_active*num_comps, data_recv_req, MPI_STATUSES_IGNORE);

	if (debug) write_log("All async SGT requests completed.");

	//Same dt check the original request_sgt() did, just once per handler instead of right
	//after each one's own blocking call.
	for (k=0; k<num_active; k++) {
		if (fabs(sgthead_out[k][0].dt-dtout)>0.00001) {
			fprintf(stderr, "dt as reported in SGT header, %f, does not match the 'dtout' argument, %f.  These must match, aborting.\n", sgthead_out[k][0].dt, dtout);
			MPI_Abort(MPI_COMM_WORLD, 2);
			exit(2);
		}
	}

	//Same copy-into-sgtbuf and ymom/zmom logic the original request_sgt() did per component,
	//done here once the data of every handler has arrived.
	for (k=0; k<num_active; k++) {
		for (c=0; c<num_comps; c++) {
			for (j=0; j<req_count[k]; j++) {
				memcpy(sgtbuf_out[k]+(long long)(j*18+6*c)*nt, raw[k]+(long long)c*req_count[k]*6*nt+(long long)j*6*nt, 6*nt*sizeof(float));
			}
		}
		free(raw[k]);
		float xmom = sgthead_out[k][0].xmom;
		//Same assumption request_sgt() already made, that xmom and ymom are the same value.
		float ymom = xmom;
		float zmom = ymom;
		if (num_comps>=2) {
			for (j=0; j<req_count[k]; j++) {
				sgthead_out[k][j].ymom = ymom;
			}
		}
		if (num_comps==3) {
			for (j=0; j<req_count[k]; j++) {
				sgthead_out[k][j].zmom = zmom;
			}
		}
	}

	free(req_count);
	free(msgs);
	free(raw);
	free(msg_send_req);
	free(idx_send_req);
	free(head_recv_req);
	free(data_recv_req);
	MPI_Type_free(&sgtheader_type);
	MPI_Type_free(&sgtdata_type);
}

float** jbsim3d_synth(float*** seis_return, struct seisheader* header, char stat[], float slon, float slat,
		int ntout, char seis_file[], char rup_geom_file[], struct sgtfileparams* sfp,
		struct sgtparams* sgtparms, struct sgtmaster sgtmast, struct sgtindex* sgtindx,
		struct geoprojection geop, long long* indx_master, int num_sgts, long long** sgts_by_handler, int* num_sgts_by_handler,
		int num_sgt_handlers, int num_rup_vars, struct rupture_variation* rup_vars, int num_rv_infos, rv_info* rvinfo_array, int my_id)
{
FILE *fopfile(), *fpr;
struct sgtfileparams sgtextract;
struct sgtfileparams sgtfilepar = *sfp;
struct sgtindex eqindx, statindx;
float *sgtbuf;
struct sgtheader *sgthead;

float **gfmech;
float **stf, **seis, **subseis, *se, *sn, *sv;
float rt, scale;
float elon, elat, edep;
float vslip, **space;
float z0, strike, dip, rake;
int fdw, ip, maxmech, nstf, ntsum, maxnt, ig;
int i,j;
float mindt, maxdelta, fweight;

char string[256], outfile[128];
char outdir[256], sgtdir[256], sname[8], rupmodfile[256];

struct standrupformat* srf;
struct srf_planerectangle **prect_ptr;
struct srf_prectsegments **prseg_ptr;
struct srf_allpoints **apnts_ptr;
struct srf_apointvalues **apval_ptr;

srf = check_malloc(sizeof(struct standrupformat) * num_rup_vars);
prect_ptr = check_malloc(sizeof(struct srf_planerectangle) * num_rup_vars);
prseg_ptr = check_malloc(sizeof(struct srf_prectsegments) * num_rup_vars);
apnts_ptr = check_malloc(sizeof(struct srf_allpoints) * num_rup_vars);
apval_ptr = check_malloc(sizeof(struct srf_apointvalues) * num_rup_vars);

struct mechparam mechpar;

int nm, non_exact;

int extract_sgt = 0;

float* tmom = check_malloc(sizeof(float) * num_rup_vars);
for (i=0; i<num_rup_vars; i++) {
	tmom[i] = 0.0;
}
float dtout = -1.0;
float slip_conv = 1.0;  /* input slip in cm on each subfault */
float tstart = 0.0;

float sdep = 0.0;

int* apv_off = check_malloc(sizeof(int)*num_rup_vars);
for (i=0; i<num_rup_vars; i++) {
	apv_off[i] = 0;
}
int inbin = 0;

int intmem = 0;
long memlen;

int ptol;
int print_tol = 25;

int slip_id, hypo_id;
int generate_seed = 0;

//Default 1 GB
int max_buf_mb = 1*1024;
long long MAX_BUFFER_SIZE;

int timing = 0;
struct timeval tv_start, tv_end;

sname[0] = '\0';

sgtextract.xfile[0] = '\0';
sgtextract.yfile[0] = '\0';
sgtextract.zfile[0] = '\0';

sgtextract.xfp = NULL;
//sgtextract.xfdr = -1;
sgtextract.yfp = NULL;
//sgtextract.yfdr = -1;
sgtextract.zfp = NULL;
//sgtextract.zfdr = -1;

sprintf(outdir,".");
sprintf(sgtdir,".");

getpar("max_buf_mb","d",&max_buf_mb);
MAX_BUFFER_SIZE = 1024*(long long)1024*max_buf_mb;

//We need this to be required, because we need the dt argument to pass to rupgen_genslip
mstpar("dtout","f",&dtout);
getpar("tstart","f",&tstart);
getpar("sname","s",sname);

//If we specify target_hypo_spacing, then pass it through to rupgen_genslip
float target_hypo_spacing = -1.0;
getpar("target_hypo_spacing", "f", &target_hypo_spacing);
//AR: "nrup_min" is the minimum number of rupture variations that get generated (genslip
//AR: itself already had this setting, default 10). DirectSynth just never let you change it
//AR: before, so I added that. If nothing is given, it stays at -1, which means "use the
//AR: default", same idea as target_hypo_spacing right above.
int nrup_min = -1;
getpar("nrup_min", "d", &nrup_min);
float rvfrac = -1.0;
getpar("rvfrac", "f", &rvfrac);

//AR: New optional parameter "velfile", the path to a 1D velocity model used to generate the
//AR: ruptures. If not given, genslip keeps using its own built-in generic model, same as
//AR: before. I made this buffer 512 bytes, not 256, so it comfortably fits long GPFS paths,
//AR: since the function that reads it in doesn't check how long the path is.
char velfile_1d[512];
velfile_1d[0] = '\0';
getpar("velfile", "s", velfile_1d);

rg_stats_t stats;
/* AR: memcached is not available on MN5, so I disable it at compile time with -DBSC_DISABLE_MEMCACHED */
#ifndef BSC_DISABLE_MEMCACHED
set_memcached_server("127.0.0.1");
#endif

char rupture_spacing_string[20];
int rupture_spacing;
sprintf(rupture_spacing_string, "random");
getpar("rupture_spacing", "s", rupture_spacing_string);
if (strcmp(rupture_spacing_string,"random")==0) {
        rupture_spacing = RUPGEN_RANDOM_HYPO;
        printf("Using random spacing.\n");
} else if (strcmp(rupture_spacing_string,"uniform")==0) {
        rupture_spacing = RUPGEN_UNIFORM_HYPO;
        printf("Using uniform spacing.\n");
} else {
        fprintf(stderr, "rupture_spacing argument %s must be one of 'random' or 'uniform', aborting.", rupture_spacing_string);
        exit(5);
}
if (debug) {
	gettimeofday(&tv_start, NULL);
}

getpar("generate_seed", "d", &generate_seed);
char params[512];
params[0] = '\0';
int params_flag = 0;
if (target_hypo_spacing>0.0) {
	params_flag = 1;
    sprintf(params, "target_hypo_spacing=%f", target_hypo_spacing);
	/*if (generate_seed==0) {
		//Overwrite with a specific seed, if we're not providing this later
		strcat(params, " seed=6000011");
	}*/
}

//AR: Same on/off idea as target_hypo_spacing right above, it's only added to the params
//AR: string genslip receives when a value is given.
if (nrup_min>=0) {
	if (params_flag==0) {
		params_flag = 1;
		sprintf(params, "nrup_min=%d", nrup_min);
	} else {
		sprintf(params, "%s nrup_min=%d", params, nrup_min);
	}
}
#ifndef _V3_3_1
if (generate_seed==1) {
	params_flag = 1;
	int seed = rupgen_get_rupture_seed(header->source_id, header->rupture_id);
	sprintf(params, "%s seed=%d", params, seed);
}
#endif

if (rvfrac>0.0) {
	params_flag = 1;
	sprintf(params, "%s rvfrac=%f", params, rvfrac);
}

//AR: If a velfile was given, I add it here once, before the loop, same as
//AR: target_hypo_spacing/rvfrac above. It carries through to every rupture-generation call
//AR: inside the loop since params already includes it.
if (velfile_1d[0] != '\0') {
	params_flag = 1;
	sprintf(params, "%s velfile=%s", params, velfile_1d);
}

//AR: I added this to time write_srf() on its own, separate from the bigger timer below that
//AR: times the whole "Generating N variations" loop. I wanted to know if writing each .srf
//AR: file (about 2.3MB, one per rupture variation) is actually slow. I add up the time for
//AR: all of them and print ONE number at the end, not one print per file, so I don't slow
//AR: things down with extra printing.
struct timeval srf_t0, srf_t1;
double srf_write_total_sec = 0.0;
int write_srf_files = 0;
getpar("write_srf","d",&write_srf_files);

//Generate all ruptures
for (i=0; i<num_rup_vars; i++) {
	//printf("Generating rupture from file %s, slip %d, hypo %d, spacing %d, dtout %f\n", rup_geom_file, rup_vars[i].slip_id, rup_vars[i].hypo_id, rupture_spacing, dtout);
	if (num_rv_infos>0 && rvinfo_array!=NULL) {
		//Look for seed and rvfrac in rvinfo structure
		int seed = find_seed(header->source_id, header->rupture_id, rup_vars[i].rup_var_id, num_rv_infos, rvinfo_array);
		float rvfrac = find_rvfrac(header->source_id, header->rupture_id, rup_vars[i].rup_var_id, num_rv_infos, rvinfo_array);
		params_flag = 1;
		sprintf(params, "%s seed=%d rvfrac=%f use_unmodified_seed=1", params, seed, rvfrac);
		if (debug) {
			char buf[256];
			sprintf(buf, "Using passed-in seed=%d, rvfrac=%f for src %d, rup %d, rv %d", seed, rvfrac, header->source_id, header->rupture_id, rup_vars[i].rup_var_id);
			write_log(buf);
		}
	}
	if (params_flag==1) {
		rupgen_genslip_with_params(rup_geom_file, rup_vars[i].slip_id, rup_vars[i].hypo_id, &stats, &(srf[i]), rupture_spacing, dtout, params);
	} else {
		rupgen_genslip(rup_geom_file, rup_vars[i].slip_id, rup_vars[i].hypo_id, &stats, &(srf[i]), rupture_spacing, dtout);
	}
	/* AR: (2026-10-01) The .srf file of each variation is written only when asked for with
	   write_srf=1. SCEC's own code had these lines commented out, I had turned them on for
	   every run thinking later steps needed the files, but nothing reads them. They take
	   about 24 MB per variation (430 GB for PAS, about 5 TB for SBSM). The default 0 is
	   SCEC's behaviour, 1 is useful to check the slip of a variation by hand. */
	if (write_srf_files) {
		char srf_out[512];
		sprintf(srf_out, "%s.%d.srf", rup_geom_file, rup_vars[i].rup_var_id);
		if (debug) gettimeofday(&srf_t0, NULL);
		write_srf(&(srf[i]), srf_out, 0);
		if (debug) {
			gettimeofday(&srf_t1, NULL);
			srf_write_total_sec += (srf_t1.tv_sec - srf_t0.tv_sec) + (srf_t1.tv_usec - srf_t0.tv_usec) / 1e6;
		}
	}
}
if (debug) {
	gettimeofday(&tv_end, NULL);
	char buf[256];
	double total_sec = (tv_end.tv_sec - tv_start.tv_sec) + (tv_end.tv_usec - tv_start.tv_usec) / 1e6;
	sprintf(buf, "Generating %d variations took %.3f sec (of which write_srf: %.3f sec, %.1f%%).\n",
		num_rup_vars, total_sec, srf_write_total_sec, total_sec > 0 ? 100.0*srf_write_total_sec/total_sec : 0.0);
	write_log(buf);
}

//write_srf(&srf, "out.srf", 0);

for (i=0; i<num_rup_vars; i++) {
	prect_ptr[i] = &(srf[i]).srf_prect;
	prseg_ptr[i] = prect_ptr[i]->prectseg;
	apnts_ptr[i] = &(srf[i]).srf_apnts;
	apval_ptr[i] = apnts_ptr[i]->apntvals;

	apv_off[i] = 0;
}

int num_comps = 0;
if (sgtfilepar.xfile[0]!='\0') {
	num_comps++;
}
if (sgtfilepar.yfile[0]!='\0') {
        num_comps++;
}
if (sgtfilepar.zfile[0]!='\0') {
        num_comps++;
}


fprintf(stdout,"Constructing synthetic this rupture\n");

long current_offset = 0;
memlen = sizeof(struct sgtmaster) + (sgtmast.globnp)*(sizeof(struct sgtindex) + sizeof(struct sgtheader) + 18*(long long)(sgtmast.nt)*sizeof(float));

fprintf(stdout,"Total memory for SGTs= %.2f Mb\n",memlen*1.0e-06);

//sgthead = (struct sgtheader *) check_malloc ((sgtmast.globnp)*sizeof(struct sgtheader));
long long sgtbufsize = 18*(long long)sgtmast.globnp*sgtmast.nt*sizeof(float);

//Need to consider buffer size too
int max_points_per_request = MAX_BUFFER_SIZE/(18/3*4*sgtmast.nt*sizeof(float)+sizeof(struct sgtheader));

gfmech = NULL;
space = NULL;
seis = NULL;
subseis = NULL;
stf = NULL;
int sgt_used_offset[4];
int* srf_indices_to_process = check_malloc(srf[0].srf_apnts.np*sizeof(int));
int num_srf_pts_found = 0;
char* srf_pts_used = check_malloc(srf[0].srf_apnts.np*sizeof(char));
int* srf_indices_split = 0;
struct sgtheader* sgt_heads_split = 0;
float* sgtbuf_split = 0;
int num_srfs_split = 0;
int num_data_pts_split = 0;
memset(srf_pts_used, 0, srf[0].srf_apnts.np);

//AR: New (2026-09-06). I build the list of active handlers (the ones with points for this
//AR: task) first, and split the share of each one into batches of at most
//AR: max_points_per_request points. It is the same batch math as the original loop, only done
//AR: for every handler before starting instead of one at a time. So far the share of every
//AR: handler has always fit into one batch (I checked it on production data), so
//AR: extra_batch_count is normally 0. It is there to stay correct if that ever changes.
int h;
int num_active = 0;
for (h=0; h<num_sgt_handlers; h++) {
	if (num_sgts_by_handler[h]>0) num_active++;
}

int* handler_ids = check_malloc(sizeof(int)*num_active);
int* req_start = check_malloc(sizeof(int)*num_active);
int* req_end = check_malloc(sizeof(int)*num_active);
//AR: Leftover batches (2nd and later, for a handler whose own share didn't fit in one
//AR: request) go here, and I fetch them the old way, one at a time, after the async round.
//AR: Every share measured so far fits in one batch, so this list should stay empty.
int* extra_handler_ids = check_malloc(sizeof(int)*num_sgt_handlers);
int* extra_start = check_malloc(sizeof(int)*num_sgt_handlers);
int* extra_end = check_malloc(sizeof(int)*num_sgt_handlers);
int num_extra = 0;

int k = 0;
for (h=0; h<num_sgt_handlers; h++) {
	if (num_sgts_by_handler[h]<=0) continue;
	handler_ids[k] = h;
	req_start[k] = 0;
	int end = max_points_per_request;
	if (end>num_sgts_by_handler[h]) end = num_sgts_by_handler[h];
	req_end[k] = end;
	k++;
	//Any leftover beyond the first batch for this handler
	int leftover_start = end;
	while (leftover_start<num_sgts_by_handler[h]) {
		int leftover_end = leftover_start+max_points_per_request;
		if (leftover_end>num_sgts_by_handler[h]) leftover_end = num_sgts_by_handler[h];
		extra_handler_ids[num_extra] = h;
		extra_start[num_extra] = leftover_start;
		extra_end[num_extra] = leftover_end;
		num_extra++;
		leftover_start = leftover_end;
	}
}

struct sgtheader** sgthead_h = check_malloc(sizeof(struct sgtheader*)*num_active);
float** sgtbuf_h = check_malloc(sizeof(float*)*num_active);

//AR: This is the change. Every handler in the list above gets asked at the same time here,
//AR: instead of one at a time inside a loop like the original code did.
if (num_active>0) {
	request_sgt_all(sgthead_h, sgtbuf_h, num_comps, num_active, handler_ids, req_start, req_end, sgts_by_handler, sgtmast.nt, dtout, my_id);
}

//AR: From here down, this is the SAME matching-and-compute logic the original while(1) loop
//AR: had for each handler, just run once per already-fetched handler instead of triggering
//AR: a fresh blocking fetch on every iteration. Nothing about how a point gets matched to
//AR: its SGT data, or how the seismogram gets computed, changed.
for (k=0; k<num_active; k++) {
	sgthead = sgthead_h[k];
	sgtbuf = sgtbuf_h[k];
	int request_from_handler_id = handler_ids[k];
	int starting_index = req_start[k];
	int ending_index = req_end[k];

	memset(srf_indices_to_process, 0, srf[0].srf_apnts.np);

	num_srf_pts_found = 0;
	if (debug) write_log("Processing SGTs from the request.");
	for (ip=0; ip<srf[0].srf_apnts.np; ip++) {
		if (srf_pts_used[ip]==0) {
			for (i=0; i<4; i++) {
				sgt_used_offset[i] = -1;
			}
			int num_found = 0;
			for (i=0; i<sgtparms[ip].nsgt; i++) {
				if (sgtparms[ip].indx[i]>=sgts_by_handler[request_from_handler_id][starting_index] && sgtparms[ip].indx[i]<=sgts_by_handler[request_from_handler_id][ending_index-1]) {
					num_found++;
					int start_index = starting_index;
					int end_index = ending_index;
					int mid_index = (start_index + end_index) / 2;
					while(end_index>=start_index) {
						mid_index = (start_index + end_index) / 2;
						if (sgtparms[ip].indx[i]>sgts_by_handler[request_from_handler_id][mid_index]) {
							start_index = mid_index+1;
						} else if (sgtparms[ip].indx[i]<sgts_by_handler[request_from_handler_id][mid_index]) {
							end_index = mid_index-1;
						} else {
							break;
						}
					}
					sgt_used_offset[i] = mid_index - starting_index;
				}
			}
			if (num_found==sgtparms[ip].nsgt) {
				srf_pts_used[ip] = 1;
				srf_indices_to_process[num_srf_pts_found] = ip;
				num_srf_pts_found++;
				for (i=0; i<sgtparms[ip].nsgt; i++) {
					int offset = sgt_used_offset[i];
					sgtparms[ip].sgt_head_ptrs[i] = sgthead+offset;
					sgtparms[ip].sgtbuf_ptrs[i] = sgtbuf+(long long)18*sgtmast.nt*offset;
				}
			} else if (num_found>0) {
				fprintf(stderr, "SRF point %d is split, saving for later.\n", ip);
				int found = 0;
				for (i=0; i<num_srfs_split; i++) {
					if (srf_indices_split[i]==ip) {
						found = 1;
						break;
					}
				}
				if (!found) {
					srf_indices_split = check_realloc(srf_indices_split, sizeof(int)*(num_srfs_split+1));
					srf_indices_split[num_srfs_split] = ip;
					num_srfs_split++;
				}

				sgt_heads_split = check_realloc(sgt_heads_split, sizeof(struct sgtheader)*(long long)(num_data_pts_split+num_found));
				sgtbuf_split = check_realloc(sgtbuf_split, sizeof(float)*(long long)(num_data_pts_split+num_found)*18*sgtmast.nt);

				for (i=0; i<sgtparms[ip].nsgt; i++) {
					if (sgt_used_offset[i]!=-1) {
						memcpy(sgt_heads_split+num_data_pts_split, sgthead+sgt_used_offset[i], sizeof(struct sgtheader));
						memcpy(sgtbuf_split+num_data_pts_split*18*sgtmast.nt, sgtbuf+(long long)sgt_used_offset[i]*18*sgtmast.nt, 18*sgtmast.nt*sizeof(float));
						num_data_pts_split++;
					}
				}
			}
		}
	}

	if (gfmech==NULL) {
		maxnt = sgthead[0].nt;
		mindt = sgthead[0].dt;

		if(dtout < 0.0)
			dtout = mindt;

		if(dtout < mindt)
			maxnt = (maxnt*mindt/dtout);

		ntsum = 2;
		while(ntsum < 4*maxnt)
			ntsum = ntsum*2;

		if(ntout < 0)
			ntout = ntsum;

		header->dt = dtout;
		header->nt = ntout;

		maxmech = 3;
		mechpar.nmech = 1;
		mechpar.flag[0] = U1FLAG;
		mechpar.flag[1] = 0;
		mechpar.flag[2] = 0;

		gfmech = check_malloc(sizeof(float*) * num_rup_vars);
		space = check_malloc(sizeof(float*) * num_rup_vars);
		seis = check_malloc(sizeof(float*) * num_rup_vars);
		subseis = check_malloc(sizeof(float*) * num_rup_vars);
		stf = check_malloc(sizeof(float*) * num_rup_vars);

		for (i=0; i<num_rup_vars; i++) {
			gfmech[i] = (float *) check_malloc (maxmech*12*ntsum*sizeof(float));
			space[i] = (float *) check_malloc (2*ntsum*sizeof(float));

			seis[i] = (float *) check_malloc (3*ntout*sizeof(float));
			subseis[i] = (float *) check_malloc (maxmech*3*ntout*sizeof(float));
			stf[i] = (float *) check_malloc (ntout*sizeof(float));

			zapit(seis[i],3*ntout);
			ptol = print_tol;
			tmom[i] = 0.0;
		}
	}

	for(ip=0; ip<num_srf_pts_found; ip++) {
		int srf_index = srf_indices_to_process[ip];
		int j;
		for (i=0; i<num_rup_vars; i++) {
			zapit(subseis[i],maxmech*3*ntout);
			get_srfpars(&(srf[i]),apv_off[i],srf_index,&rt,&vslip,&mechpar.stk,&mechpar.dip,&mechpar.rak,&mechpar, my_id);
			scale = slip_conv*apval_ptr[i][srf_index].area;
			mech_sgt(gfmech[i],&sgtparms[srf_index],ntsum,mechpar,&scale);
			tmom[i] = tmom[i] + vslip*scale;
			sum_sgt(subseis[i],ntout,gfmech[i],&sgtparms[srf_index],ntsum,&rt,&tstart,mechpar, srf_index);
			srf_stf(&(srf[i]),apv_off[i],srf_index,seis[i],subseis[i],stf[i],ntout,&dtout,mechpar,space[i]);
		}
	}

	free(sgthead_h[k]);
	free(sgtbuf_h[k]);
}
free(sgthead_h);
free(sgtbuf_h);
free(handler_ids);
free(req_start);
free(req_end);

//AR: The rare leftover batches described above, fetched the old way, one at a time. It has
//AR: never run on the data so far, and I keep it only to stay correct in a case not seen yet.
if (num_extra>0) {
	sgtbuf = check_malloc((long long)18*max_points_per_request*sgtmast.nt*sizeof(float));
	sgthead = check_malloc(max_points_per_request*sizeof(struct sgtheader));
	int e;
	for (e=0; e<num_extra; e++) {
		int request_from_handler_id = extra_handler_ids[e];
		int starting_index = extra_start[e];
		int ending_index = extra_end[e];
		request_sgt(sgthead, sgtbuf, num_comps, request_from_handler_id, sgts_by_handler, starting_index, ending_index, sgtmast.nt, my_id);

		memset(srf_indices_to_process, 0, srf[0].srf_apnts.np);
		num_srf_pts_found = 0;
		for (ip=0; ip<srf[0].srf_apnts.np; ip++) {
			if (srf_pts_used[ip]==0) {
				for (i=0; i<4; i++) {
					sgt_used_offset[i] = -1;
				}
				int num_found = 0;
				for (i=0; i<sgtparms[ip].nsgt; i++) {
					if (sgtparms[ip].indx[i]>=sgts_by_handler[request_from_handler_id][starting_index] && sgtparms[ip].indx[i]<=sgts_by_handler[request_from_handler_id][ending_index-1]) {
						num_found++;
						int start_index = starting_index;
						int end_index = ending_index;
						int mid_index = (start_index + end_index) / 2;
						while(end_index>=start_index) {
							mid_index = (start_index + end_index) / 2;
							if (sgtparms[ip].indx[i]>sgts_by_handler[request_from_handler_id][mid_index]) {
								start_index = mid_index+1;
							} else if (sgtparms[ip].indx[i]<sgts_by_handler[request_from_handler_id][mid_index]) {
								end_index = mid_index-1;
							} else {
								break;
							}
						}
						sgt_used_offset[i] = mid_index - starting_index;
					}
				}
				if (num_found==sgtparms[ip].nsgt) {
					srf_pts_used[ip] = 1;
					srf_indices_to_process[num_srf_pts_found] = ip;
					num_srf_pts_found++;
					for (i=0; i<sgtparms[ip].nsgt; i++) {
						int offset = sgt_used_offset[i];
						sgtparms[ip].sgt_head_ptrs[i] = sgthead+offset;
						sgtparms[ip].sgtbuf_ptrs[i] = sgtbuf+(long long)18*sgtmast.nt*offset;
					}
				} else if (num_found>0) {
					int found = 0;
					for (i=0; i<num_srfs_split; i++) {
						if (srf_indices_split[i]==ip) {
							found = 1;
							break;
						}
					}
					if (!found) {
						srf_indices_split = check_realloc(srf_indices_split, sizeof(int)*(num_srfs_split+1));
						srf_indices_split[num_srfs_split] = ip;
						num_srfs_split++;
					}
					sgt_heads_split = check_realloc(sgt_heads_split, sizeof(struct sgtheader)*(long long)(num_data_pts_split+num_found));
					sgtbuf_split = check_realloc(sgtbuf_split, sizeof(float)*(long long)(num_data_pts_split+num_found)*18*sgtmast.nt);
					for (i=0; i<sgtparms[ip].nsgt; i++) {
						if (sgt_used_offset[i]!=-1) {
							memcpy(sgt_heads_split+num_data_pts_split, sgthead+sgt_used_offset[i], sizeof(struct sgtheader));
							memcpy(sgtbuf_split+num_data_pts_split*18*sgtmast.nt, sgtbuf+(long long)sgt_used_offset[i]*18*sgtmast.nt, 18*sgtmast.nt*sizeof(float));
							num_data_pts_split++;
						}
					}
				}
			}
		}
		for(ip=0; ip<num_srf_pts_found; ip++) {
			int srf_index = srf_indices_to_process[ip];
			int j;
			for (i=0; i<num_rup_vars; i++) {
				zapit(subseis[i],maxmech*3*ntout);
				get_srfpars(&(srf[i]),apv_off[i],srf_index,&rt,&vslip,&mechpar.stk,&mechpar.dip,&mechpar.rak,&mechpar, my_id);
				scale = slip_conv*apval_ptr[i][srf_index].area;
				mech_sgt(gfmech[i],&sgtparms[srf_index],ntsum,mechpar,&scale);
				tmom[i] = tmom[i] + vslip*scale;
				sum_sgt(subseis[i],ntout,gfmech[i],&sgtparms[srf_index],ntsum,&rt,&tstart,mechpar, srf_index);
				srf_stf(&(srf[i]),apv_off[i],srf_index,seis[i],subseis[i],stf[i],ntout,&dtout,mechpar,space[i]);
			}
		}
	}
	free(sgtbuf);
	free(sgthead);
}
free(extra_handler_ids);
free(extra_start);
free(extra_end);

//Once we've finished processing every handler, iterate over all the points we rolled over
num_srf_pts_found = 0;
for (ip=0; ip<num_srfs_split; ip++) {
	int srf_index = srf_indices_split[ip];
	srf_pts_used[srf_index] = 1;
	for (i=0; i<4; i++) {
		sgt_used_offset[i] = -1;
	}
	//Find these points - can't assume any order in the split buffers, so linear search
	//The split buffers should be very small anyway
	for (i=0; i<sgtparms[srf_index].nsgt; i++) {
		//populate sgt_used_offset
		for (j=0; j<num_data_pts_split; j++) {
			if (sgt_heads_split[j].indx == sgtparms[srf_index].indx[i]) {
				sgt_used_offset[i] = j;
				break;
			}
		}
		if (sgt_used_offset[i]==-1) {
			fprintf(stderr, "Error:  Couldn't find SGT indx %ld in the split list, aborting.\n", sgtparms[srf_index].indx[i]);
			exit(-2);
		}
		//set up pointers in sgtparams
		sgtparms[srf_index].sgt_head_ptrs[i] = sgt_heads_split + sgt_used_offset[i];
		sgtparms[srf_index].sgtbuf_ptrs[i] = sgtbuf_split + sgt_used_offset[i]*18*sgtmast.nt;
	}
	//process this point
	for (i=0; i<num_rup_vars; i++) {
		zapit(subseis[i],maxmech*3*ntout);

		get_srfpars(&(srf[i]),apv_off[i],srf_index,&rt,&vslip,&mechpar.stk,&mechpar.dip,&mechpar.rak,&mechpar,my_id);
		scale = slip_conv*apval_ptr[i][srf_index].area;

		mech_sgt(gfmech[i],&sgtparms[srf_index],ntsum,mechpar,&scale);
		tmom[i] = tmom[i] + vslip*scale;

		sum_sgt(subseis[i],ntout,gfmech[i],&sgtparms[srf_index],ntsum,&rt,&tstart,mechpar);
		srf_stf(&(srf[i]),apv_off[i],srf_index,seis[i],subseis[i],stf[i],ntout,&dtout,mechpar,space[i]);
	}
}
int all_points_processed = 1;
free(srf_indices_to_process);
free(srf_pts_used);

free(srf_indices_split);
free(sgt_heads_split);
free(sgtbuf_split);

for (i=0; i<num_rup_vars; i++) {
	free(gfmech[i]);
	free(space[i]);
	free(subseis[i]);
	free(stf[i]);
}
free(gfmech);
free(space);
free(subseis);
free(stf);

//Accumulate data for writing in single buffer
char* writing_buffer = check_malloc((sizeof(struct seisheader) + sizeof(float)*3*ntout)*num_rup_vars);
int entry_size = 0;
int offset = 0;

if (sgtfilepar.xfile[0]!='\0') {
	if (sgtfilepar.yfile[0]!='\0') {
		if (sgtfilepar.zfile[0]!='\0') { //x,y,z
			header->comps = (X_COMP_FLAG | Y_COMP_FLAG | Z_COMP_FLAG);
			entry_size = 3*sizeof(float)*ntout;
		} else { //x,y
			header->comps = X_COMP_FLAG | Y_COMP_FLAG;
			entry_size = 2*sizeof(float)*ntout;
			offset = ntout;
		}
	}
	else if (sgtfilepar.zfile[0]!='\0') { //x,z
                header->comps = X_COMP_FLAG | Z_COMP_FLAG;
    			entry_size = 2*sizeof(float)*ntout;
	} else { //x
        		header->comps = X_COMP_FLAG;
        		offset = ntout;
	}
} else if (sgtfilepar.yfile[0]!='\0') {
	if (sgtfilepar.zfile[0]!='\0') { //y,z
		printf("Can't output merged Y and Z components.\n");
		exit(2);
	} else { //y
		header->comps = Y_COMP_FLAG;
		entry_size = sizeof(float)*ntout;
		offset = 2*ntout;
	}
} else if (sgtfilepar.zfile[0]!='\0') { //z
	header->comps = Z_COMP_FLAG;
	entry_size = sizeof(float)*ntout;
}

for (i=0; i<num_rup_vars; i++) {
	header->rup_var_id = rup_vars[i].rup_var_id;
	memcpy(writing_buffer+(sizeof(struct seisheader)+entry_size)*i, header, sizeof(struct seisheader));
	memcpy(writing_buffer+(sizeof(struct seisheader)+entry_size)*i + sizeof(struct seisheader), seis[i]+offset, entry_size);
	(*seis_return)[i] = seis[i]+offset;
	if (debug) {
		char buf[256];
		sprintf(buf, "Copying %d bytes from offset %d to offset %d", entry_size, offset, (sizeof(struct seisheader)+entry_size)*i);
		write_log(buf);
	}
}

//Add 1 to ending rup_var_id because it's exclusive
send_data_cluster(seis_file, header->source_id, header->rupture_id, rup_vars[0].rup_var_id, rup_vars[num_rup_vars-1].rup_var_id+1, writing_buffer, (sizeof(struct seisheader)+entry_size)*num_rup_vars, my_id);

free(writing_buffer);

fprintf(stdout,"Total moment= %13.5e\n",tmom);

for (i=0; i<num_rup_vars; i++) {
	free_srf_ptrs(&(srf[i]));
}
free(srf);
free(prect_ptr);
free(prseg_ptr);
free(apnts_ptr);
free(tmom);
free(apv_off);

return seis;
}
