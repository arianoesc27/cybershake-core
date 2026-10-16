/* AR: Minimal memcached stubs for systems without libmemcached (e.g. MN5/BSC).
 * AR: Activated by compiling with -DBSC_DISABLE_MEMCACHED (set in Makefile.bsc-mn5).
 * AR: memcached_get always returns NULL, so all callers fall through to the disk-read path.
 * AR: This file is mine, for MareNostrum 5 only. It is not in SCEC's repository. */
#ifndef MEMCACHED_STUB_H
#define MEMCACHED_STUB_H
#include <stddef.h>
#include <stdint.h>

typedef int memcached_return;
typedef int memcached_st;
typedef int memcached_server_st;

#define MEMCACHED_SUCCESS 0

static inline memcached_st* memcached_create(void* p) { (void)p; return NULL; }

static inline memcached_server_st* memcached_server_list_append(
    memcached_server_st* s, const char* n, int p, memcached_return* r)
    { (void)s; (void)n; (void)p; (void)r; return NULL; }

static inline memcached_return memcached_server_push(
    memcached_st* m, memcached_server_st* s)
    { (void)m; (void)s; return 0; }

static inline void memcached_server_free(memcached_server_st* s) { (void)s; }

/* AR: Always returns NULL (a cache miss), so the code goes on to read from disk. */
static inline char* memcached_get(
    memcached_st* m, const char* k, size_t kl,
    size_t* vl, uint32_t* f, memcached_return* r)
    { (void)m; (void)k; (void)kl; (void)vl; (void)f; (void)r; return NULL; }

static inline memcached_return memcached_set(
    memcached_st* m, const char* k, size_t kl,
    const char* v, size_t vl, unsigned int t, uint32_t f)
    { (void)m; (void)k; (void)kl; (void)v; (void)vl; (void)t; (void)f; return 0; }

static inline const char* memcached_strerror(memcached_st* m, memcached_return r)
    { (void)m; (void)r; return "memcached disabled (BSC_DISABLE_MEMCACHED)"; }

static inline void memcached_free(memcached_st* m) { (void)m; }

#endif /* MEMCACHED_STUB_H */
