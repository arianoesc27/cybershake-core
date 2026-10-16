#!/bin/csh

if ($# != 16 && $# != 17) then
  echo "Usage:  single_exe.csh <site> <model_cords file> <nx> <ny> <nz> <CS_PATH> <SCRATCH_PATH> <LOG_ROOT> <MODELS> <MPI_CMD> <JOB_ID> <format> <surface_cvm_depth> <ely_taper> <taper_depth> <taper models> [min vs]"
  exit 1
endif

set SITE = $1
set MODELCORDS = $2
set NX = $3
set NY = $4
set NZ = $5
set CS_PATH = $6
set SCRATCH_PATH = $7
set LOG_ROOT = $8
set MODELS = $9
set MPI_CMD = $10
set JOB_ID = $11
set FORMAT = $12
set SURF_CVM_DEPTH = $13
set ELY_TAPER = $14
set TAPER_DEPTH = $15
set TAPER_MODELS = $16

if ($# == 17) then
	set VSMIN = $17
	if ($VSMIN == 900.0) then
		set VPMIN = 1800.0
		set DENMIN = 2000.0
	else
		set VPMIN = `echo "3.4 * $VSMIN" | bc`
		set DENMIN = $VPMIN
	endif
else
	set VSMIN = 500.0
	set VPMIN = 1700.0
	set DENMIN = 1700.0
endif

echo "VSMIN=$VSMIN, VPMIN=$VPMIN, DENMIN=$DENMIN"

#set BPATH = `pwd`
set BPATH = ${CS_PATH}/UCVM
set BPROG = bin/ucvm-single-mpi
set MODELDIR = ${BPATH}/DataFiles

#set OUTDIR = ../../data/LayerBin/${SITE}
set OUTDIR = `pwd`
set SCRATCH_OUTDIR = ${SCRATCH_PATH}/job-${JOB_ID}/data/LayerBin
set MAIN_RUNDIR = ./

umask 002

mkdir -p ${OUTDIR}
#\mkdir -p ${SCRATCH_OUTDIR}
#ls -lt ${SCRATCH_OUTDIR}

#set FILEROOT = v4_sgt
set FILEROOT = v_sgt-${SITE}
if ($FORMAT == "awp") then
	set FILEROOT = awp.${SITE}.media
endif
#set LOGDIR = ../../logs/GenLog/${SITE}
set LOGDIR = ${LOG_ROOT}/GenLog/${SITE}

#set MODELCORDS = ../../data/ModelParams/${SITE}/model_coords_GC_${SITE}
#set DEPFILE = ../../data/ModelParams/${SITE}/zdepths.meter
set DEPFILE = zdepths.meter

if ($MPI_CMD == "mpirun") then
	set NP = `wc -l < ${PBS_NODEFILE}`
	set MPI_CMD = "${MPI_CMD} -np ${NP} -machinefile ${PBS_NODEFILE}"
else if ($MPI_CMD == "aprun") then
	@ NP = $PBS_NUM_NODES * 8
	#if ($FORMAT == "rwg") then
	#	while (`expr $NY \* $NZ % $NP` != 0)
	#		@ NP = $NP - 1
	#	end
	#else if ($FORMAT == "awp") then
	#	while (`expr $NX \* $NZ % $NP` != 0)
	#		@ NP = $NP - 1
	#	end
	#endif
	if ${MODELS} == "cvmh" then
	        set MPI_CMD = "${MPI_CMD} -n ${NP}"
	else
	        set MPI_CMD = "${MPI_CMD} -n ${NP}"
	endif
	set MPI_CMD = "${MPI_CMD} -S 4"
else if ($MPI_CMD == "jsrun") then
	#We're running on Summit
	#LSB_DJOB_NUMPROC is 42*num_nodes + 1
	@ NP = $LSB_DJOB_NUMPROC - 1
	@ NNODES = $NP / 42
	@ NUM_RESOURCE_SETS = $NNODES * 42
	#-a 1: 1 MPI tasks per core
	#-c 1: 1 core per resource set
	#-r 42: 42 resource sets per node
	set MPI_CMD = "${MPI_CMD} -n $NUM_RESOURCE_SETS -a 1 -c 1 -r 42 -l CPU-CPU"
	echo "LSB_DJOB_NUMPROC=$LSB_DJOB_NUMPROC, NP=$NP, NNODES=$NNODES, NUM_RESOURCE_SETS=$NUM_RESOURCE_SETS"
else if ($MPI_CMD == 'srun') then
	#Running on Frontier
	echo "SLURM_JOB_NUM_NODES = $SLURM_JOB_NUM_NODES"
	echo "SLURM_CPUS_ON_NODE = $SLURM_CPUS_ON_NODE"
	@ NNODES = $SLURM_JOB_NUM_NODES
	#AR: This used to always use the whole node's total CPU count (112 on MN5), ignoring
	#AR: whatever number of tasks per node the job actually asked for. On MN5, trying to
	#AR: run that many processes on one node at once crashes. Now, if the job specified how
	#AR: many tasks per node it wants, I use that number instead, and only fall back to the
	#AR: whole node's CPU count when nothing specific was requested.
	if ($?SLURM_NTASKS_PER_NODE) then
		@ TASKS_PER_NODE = $SLURM_NTASKS_PER_NODE
	else
		@ TASKS_PER_NODE = $SLURM_CPUS_ON_NODE
	endif
	@ NP = $NNODES * $TASKS_PER_NODE
	set MPI_CMD = "${MPI_CMD} -N $NNODES -n $NP -c 1 --ntasks-per-node $TASKS_PER_NODE"
	#AR: When this script runs inside the workflow's job, which asks for two different
	#AR: kinds of nodes together (regular and GPU nodes), a plain srun call here fails,
	#AR: since it needs to be told which group of nodes to actually use. I first tried
	#AR: reading that group number from a SLURM variable directly, but that did not work,
	#AR: since the worker process running this script does not receive that variable the
	#AR: same way a normal job script would. Instead, I set my own variable
	#AR: (UCVM_MESH_HET_GROUP) explicitly from the Python code that calls this script,
	#AR: since it controls the environment directly there, so this doesn't depend on
	#AR: guessing what gets passed through. This variable stays unset for the standalone
	#AR: test script, so it isn't affected.
	if ($?UCVM_MESH_HET_GROUP) then
		set MPI_CMD = "${MPI_CMD} --het-group=${UCVM_MESH_HET_GROUP}"
	endif
endif

#set VPMIN = 1700.0
#set VSMIN = 500.0
#set DENMIN = 1700.0

#Add to LD_LIBRARY_PATH
#setenv LD_LIBRARY_PATH /work/00940/tera3d/CyberShake/software/UCVM/ucvm_12.2.0/lib:/work/00940/tera3d/CyberShake/software/UCVM/cvmh_11.9.1/lib:/work/00940/tera3d/CyberShake/software/UCVM/cvms/lib:/work/00940/tera3d/CyberShake/software/UCVM/euclid3-1.3/libsrc:/work/00940/tera3d/CyberShake/software/UCVM/proj_4.7.0/lib

#AR: This used to point at a different cluster's UCVM install. I point it at my own UCVM
#AR: install on MN5 instead.
set UCVM_HOME = /gpfs/projects/bsc115/bsc203748/software/ucvm/install
#AR: One of the models (cvmsi) reads this specific variable itself to find its own data
#AR: files, it doesn't use the other UCVM config variable for that. Without setting it,
#AR: cvmsi builds the wrong path to its data and fails to start.
setenv UCVM_INSTALL_PATH ${UCVM_HOME}
#AR: This shell gives an error if LD_LIBRARY_PATH was never set at all, for example when
#AR: modules weren't loaded first. I start it as empty text in that case instead of
#AR: crashing.
if (! $?LD_LIBRARY_PATH) setenv LD_LIBRARY_PATH ""
#AR: (2026-10-07) hdf5 and tiff added. The sfcvm model library needs UCVM's own libhdf5, and
#AR: without it here the mesher only started when the caller happened to have that path set.
setenv LD_LIBRARY_PATH ${UCVM_HOME}/lib/euclid3/lib:${UCVM_HOME}/lib/proj/lib:${UCVM_HOME}/lib/hdf5/lib:${UCVM_HOME}/lib/tiff/lib:${UCVM_HOME}/model/cvms/lib:${UCVM_HOME}/model/cvmsi/lib:${UCVM_HOME}/model/cvmh/lib:${UCVM_HOME}/model/sfcvm/lib:$LD_LIBRARY_PATH
setenv PROJ_LIB ${UCVM_HOME}/lib/proj/share/proj
#AR: The program that reads the velocity model needs this variable set, pointing at my
#AR: config file, the same as the other tools in this project that use UCVM.
setenv UCVM_CONF_FILE ${UCVM_HOME}/conf/ucvm.conf

echo $LD_LIBRARY_PATH
#ldd ${BPATH}/${BPROG}

#AR: I removed a module load that only existed on a different cluster and doesn't exist on
#AR: MN5. It isn't needed here anyway.

if (${ELY_TAPER} == "none") then
	#Omit the rest of the taper arguments
	set TAPER_ARGS = ""
else
	set TAPER_ARGS = "ely_transition_depth=${TAPER_DEPTH} ely_taper_models=${TAPER_MODELS}"
endif

echo "${MPI_CMD} ${BPATH}/${BPROG} nx=${NX} ny=${NY} nz=${NZ} cordfile=${MODELCORDS} depfile=${DEPFILE} modeldir=${MODELDIR} outfile=${OUTDIR}/${FILEROOT} models=${MODELS} min_vp=${VPMIN} min_vs=${VSMIN} min_rho=${DENMIN} format=${FORMAT} logdir=${LOGDIR} surface_cvm_depth=${SURF_CVM_DEPTH} ely_taper=${ELY_TAPER} ${TAPER_ARGS}"

set valgrind_path=/lustre/orion/geo156/proj-shared/CyberShake/utils/valgrind_3.22.0/bin/valgrind

${MPI_CMD} ${BPATH}/${BPROG} nx=${NX} ny=${NY} nz=${NZ} cordfile=${MODELCORDS} depfile=${DEPFILE} modeldir=${MODELDIR} outfile=${OUTDIR}/${FILEROOT} models=${MODELS} min_vp=${VPMIN} min_vs=${VSMIN} min_rho=${DENMIN} format=${FORMAT} logdir=${LOGDIR} surface_cvm_depth=${SURF_CVM_DEPTH} ely_taper=${ELY_TAPER} ${TAPER_ARGS}

set RC = $?

if (${RC} == 0) then
  echo "Station= ${SITE}: Done   " `date`
else
  echo "Station= ${SITE}: FAILED WITH CODE ${RC}!   " `date`
  exit 1
endif

if ($FORMAT == "awp" || $FORMAT == "awpz" ) then
	#Move to awp.<site>.media if it's different
	mv ${OUTDIR}/${FILEROOT} ${OUTDIR}/awp.${SITE}.media
endif

#echo "\\cp -r ${SCRATCH_OUTDIR}/* ${OUTDIR}"
#\cp -r ${SCRATCH_OUTDIR}/* ${OUTDIR}

