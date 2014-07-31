#include "cado.h"
/* test_p_2.c is sed- generated from test.c.meta */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <unistd.h>
#include <gmp.h>
#include <time.h>

#include "mpfq_p_2.h"
#include "mpfq_name_K.h"

Kfield K;

#define DO_ONE_TEST(name, CODE)						\
	do {								\
	for(int i = 0 ; i < ntests ; i++) {				\
		do { CODE } while (0);					\
		if (Kcmp(r1, r2) == 0)					\
			continue;					\
		fprintf(stderr, "Test failed [" name "]/%d\n", i);	\
		fprintf(stderr, "Seed is %lu, nb_tests is %d\n", seed, ntests);\
		abort();						\
	}								\
	if (!quiet) fprintf(stderr, "ok - [" name "], %d times\n", ntests);\
        } while (0)

#define DO_ONE_TEST_VEC(name, CODE)					\
	do {								\
	for(int i = 0 ; i < ntests ; i++) {				\
		do { CODE } while (0);					\
		if (Kvec_cmp(v1, v2, deg) == 0)				\
			continue;					\
		fprintf(stderr, "Test failed [" name "]/%d\n", i);	\
		fprintf(stderr, "Seed is %lu, nb_tests is %d\n", seed, ntests);\
		abort();						\
	}								\
	if (!quiet) fprintf(stderr, "ok - [" name "], %d times\n", ntests);\
        } while (0)
#define DO_ONE_TEST_POLY(name, CODE)					\
	do {								\
	for(int i = 0 ; i < ntests ; i++) {				\
		do { CODE } while (0);					\
		if (Kpoly_cmp(p1, p2) == 0)				\
			continue;					\
		fprintf(stderr, "Test failed [" name "]/%d\n", i);	\
		fprintf(stderr, "Seed is %lu, nb_tests is %d\n", seed, ntests);\
		abort();						\
	}								\
	if (!quiet) fprintf(stderr, "ok - [" name "], %d times\n", ntests);\
        } while (0)


void usage() {
    fprintf(stderr, "usage: ./test [-q] [-N <nb_loops>] [-n <nb_tests>] [-s <seed>]\n");
    fprintf(stderr, "  -N 0 yields an infinite loop\n");
    fprintf(stderr, "  -q means quiet\n");
    exit(1);
}


#ifndef FIX_PRIME
void get_random_prime(mpz_t z, mp_bitcnt_t n, int quiet, gmp_randstate_t rnd) {
    do {
        mpz_urandomb(z, rnd, n);
        if (mpz_sizeinbase(z, 2) != n) continue;
    } while (!mpz_probab_prime_p(z, 5));
    if (!quiet) gmp_fprintf(stderr, "Using prime p = %Zd\n", z);
}
#endif





int main(int argc, char * argv[])
{
    int ntests = 100;
    int nloops = 1;
    int quiet = 0;
    Kelt a0, a1, a2, a3, a4, a5;
    Kelt  r1, r2;
    Kvec  v1, v2;
    Kvec  w1, w2, w3, w4;
    Kpoly  p1, p2;
    Kpoly  q1, q2, q3, q4;
    unsigned long seed = (unsigned long)time(NULL);
    const char * prime_str = NULL;

    while (argc > 1 && argv[1][0] == '-') {
        if (argc > 2 && strcmp(argv[1], "-s") == 0) {
            seed = atol(argv[2]);
            argc -= 2;
            argv += 2;
        } else if (argc > 2 && strcmp(argv[1], "-N") == 0) {
            nloops = atol(argv[2]);
            argc -= 2;
            argv += 2;
        } else if (argc > 2 && strcmp(argv[1], "-n") == 0) {
            ntests = atol(argv[2]);
            argc -= 2;
            argv += 2;
#ifndef FIX_PRIME
        } else if (argc > 2 && strcmp(argv[1], "-p") == 0) {
            prime_str = argv[2];
            argc -= 2;
            argv += 2;
#endif
        } else if (argc > 1 && strcmp(argv[1], "-q") == 0) {
            quiet = 1;
            argc--;
            argv++;
        } else 
            usage();
    }
    if (argc > 1)
        usage();


    if (!quiet) fprintf(stderr, "--- testing for p_2\n");

    int i = 0;
    while ( (nloops == 0) || (i < nloops) ) {
        gmp_randstate_t  rstate;

        Kfield_init();

        if (!quiet)
            fprintf(stderr, "seeding random generator with %lu\n", seed);
        gmp_randinit_mt(rstate);
        gmp_randseed_ui(rstate, seed);

#ifndef CHAR2
#ifndef FIX_PRIME
#ifndef EXTENSION_OF_GFP
        mpz_t p;
        mpz_init(p);
#ifdef  VARIABLE_SIZE_PRIME
        mp_bitcnt_t size_prime= 12*GMP_NUMB_BITS;
#endif
#ifndef VARIABLE_SIZE_PRIME
        mp_bitcnt_t size_prime=Kimpl_max_characteristic_bits();
#endif
        if (prime_str) {
            mpz_set_str(p, prime_str, 0);
        } else {
            get_random_prime(p, size_prime, quiet, rstate);
        }
        Kfield_specify(MPFQ_PRIME_MPZ, p);
        mpz_clear(p);
#endif
#endif
#endif

#ifdef EXTENSION_OF_GFP
        mpz_t p;
        mpz_init(p);
#ifdef  VARIABLE_SIZE_PRIME
        mp_bitcnt_t size_prime= 12*GMP_NUMB_BITS;
#else
        mp_bitcnt_t size_prime=Kimpl_max_characteristic_bits();
#endif
        get_random_prime(p, size_prime, quiet, rstate);
        Kfield_specify(MPFQ_PRIME_MPZ, p);
        mpz_clear(p);

        int extdeg = 5;
        MPFQ_CREATE_FUNCTION_NAME(BFIELD, poly) defpol;
        MPFQ_CREATE_FUNCTION_NAME(BFIELD, poly_init) (K->kbase, defpol, 0);
        MPFQ_CREATE_FUNCTION_NAME(BFIELD, poly_random) (K->kbase, defpol, extdeg, rstate);
        MPFQ_CREATE_FUNCTION_NAME(BFIELD, poly_setcoeff_ui) (K->kbase, defpol, 1, extdeg);
        MPFQ_CREATE_FUNCTION_NAME(BFIELD, poly_setcoeff_ui) (K->kbase, defpol, 1, 0);
        Kfield_specify(MPFQ_POLYNOMIAL, defpol);
#endif


        Kinit(&a0);
        Kinit(&a1);
        Kinit(&a2);
        Kinit(&a3);
        Kinit(&a4);
        Kinit(&a5);
        Kinit (&r1);
        Kinit (&r2);

        /*-----------------------------------------------------------*/
        /*          Common tests                                     */
        /*-----------------------------------------------------------*/

        DO_ONE_TEST("add commutativity", {
                Krandom2 (a0, rstate);
                Krandom2 (a1, rstate);
                Kadd (r1, a0, a1);
                Kadd (r2, a1, a0);
                });

        DO_ONE_TEST("sub = add o neg", {
                Krandom2 (a0, rstate);
                Krandom2 (a1, rstate);
                Ksub (r1, a0, a1);
                Kneg(r2, a1);
                Kadd(r2, r2, a0);
                });

        DO_ONE_TEST("add o sub = id", {
                Krandom2 (a0, rstate);
                Krandom2 (a1, rstate);
                Ksub (r1, a0, a1);
                Kadd (r1, r1, a1);
                Kset (r2, a0);
                });

        DO_ONE_TEST("sub_ui(y) o set_ui(x) == set_mpz(x-y)", {
                mpz_t z;
                unsigned long x = gmp_urandomm_ui(rstate, 32);
                unsigned long y = gmp_urandomm_ui(rstate, 32);
                mpz_init_set_ui(z, x);
                mpz_sub_ui(z, z, y);
                Kset_ui(a1, x);
                Ksub_ui(r1, a1, y);
                Kset_mpz(r2, z);
                mpz_clear(z);
                });

        DO_ONE_TEST("add_ui(y) o neg o set_ui(x) == set_mpz(y-x)", {
                mpz_t z;
                unsigned long x = gmp_urandomm_ui(rstate, 32);
                unsigned long y = gmp_urandomm_ui(rstate, 32);
                mpz_init_set_ui(z, y);
                mpz_sub_ui(z, z, x);
                Kset_ui(a1, x);
                Kneg(a1, a1);
                Kadd_ui(r1, a1, y);
                Kset_mpz(r2, z);
                mpz_clear(z);
                });

        DO_ONE_TEST("add_ui o sub_ui = id", {
                Krandom2 (a0, rstate);
                Krandom2 (a1, rstate);
                unsigned long xx;
                xx = ((unsigned long *)(void *)(a1))[0];
                Ksub_ui (r1, a0, xx);
                Kadd_ui (r1, r1, xx);
                Kset (r2, a0);
                });

        DO_ONE_TEST("add o neg = sub", {
                Krandom2 (a0, rstate);
                Krandom2 (a1, rstate);
                Ksub (r1, a0, a1);
                Kneg (r2, a1);
                Kadd (r2, r2, a0);
                });

        DO_ONE_TEST("mul commutativity", {
                Krandom2 (a0, rstate);
                Krandom2 (a1, rstate);
                Kmul (r1, a0, a1);
                Kmul (r2, a1, a0);
                });

        DO_ONE_TEST("sqr(x) = mul(x,x)", {
                Krandom2 (a0, rstate);
                Kmul (r1, a0, a0);
                Ksqr (r2, a0);
                });

        DO_ONE_TEST("mul distributivity", {
                Krandom2 (a0, rstate);
                Krandom2 (a1, rstate);
                Krandom2 (a2, rstate);
                Kadd (a3, a1, a2);
                Kmul (r1, a0, a3);
                Kmul (a4, a0, a1);
                Kmul (a5, a0, a2);
                Kadd (r2, a4, a5);
                });

#ifndef HALF_WORD
        DO_ONE_TEST("mul_ui distributivity", {
                Krandom2 (a0, rstate);
                Krandom2 (a1, rstate);
                Krandom2 (a2, rstate);
                unsigned long xx;
                xx = ((unsigned long *)(void *)(a0))[0];
                Kadd (a3, a1, a2);
                Kmul_ui (r1, a3, xx);
                Kmul_ui (a4, a1, xx);
                Kmul_ui (a5, a2, xx);
                Kadd (r2, a4, a5);
                });
#endif

        /* we can't be sure that our polynomial is irreducible... */
        DO_ONE_TEST("inversion", {
                do {
                    Krandom2 (r1, rstate);
                    if (Kcmp_ui(r1, 0) == 0) continue;
                } while (!Kinv (a1, r1));
                Kmul (a2, a1, r1);
                Kmul (r2, r1, a2);
                });

        DO_ONE_TEST("reduce o mul_ur = mul", {
                Krandom2 (a0, rstate);
                Krandom2 (a1, rstate);
                Kelt_ur tmp;
                Kelt_ur_init(&tmp);
                Kmul_ur(tmp, a0, a1);
                Kreduce(r1, tmp);
                Kelt_ur_clear(&tmp);
                Kmul(r2, a0, a1);
                });

        DO_ONE_TEST("reduce o sqr_ur = sqr", {
                Krandom2 (a0, rstate);
                Kelt_ur tmp;
                Kelt_ur_init(&tmp);
                Ksqr_ur(tmp, a0);
                Kreduce(r1, tmp);
                Kelt_ur_clear(&tmp);
                Ksqr(r2, a0);
                });
        /* currently mpfq_pz does not have sqrt implemented */
#ifndef VARIABLE_SIZE_PRIME
        DO_ONE_TEST("sqr o sqrt o sqr = sqr", {
                Krandom2 (a0, rstate);
                Ksqr(r1, a0);
                Ksqrt(r2, r1);
                Ksqr(r2, r2);
                });
#endif
        /*-----------------------------------------------------------*/
        /*          Tests specific to prime fields                   */
        /*-----------------------------------------------------------*/
#ifndef CHAR2
        DO_ONE_TEST("sscan o asprint = id", {
                Krandom2 (a0, rstate);
                char *str;
                Kset(r1, a0);
                Kasprint(&str, a0);
                int ret = Ksscan(r2, str);
                free(str);
                if (!ret) abort();
                });

        DO_ONE_TEST("mul by 3 = add o add", {
                Krandom2 (a0, rstate);
                Kset_ui(a1, 3);
                Kmul (r1, a0, a1);
                Kadd (r2, a0, a0);
                Kadd (r2, r2, a0);
                });
#ifndef EXTENSION_OF_GFP 
        DO_ONE_TEST("Fermat by pow", {
                Krandom2 (a0, rstate);
                Kset(r1, a0);
                Kpowz(r2, a0, K->p);
                });
#endif
        DO_ONE_TEST("x^(q-1) == 1/x", {
                do { Krandom2(a0, rstate); } while (Kcmp_ui(a0,0) == 0);
                mpz_t z;
                mpz_init_set_si(z, -1);
                Kpowz(r1, a0, z);
                Kinv(r2, a0);
                mpz_clear(z);
                });
        DO_ONE_TEST("x^(#K^2) == x", {
                Krandom2 (a0, rstate);
                mpz_t z;
                mpz_init(z);
                Kfield_characteristic(z);
                mpz_pow_ui(z, z, Kfield_degree() * 2);
                Kpowz(r1, a0, z);
                Kset(r2, a0);
                mpz_clear(z);
                });
#if 0
        DO_ONE_TEST("is_sqr o (mul(sqr,nsqr)) = false", {
                do {
                    Krandom2 (a0, rstate);
                } while (Kcmp_ui(a0, 0) == 0);
                Ksqr(r1, a0);
                Kmul(r1, r1, (Ksrc_elt)K->ts_info.z);
                Kset_ui(r2, Kis_sqr(r1));
                if (Kcmp_ui(r1, 0) == 0) 
                    Kset_ui(r1, 1);
                else 
                    Kset_ui(r1, 0);
                });

        DO_ONE_TEST("is_sqr o sqr = true", {
                do {
                    Krandom2 (a0, rstate);
                } while (Kcmp_ui(a0, 0) == 0);
                Kset_ui(r1, 1);
                Ksqr(a0, a0);
                Kset_ui(r2, Kis_sqr(a0));
                });
#endif        
        DO_ONE_TEST("ur_add 500 times and reduce", {
                Krandom2 (a0, rstate);
                Krandom2 (a1, rstate);
                Kelt_ur tmp0;
                Kelt_ur tmp1;
                Kelt_ur_init(&tmp0);
                Kelt_ur_init(&tmp1);
                Kmul_ur(tmp0, a0, a1);
                Kelt_ur_set_ui(tmp1, 0);
                {
                  int j;
                  for (j = 0; j < 500; ++j)
                    Kelt_ur_add(tmp1, tmp1, tmp0);
                }
                Kreduce(r1, tmp1);
                Kelt_ur_clear(&tmp0);
                Kelt_ur_clear(&tmp1);
                Kmul(r2, a0, a1);
                Kmul_ui(r2, r2, 500);
                });
        
        DO_ONE_TEST("ur_sub 500 times and reduce", {
                Krandom2 (a0, rstate);
                Krandom2 (a1, rstate);
                Kelt_ur tmp0;
                Kelt_ur tmp1;
                Kelt_ur_init(&tmp0);
                Kelt_ur_init(&tmp1);
                Kmul_ur(tmp0, a0, a1);
                Kelt_ur_set_ui(tmp1, 0);
                {
                  int j;
                  for (j = 0; j < 500; ++j)
                    Kelt_ur_sub(tmp1, tmp1, tmp0);
                }
                Kreduce(r1, tmp1);
                Kelt_ur_clear(&tmp0);
                Kelt_ur_clear(&tmp1);
                Kneg(r2, a0);
                Kmul(r2, r2, a1);
                Kmul_ui(r2, r2, 500);
                });

        DO_ONE_TEST("ur_neg o ur_sub = ur_add", {
                Krandom2 (a0, rstate);
                Krandom2 (a1, rstate);
                Krandom2 (a2, rstate);
                Krandom2 (a3, rstate);
                Kelt_ur tmp0;
                Kelt_ur tmp1;
                Kelt_ur tmp2;
                Kelt_ur_init(&tmp0);
                Kelt_ur_init(&tmp1);
                Kelt_ur_init(&tmp2);
                Kmul_ur(tmp0, a0, a1);
                Kmul_ur(tmp1, a2, a3);
                Kelt_ur_sub(tmp2, tmp1, tmp0);
                Kreduce(r1, tmp2);
                Kelt_ur_neg(tmp0, tmp0);
                Kelt_ur_add(tmp2, tmp1, tmp0);
                Kreduce(r2, tmp2);
                Kelt_ur_clear(&tmp0);
                Kelt_ur_clear(&tmp1);
                Kelt_ur_clear(&tmp2);
                });

#endif
        /*-----------------------------------------------------------*/
        /*          Tests specific to Montgomery representation      */
        /*-----------------------------------------------------------*/
#ifdef MGY
        DO_ONE_TEST("mgy_enc o mgy_dec = id", {
                Krandom2 (a0, rstate);
                Kset(r1, a0);
                Kmgy_enc(r2, a0);
                Kmgy_dec(r2, r2);
                });
#endif
        /*-----------------------------------------------------------*/
        /*          Tests specific to characteristic 2               */
        /*-----------------------------------------------------------*/

#ifdef CHAR2
        DO_ONE_TEST("add_uipoly o sub_uipoly = id", {
                Krandom2 (a0, rstate);
                Krandom2 (a1, rstate);
                Ksub_uipoly (r1, a0, a1[0]);
                Kadd_uipoly (r1, r1, a1[0]);
                Kset (r2, a0);
                });

        DO_ONE_TEST("mul_uipoly distributivity", {
                Krandom2 (a0, rstate);
                Krandom2 (a1, rstate);
                Krandom2 (a2, rstate);
                Kadd (a3, a1, a2);
                Kmul_uipoly(r1, a3, a0[0]);
                Kmul_uipoly(a4, a1, a0[0]);
                Kmul_uipoly(a5, a2, a0[0]);
                Kadd (r2, a4, a5);
                });

        DO_ONE_TEST("squaring period", {
                Krandom2 (r1, rstate);
                Kset (r2, r1);
                for(int j = 0 ; j < Kdegree ; j++) {
                        Ksqr (r2, r2);
                }
        });

        DO_ONE_TEST("inv by pow", {
                // inv of 0 is undefined.
                do { Krandom2(a0, rstate); } while (Kcmp_ui(a0,0) == 0);
                mpz_t zz;
                mpz_init_set_ui(zz, 1);
                mpz_mul_2exp(zz, zz, Kfield_degree());
                mpz_sub_ui(zz, zz, 2);
                Kpowz(r1, a0, zz);
                mpz_clear(zz);
                Kinv(r2, a0);
                });

        DO_ONE_TEST("sqrt linearity", {
                Krandom2 (a0, rstate);
                Krandom2 (a1, rstate);
                Kadd (a2, a0, a1);
                Ksqrt(a3, a0);
                Ksqrt(a4, a1);
                Kadd (r1, a3, a4);
                Ksqrt(r2, a2);
        });

        DO_ONE_TEST("artin-schreier equation", {
                Krandom2 (r1, rstate);
                Ksqr(a1, r1);
                Kadd(a1, a1, r1);
                Kas_solve(r2, a1);

                /* FIXME ; users of the library may want to be able to do
                 * this kind of thing */
                r1[0] &= ~1UL;
                r2[0] &= ~1UL;
        });

        DO_ONE_TEST("artin-schreier equation (2)", {
                Krandom2 (a0, rstate);
                if (Ktrace(a0) != 0) continue;
                Krandom2 (a2, rstate);
                if (Ktrace(a2) != 0) continue;

                Kas_solve(a1, a0);
                Ksqr(r1, a1);
                Kadd(r1, r1, a1);
                Kadd(r1, r1, a0);

                Kas_solve(a3, a2);
                Ksqr(r2, a3);
                Kadd(r2, r2, a3);
                Kadd(r2, r2, a2);
        });

        DO_ONE_TEST("ur_add and reduce", {
                Krandom2 (a0, rstate);
                Krandom2 (a1, rstate);
                Krandom2 (a2, rstate);
                Krandom2 (a3, rstate);
                Kelt_ur tmp0;
                Kelt_ur tmp1;
                Kelt_ur_init(&tmp0);
                Kelt_ur_init(&tmp1);
                Kmul_ur(tmp0, a0, a1);
                Kmul_ur(tmp1, a2, a3);
                Kelt_ur_add(tmp1, tmp1, tmp0);
                Kreduce(r1, tmp1);
                Kelt_ur_clear(&tmp0);
                Kelt_ur_clear(&tmp1);
                Kmul(r2, a0, a1);
                Kmul(a4, a2, a3);
                Kadd(r2, r2, a4);
                });

        DO_ONE_TEST("sscan o asprint = id, base 10", {
                Krandom2 (a0, rstate);
                unsigned long base=10;
                Kfield_setopt(MPFQ_IO_TYPE, &base);
                char *str;
                Kset(r1, a0);
                Kasprint(&str, a0);
                int ret = Ksscan(r2, str);
                free(str);
                if (!ret) abort();
                });

        DO_ONE_TEST("sscan o asprint = id, base 2", {
                Krandom2 (a0, rstate);
                unsigned long base=2;
                Kfield_setopt(MPFQ_IO_TYPE, &base);
                char *str;
                Kset(r1, a0);
                Kasprint(&str, a0);
                int ret = Ksscan(r2, str);
                free(str);
                if (!ret) abort();
                });

         DO_ONE_TEST("sscan o asprint = id, base 16", {
                Krandom2 (a0, rstate);
                unsigned long base=16;
                Kfield_setopt(MPFQ_IO_TYPE, &base);
                char *str;
                Kset(r1, a0);
                Kasprint(&str, a0);
                int ret = Ksscan(r2, str);
                free(str);
                if (!ret) abort();
                });
#endif

#ifdef  HAVE_mpfq_p_2_hadamard
         /* This does no checking of course. However it is useful to
          * trigger linking of the function, since when it is done in
          * assembly, it is likely to trigger register allocation
          * problems in some instances */
         Khadamard(a0,a1,a2,a3);
#endif
        /*-----------------------------------------------------------*/
        /*          Tests related to vectors                         */
        /*-----------------------------------------------------------*/
        const int deg=7;
//        int test_deg;
        Kvec_init(&v1, 2*deg);
        Kvec_init(&v2, 2*deg);
        Kvec_init(&w1, 2*deg);
        Kvec_init(&w2, 2*deg);
        Kvec_init(&w3, 2*deg);
        Kvec_init(&w4, 2*deg);
        DO_ONE_TEST_VEC("vec_add commutativity", {
                Kvec_random(w1, deg, rstate);
                Kvec_random(w2, deg, rstate);
                Kvec_add(v1, w1, w2, deg);
                Kvec_add(v2, w2, w1, deg);
//                test_deg = deg;
                });
        DO_ONE_TEST_VEC("vec_add associativity", {
                Kvec_random(w1, deg, rstate);
                Kvec_random(w2, deg, rstate);
                Kvec_random(w3, deg, rstate);
                Kvec_add(v1, w1, w2, deg);
                Kvec_add(v1, v1, w3, deg);
                Kvec_add(v2, w2, w3, deg);
                Kvec_add(v2, v2, w1, deg);
//                test_deg = deg;
                });
        DO_ONE_TEST_VEC("vec linearity", {
                Krandom2 (a0, rstate);
                Kvec_random(w1, deg, rstate);
                Kvec_random(w2, deg, rstate);
                Kvec_scal_mul(v1, w1, a0, deg);
                Kvec_scal_mul(v2, w2, a0, deg);
                Kvec_add(v1, v1, v2, deg);
                Kvec_add(w1, w1, w2, deg);
                Kvec_scal_mul(v2, w1, a0, deg);
//                test_deg = deg;
                });
        DO_ONE_TEST_VEC("vec_conv linearity", {
                Kvec_random(w1, deg, rstate);
                Kvec_random(w2, deg, rstate);
                Kvec_random(w3, deg, rstate);
                Kvec_add(w4, w2, w3, deg);
                Kvec_conv(v1, w1, deg, w4, deg);
                Kvec_conv(w4, w1, deg, w2, deg);
                Kvec_conv(v2, w1, deg, w3, deg);
                Kvec_add(v2, v2, w4, 2*deg-1);
//                test_deg = 2*deg-1;
                });
        Kvec_clear(&v1, 2*deg);
        Kvec_clear(&v2, 2*deg);
        Kvec_clear(&w1, 2*deg);
        Kvec_clear(&w2, 2*deg);
        Kvec_clear(&w3, 2*deg);
        Kvec_clear(&w4, 2*deg);
        /*-----------------------------------------------------------*/
        /*          Tests related to polynomials                     */
        /*-----------------------------------------------------------*/
        Kpoly_init(p1, 2*deg);
        Kpoly_init(p2, 2*deg);
        Kpoly_init(q1, 2*deg);
        Kpoly_init(q2, 2*deg);
        Kpoly_init(q3, 2*deg);
        Kpoly_init(q4, 2*deg);
        DO_ONE_TEST_POLY("poly_add commutativity", {
                Kpoly_random(q1, deg, rstate);
                Kpoly_random(q2, deg, rstate);
                Kpoly_add(p1, q1, q2);
                Kpoly_add(p2, q2, q1);
                });
        DO_ONE_TEST_POLY("poly_add associativity", {
                Kpoly_random(q1, deg, rstate);
                Kpoly_random(q2, deg, rstate);
                Kpoly_random(q3, deg, rstate);
                Kpoly_add(p1, q1, q2);
                Kpoly_add(p1, p1, q3);
                Kpoly_add(p2, q2, q3);
                Kpoly_add(p2, p2, q1);
                });
        DO_ONE_TEST_POLY("poly linearity", {
                Krandom2 (a0, rstate);
                Kpoly_random(q1, deg, rstate);
                Kpoly_random(q2, deg, rstate);
                Kpoly_scal_mul(p1, q1, a0);
                Kpoly_scal_mul(p2, q2, a0);
                Kpoly_add(p1, p1, p2);
                Kpoly_add(q1, q1, q2);
                Kpoly_scal_mul(p2, q1, a0);
                });
        DO_ONE_TEST_POLY("poly_mul linearity", {
                Kpoly_random(q1, deg, rstate);
                Kpoly_random(q2, deg, rstate);
                Kpoly_random(q3, deg, rstate);
                Kpoly_add(q4, q2, q3);
                Kpoly_mul(p1, q1, q4);
                Kpoly_mul(q4, q1, q2);
                Kpoly_mul(p2, q1, q3);
                Kpoly_add(p2, p2, q4);
                });
        DO_ONE_TEST_POLY("poly_gcd", {
                /* make sure leading coefficients are invertible.
                 * Normally we're over a field, so we don't have to.
                 * Unfortunately our testing is so dumb that we don't
                 * promise that we're over a field...
                 */
                do {
                    Kpoly_random(q1, deg, rstate);
                    Kpoly_random(q2, deg, rstate);
                    Kpoly_getcoeff(a1, q1, deg);
                    if (!Kinv(a0, a1)) continue;
                    Kpoly_getcoeff(a2, q2, deg);
                    if (!Kinv(a0, a2)) continue;
                    Kpoly_gcd(p1, q1, q2);
                } while (Kpoly_deg(p1) != 0);
                do {
                    Kpoly_random(q3, deg,rstate);
                    Kpoly_getcoeff(a3, q3, deg);
                } while (!Kinv(a0, a3));
                Kpoly_mul(q1, q1, q3);
                Kpoly_mul(q2, q2, q3);
                Kpoly_setmonic(p2, q3);
                Kpoly_gcd(p1, q1, q2);
                });
        DO_ONE_TEST_POLY("poly_xgcd", {
                do {
                    Kpoly_random(q1, deg, rstate);
                    Kpoly_random(q2, deg, rstate);
                    Kpoly_gcd(p1, q1, q2);
                } while (Kpoly_deg(p1) != 0);
                Kpoly_random(q3, deg, rstate);
                Kpoly_setmonic(p2, q3);
                Kpoly_mul(q1, q1, q3);
                Kpoly_mul(q2, q2, q3);
                Kpoly_xgcd(p1, q3, q4, q1, q2);
                Kpoly_mul(q3, q3, q1);
                Kpoly_mul(q4, q4, q2);
                Kpoly_add(p1, q3, q4);
                });
        Kpoly_clear(p1);
        Kpoly_clear(p2);
        Kpoly_clear(q1);
        Kpoly_clear(q2);
        Kpoly_clear(q3);
        Kpoly_clear(q4);

        Kclear (&a0);
        Kclear (&a1);
        Kclear (&a2);
        Kclear (&a3);
        Kclear (&a4);
        Kclear (&a5);
        Kclear (&r1);
        Kclear (&r2);

        Kfield_clear();

        if (quiet) {
            fprintf(stderr, ".");
            fflush(stderr);
        }

        i++;
        seed = gmp_urandomb_ui(rstate, 64);

        gmp_randclear(rstate);
    }
}

/* vim:set ft=c: */
