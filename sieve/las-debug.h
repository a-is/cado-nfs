#ifndef LAS_DEBUG_H_
#define LAS_DEBUG_H_

#include <stdint.h>

#include "las-config.h"
#include "las-types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* When TRACE_K is defined, we are exposing some non trivial stuff.
 * Otherwise this all collapses to no-ops */

#ifdef TRACE_K

extern int test_divisible(where_am_I_ptr w);
extern void trace_per_sq_init(sieve_info_srcptr si);
extern void trace_per_sq_clear(sieve_info_srcptr si);

struct trace_Nx_t { unsigned int N; unsigned int x; };
struct trace_ab_t { int64_t a; uint64_t b; };
struct trace_ij_t { int i; unsigned int j; };

extern struct trace_Nx_t trace_Nx;
extern struct trace_ab_t trace_ab;
extern struct trace_ij_t trace_ij;

extern mpz_t traced_norms[2];

static inline int trace_on_spot_N(unsigned int N) {
    if (trace_Nx.x == UINT_MAX) return 0;
    return N == trace_Nx.N;
}

static inline int trace_on_spot_Nx(unsigned int N, unsigned int x) {
    if (trace_Nx.x == UINT_MAX) return 0;
    return N == trace_Nx.N && x == trace_Nx.x;
}

static inline int trace_on_range_Nx(unsigned int N, unsigned int x0, unsigned int x1) {
    if (trace_Nx.x == UINT_MAX) return 0;
    return N == trace_Nx.N && x0 <= trace_Nx.x && trace_Nx.x < x1;
}

static inline int trace_on_spot_x(unsigned int x) {
    return x == (trace_Nx.N << LOG_BUCKET_REGION) + trace_Nx.x;
}

static inline int trace_on_spot_ab(int64_t a, uint64_t b) {
    return a == trace_ab.a && b == trace_ab.b;
}

static inline int trace_on_spot_ij(int i, unsigned int j) {
    return i == trace_ij.i && j == trace_ij.j;
}

void sieve_decrease_logging(unsigned char *S, const unsigned char logp, where_am_I_ptr w);
void sieve_decrease(unsigned char *S, const unsigned char logp, where_am_I_ptr w);

#else
static inline int test_divisible(where_am_I_ptr w MAYBE_UNUSED) { return 1; }
static inline void trace_per_sq_init(sieve_info_srcptr si MAYBE_UNUSED) { }
static inline void trace_per_sq_clear(sieve_info_srcptr si MAYBE_UNUSED) { }
static inline int trace_on_spot_N(unsigned int N MAYBE_UNUSED) { return 0; }
static inline int trace_on_spot_Nx(unsigned int N MAYBE_UNUSED, unsigned int x MAYBE_UNUSED) { return 0; }
static inline int trace_on_range_Nx(unsigned int N MAYBE_UNUSED, unsigned int x0 MAYBE_UNUSED, unsigned int x1 MAYBE_UNUSED) { return 0; }
static inline int trace_on_spot_x(unsigned int x MAYBE_UNUSED) { return 0; }
static inline int trace_on_spot_ab(int64_t a MAYBE_UNUSED, uint64_t b MAYBE_UNUSED) { return 0; }
static inline int trace_on_spot_ij(int i MAYBE_UNUSED, unsigned int j MAYBE_UNUSED) { return 0; }

#ifdef CHECK_UNDERFLOW
void sieve_decrease_underflow_trap(unsigned char *S, const unsigned char logp, where_am_I_ptr w);
#endif  /* CHECK_UNDERFLOW */

static inline void sieve_decrease(unsigned char *S, const unsigned char logp, where_am_I_ptr w MAYBE_UNUSED)
{
#ifdef CHECK_UNDERFLOW
    if (*S < logp)
    sieve_decrease_underflow_trap(S, logp, w);
#endif  /* CHECK_UNDERFLOW */
    unsigned int s = *S, t = s - logp;

    /* version with conditional affectation: no jump at all */
    *S = (UNLIKELY(t >> 31)) ? 0 : t;

    /* version with 1 jump vhen *S < logp, but *S is not updated. Correct ? */
    /*
    if (LIKELY(!(t >> 31))) *S = t;
    */

    /* version with 2 jumps when *S < logp, *S is updated */
    /*
    if (UNLIKELY(t >> 31)) {
      if (UNLIKELY(s))
	*S = 0;
    } else
      *S = t;
    */
}

#endif  /* TRACE_K */

#ifdef __cplusplus
}
#endif

#endif	/* LAS_DEBUG_H_ */
