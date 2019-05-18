#ifndef POLYMAT_H_
#define POLYMAT_H_

#include "mpfq_layer.h"
#include "macros.h"

class matpoly;
struct polymat;

#include "lingen-matpoly.hpp"

/* This is used only for plingen. */

struct polymat {
    abdst_field ab = NULL;
    unsigned int m = 0;
    unsigned int n = 0;
    size_t size = 0;
    size_t alloc = 0;
    abvec x = NULL;

    polymat() : polymat(NULL, 0,0,0) {}
    polymat(polymat const&) = delete;
    polymat& operator=(polymat const&) = delete;
    polymat(polymat &&);
    polymat& operator=(polymat &&);
    ~polymat();
    polymat(abdst_field ab, unsigned int m, unsigned int n, int len);
    int check_pre_init() const;
    void realloc(size_t newalloc);
    void zero();
#if 0
    void swap(polymat & b);
#endif
    void fill_random(unsigned int nsize, gmp_randstate_t rstate);
    int cmp(polymat const & b);

    /* {{{ access interface for polymat */
    inline abdst_vec part(unsigned int i, unsigned int j, unsigned int k) {
        /* Assume row-major in all circumstances. Old code used to support
         * various orderings, here we don't */
        ASSERT_ALWAYS(size);
        return abvec_subvec(ab, x, (k*m+i)*n+j);
    }
    inline abdst_elt coeff(unsigned int i, unsigned int j, unsigned int k) {
        return abvec_coeff_ptr(ab, part(i,j,k),0);
    }
    inline absrc_vec part(unsigned int i, unsigned int j, unsigned int k) const {
        /* Assume row-major in all circumstances. Old code used to support
         * various orderings, here we don't */
        ASSERT_ALWAYS(size);
        return abvec_subvec_const(ab, (absrc_vec)x,(k*m+i)*n+j);
    }
    inline absrc_elt coeff(unsigned int i, unsigned int j, unsigned int k) const {
        return abvec_coeff_ptr_const(ab, part(i,j,k),0);
    }
    /* }}} */

    void addmat(
        unsigned int kc,
        polymat const & a, unsigned int ka,
        polymat const & b, unsigned int kb);
    void submat(
        unsigned int kc,
        polymat const & a, unsigned int ka,
        polymat const & b, unsigned int kb);
    void mulmat(
        unsigned int kc,
        polymat const & a, unsigned int ka,
        polymat const & b, unsigned int kb);
    void addmulmat(
        unsigned int kc,
        polymat const & a, unsigned int ka,
        polymat const & b, unsigned int kb);
    void multiply_column_by_x(unsigned int j, unsigned int size);
    void truncate(polymat const & src, unsigned int nsize);
    void extract_column(
        unsigned int jdst, unsigned int kdst,
        polymat const & src, unsigned int jsrc, unsigned int ksrc);
    void extract_row_fragment(
        unsigned int i1, unsigned int j1,
        polymat const & src, unsigned int i0, unsigned int j0,
        unsigned int n);
    void mul(polymat const & a, polymat const & b);
    void addmul(polymat const & a, polymat const & b);
    void mp(polymat const & a, polymat const & b);
    void addmp(polymat const & a, polymat const & b);

    void set_matpoly(matpoly const &);
    void rshift(polymat const & src, unsigned int k);

};

/* {{{ cut-off structures */
/* This structure is used to decide which algorithm to use for a given
 * input length. This essentially is a function from N*N to a finite set of
 * choices (so far, {0,1} only). The value returned for a balanced input
 * length x*x is:
 * if x >= cut: 1
 * if x < cut:
 *      if table == NULL:
 *              return 0
 *      find last (s,a) in table such that s <= x
 *      return a
 * For unbalanced input length x*y, we compare MIN(x,y) with subdivide.
 * At the moment there is no additional table designed to improve the
 * choice.
 *
 * Cutoffs are used only by lingen-polymat.c and lingen-bigpolymat.c -- however the
 * tuning program also uses it, so we expose it here too.
 */
/* Such a cutoff structure is used for finding which algorithm to used
 * for:
 *  - a balanced x*x product. -> basecase(0) or karatsuba(1)
 *  - an unbalanced x*y product. -> basecase(0) or split into balanced
 *  sizes(1)
 */

struct polymat_cutoff_info {
    unsigned int cut;
    unsigned int subdivide;
    unsigned int (*table)[2];
    unsigned int table_size;
};
#ifdef __cplusplus
extern "C" {
#endif
void polymat_cutoff_info_init(struct polymat_cutoff_info * c);
void polymat_cutoff_info_clear(struct polymat_cutoff_info * c);
void polymat_cutoff_add_step(struct polymat_cutoff_info * c, unsigned int size, unsigned int alg);
void polymat_set_mul_kara_cutoff(const struct polymat_cutoff_info * new_cutoff, struct polymat_cutoff_info * old_cutoff);
void polymat_set_mp_kara_cutoff(const struct polymat_cutoff_info * new_cutoff, struct polymat_cutoff_info * old_cutoff);
#ifdef __cplusplus
}
#endif
/* }}} */

#endif	/* POLYMAT_H_ */
