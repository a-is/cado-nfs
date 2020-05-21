#include "cado.h"
#include <sstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <sstream>
#include <fstream>
#include <vector>
#include <exception>
#include <iomanip>
#include <algorithm>
#include <list>
#include <map>
#include "gzip.h"       // ifstream_maybe_compressed
#include "renumber.hpp"
#include "badideals.hpp"
#include "mpz_poly.h"   // mpz_poly
#include "rootfinder.h" // mpz_poly_roots
#include "mod_ul.h"     // modulusul_t
#include "stats.h"      // for the builder process

/* Some documentation on the internal encoding of the renumber table...
 *
 * XXX This format has changed incompatibly in 2020 XXX
 *
 * The goal is to have a table that converts to/from two formats:
 *
 *  - an integer index
 *
 *  - a triple (side, prime number p, root in [0..p]) that represents a
 *    prime ideal.
 *    (yes, p included -- that means a projective root, i.e. an ideal
 *    above p that divides J=<1,alpha>^-1).
 *
 * We want to minimize the storage, and guarantee cheap lookups in both
 * cases.
 */

/* {{{ As of 20200515, the known uses of this table in cado-nfs, taking out
 * the uses that pertain only to the building and handling of the
 * database itself, are the following.
 *
 * index_from_p_r :
 *      filter/dup2.cpp
 *      sieve/las-dlog-base.cpp
 *      sieve/fake_rels.cpp
 * 
 * indices_from_p_a_b :
 *      filter/dup2.cpp
 *      sieve/las-dlog-base.cpp  -- not yet, but maybe at some point.
 * 
 * p_r_from_index but as a forward iterator only (TODO: implement one!)
 *      filter/filter_galois.cpp
 *      filter/reconstructlog.cpp
 *      sieve/fake_rels.cpp
 *      sieve/fake_rels.cpp
 *      filter/reconstructlog.cpp
 *
 * p_r_from_index, random access:
 *      misc/convert_rels.cpp
 *
 * is_bad :
 *      filter/dup2.cpp
 *      sieve/las-dlog-base.cpp
 *      sieve/fake_rels.cpp
 *      (there, we do something very simple, and only pick a random index for a bad ideal, in [index, index+nb) )
 *
 * is_additional_column :
 *      filter/reconstructlog.cpp
 *      sieve/fake_rels.cpp
 *
 * }}} */

/* In the above uses, the lookup (side,p,r) -> index is critical in the
 * dup2 and descent steps, while the lookup index -> (side,p,r) is most
 * often used only for forward iterators, where the context can be used
 * to carry some extra information.
 */

/* There are various strategies that can be used to have an in-memory
 * structure that makes these lookups possible.
 */

constexpr const int renumber_format_traditional = 20130603; /*{{{*/
/* The traditional way that cado-nfs has been using for quite a while now
 * (2013-2020) uses the following memory layout.
 * In memory, the table is stored as a sequence of integers.  The info
 * for each prime is stored as a sequence (vp,vr0,vr1,...,vrk) with
 * vp>=vr0>...>vrk. vp is a representative for p.
 * Whenever two adjacent integers in the in-memory table are in strictly
 * increasing order, the latter is a marker that indicates a new p.
 *
 * In the traditional scheme, vr for an ideal (side,p,r) is
 * side'*(p+1)+r, where side' is the side index counted among the
 * non-rational sides. vp is (N-c)*(p+1)-1+c, with N the number of sides
 * and c is 1 if there is a rational side. (In fact, the last "+c" is
 * useless.)
 *
 * The goal of this traditional scheme is that exactly one integer
 * appears in the database for each ideal -- namely, p is always
 * implicit. This means, in particular, that when we have no rational
 * side, there is no room to store _all_ roots: one of them must be
 * sacrificed. The big advantage of this layout is that the
 * index_from_p_r lookup, after the initial dichotomy, is
 * straightforward. The big disadvantage is that p_r_from_index has to
 * recompute the roots mod p in order to return the root mod p that was
 * overwritten in order to store p. (This overhead occurs even in the
 * case of a forward iterator.)
 */
/*}}}*/

constexpr const int renumber_format_variant = 20199999; /*{{{*/
/* Variations around the previous scheme may be tried: we may try to
 * store _all_ roots on non-rational sides.
 * (The rational root is still implicit, if there is a rational side).
 * Basically, we define vp and vr as above, but we include all vr's. This
 * avoids the overhead of recomputing the roots when doing the
 * p_r_from_index lookup.
 * There are two disadvantages.
 *  - First, storage is slightly larger. Instead of one integer per
 *  ideal, we store approximately 1.4 integers. The density of ideals
 *  in either side is given by D, and the number of integers that we
 *  store is given by F, with the following magma code -- assuming
 *  generic Galois groups on both sides:
      D:=func<d1,d2|&+[m+n eq 0 select 0 else m+n where m is #Fix(s) where n is #Fix(t): s in S, t in T] * 1.0/#S/#T where S is SymmetricGroup(d1) where T is SymmetricGroup(d2)>;
      F:=func<d1,d2|&+[m+n eq 0 select 0 else 1+m+n where m is #Fix(s) where n is #Fix(t): s in S, t in T] * 1.0/#S/#T where S is SymmetricGroup(d1) where T is SymmetricGroup(d2)>;
      F(3,4)/D(3,4) is 1.4375 (on the first thought)
 *  - But the real problem is not storage. The index_from_p_r lookup is
 *  destroyed by this layout, since by making p explicit, we have data in
 *  the table that does not correspond to an ideal. This means that we
 *  need to store _more_ stuff to find the consecutive index, once we've
 *  found the position of vr in the table. We may, right after vp, store
 *  vp+I, with I the consecutive index of the first ideal above p. This
 *  means that we have in the table increasing sequences of length 2 at
 *  each p. This keeps the synchronization property. Note that the
 *  p_r_from_index lookup is now _also_ impacted, since we moved from
 *  O(1) to a mildly logarithmic cost (amortized O(1) for forward
 *  iterators). This extra info costs somewhat more. By modifying F
 *  above, we reach:
      F(3,4)/D(3,4) is 1.875 
 */
/*}}}*/

constexpr const int renumber_format_flat = 20200515; /*{{{*/
/* My personal preference would be to bite the bullet and allow for two
 * integers per ideal, always storing both p and r explicitly:
 *  - The _from_index lookups would be trivial (we could keep the encoding
 *    of r as vr = side'*(p+1)+r).
 *  - The _from_p_r lookups would also be doable with very simple-minded
 *    dichotomy (even the standard library can be put to some use here)
 *  - The database in itself would be human readable.
 */
/*}}}*/

constexpr const int renumber_format = renumber_format_variant;

/* {{{ exceptions */
renumber_t::corrupted_table::corrupted_table(std::string const & x)
    : std::runtime_error(std::string("Renumber table is corrupt: ") + x)
{}
static renumber_t::corrupted_table cannot_find_p(p_r_values_t p) {/*{{{*/
    std::ostringstream os;
    os << std::hex;
    os << "cannot find data for prime 0x" << p;
    os << " ; note: isprime(p)==" << ulong_isprime(p);
#if SIZEOF_INDEX != 8
    os << "\n"
        << "Note: above 2^32 ideals or relations,"
        << " add FLAGS_SIZE=\"-DSIZEOF_P_R_VALUES=8 -DSIZEOF_INDEX=8\""
        << " to local.sh\n";
#endif
    return renumber_t::corrupted_table(os.str());
}/*}}}*/
static renumber_t::corrupted_table cannot_find_i(index_t i) {/*{{{*/
    std::ostringstream os;
    os << std::hex;
    os << "cannot find data with index 0x" << i;
    return renumber_t::corrupted_table(os.str());
}/*}}}*/
static renumber_t::corrupted_table wrong_entry(p_r_values_t p, p_r_values_t vr) {/*{{{*/
    std::ostringstream os;
    os << std::hex;
    os << "above prime 0x" << p
        << ", the index 0x" << vr << " makes no sense";
    return renumber_t::corrupted_table(os.str());
}/*}}}*/
static renumber_t::corrupted_table prime_is_too_large(p_r_values_t p) {/*{{{*/
    std::ostringstream os;
    os << std::hex;
    os << "prime 0x" << p << " is too large!";
#if SIZEOF_INDEX != 8
    os << "\n"
        << "Note: above 2^32 ideals or relations,"
        << " add FLAGS_SIZE=\"-DSIZEOF_P_R_VALUES=8 -DSIZEOF_INDEX=8\""
        << " to local.sh\n";
#endif
    return renumber_t::corrupted_table(os.str());
}/*}}}*/
static renumber_t::corrupted_table prime_maps_to_garbage(p_r_values_t p, index_t i, p_r_values_t q)/*{{{*/
{
    std::ostringstream os;
    os << std::hex;
    os << "cached index for prime p=0x" << p;
    os << " is " << i << ", which points to q=0x" << q;
    if (renumber_format == renumber_format_flat)
        os << " (should be p)";
    else
        os << " (should be vp)";
    os << " ; note: isprime(p)==" << ulong_isprime(p);
    return renumber_t::corrupted_table(os.str());
}/*}}}*/
static renumber_t::corrupted_table cannot_find_pr(renumber_t::p_r_side x, p_r_values_t vp, p_r_values_t vr)/*{{{*/
{
    std::ostringstream os;
    os << std::hex;
    os << "cannot find p=0x" << x.p << ", r=0x" << x.r << " on side " << x.side;
    os << "; note: vp=0x" << vp << ", vr=0x" << vr;
    return renumber_t::corrupted_table(os.str());
}/*}}}*/
static renumber_t::corrupted_table cannot_find_pr(renumber_t::p_r_side x)/*{{{*/
{
    std::ostringstream os;
    os << std::hex;
    os << "cannot find p=0x" << x.p << ", r=0x" << x.r << " on side " << x.side;
    return renumber_t::corrupted_table(os.str());
}/*}}}*/
static renumber_t::corrupted_table cannot_lookup_p_a_b_in_bad_ideals(renumber_t::p_r_side x, int64_t a, uint64_t b)/*{{{*/
{
    std::ostringstream os;
    os << "failed bad ideal lookup for"
        << " (a,b)=(" << a << "," << b << ")"
        << " at p=0x" << std::hex << x.p
        << " on side " << x.side;
    return renumber_t::corrupted_table(os.str());
}/*}}}*/
static renumber_t::corrupted_table parse_error(std::string const & what, std::istream& is)/*{{{*/
{
    std::ostringstream os;
    std::string s;
    getline(is, s);
    os << "parse error (" << what << "), next to read is: " << s;
    return renumber_t::corrupted_table(os.str());
}/*}}}*/
static renumber_t::corrupted_table parse_error(std::string const & what)/*{{{*/
{
    std::ostringstream os;
    os << "parse error (" << what << ")";
    return renumber_t::corrupted_table(os.str());
}/*}}}*/
/* }}} */

/* {{{ helper functions relative to the "traditional" and "variant"
 * formats
 */

/* This one is a helper only, because we use it for both T ==
 * p_r_values_t and T == uint64_t */
template<typename T> inline T vp_from_p(T p, int n, int c)
{
    /* The final "+c" is not necessary, but we keep it for compatibility */
    int d = (renumber_format == renumber_format_traditional) ? (c - 1) : 0;
    return (n - c) * (p + 1) + d;
}

/* only used for renumber_format == renumber_format_{traditional,variant} */
p_r_values_t renumber_t::compute_vp_from_p (p_r_values_t p) const/*{{{*/
{
    int n = get_nb_polys();
    int c = (get_rational_side() >= 0);
    return vp_from_p(p, n, c);
}/*}}}*/

/* Inverse function of compute_vp_from_p */
/* only used for renumber_format == renumber_format_{traditional,variant} */
p_r_values_t renumber_t::compute_p_from_vp (p_r_values_t vp) const/*{{{*/
{
    int n = get_nb_polys();
    int c = (get_rational_side() >= 0);
    int d = (renumber_format == renumber_format_traditional) ? (c - 1) : 0;
    return (vp - d) / (n - c) - 1;
}/*}}}*/

/* only used for renumber_format == renumber_format_{traditional,variant} */
p_r_values_t renumber_t::compute_vr_from_p_r_side (renumber_t::p_r_side x) const/*{{{*/
{
    if (x.side == get_rational_side()) {
        /* the rational root _always_ has r encoded implicitly. In truth,
         * we rarely need to look it up, except that we have to decide on
         * something to store in the table...
         */
        return compute_vp_from_p(x.p);
    }
    p_r_values_t vr = x.side * (x.p + 1) + x.r;
    if (get_rational_side() >= 0 && x.side > get_rational_side())
        vr -= x.p + 1;
    return vr;
}/*}}}*/

/* only used for renumber_format == renumber_format_{traditional,variant} */
renumber_t::p_r_side renumber_t::compute_p_r_side_from_p_vr (p_r_values_t p, p_r_values_t vr) const/*{{{*/
{
    /* Note that vr is only used for the encoding of non-rational ideals.
     */
    p_r_side res { p, 0, 0 };

    res.r = vr;
    if (format == format_traditional && get_rational_side() < 0 && vr == compute_vp_from_p(p)) {
        if (traditional_get_largest_nonbad_root_mod_p(res))
            return res;
        throw wrong_entry(p, vr);
    }
    for(res.side = 0 ; res.side < (int) get_nb_polys() ; res.side++) {
        if (res.side == get_rational_side())
            continue;
        if (res.r <= p)
            return res;
        res.r -= p + 1;
    }
    if (res.r == 0) {
        if (get_rational_side() >= 0) {
            res.side = get_rational_side();
            return res;
        } else if (renumber_format == renumber_format_traditional && traditional_get_largest_nonbad_root_mod_p(res)) {
            /* that is the _really, really_ annoying thing with the
             * traditional format */
            return res;
        }
    }
    throw wrong_entry(p, vr);
}/*}}}*/
/* }}} */

/* sort in decreasing order. Faster than qsort for ~ < 15 values in r[] */
/* only used for renumber_format == renumber_format_{traditional,variant} */
/* XXX This is total legacy, and should go away soon (we only temporarily
 * keep it for measurement). See test-sort in
 * tests/utils. This code is actually slow in most cases, and clearly
 * outperformed by the code in iqsort.h
 */
inline void renumber_sort_ul (unsigned long *r, size_t n)
{
    unsigned long rmin;

    if (UNLIKELY (n < 2))
        return;

    if (UNLIKELY (n == 2)) {
        if (r[0] < r[1]) {
            rmin = r[0];
            r[0] = r[1];
            r[1] = rmin;
        }
        return;
    }

    for (size_t i = n; --i;) {
        size_t min = i;
        rmin = r[min];
        for (size_t j = i; j--;) {
            unsigned long rj = r[j];
            if (UNLIKELY (rj < rmin)) {
                min = j;
                rmin = rj;
            }
        }
        if (LIKELY (min != i)) {
            r[min] = r[i];
            r[i] = rmin;
        }
    }
}

renumber_t::cooked renumber_t::cook(unsigned long p, std::vector<std::vector<unsigned long>> & roots) const
{
    cooked C;

    size_t total_nroots = 0;

    /* Note that all_roots always a root on the rational side, even
     * though it's only a zero -- the root itself isn't computed.
     */
    for (unsigned int i = 0; i < get_nb_polys() ; i++) {
        C.nroots.push_back(roots[i].size());
        total_nroots += roots[i].size();
    }

    if (total_nroots == 0) return C;

    if (renumber_format != renumber_format_flat) {
        for (unsigned int i = 0; i < get_nb_polys() ; i++)
            renumber_sort_ul (&roots[i][0], roots[i].size());

        /* With the traditional format, the root on ratside side becomes vp.
         * If there is no ratside side or not root on ratside side for this
         * prime (i.e., lpb < p), then the largest root becomes vp.
         */
        p_r_values_t vp = compute_vp_from_p (p);

        if (renumber_format == renumber_format_variant) {
            /* The "variant" has the advantage of being fairly simple */
            C.traditional.push_back(vp);
            /* except that we'll need to tweak this field */
            C.traditional.push_back(vp);
            for (int side = get_nb_polys(); side--; ) {
                if (side == get_rational_side())
                    continue;
                for (auto r : roots[side]) {
                    p_r_side x { (p_r_values_t) p, (p_r_values_t) r, side };
                    C.traditional.push_back(compute_vr_from_p_r_side (x));
                }
            }
        } else {
            C.traditional.push_back(vp);
            /* if there _is_ a rational side, then it's an obvious
             * candidate for which root is going to be explicit. This
             * does not work if the lpb on the rational side is too
             * small, however.
             */
            int print_it = get_rational_side() >= 0 && !(p >> get_lpb(get_rational_side()));
            for (int side = get_nb_polys(); side--; ) {
                if (side == get_rational_side())
                    continue;

                for (auto r : roots[side]) {
                    if (print_it++) {
                        p_r_side x { (p_r_values_t) p, (p_r_values_t) r, side };
                        C.traditional.push_back(compute_vr_from_p_r_side (x));
                    }
                }
            }
        }
        std::ostringstream os;
        os << std::hex;
        if (renumber_format == renumber_format_variant) {
            ASSERT_ALWAYS(C.traditional.size() >= 2);
            for(auto it = C.traditional.begin() + 2 ; it != C.traditional.end() ; ++it)
                os << *it << "\n";
        } else {
            for(auto x : C.traditional)
                os << x << "\n";
        }
        C.text = os.str();
    } else {
        for (int side = 0; side < (int) get_nb_polys(); ++side) {
            for (auto r : roots[side]) {
                p_r_side x { (p_r_values_t) p, (p_r_values_t) r, side };
                C.flat.emplace_back(
                        std::array<p_r_values_t, 2> {
                            (p_r_values_t) p,
                            compute_vr_from_p_r_side (x)
                        });
            }
        }
        std::ostringstream os;
        for(auto x : C.flat)
            os << x[0] << " " << x[1] << "\n";
        C.text = os.str();
    }
    return C;
}

/* Only for the traditional format, when there is no rational side.
 *
 * Set x.r to the largest root of f modulo x.p such that (x.p,x.r) corresponds to an
 * ideal on side x.side which is not a bad ideal.
 * Note: If there is a projective root, it is the largest (r = p by convention)
 * Return true if such root mod p exists, else non-zero
 */
bool
renumber_t::traditional_get_largest_nonbad_root_mod_p (p_r_side & x) const
{
    for(x.side = get_nb_polys(); x.side--; ) {
        mpz_poly_srcptr f = cpoly->pols[x.side];
        mpz_srcptr lc = f->coeff[f->deg];
        p_r_values_t p = x.p;
        int side = x.side;
        if (x.p >> lpb[x.side]) continue;

        if (mpz_divisible_ui_p (lc, p) && !is_bad ({p, p, side})) {
            x.r = p;
            return true;
        }

        auto roots = mpz_poly_roots(cpoly->pols[side], (unsigned long) p);
        renumber_sort_ul (&roots[0], roots.size()); /* sort in decreasing order */
        for (auto r : roots) {
            if (!is_bad ({ (p_r_values_t) p, (p_r_values_t) r, side})) {
                x.r = r;
                return true;
            }
        }
    }
    return false;
}

/* return j such that min <= j <= i, and j maximal, with
 * traditional_data[j] pointing to a vp value (i.e. one of the rare
 * values j such that traditional_data[j-1] <= traditional_data[j]
 * (in the traditional_variant mode, such increases always come in pairs.
 */
index_t renumber_t::traditional_backtrack_until_vp(index_t i, index_t min) const
{
    for( ; i > min && traditional_data[i-1] > traditional_data[i] ; --i);
    if (renumber_format == renumber_format_variant) {
        if (i == min + 1)
            i--;
        else if (i > min + 1 && traditional_data[i-2] < traditional_data[i-1])
            i--;
    }
    return i;
}

/* return the number of bad ideals above x (and therefore zero if
 * x is not bad). If the ideal is bad, put in the reference [first] the
 * first index that corresponds to the bad ideals.
 */
int renumber_t::is_bad (index_t & first, p_r_side x) const
{
    if (x.p > bad_ideals_max_p) return 0;

    /* bad ideals start after the additional columns in the table. */
    first = above_add;

    for (auto const & I : bad_ideals) {
        if (x == I.first)
            return I.second.nbad;
        first += I.second.nbad;
    }
    return 0;
}

int renumber_t::is_bad (p_r_side x) const
{
    index_t first;
    return is_bad(first, x);
}

/* i-above_bad is in [0..traditional_data.size()]
 */
bool renumber_t::traditional_is_vp_marker(index_t i) const
{
    if (i == traditional_data.size()) return true;
    if (i == 0) return true;
    if (traditional_data[i] > traditional_data[i-1]) {
        if (renumber_format == renumber_format_variant) {
            ASSERT_ALWAYS(i + 1 < traditional_data.size());
            return traditional_data[i] <= traditional_data[i + 1];
        }
        return true;
    }
    return false;
}


/* XXX This is an important part of the index_from lookup. This one needs
 * to do a dichotomy, one way or another.
 *
 * Note that we return the index relative to the internal table, which is
 * shifted by [[above_bad]] compared to the indices that we return in the
 * public interface.
 */
index_t renumber_t::get_first_index_from_p(p_r_side x) const
{
    p_r_values_t p = x.p;
    p_r_values_t side = x.side;
    if (p < index_from_p_cache.size()) {
        index_t i = index_from_p_cache[p];
        if (renumber_format == renumber_format_flat) {
            if (UNLIKELY(i >= flat_data.size() || flat_data[i][0] != p))
                throw prime_maps_to_garbage(p, i, flat_data[i][0]);
        } else {
            p_r_values_t vp = compute_vp_from_p (p);
            if (UNLIKELY(i >= traditional_data.size() || traditional_data[i] != vp))
                throw prime_maps_to_garbage(p, i, traditional_data[i]);
        }
        return i;
    }

    if (UNLIKELY(p >> lpb[side]))
        throw prime_is_too_large(p);

    if (renumber_format == renumber_format_flat) {
        std::array<p_r_values_t, 2> p0 {{ p, 0 }};
        auto it = std::lower_bound(flat_data.begin(), flat_data.end(), p0);
        if (it == flat_data.end())
            throw prime_is_too_large(p);
        index_t i = it - flat_data.begin();
        if ((*it)[0] != p)
            throw prime_maps_to_garbage(p, i, (*it)[0]);
        return i;
    } else {
        /* A priori, traditional_data has exactly above_all-above_bad
         * entries. Except that it holds _only_ in the
         * renumber_format_traditional case, since we insert extra data
         * in the renumber_format_variant case
         */
        if (renumber_format == renumber_format_traditional)
            ASSERT_ALWAYS(above_all == above_bad + traditional_data.size());
        index_t max = traditional_data.size();
        index_t min = above_cache - above_bad;
        p_r_values_t vp = compute_vp_from_p (p);
        /* We have to look for i such that tab[i] == vp between min and max. */
        for( ; max > min ; ) {
            index_t i = min + (max - min) / 2; /* avoids overflow when
                                                  min + max >= UMAX(index_t) */
            i = traditional_backtrack_until_vp(i, min);
            if (traditional_data[i] == vp)
                return i;
            if (traditional_data[i] < vp) {
                if (UNLIKELY(i == min)) {
                    /* This is a corner case. We're below what we're
                     * looking for, sure, but we're actually looping.
                     * Need to break the corner case. We'll finish soon
                     * anyway, because our middle index landed inside the
                     * range for the smallest p, which is obviously truly
                     * exceptional.
                     */
                    i++;
                    for( ; i < max && !traditional_is_vp_marker(i) ; i++)
                        ;
                    if (traditional_data[i] == vp) return i;
                }
                min = i;
            } else {
                max = i;
            }
        }
        throw cannot_find_p(p);
    }
}

index_t renumber_t::index_from_p_r (p_r_side x) const
{
    index_t i;
    if (is_bad(i, x))
        return i;
    i = get_first_index_from_p(x);
    p_r_values_t vr = compute_vr_from_p_r_side(x);

    if (renumber_format == renumber_format_flat) {
        /* The "flat" format has really simple lookups */
        for( ; flat_data[i][0] == x.p ; i++) {
            if (flat_data[i][1] == vr)
                return above_bad + i;
        }
        throw cannot_find_pr(x);
    }

    p_r_values_t vp = traditional_data[i];
    /* Now i points to the beginning of data for p.
     *  tab[i] is vp.
     *  in variant format: then comes j such that the first ideal above p
     *  actually has index j
     *  then come the descriptors for all roots mod p
     */

    int outer_idx;
    if (renumber_format == renumber_format_variant) {
        /* This is the special thing about the "variant" format */
        outer_idx = above_bad + traditional_data[++i] - vp;
    } else {
        outer_idx = above_bad + i;
    }

    /* get first vr in the sequence, once we've skipped vp. Note that
     * if there's no vr at all, i might actually be the next vp already.
     */
    i++;

    /* In the "traditional" format, among all roots, the one with the
     * largest vr (which is the rational one if there is a rational side)
     * is actually missing in the table, and replaced by vp. Which makes
     * for several cases in which we want to return outer_idx
     * immediately:
     *  - an ideal on rational side always corresponds to the first
     *    element of a sequence
     *  - if i is the last index of the table
     *  - if the sequence contains only one value
     *  - if the first sequence element is less than vr
     */
    // this first case probably makes no sense in the variant format, as
    // we'll never use it when there is a rational side.
    if (x.side == get_rational_side())
        return outer_idx;
    if (i == traditional_data.size())
        return outer_idx;
    // likewise, in the variant format, vp is not used to store something
    // implicit, so there's no reason for the sequence that begins with
    // vp to be empty.
    if (vp < traditional_data[i])
        return outer_idx;
    if (vr > traditional_data[i])
        return outer_idx;
    /* otherwise we'll find it eventually. */
    if (renumber_format == renumber_format_traditional)
        outer_idx++;
    for(unsigned int j = 0 ; traditional_data[i + j] < vp ; j++) {
        if (vr == traditional_data[i + j])
            return outer_idx + j;
    }
    throw cannot_find_pr(x, vp, vr);
}

/* This used to be handle_bad_ideals in filter/filter_badideals.cpp ; in
 * fact, this really belongs here.
 */
std::pair<index_t, std::vector<int>> renumber_t::indices_from_p_a_b(p_r_side x, int e, int64_t a, uint64_t b) const
{
    /* We want this interface to be called for bad ideals only.
     *
     * A priori we don't need x.r since it can be reocmputed. However,
     * since in all cases the caller just computed this r a moment ago,
     * and it's used as a lookup key in the badideals data, let's use it.
     * x.p and x.side are of course necessary.
     *
     */

    /* bad ideals start after the additional columns in the table. */
    index_t first = above_add;
    std::vector<int> exps;
    for (auto const & I : bad_ideals) {
        if (x == I.first) {
            std::vector<int> exps(I.second.nbad, 0);

            /* work here, and return */
            for(auto J : I.second.branches) {
                int k = J.k;
                p_r_values_t pk = x.p;
                for( ; --k ; ) {
                    p_r_values_t pk1 = pk * x.p;
                    /* we want to be sure that we don't wrap around ! */
                    ASSERT_ALWAYS(pk1 > pk);
                    pk = pk1;
                }
                p_r_values_t rk = mpz_get_uint64(J.r);
                /* rk represents an element in P^1(Z/p^k). Ir rk < pk, it's
                 * (rk:1), otherwise it's (1:p*(rk-pk)) (I think)
                 */
                p_r_values_t uk = rk;
                p_r_values_t vk = 1;
                if (rk >= pk) {
                    uk = 1;
                    vk = rk - pk;
                }
                modulusul_t m;
                residueul_t ma, mb, muk, mvk;
                modul_initmod_ul (m, pk);
                modul_init (ma, m);
                modul_init (mb, m);
                modul_init (muk, m);
                modul_init (mvk, m);
                modul_set_int64 (ma, a, m);
                modul_set_uint64 (mb, b, m);
                modul_set_int64  (muk, uk, m);
                modul_set_uint64 (mvk, vk, m);
                modul_mul(ma, ma, mvk, m);
                modul_mul(mb, mb, muk, m);
                modul_sub(ma, ma, mb, m);
                if (modul_intcmp_ul(ma, 0) == 0) {
                    /* we found the right ideal ! */
                    for(int v : J.v) {
                        if (v >= 0) {
                            exps.push_back(v);
                        } else {
                            ASSERT_ALWAYS(e >= -v);
                            exps.push_back(-v);
                        }
                    }
                    return { first, exps };
                }
            }
        }
        first += I.second.nbad;
    }
    throw cannot_lookup_p_a_b_in_bad_ideals(x, a, b);
}

/* This takes an index i in the range [0, above_all-above_bad[, and returns
 * in ii the actual position of the i-th interesting data element in the
 * traditional_data[] array. i0 is the index of the corresponding vp
 * marker.
 */
void renumber_t::variant_translate_index(index_t & i0, index_t & ii, index_t i) const
{
    index_t max = traditional_data.size();
    index_t min = 0;
    index_t maxroots = 0;
    for(int side = 0 ; side < (int) get_nb_polys() ; side++) {
        maxroots += get_poly(side)->deg;
    }
    /* We have to look for i0 and i1 two consecutive vp markers
     * (possibly with i1==max) such that
     * traditional_data[i0 + 1] <= i < traditional_data[i1 + 1]
     */
    for( ; max > min ; ) {
        index_t middle = min + (max - min) / 2; /* avoids overflow */
        middle = traditional_backtrack_until_vp(middle, min);
        long delta = traditional_data[middle + 1] - traditional_data[middle];
        if (middle == min || (delta <= i && delta + maxroots > i)) {
            /* got it, probably. need to adjust a little, but that
             * will be quick */
            int run = maxroots;
            if (middle == min) run = 3 * maxroots;
            i0 = middle;
            index_t j;
            for(j = i0 + 2 ; j < max ; j++) {
                if (traditional_is_vp_marker(j)) {
                    i0 = j;
                    delta = traditional_data[i0 + 1] - traditional_data[i0];
                    j++;
                    continue;
                }
                if (run-- < 0)
                    throw cannot_find_i(above_bad + i);
                if (delta + (j-(i0 + 2)) == i)
                    break;
            }
            if (j == max)
                throw cannot_find_i(above_bad + i);
            break;
        } else if (delta < i) {
            min = middle;
        } else {
            max = middle;
        }
    }
    if (min >= max)
        throw cannot_find_i(above_bad + i);
    index_t vp = traditional_data[i0];
    unsigned int di = i - (traditional_data[i0 + 1] - vp);
    /* Note that there is no point in using the "variant" format when we
     * have a rational side.
     */
    if (get_rational_side() >= 0) {
        ii = di ? i0 + 1 + di : i0;
    } else {
        ii = i0 + 2 + di;
    }
}


/* additional columns _must_ be handled differently at this point */
renumber_t::p_r_side renumber_t::p_r_from_index (index_t i) const
{
    if (i < above_add) {
        for(int side = 0 ; side < (int) get_nb_polys() ; side++) {
            if (mpz_poly_is_monic(cpoly->pols[side]))
                continue;
            if (i == 0)
                return { 0, 0, side };
            i--;
        }
        throw corrupted_table("bad additional columns");
    }
    if (i < above_bad) {
        i -= above_add;
        for (auto const & I : bad_ideals) {
            if (i < (index_t) I.second.nbad)
                return I.first;
            i -= I.second.nbad;
        }
        throw corrupted_table("bad bad ideals");
    }
    i -= above_bad;
    if (renumber_format == renumber_format_flat) {
        p_r_values_t p = flat_data[i][0];
        p_r_values_t vr = flat_data[i][1];
        return compute_p_r_side_from_p_vr(p, vr);
    }
    if (renumber_format == renumber_format_traditional) {
        index_t i0 = traditional_backtrack_until_vp(i, 0);
        index_t vr = traditional_data[i];
        index_t vp = traditional_data[i0];
        index_t p  = compute_p_from_vp(vp);
        if (i == i0) {
            /* then we're victims of the "optimization" that uses the same
             * value to represent both the implicit root and a projective
             * root on the last side. Note that this is not the case when
             * we have a rational side.
             */
            int c = get_rational_side() == -1;
            return compute_p_r_side_from_p_vr(p, vr + c);
        }
        return compute_p_r_side_from_p_vr(p, vr);
    }
    if (renumber_format == renumber_format_variant) {
        /* That is the annoying part. lookup at i is probably not right. */
        index_t i0, ii;
        variant_translate_index(i0, ii, i);
        p_r_values_t vp = traditional_data[i0];
        p_r_values_t vr = traditional_data[ii];
        index_t p  = compute_p_from_vp(vp);
        return compute_p_r_side_from_p_vr(p, vr);
    }
}

static uint64_t previous_prime_of_powers_of_2[65] = { 0x0, 0x0, 0x3, 0x7, 0xd,
        0x1f, 0x3d, 0x7f, 0xfb, 0x1fd, 0x3fd, 0x7f7, 0xffd, 0x1fff, 0x3ffd,
        0x7fed, 0xfff1, 0x1ffff, 0x3fffb, 0x7ffff, 0xffffd, 0x1ffff7, 0x3ffffd,
        0x7ffff1, 0xfffffd, 0x1ffffd9, 0x3fffffb, 0x7ffffd9, 0xfffffc7,
        0x1ffffffd, 0x3fffffdd, 0x7fffffff, 0xfffffffb, 0x1fffffff7,
        0x3ffffffd7, 0x7ffffffe1, 0xffffffffb, 0x1fffffffe7, 0x3fffffffd3,
        0x7ffffffff9, 0xffffffffa9, 0x1ffffffffeb, 0x3fffffffff5, 0x7ffffffffc7,
        0xfffffffffef, 0x1fffffffffc9, 0x3fffffffffeb, 0x7fffffffff8d,
        0xffffffffffc5, 0x1ffffffffffaf, 0x3ffffffffffe5, 0x7ffffffffff7f,
        0xfffffffffffd1, 0x1fffffffffff91, 0x3fffffffffffdf, 0x7fffffffffffc9,
        0xfffffffffffffb, 0x1fffffffffffff3, 0x3ffffffffffffe5, 0x7ffffffffffffc9,
        0xfffffffffffffa3, 0x1fffffffffffffff, 0x3fffffffffffffc7,
        0x7fffffffffffffe7, 0xffffffffffffffc5 };


void check_needed_bits(unsigned int nbits)
{
    if (nbits > 8 * sizeof(p_r_values_t))
        throw std::overflow_error("p_r_values_t is too small to store ideals, "
                "recompile with FLAGS_SIZE=\"-DSIZEOF_P_R_VALUES=8\"\n");
}

unsigned int renumber_t::needed_bits() const
{
    /* based on the lpbs and the number of sides, return 32 or 64 */
    uint64_t p = previous_prime_of_powers_of_2[get_max_lpb()];
    uint64_t vp = vp_from_p(p, get_nb_polys(), get_rational_side() >= 0);
    if (nbits (vp) <= 32)
        return 32;
    else
        return 64;
}

void renumber_t::compute_bad_ideals_from_dot_badideals_hint(std::istream & is, unsigned int n)
{
    ASSERT_ALWAYS (renumber_format == renumber_format_traditional);
    ASSERT_ALWAYS (above_all == above_bad);
    ASSERT_ALWAYS (above_cache == above_bad);
    above_bad = above_add;
    bad_ideals_max_p = 0;

    /* the bad ideal computation is per p, so any hints that we'll see
     * triggers the insertion of all bad ideals above p. Of course when
     * there are multiple bad ideals above the same p, we must do this
     * only once.
     */
    p_r_side latest_x { 0,0,0 };

    for( ; is && n-- ; ) {
        p_r_side x;
        for(std::string s; std::ws(is).peek() == '#' ; getline(is, s) ) ;
        std::string s;
        getline(is, s);
        if (is.eof() && s.empty()) break;
        int rc = sscanf(s.c_str(), "%" SCNpr ",%" SCNpr ":%d:", &x.p, &x.r, &x.side);
        if (rc != 3)
            throw parse_error("bad ideals", is);

        if (x.same_p(latest_x)) continue;

        mpz_poly_srcptr f = cpoly->pols[x.side];
        for(badideal & b : badideals_above_p(f, x.side, x.p)) {
            above_bad += b.nbad;
            bad_ideals.emplace_back(x, std::move(b));
        }
        if (x.p >= bad_ideals_max_p)
            bad_ideals_max_p = x.p;
        latest_x = x;
    }
    above_all = above_cache = above_bad;
}

void renumber_t::read_header(std::istream& is)
{
    std::ios_base::fmtflags ff = is.flags();
    is >> std::dec;

    ASSERT_ALWAYS(above_all == above_add);
    if (renumber_format == renumber_format_traditional) {
        for(std::string s; std::ws(is).peek() == '#' ; getline(is, s) ) ;
        unsigned int nbits, nbad, nadd, nonmonic_bitmap, nbpol;
        int ratside;
        is >> nbits >> ratside >> nbad >> nadd
            >> std::hex >> nonmonic_bitmap 
            >> std::dec >> nbpol;
        for(auto & x : lpb) is >> x;
        if (!is) throw parse_error("header");
        if (::nbits(nonmonic_bitmap) > (int) nbpol)
            throw parse_error("header, bad bitmap");
        if (above_add == 0 && nadd)
            above_add = above_bad = above_cache = above_all = nadd;
        if (nbpol != get_nb_polys())
            throw std::runtime_error("incompatible renumber table -- mismatch in number of polynomials");
        if (nbits != needed_bits())
            throw std::runtime_error("incompatible renumber table -- wrong needed_bits");
        if (ratside != get_rational_side())
            throw std::runtime_error("incompatible renumber table -- different rational_side");
        if (nadd != above_add)
            throw std::runtime_error("incompatible renumber table -- mismatch in number of additional columns");
        compute_bad_ideals_from_dot_badideals_hint(is, nbad);
    } else {
        // we only have to parse the large prime bounds
        for(std::string s; std::ws(is).peek() == '#' ; getline(is, s) ) ;
        int format;
        is >> format;
        if (!is) throw parse_error("header");
        if (format != renumber_format) throw parse_error("wrong format");

        for(std::string s; std::ws(is).peek() == '#' ; getline(is, s) ) ;
        for(auto & x : lpb) is >> x;
        if (!is) throw parse_error("header");
    }
    is.flags(ff);
}

/* This reads the bad ideals section of the new-format renumber file
 */
void renumber_t::read_bad_ideals(std::istream& is)
{
    std::ios_base::fmtflags ff = is.flags();

    ASSERT_ALWAYS (renumber_format != renumber_format_traditional);
    ASSERT_ALWAYS (above_all == above_bad);
    ASSERT_ALWAYS (above_cache == above_bad);
    above_bad = above_add;
    bad_ideals_max_p = 0;
    for(int side = 0; is && side < (int) get_nb_polys(); side++) {
        for(std::string s; std::ws(is).peek() == '#' ; getline(is, s) ) ;
        int x, n;
        /* The "side, number of bad ideals" is in decimal */
        is >> std::dec;
        is >> x >> n;
        ASSERT_ALWAYS(x == side);
        for( ; n-- ; ) {
            /* The "p, r", for consistency with the rest of the renumber
             * file, are in hex, while the rest of the bad ideal
             * description is parsed in decimal. This is enforced by the
             * badideal(std::istream &) ctor.
             */
            is >> std::hex;
            badideal b(is);
            p_r_side x { (p_r_values_t) mpz_get_ui(b.p), (p_r_values_t) mpz_get_ui(b.r), side };
            bad_ideals.emplace_back(x, b);
            above_bad += b.nbad;
            if (x.p >= bad_ideals_max_p)
                bad_ideals_max_p = x.p;
        }
    }
    above_all = above_cache = above_bad;
    if (!is.good())
        throw parse_error("header, stream failed after reading bad ideals");
    is.flags(ff);
}

void renumber_t::write_header(std::ostream& os) const
{
    std::ios_base::fmtflags ff = os.flags();
    os << std::dec;
    /* The traditional format doesn't even accept comments in the very
     * first line !
     */
    if (renumber_format == renumber_format_traditional) {
        // the first line
        unsigned long nonmonic_bitmap = 0;
        for (unsigned int i = get_nb_polys(); i-- ; ) {
            nonmonic_bitmap <<= 1;
            nonmonic_bitmap += !mpz_poly_is_monic(cpoly->pols[i]);
        }
        os << needed_bits()
            << " " << get_rational_side()
            << " " << bad_ideals.size() // above_bad - above_add
            << " " << above_add
            << " " << std::hex << nonmonic_bitmap << std::dec
            << " " << get_nb_polys();
        for(auto x : lpb)
            os << " " << x;
        os << "\n";
    }

    // the comment is never significant. However only the newer formats
    // give us the opportunity to stick in a marker for the file format.
    // So if we want (for the moment) to provide old-format renumber
    // files that can be parsed by old code, we can only convey the
    // format information as an (unparsed) side note.
    os << "# Renumber file using format " << renumber_format << std::endl;

    /* Write the polynomials as comments */
    for (unsigned int i = 0; i < get_nb_polys() ; i++) {
        os << "# pol" << i << ": "
            << cxx_mpz_poly(cpoly->pols[i]).print_poly("x")
            << "\n";
    }

    if (renumber_format != renumber_format_traditional) {
        os << renumber_format << "\n";
        // number of additional columns is implicit anyway.
        os << "# large prime bounds:\n";
        for (unsigned int i = 0; i < get_nb_polys() ; i++) {
            if (i) os << " ";
            os << lpb[i];
        }
        os << "\n";
    }

    os << "# " << above_add << " additional columns\n";
    os.flags(ff);
}

void renumber_t::write_bad_ideals(std::ostream& os) const
{
    std::ios_base::fmtflags ff = os.flags();
    /* Write first the bad ideal information at the beginning of file */
    for(int side = 0; os && side < (int) get_nb_polys(); side++) {
        os << "# bad ideals on side " << side << ": ";
        {
            unsigned int n = 0;
            for(auto const & b : bad_ideals) {
                if (b.first.side == side) {
                    if (n++) os << "+";
                    n++;
                    os << b.second.nbad;
                }
            }
            if (n == 0) os << "not used";
        }
        os << std::endl;
        if (renumber_format == renumber_format_traditional) {
            for(auto const & b : bad_ideals) {
                if (b.first.side == side)
                    os << std::hex
                        << b.first.p << "," << b.first.r << ":"
                        << std::dec
                        << b.first.side
                        << ": " << b.second.nbad << "\n";
            }
        } else if (renumber_format != renumber_format_traditional) {
            unsigned int n = 0;
            for(auto const & b : bad_ideals)
                if (b.first.side == side) n++;
            os << side << " " << n << std::endl;
            for(auto const & b : bad_ideals) {
                if (b.first.side == side) {
                    // we print p,r in hexa at the beginning of the line,
                    // but the rest of the bad ideal information is in
                    // decimal.
                    os << std::hex << b.second;
                }
            }
        }
    }
    os.flags(ff);
    os << "# renumber table for all indices above " << above_bad << ":\n";
}

std::vector<int> renumber_t::get_sides_of_additional_columns() const
{
    std::vector<int> res;
    for(unsigned int side = 0 ; side < get_nb_polys() ; side++) {
        mpz_poly_srcptr f = cpoly->pols[side];
        if (f->deg > 1 && !mpz_poly_is_monic(f))
            res.push_back(side);
    }
    return res;
}

void renumber_t::use_additional_columns_for_dl()
{
    ASSERT_ALWAYS(above_all == 0);
    above_add = get_sides_of_additional_columns().size();
    above_bad = above_add;
    above_cache = above_add;
    above_all = above_add;
}

void renumber_t::compute_bad_ideals()
{
    ASSERT_ALWAYS (above_all == above_bad);
    ASSERT_ALWAYS (above_cache == above_bad);
    /* most useful for traditional format, where we need to do this on
     * every open. Well, it's super cheap anyway
     *
     * Note that in all cases, we also use this function to compute bad
     * ideals when we create the table in the first place, from
     * freerel.cpp
     */
    above_bad = above_add;
    bad_ideals_max_p = 0;
    for(int side = 0 ; side < (int) get_nb_polys() ; side++) {
        cxx_mpz_poly f(cpoly->pols[side]);
        if (f->deg == 1) continue;
        for(auto const & b : badideals_for_polynomial(f, side)) {
            p_r_values_t p = mpz_get_ui(b.p);
            p_r_values_t r = mpz_get_ui(b.r);
            p_r_side x { p, r, side };
            bad_ideals.emplace_back(x, b);
            above_bad += b.nbad;
            if (p >= bad_ideals_max_p)
                bad_ideals_max_p = p;
        }
    }
    above_all = above_cache = above_bad;
}

void renumber_t::use_cooked(p_r_values_t p, cooked & C)
{
    if (C.empty()) return;
    /* We must really use above_all - above_bad, and not
     * traditional_data.size() + flat_data.size() -- in the
     * renumber_format_variant case, they're different !
     */
    index_t pos_hard = traditional_data.size() + flat_data.size();
    above_all = use_cooked_nostore(above_all, p, C);
    traditional_data.insert(traditional_data.end(), C.traditional.begin(), C.traditional.end());
    flat_data.insert(flat_data.end(), C.flat.begin(), C.flat.end());
    if (!(p >> RENUMBER_MAX_LOG_CACHED)) {
        index_from_p_cache.insert(index_from_p_cache.end(),
                p - index_from_p_cache.size(),
                std::numeric_limits<index_t>::max());
        ASSERT_ALWAYS(index_from_p_cache.size() == p);
        index_from_p_cache.push_back(pos_hard);
        above_cache = above_all;
    }
}
index_t renumber_t::use_cooked_nostore(index_t n0, p_r_values_t p MAYBE_UNUSED, cooked & C)
{
    if (C.empty()) return n0;
    index_t pos_logical = n0 - above_bad;
    if (renumber_format == renumber_format_variant) {
        /* This fixup is required by the "variant" format. Well, it's
         * actually the whole point... */
        ASSERT_ALWAYS(C.traditional.size() >= 2);
        C.traditional[1] += pos_logical;
        std::ostringstream os;
        os << std::hex
            << C.traditional[0] << "\n"
            << C.traditional[1] << "\n"
            << C.text;
        C.text = os.str();
    }
    for(auto n : C.nroots) n0 += n;
    return n0;
}


void renumber_t::read_table(std::istream& is)
{
    for(std::string s; std::ws(is).peek() == '#' ; getline(is, s) ) ;
    if (renumber_format == renumber_format_flat) {
        for(p_r_values_t p, r ; is >> p >> r ; ) {
            flat_data.emplace_back(std::array<p_r_values_t, 2> {{ p, r }});
            above_all++;
        }
    } else {
        std::ios_base::fmtflags ff = is.flags();
        is >> std::hex;
        if (renumber_format == renumber_format_traditional) {
            for(p_r_values_t v ; is >> v ; ) {
                traditional_data.push_back(v);
                above_all++;
            }
        } else if (renumber_format == renumber_format_variant) {
            for(p_r_values_t v, vp = 0 ; is >> v ; ) {
                if (v > vp) {
                    above_all -= 2;
                    vp = v;
                }
                traditional_data.push_back(v);
                above_all++;
            }
        }
        is.flags(ff);
    }

    if (renumber_format == renumber_format_traditional || renumber_format == renumber_format_variant) {
        p_r_values_t vp = 0;
        index_t i = 0;
        index_t logical_adjust = 0;
        for( ; i < traditional_data.size() ; i++)  {
            p_r_values_t v = traditional_data[i];
            if (v <= vp) continue;
            vp = v;
            p_r_values_t p = compute_p_from_vp(vp);
            if (p >> RENUMBER_MAX_LOG_CACHED)
                break;
            index_from_p_cache.insert(index_from_p_cache.end(),
                    p - index_from_p_cache.size(),
                    std::numeric_limits<index_t>::max());
            ASSERT_ALWAYS(index_from_p_cache.size() == p);
            index_from_p_cache.push_back(i);
            if (renumber_format == renumber_format_variant) {
                i++;
                logical_adjust += 2;
            }
        }
        above_cache = above_bad + i - logical_adjust;
    } else {
        throw std::runtime_error("not implemented");
    }
}

void renumber_t::read_from_file(const char * filename)
{
    ifstream_maybe_compressed is(filename);
    read_header(is);
    if (renumber_format == renumber_format_traditional) {
        compute_bad_ideals();
    } else {
        read_bad_ideals(is);
    }
    read_table(is);
}

void renumber_t::read_from_file(const char * filename, const char * badidealinfofile)
{
    ifstream_maybe_compressed is(filename);
    read_header(is);
    if (badidealinfofile) {
        std::ifstream isi(badidealinfofile);
        read_bad_ideals_info(isi);
    } else {
        // if this function was called with badidealinfo file set to
        // NULL, assume that this reflects an intention to avoid bad
        // ideals entirely.
    }
    read_table(is);
}

void renumber_t::read_bad_ideals_info(std::istream & is)
{
    std::ios_base::fmtflags ff = is.flags();
    is >> std::dec;
    /* the badidealinfo file of old is slightly annoying to deal with.
     */
    ASSERT_ALWAYS (above_all == above_bad);
    ASSERT_ALWAYS (above_cache == above_bad);
    above_bad = above_add;
    bad_ideals_max_p = 0;
    bad_ideals.clear();
    std::map<p_r_side, badideal> met;
    for( ; is ; ) {
        for(std::string s; std::ws(is).peek() == '#' ; getline(is, s) ) ;
        if (is.eof()) break;
        p_r_values_t p, rk;
        int k, side;
        is >> p >> k >> rk >> side;
        if (!is) throw corrupted_table("bad bad ideals");
        p_r_values_t r = mpz_get_ui(badideal::r_from_rk(p, k, rk)); 
        p_r_side x { p, r, side };
        badideal b(p, r);
        badideal::branch br;
        br.k = k;
        br.r = rk;
        for(int e ; is >> e ; br.v.push_back(e));
        b.nbad = br.v.size();
        auto it = met.find(x);
        if (it != met.end() && it->second.nbad != b.nbad) {
            std::ostringstream os;
            os << "badidealinfo file is bad ;"
                << " valuation vector found in branch description"
                << " is not consistent above"
                << "(" << p << ", " << r << ", side " << side << ")";
            throw std::runtime_error(os.str());
        } else if (it == met.end()) {
            met[x] = b;
        }
        met[x].branches.emplace_back(std::move(br));
    }
    for(auto const & prb : met) {
        p_r_values_t p = prb.first.p;
        bad_ideals.emplace_back(prb);
        above_bad += prb.second.nbad;
        if (p >= bad_ideals_max_p)
            bad_ideals_max_p = p;
    }
    above_all = above_cache = above_bad;
    is.flags(ff);
}

std::string renumber_t::debug_data(index_t i) const
{
    p_r_side x = p_r_from_index (i);
    std::ostringstream os;

    os << "i=0x" << std::hex << i;

    if (is_additional_column (i)) {
        os << " tab[i]=#"
            << " added column for side " << std::dec << x.side;
    } else if (is_bad(i)) {
        os << " tab[i]=#"
            << " bad ideal";
        index_t j = i - above_add;
        for(auto const & b : bad_ideals) {
            if (j < (index_t) b.second.nbad) {
                os << std::dec
                    << " (number " << (1+j) << "/" << b.second.nbad << ")";
                break;
            }
            j -= b.second.nbad;
        }
        os  << " above"
            << " (" << x.p << "," << x.r << ")"
            << " on side " << x.side;
    } else {
        i -= above_bad;
        os << std::hex;
        if (renumber_format == renumber_format_flat) {
            os << " tab[i]=";
            os << " (0x" << flat_data[i][0] << ",0x" << flat_data[i][1] << ")";
        } else if (renumber_format == renumber_format_variant) {
            index_t i0, ii;
            variant_translate_index(i0, ii, i);
            if (i0 == ii) {
                os << " tab[0x" << i0 << "]=";
            } else {
                os << " tab[0x" << i0 << "+1+"
                    << std::dec << (ii-(i0+1)) << "]="
                    << std::hex;
            }
            os << "0x" << traditional_data[ii];
        } else {
            os << " tab[i]=";
            os << "0x" << traditional_data[i];
        }
        os << " p=0x" << x.p;
        if (x.side == get_rational_side()) {
            os << " rat";
            os << " side " << std::dec << x.side;
        } else {
            os << " r=0x" << x.r;
            os << " side " << std::dec << x.side;
            if (x.r == x.p)
                os << " proj";
        }
    }

    return os.str();
}

static int builder_switch_lcideals = 0;
void renumber_t::builder_configure_switches(cxx_param_list & pl)
{
    param_list_configure_switch(pl, "-lcideals", &builder_switch_lcideals);
}

void renumber_t::builder_declare_usage(cxx_param_list & pl)
{
    param_list_decl_usage(pl, "renumber", "output file for renumbering table");
    param_list_decl_usage(pl, "badideals", "file describing bad ideals (for DL). Only the primes are used, most of the data is recomputed anyway.");
    param_list_decl_usage(pl,
                          "lcideals",
                          "Add ideals for the leading "
                          "coeffs of the polynomials (for DL)");
}

void renumber_t::builder_lookup_parameters(cxx_param_list & pl)
{
    param_list_lookup_string(pl, "renumber");
    param_list_lookup_string(pl, "badideals");
    param_list_lookup_string(pl, "lcideals");
}

/* This is the core of the renumber table building routine. Part of this
 * code used to exist in freerel.cpp file.
 */
struct renumber_t::builder{/*{{{*/
    struct prime_chunk {/*{{{*/
        bool preprocess_done = false;
        bool end_mark = false;
        std::vector<unsigned long> primes;
        std::vector<renumber_t::cooked> C;
        prime_chunk(std::vector<unsigned long> && primes) : primes(primes) {}
        static prime_chunk end_marker() {
            prime_chunk x;
            x.end_mark = true;
            return x;
        }
        private:
        prime_chunk() = default;
    };/*}}}*/

    renumber_t & R;
    std::ostream * os_p;
    renumber_t::hook * hook;
    stats_data_t stats;
    unsigned long nprimes = 0; // sigh... *must* be ulong for stats().
    index_t R_max_index; // we *MUST* follow it externally, since we're not storing the table in memory.
    builder(renumber_t & R, std::ostream * os_p, renumber_t::hook * hook)
        : R(R)
        , os_p(os_p)
        , hook(hook)
        , R_max_index(R.get_max_index())
    {
        /* will print report at 2^10, 2^11, ... 2^23 computed primes
         * and every 2^23 primes after that */
        stats_init(stats, stdout, &nprimes, 23, "Processed", "primes", "", "p");
    }
    void progress() {
        if (stats_test_progress(stats))
            stats_print_progress(stats, nprimes, 0, 0, 0);
    }
    ~builder() {
        stats_print_progress(stats, nprimes, 0, 0, 1);
    }
    index_t operator()();
    void preprocess(prime_chunk & P);
    void postprocess(prime_chunk & P);
};/*}}}*/

void renumber_t::builder::preprocess(prime_chunk & P)/*{{{*/
{
    ASSERT_ALWAYS(!P.preprocess_done);
    /* change x (list of input primes) into the list of integers that go
     * to the renumber table, and then set "done" to true.
     * This is done asynchronously.
     */
    for(auto p : P.primes) {
        std::vector<std::vector<unsigned long>> all_roots;
        for (unsigned int side = 0; side < R.get_nb_polys(); side++) {
            std::vector<unsigned long> roots;
            mpz_poly_srcptr f = R.get_poly(side);

            if (UNLIKELY(p >> R.get_lpb(side))) {
                all_roots.emplace_back(roots);
                continue;
            } else if (f->deg == 1) {
                roots.assign(1, 0);
            } else {
                /* Note that roots are sorted */
                roots = mpz_poly_roots(f, p);
            }

            /* Check for a projective root ; append it (so that the list of roots
             * is still sorted)
             */

            if ((int) roots.size() != R.get_poly_deg(side)
                    && mpz_divisible_ui_p(f->coeff[f->deg], p))
                roots.push_back(p);

            /* take off bad ideals from the list, if any. */
            if (p <= R.get_max_bad_p()) { /* can it be a bad ideal ? */
                for (size_t i = 0; i < roots.size() ; i++) {
                    unsigned long r = roots[i];
                    if (!R.is_bad(p, r, side))
                        continue;
                    /* bad ideal -> remove this root from the list */
                    roots.erase(roots.begin() + i);
                    i--;
                }
            }
            all_roots.emplace_back(roots);
        }

        /* Data is written in the temp buffer in a way that is not quite
         * similar to the renumber table, but still close enough.
         */
        P.C.emplace_back(R.cook(p, all_roots));
    }
#pragma omp atomic write
    P.preprocess_done = true;
}/*}}}*/

void renumber_t::builder::postprocess(prime_chunk & P)/*{{{*/
{
    ASSERT_ALWAYS(P.preprocess_done);
    /* put all entries from x into the renumber table, and also print
     * to freerel_file any free relation encountered. This is done
     * synchronously.
     *
     * (if freerel_file is NULL, store only into the renumber table)
     */
    for(size_t i = 0; i < P.primes.size() ; i++) {
        p_r_values_t p = P.primes[i];
        renumber_t::cooked & C = P.C[i];

        if (hook) (*hook)(R, p, R_max_index, C);

        if (os_p) {
            R_max_index = R.use_cooked_nostore(R_max_index, p, C);
            (*os_p) << C.text;
        } else {
            ASSERT_ALWAYS(R_max_index == R.get_max_index());
            R.use_cooked(p, C);
            R_max_index = R.get_max_index();
        }

        nprimes++;
    }
    /* free memory ! */
    P.primes.clear();
    P.C.clear();
    progress();
}/*}}}*/

index_t renumber_t::builder::operator()()/*{{{*/
{
    /* Generate the renumbering table. */

    prime_info pi;
    prime_info_init(pi);
    unsigned long p = 2;
    constexpr const unsigned int granularity = 1024;
    std::list<prime_chunk> chunks;
    chunks.push_back(prime_chunk::end_marker());
    std::list<prime_chunk>::iterator next_to_postprocess = chunks.begin();
    std::list<prime_chunk>::iterator next_to_preprocess = chunks.begin();
    unsigned long lpbmax = 1UL << R.get_max_lpb();

#pragma omp parallel
    {
#pragma omp single
        {
            for (; p <= lpbmax || next_to_postprocess != next_to_preprocess ;) {
                if (p <= lpbmax) {
                    std::vector<unsigned long> pp;
                    pp.reserve(granularity);
                    for (; p <= lpbmax && pp.size() < granularity;) {
                        pp.push_back(p);
                        p = getprime_mt(pi); /* get next prime */
                    }
                    *next_to_preprocess = prime_chunk(std::move(pp));
                    chunks.push_back(prime_chunk::end_marker());
#pragma omp task firstprivate(next_to_preprocess)
                    {
                        preprocess(*next_to_preprocess);
                    }
                    next_to_preprocess++;
                }

                for (; next_to_postprocess != next_to_preprocess ; ) {
                    bool ok;
#pragma omp atomic read
                    ok = next_to_postprocess->preprocess_done;
                    if (!ok)
                        break;
                    postprocess(*next_to_postprocess);
                    chunks.erase(next_to_postprocess++);
                }
            }
        }
    }
    prime_info_clear(pi);

    return R_max_index;
}/*}}}*/

index_t renumber_t::build(hook * f)
{
    cxx_param_list pl;
    return build(pl, f);
}

index_t renumber_t::build(cxx_param_list & pl, hook * f)
{
    const char * badidealsfilename = param_list_lookup_string(pl, "badideals");
    const char * renumberfilename = param_list_lookup_string(pl, "renumber");

    if (builder_switch_lcideals)
        use_additional_columns_for_dl();
    if (badidealsfilename) {
        /* we don't really care about the .badideals file. Except that if
         * we have it at hand, we might as well use it as a set of hints
         * to avoid the trial division when rediscovering bad ideals.
         *
         * Note that it's undoubtedly more expensive than the old version
         * which was just simply reusing the .badideals file. But we do
         * more, here. And anyway we're talking a ridiculous overhead
         * compared to the rest of the computation.
         */
        std::ifstream is(badidealsfilename);
        compute_bad_ideals_from_dot_badideals_hint(is);
    } else {
        /* We can pass some hint primes as well, in a vector */
        compute_bad_ideals();
    }

    std::unique_ptr<std::ostream> out;

    if (renumberfilename) {
        out.reset(new ofstream_maybe_compressed(renumberfilename));

        write_header(*out);
        write_bad_ideals(*out);
    }

    return builder(*this, out.get(), f)();
}

