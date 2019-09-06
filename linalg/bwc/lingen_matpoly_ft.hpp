#ifndef LINGEN_MATPOLY_FT_HPP_
#define LINGEN_MATPOLY_FT_HPP_

#include <mutex>
#ifdef SELECT_MPFQ_LAYER_u64k1
#include "lingen_matpoly_binary.hpp"
#include "gf2x-fft.h"
#else
#include "lingen_matpoly.hpp"
#include "flint-fft/fft.h"
#endif
#include "lingen_substep_schedule.hpp"
#include "tree_stats.hpp"
#include "misc.h"
#include "lingen_call_companion.hpp"

template<typename fft_type>
class matpoly_ft {
    typedef memory_pool_strict memory_pool_type;
    static memory_pool_type memory;
    friend memory_pool_type::guard<matpoly_ft>;
public:
    typedef memory_pool_type::guard<matpoly_ft> memory_guard;
    fft_type fti;
    unsigned int m = 0;
    unsigned int n = 0;
    size_t fft_alloc_sizes[3];
    typename fft_type::ptr data = NULL;
    inline unsigned int nrows() const { return m; }
    inline unsigned int ncols() const { return n; }

    bool check_pre_init() const { return data == NULL; }
    /* {{{ ctor / dtor */
    matpoly_ft(fft_type const & fti) : fti(fti) {
        fti.get_alloc_sizes(fft_alloc_sizes);
    }
    matpoly_ft(unsigned int m, unsigned int n, fft_type const & fti)
        : matpoly_ft(fti)
    {
        this->m = m;
        this->n = n;
        data = (typename fft_type::ptr) memory.alloc(m * n * fft_alloc_sizes[0]);
        memset(data, 0, m * n * fft_alloc_sizes[0]);
#ifdef HAVE_OPENMP
#pragma omp parallel for collapse(2)
#endif
        for(unsigned int i = 0 ; i < m ; i++) {
            for(unsigned int j = 0 ; j < n ; j++) {
                fti.prepare(part(i,j));
            }
        }
    }
    matpoly_ft() = default;
    matpoly_ft(matpoly_ft const&) = delete;
    matpoly_ft& operator=(matpoly_ft const&) = delete;
    matpoly_ft(matpoly_ft && a) : fti(a.fti), m(a.m), n(a.n) {
        memcpy(fft_alloc_sizes, a.fft_alloc_sizes, sizeof(fft_alloc_sizes));
        data=a.data;
        a.m=a.n=0;
        a.data=NULL;
    }
    matpoly_ft& operator=(matpoly_ft && a) {
        if (data) memory.free(data, m * n * fft_alloc_sizes[0]);
        memcpy(fft_alloc_sizes, a.fft_alloc_sizes, sizeof(fft_alloc_sizes));
        fti = a.fti;
        m=a.m;
        n=a.n;
        data=a.data;
        a.m=a.n=0;
        a.data=NULL;
        return *this;
    }
    ~matpoly_ft() {
        if (data) memory.free(data, m * n * fft_alloc_sizes[0]);
    }
    /* }}} */
    /* {{{ direct access interface */
    inline typename fft_type::ptr part(unsigned int i, unsigned int j) {
        return (typename fft_type::ptr) pointer_arith(data, (i*n+j) * fft_alloc_sizes[0]);
    }
    inline typename fft_type::srcptr part(unsigned int i, unsigned int j) const {
        return (typename fft_type::ptr) pointer_arith(data, (i*n+j) * fft_alloc_sizes[0]);
    }
    /* }}} */
    /* {{{ views -- some of the modifiers are implemented here only */
    struct view_t;
    struct const_view_t;

    struct view_t : public submatrix_range {/*{{{*/
        matpoly_ft & M;
        view_t(matpoly_ft & M, submatrix_range S) : submatrix_range(S), M(M) {}
        view_t(matpoly_ft & M) : submatrix_range(M), M(M) {}
        inline typename fft_type::ptr part(unsigned int i, unsigned int j) {
            return M.part(i0+i, j0+j);
        }
        inline typename fft_type::srcptr part(unsigned int i, unsigned int j) const {
            return M.part(i0+i, j0+j);
        }
        void zero() { /*{{{*/
            unsigned int nrows = this->nrows();
            unsigned int ncols = this->ncols();
#ifdef HAVE_OPENMP
#pragma omp parallel for collapse(2)
#endif
            for(unsigned int i = 0 ; i < nrows ; i++) {
                for(unsigned int j = 0 ; j < ncols ; j++) {
                    M.fti.zero(part(i,j));
                }
            }
        }/*}}}*/
#if 0
        void fill_random(gmp_randstate_t rstate) { /*{{{*/
            unsigned int nrows = this->nrows();
            unsigned int ncols = this->ncols();
            // the rstate is shared: it is not safe to openmp-it.
#ifdef HAVE_OPENMP
#pragma omp parallel for collapse(2)
#endif
            for(unsigned int i = 0 ; i < nrows ; i++) {
                for(unsigned int j = 0 ; j < ncols ; j++) {
                    M.fti.fill_random(part(i,j), rstate);
                }
            }
        }/*}}}*/
#endif

        void to_export() { /*{{{*/
            unsigned int nrows = this->nrows();
            unsigned int ncols = this->ncols();
            ASSERT(check());
#ifdef HAVE_OPENMP
#pragma omp parallel for collapse(2)
#endif
            for(unsigned int i = 0 ; i < nrows ; i++) {
                for(unsigned int j = 0 ; j < ncols ; j++) {
                    M.fti.to_export(part(i,j));
                }
            }
        } /*}}}*/
        void to_import() { /*{{{*/
            unsigned int nrows = this->nrows();
            unsigned int ncols = this->ncols();
#ifdef HAVE_OPENMP
#pragma omp parallel for collapse(2)
#endif
            for(unsigned int i = 0 ; i < nrows ; i++) {
                for(unsigned int j = 0 ; j < ncols ; j++) {
                    M.fti.to_import(part(i,j));
                }
            }
            ASSERT(check());
        }/*}}}*/
        inline int check() { return const_view_t(*this).check(); }
    };/*}}}*/
    struct const_view_t : public submatrix_range {/*{{{*/
        matpoly_ft const & M;
        const_view_t(matpoly_ft const & M, submatrix_range S) : submatrix_range(S), M(M) {}
        const_view_t(matpoly_ft const & M) : submatrix_range(M), M(M) {}
        const_view_t(view_t const & V) : submatrix_range(V), M(V.M) {}
        inline typename fft_type::srcptr part(unsigned int i, unsigned int j) const {
            return M.part(i0+i, j0+j);
        }
        bool check() {/*{{{*/
            for(unsigned int i = 0 ; i < nrows() ; i++) {
                for(unsigned int j = 0 ; j < ncols() ; j++) {
                    if (!M.fti.check(part(i,j), 1))
                        return false;
                }
            }
            return true;
        }/*}}}*/
    };/*}}}*/
    /* }}} */
    view_t view(submatrix_range S) { ASSERT_ALWAYS(S.valid(*this)); return view_t(*this, S); }
    const_view_t view(submatrix_range S) const { ASSERT_ALWAYS(S.valid(*this)); return const_view_t(*this, S); }
    view_t view() { return view_t(*this); }
    const_view_t view() const { return const_view_t(*this); }

    inline void zero(submatrix_range R) { view(R).zero(); }
    inline void zero() { view().zero(); }

#if 0
    inline void fill_random(submatrix_range const & R, gmp_randstate_t rstate) {
        view(R).fill_random(rstate);
    }
    inline void fill_random(gmp_randstate_t rstate) {
        view().fill_random(rstate);
    }
#endif

    void to_import(submatrix_range const & R) { view(R).to_import(); }
    void to_import() { view().to_import(); }

    void to_export(submatrix_range const & R) { view(R).to_import(); }
    void to_export() { view().to_export(); }

    int check(submatrix_range const & R) const { return view(R).check(); }
    int check() const { return view().check(); }

    static void dft(view_t t, matpoly::const_view_t a)/*{{{*/
    {
        unsigned int nrows = a.nrows();
        unsigned int ncols = a.ncols();
        ASSERT_ALWAYS(t.nrows() == nrows);
        ASSERT_ALWAYS(t.ncols() == ncols);
#ifdef HAVE_OPENMP
#pragma omp parallel
#endif
        {
            typename fft_type::ptr tt = (typename fft_type::ptr) malloc(t.M.fft_alloc_sizes[1]);
#ifdef HAVE_OPENMP
#pragma omp for collapse(2)
#endif
            for(unsigned int i = 0 ; i < nrows ; i++) {
                for(unsigned int j = 0 ; j < ncols ; j++) {
                    typename fft_type::ptr tij = t.part(i, j);
                    absrc_vec aij = a.part(i, j);
                    /* ok, casting like this is a crude hack ! */
                    t.M.fti.dft(tij, (const mp_limb_t *) aij, a.M.size, tt);
                }
            }
            free(tt);
        }
    }/*}}}*/
    static void ift(matpoly::view_t a, view_t t)/*{{{*/
    {
        unsigned int nrows = a.nrows();
        unsigned int ncols = a.ncols();
        ASSERT_ALWAYS(t.nrows() == nrows);
        ASSERT_ALWAYS(t.ncols() == ncols);
#ifdef HAVE_OPENMP
#pragma omp parallel
#endif
        {
            typename fft_type::ptr tt = (typename fft_type::ptr) malloc(t.M.fft_alloc_sizes[1]);
#ifdef HAVE_OPENMP
#pragma omp for collapse(2)
#endif
            for(unsigned int i = 0 ; i < nrows ; i++) {
                for(unsigned int j = 0 ; j < ncols ; j++) {
                    typename fft_type::ptr tij = t.part(i,j);
                    abdst_vec aij = a.part(i, j);
                    /* ok, casting like this is a crude hack ! */
                    t.M.fti.ift((mp_limb_t *) aij, a.M.size, tij, tt);
                }
            }
            free(tt);
        }
    }/*}}}*/
    static void addcompose(view_t t, const_view_t t0, const_view_t t1)/*{{{*/
    {
        unsigned int nrows = t.nrows();
        unsigned int ncols = t.ncols();
        unsigned int nadd = t0.ncols();
        ASSERT_ALWAYS(t0.nrows() == nrows);
        ASSERT_ALWAYS(t1.ncols() == ncols);
        ASSERT_ALWAYS(t0.ncols() == nadd);
        ASSERT_ALWAYS(t1.nrows() == nadd);
        ASSERT(t0.check());
        ASSERT(t1.check());
        ASSERT(t.check());
#ifdef HAVE_OPENMP
#pragma omp parallel
#endif
        {
            typename fft_type::ptr qt = (typename fft_type::ptr) malloc(t.M.fft_alloc_sizes[1]);
            typename fft_type::ptr tt = (typename fft_type::ptr) malloc(t.M.fft_alloc_sizes[2]);
            memset(qt, 0, t.M.fft_alloc_sizes[1]);

#ifdef HAVE_OPENMP
#pragma omp for collapse(2)
#endif
            for(unsigned int i = 0 ; i < nrows ; i++) {
                for(unsigned int j = 0 ; j < ncols ; j++) {
                    memset(tt, 0, t.M.fft_alloc_sizes[2]);
                    for(unsigned int k = 0 ; k < nadd ; k++) {
                        t.M.fti.addcompose(t.part(i,j), t0.part(i,k), t1.part(k,j), tt, qt);
                    }
                }
            }
            free(tt);
            free(qt);
        }
    }/*}}}*/

    static void mul(view_t t, const_view_t t0, const_view_t t1)
    {
        t.zero();
        addcompose(t, t0, t1);
    }

    /*
void add(matpoly_ft::view_t t, matpoly_ft::const_view_t t0, matpoly_ft::const_view_t t1)
{
    unsigned int nrows = t.nrows();
    unsigned int ncols = t.ncols();
    ASSERT_ALWAYS(t0.nrows() == nrows);
    ASSERT_ALWAYS(t0.ncols() == ncols);
    ASSERT_ALWAYS(t1.nrows() == nrows);
    ASSERT_ALWAYS(t1.ncols() == ncols);

#ifdef HAVE_OPENMP
#pragma omp parallel for collapse(2)
#endif
    for(unsigned int i = 0 ; i < nrows ; i++) {
        for(unsigned int j = 0 ; j < ncols ; j++) {
            fft_add(t.part(i,j), t0.part(i,j), t1.part(i,j), t.M.fti);
        }
    }
}
*/

    /* In a way, this is the only real API exported by this module */

    static void mp_caching_adj(matpoly & c, matpoly const & a, matpoly const & b, unsigned int adj, lingen_call_companion::mul_or_mp_times * M);
    static void mul_caching_adj(matpoly & c, matpoly const & a, matpoly const & b, unsigned int adj, lingen_call_companion::mul_or_mp_times * M);
    static inline void mp_caching(matpoly & c, matpoly const & a, matpoly const & b, lingen_call_companion::mul_or_mp_times * M) {
        mp_caching_adj(c, a, b, UINT_MAX, M);
    }
    static inline void mul_caching(matpoly & c, matpoly const & a, matpoly const & b, lingen_call_companion::mul_or_mp_times * M) {
        mul_caching_adj(c, a, b, UINT_MAX, M);
    }
};

template<typename T> struct is_binary;

#ifdef SELECT_MPFQ_LAYER_u64k1
extern template class matpoly_ft<gf2x_fake_fft_info>;
extern template class matpoly_ft<gf2x_cantor_fft_info>;
extern template class matpoly_ft<gf2x_ternary_fft_info>;
template<> struct is_binary<gf2x_fake_fft_info> {
    static constexpr const bool value = true;
};
template<> struct is_binary<gf2x_cantor_fft_info> {
    static constexpr const bool value = true;
};
template<> struct is_binary<gf2x_ternary_fft_info> {
    static constexpr const bool value = true;
};
#else
extern template class matpoly_ft<fft_transform_info>;
template<> struct is_binary<fft_transform_info> {
    static constexpr const bool value = false;
};
#endif

/*
 * unused
template<typename fft_type> struct memory_guard<matpoly_ft<fft_type>> {
    typedef memory_pool_strict::guard<matpoly_ft<fft_type>> type;
};
 */


#endif	/* LINGEN_MATPOLY_FT_HPP_ */
