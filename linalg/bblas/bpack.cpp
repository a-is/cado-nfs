#include "cado.h"
#include "bblas_level4.hpp"
#include "bblas_level4_ple_internal.hpp"
#include "bblas_level4_ple_internal_inl.hpp"
#include "gmp_aux.h"
#ifdef HAVE_OPENMP
#include <omp.h>
#endif

/* Is it sufficient to meet the ODR requirement ? There are places where
 * we do std::min(B, foo). That requires that a definition of B be
 * available somewhere. It's not clear to me that the following kind of
 * template constexpr definition does the trick.
 */
template<typename matrix> constexpr const unsigned int bpack_view_base<matrix>::B;
template<typename matrix> constexpr const unsigned int bpack<matrix>::B;

template<typename matrix>
void bpack_view<matrix>::fill_random(gmp_randstate_t rstate) {
    memfill_random((void *) X, mblocks * nblocks * sizeof(matrix), rstate);
}

/* propose implementation for the PLE front-ends */
template<typename matrix>
std::vector<unsigned int> bpack_view<matrix>::ple(std::vector<unsigned int> const & d)
{
    auto ple = PLE<matrix>(*this, d);
    return ple();
}

template<typename matrix>
std::vector<unsigned int> bpack_view<matrix>::ple()
{
    auto ple = PLE<matrix>(*this);
    return ple();
}

template<typename matrix>
void bpack_view<matrix>::propagate_row_permutations(std::vector<unsigned int> const & p)
{
    /* X has size (B*mblocks) * (B*nblocks), and blocks are stored
     * row-major (i.e. we have mblocks lists of nblocks consecutive mat64's).
     * 
     * exchange row 0 with row p[0]
     * exchange row 1 with row p[1]
     * and so on.
     *
     * It turns out that we have a function in ple that does exactly this !
     */
    PLE<matrix>(*this).propagate_row_permutations(p.size(), UINT_MAX, p.begin(), p.end());
}

/* This replaces, in-place, the blocks of the lower triangular part of
 * the matrix X, by the inverse. Blocks cell(i, j) with j>i are not
 * touched. Blocks with j>nblocks do not exist in X, but are implicitly assumed
 * to correspond to a fragment of the identity matrix (and need not be
 * touched anyway)
 *
 * Note that the upper part of the diagonal blocks is replaced by zeroes.
 * Fixing that wouldn't be terribly hard.
 */
template<typename matrix>
void bpack_view<matrix>::invert_lower_triangular()
{
    for(unsigned int j = 0 ; j < nblocks && j < mblocks ; j++) {
        matrix Ljj_inv = 1;
        matrix::trsm(cell(j, j), Ljj_inv);
        cell(j, j) = Ljj_inv;
    }
    for(unsigned int j = 0 ; j < nblocks ; j++) {
        for(unsigned int i = j + 1 ; i < mblocks ; i++) {
            // we have i > j
            matrix S = 0;
            // L_{i,j} * R_{j,j} + L_{i,j+1} * R{j+1,j} + ... + L_{i,i} * R_{i,j} = 0
            for(unsigned int k = j ; k < i && k < nblocks ; k++) {
                /* if k > nblocks, cell(i, k) is zero */
                matrix::addmul(S, cell(i, k), cell(k, j));
            }
            if (i < nblocks) {
                /* mul_lt_ge is a very shallow win. */
                matrix::mul_lt_ge(cell(i, j), cell(i, i), S);
            } else {
                cell(i, j) = S;
            }
        }
    }
}

template<typename matrix>
bpack_view<matrix>& bpack_view<matrix>::set(int a)
{
    std::fill_n(X, mblocks * nblocks, 0);
    if (a&1)
        triangular_make_unit();
    return *this;
}

template<typename matrix>
bpack_view<matrix>& bpack_view<matrix>::set(bpack_const_view<matrix> v)
{
    ASSERT_ALWAYS(mblocks == v.mblocks);
    ASSERT_ALWAYS(nblocks == v.nblocks);
    for(unsigned int j = 0 ; j < nblocks ; j++) {
        for(unsigned int i = 0 ; i < mblocks ; i++) {
            cell(i, j) = v.cell(i, j);
        }
    }
    return *this;
}


template<typename matrix>
bool bpack_const_view<matrix>::operator==(int a) const {
    for(unsigned int j = 0 ; j < nblocks ; j++) {
        for(unsigned int i = 0 ; i < mblocks ; i++) {
            if (j == i) {
                if (cell(j, j) != a) return false;
            } else {
                if (cell(i, j) != 0) return false;
            }
        }
    }
    return true;
}
/*
template<typename matrix>
void bpack_ops<matrix>::fill_random(bpack_view<matrix> A, gmp_randstate_t rstate) {
    A.fill_random(rstate);
}
*/

template<typename matrix>
bool bpack_const_view<matrix>::is_lowertriangular() const {
    for(unsigned int j = 0 ; j < nblocks ; j++) {
        for(unsigned int i = 0 ; i < mblocks ; i++) {
            if (j > i) {
                if (cell(i, j) != 0) return false;
            } else if (j == i) {
                if (!cell(i, j).is_lowertriangular()) return false;
            }
        }
    }
    return true;
}

template<typename matrix>
bool bpack_const_view<matrix>::is_uppertriangular() const {
    for(unsigned int j = 0 ; j < nblocks ; j++) {
        for(unsigned int i = 0 ; i < mblocks ; i++) {
            if (j < i) {
                if (cell(i, j) != 0) return false;
            } else if (j == i) {
                if (!cell(i, j).is_uppertriangular()) return false;
            }
        }
    }
    return true;
}

template<typename matrix>
bool bpack_const_view<matrix>::triangular_is_unit() const {
    for(unsigned int j = 0 ; j < nblocks && j < mblocks ; j++) {
        if (!cell(j, j).triangular_is_unit()) return false;
    }
    return true;
}

template<typename matrix>
void bpack_view<matrix>::make_lowertriangular() {
    for(unsigned int j = 0 ; j < nblocks ; j++) {
        for(unsigned int i = 0 ; i < mblocks ; i++) {
            if (j > i)
                cell(i, j) = 0;
            else if (j == i) {
                cell(i, j).make_lowertriangular();
            }
        }
    }
}

template<typename matrix>
void bpack_view<matrix>::make_uppertriangular() {
    for(unsigned int j = 0 ; j < nblocks ; j++) {
        for(unsigned int i = 0 ; i < mblocks ; i++) {
            if (j < i)
                cell(i, j) = 0;
            else if (j == i) {
                cell(i, j).make_uppertriangular();
            }
        }
    }
}

template<typename matrix>
void bpack_view<matrix>::make_unit_lowertriangular() {
    make_lowertriangular();
    triangular_make_unit();
}

template<typename matrix>
void bpack_view<matrix>::make_unit_uppertriangular() {
    make_uppertriangular();
    triangular_make_unit();
}

template<typename matrix>
void bpack_view<matrix>::triangular_make_unit() {
    for(unsigned int j = 0 ; j < nblocks && j < mblocks ; j++) {
        cell(j, j).triangular_make_unit();
    }
}

template<typename matrix>
void bpack_ops<matrix>::mul(bpack_view<matrix> C, bpack_const_view<matrix> A, bpack_const_view<matrix> B)
{
    if (C.overlaps(A) || C.overlaps(B)) {
        bpack<matrix> CC(A.nrows(), B.ncols());
        mul(CC.view(), A, B);
        C.set(CC);
        return;
    }
    C.set(0);
    ASSERT_ALWAYS(C.nrowblocks() == A.nrowblocks());
    ASSERT_ALWAYS(C.ncolblocks() == B.ncolblocks());
    ASSERT_ALWAYS(A.ncolblocks() == B.nrowblocks());
    for(unsigned int bi = 0 ; bi < A.nrowblocks() ; bi++) {
        for(unsigned int bj = 0 ; bj < B.ncolblocks() ; bj++) {
            for(unsigned int bk = 0 ; bk < A.ncolblocks() ; bk++) {
                matrix::addmul(C.cell(bi, bj), A.cell(bi, bk), B.cell(bk, bj));
            }
        }
    }
}

template<typename matrix>
void bpack_ops<matrix>::mul_lt_ge(bpack_const_view<matrix> A, bpack_view<matrix> X)
{
    /* A is considered an an implicitly square matrix. It is
     * completed to the right to as many blocks as is necessary
     * to match the number of row blocks of X
     */
    ASSERT_ALWAYS(A.nrowblocks() == X.nrowblocks());
    ASSERT_ALWAYS(A.ncolblocks() <= A.nrowblocks());
#if 0
    for(unsigned int bi = X.nrowblocks() ; bi-- ; ) {
// #ifdef HAVE_OPENMP
// #pragma omp parallel for
// #endif
        for(unsigned int cbj = 0 ; cbj < X.ncolblocks() ; cbj++) {
            unsigned int bj = X.ncolblocks() - 1 - cbj;
            /* (AX)_{i,j} is the sum for 0<=k<=i of A_{i,k}X_{k,j}
             * (when X is also lt we even have j<=k).
             *
             * except for bi>=A.ncolblocks, where it is:
             * X_{i,j}+sum for 0<=k<A.ncolblocks A_{i,k}X_{k,j}
             *
             */
            matrix & S = X.cell(bi, bj);
            if (bi < A.ncolblocks())
                matrix::mul(S, A.cell(bi, bi), S);
            for(unsigned int bk = 0 ; bk < bi && bk < A.ncolblocks() ; bk++) {
                matrix::addmul(S, A.cell(bi, bk), X.cell(bk, bj));
            }
        }
    }
#else
    /* This approach is significantly faster when the multiplication code
     * benefits from doing precomputations on its right-hand side.
     */
#ifdef HAVE_OPENMP
#pragma omp parallel
#endif
    {
        constexpr const unsigned int B = matrix::width;
        /* We may adjust the number of columns to our liking, but it
         * seems that the fewer the better.
         */
        bpack<matrix> T(X.nrows(), B);
        size_t A_stride = &A.cell(1,0) - &A.cell(0,0);
        size_t T_stride = &T.cell(1,0) - &T.cell(0,0);
#ifdef HAVE_OPENMP
#pragma omp for
#endif
    for(unsigned int bj = 0 ; bj < X.ncolblocks() ; bj += T.ncolblocks()) {
        T = 0;
        unsigned int ndbj = std::min(T.ncolblocks(), X.ncolblocks() - bj);
        for(unsigned int bk = 0 ; bk < A.ncolblocks() ; bk++) {
            for(unsigned int dbj = 0 ; dbj < ndbj ; dbj++) {
                matrix::addmul_blocks(&T.cell(0,dbj), &A.cell(0, bk), X.cell(bk, bj + dbj), A.nrowblocks(), T_stride, A_stride);
            }
        }
        /* We could conceivably parallelize the loops below, but openmp
         * won't let us do it, and I don't know how I can work around
         * this limitation (it would require cooperation from the
         * enclosing loop)
         */
        for(unsigned int bi = 0 ; bi < A.ncolblocks() ; bi++) {
            for(unsigned int dbj = 0 ; dbj < ndbj ; dbj++) {
                X.cell(bi, bj + dbj) = T.cell(bi, dbj);
            }
        }
        for(unsigned int bi = A.ncolblocks() ; bi < X.nrowblocks() ; bi++) {
            for(unsigned int dbj = 0 ; dbj < ndbj ; dbj++) {
                matrix::add(X.cell(bi, bj + dbj), X.cell(bi, bj + dbj), T.cell(bi, dbj));
            }
        }
    }
    }
#endif
}

template<typename matrix>
void bpack_ops<matrix>::extract_uppertriangular(bpack_view<matrix> a, bpack_const_view<matrix> const b)
{
    ASSERT_ALWAYS(a.nrowblocks() == b.nrowblocks());
    ASSERT_ALWAYS(a.ncolblocks() == b.ncolblocks());
    for(unsigned int bi = 0 ; bi < b.nrowblocks() ; bi++) {
        for(unsigned int bj = 0 ; bj < b.ncolblocks() ; bj++) {
            if (bi > bj)
                a.cell(bi, bj) = 0;
            else if (bi == bj)
                matrix::extract_uppertriangular(a.cell(bi, bj), b.cell(bi, bj));
            else
                a.cell(bi, bj) = b.cell(bi, bj);
        }
    }
}

template<typename matrix>
void bpack_ops<matrix>::extract_lowertriangular(bpack_view<matrix> a, bpack_const_view<matrix> const b)
{
    ASSERT_ALWAYS(a.nrowblocks() == b.nrowblocks());
    ASSERT_ALWAYS(a.ncolblocks() == b.ncolblocks());
    for(unsigned int bi = 0 ; bi < b.nrowblocks() ; bi++) {
        for(unsigned int bj = 0 ; bj < b.ncolblocks() ; bj++) {
            if (bi < bj)
                a.cell(bi, bj) = 0;
            else if (bi == bj)
                matrix::extract_lowertriangular(a.cell(bi, bj), b.cell(bi, bj));
            else
                a.cell(bi, bj) = b.cell(bi, bj);
        }
    }
}

template<typename matrix>
void bpack_ops<matrix>::extract_LU(bpack_view<matrix> L, bpack_view<matrix> U)
{
    ASSERT_ALWAYS(L.nrowblocks() == U.nrowblocks());
    ASSERT_ALWAYS(L.ncolblocks() == U.ncolblocks());
    for(unsigned int bi = 0 ; bi < U.nrowblocks() ; bi++) {
        for(unsigned int bj = 0 ; bj < U.ncolblocks() ; bj++) {
            if (bi < bj) {
                L.cell(bi, bj) = 0;
                /* U unchanged */
            } else if (bi == bj) {
                matrix::extract_LU(L.cell(bi, bj), U.cell(bi, bj));
            } else {
                L.cell(bi, bj) = U.cell(bi, bj);
                U.cell(bi, bj) = 0;
            }
        }
    }
}

template<typename matrix>
bool bpack_const_view<matrix>::operator==(bpack_const_view<matrix> v) const
{
    if (mblocks != v.mblocks) return false;
    if (nblocks != v.nblocks) return false;
    for(unsigned int bi = 0 ; bi < mblocks ; bi++) {
        for(unsigned int bj = 0 ; bj < nblocks ; bj++) {
            if (cell(bi, bj) != v.cell(bi, bj)) return false;
        }
    }
    return true;
}

template struct bpack_ops<mat64>;
template struct bpack_ops<mat8>;
template struct bpack_view<mat64>;
template struct bpack_view<mat8>;
template struct bpack_const_view<mat64>;
template struct bpack_const_view<mat8>;
template struct bpack<mat64>;
template struct bpack<mat8>;

template struct PLE<mat64>;
template struct PLE<mat8>;
