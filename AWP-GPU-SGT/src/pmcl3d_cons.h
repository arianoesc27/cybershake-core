#define BLOCK_SIZE_X 2
#define BLOCK_SIZE_Y 2
/* AR: Pass -DBLOCK_SIZE_Z_OVERRIDE=N at compile time to select value for target GPU.
   MN5 H100: use 200 (set in Makefile.bsc-mn5). Upstream default: 252. */
#ifdef BLOCK_SIZE_Z_OVERRIDE
#define BLOCK_SIZE_Z BLOCK_SIZE_Z_OVERRIDE
#else
//#define BLOCK_SIZE_Z 256
//#define BLOCK_SIZE_Z 200
//#define BLOCK_SIZE_Z 288
#define BLOCK_SIZE_Z 252
//#define BLOCK_SIZE_Z 250
//#define BLOCK_SIZE_Z 320
//#define BLOCK_SIZE_Z 400
#endif
#define align 32
#define loop  1 

#define Both  0
#define Left  1
#define Right 2
#define Front 3
#define Back  4

//HIGHEST ORDER OF FILTER is MAXFILT-1
#define MAXFILT 20


/*
 * Intercept CUDA errors. Usage: pass the CUDA
 * library function call to this macro. 
 * For example, CUCHK(cudaMalloc(...));
 * This check will be disabled if the preprocessor macro NDEBUG is defined (same
 * macro that disables assert() )
 */
#ifndef NDEBUG
#define CUCHK(call) {                                                         \
  cudaError_t err = call;                                                     \
  if( cudaSuccess != err) {                                                   \
  fprintf(stderr, "CUDA error in %s:%i %s(): %s.\n",                          \
          __FILE__, __LINE__, __func__, cudaGetErrorString(err) );            \
  fflush(stderr);                                                             \
  exit(EXIT_FAILURE);                                                         \
  }                                                                           \
}                                                                             
#else
#define CUCHK(call) {}
#endif

// intercept MPI errors. Same usage as for CUCHK
#ifndef NDEBUG
#define MPICHK(err) {                                                         \
 if (err != MPI_SUCCESS) {                                                    \
 char error_string[2048];                                                     \
 int length_of_error_string;                                                  \
 MPI_Error_string((err), error_string, &length_of_error_string);              \
 fprintf(stderr, "MPI error: %s:%i %s(): %s\n",                               \
         __FILE__, __LINE__, __func__, error_string);                         \
 MPI_Abort(MPI_COMM_WORLD, err);                                              \
 fflush(stderr);                                                              \
 exit(EXIT_FAILURE);                                                          \
}                                                                             \
}
#else
#define MPICHK(err) {}
#endif
