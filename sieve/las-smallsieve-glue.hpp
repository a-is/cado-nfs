#ifndef LAS_SMALLSIEVE_GLUE_HPP_
#define LAS_SMALLSIEVE_GLUE_HPP_

#include "las-smallsieve-lowlevel.hpp"

#ifdef HAVE_SSE2
#include <x86intrin.h>
#endif

/* generated code for small sieve critical routine. */

struct list_nil {};
template<typename T, typename U> struct list_car {};

template<typename T, typename U, int b, typename F> struct choice_list_car {};

template<int b> struct make_best_choice_list {
    typedef choice_list_car<
        typename best_evenline<b>::type,
                 typename best_oddline<b>::type,
                 b,
                 typename make_best_choice_list<b-1>::type> type;
};
template<> struct make_best_choice_list<-1> {
    typedef list_nil type;
};

#if defined(HAVE_GCC_STYLE_AMD64_INLINE_ASM)
// && !defined(TRACK_CODE_PATH)
#if 1
/* The selection below is really scrapping everything we've done to
 * design a code path by bits and pieces, and strives to use almost a
 * one-size-fits-all approach (which admittedly also has its advantages).
 *
 * currently it does seem to be the case that this one-size-fits-all
 * choice wins over the tailored-fit choice above (at least on my
 * laptop), reaching a 12% win as per the report of test-smallsieve -A 31
 * -I 16 -B 16 --only-complete-functions (commit faa0396)
 */
template<> struct make_best_choice_list<12> {
    typedef choice_list_car<
                assembly_generic_oldloop,
                assembly_generic_oldloop,
                3,
            /* XXX we have a bug with assembly2x here */
            choice_list_car<
                assembly2x,
                assembly2x,
                0,
            list_nil
            >> type;
};
#endif
#else
template<> struct make_best_choice_list<12> {
    typedef choice_list_car<
                manual_oldloop,
                manual_oldloop,
                0,
            list_nil
            > type;
};
#endif

/*{{{ small sieve classes */
/*{{{ tristate booleans */
/* The tribool classes offer a test(y, n, m) function: if the value of the
   tribool is true, the first parameter (y) is returned, if it is false,
   the second is returned, if it is maybe, the third is returned. */
struct tribool_maybe {
    template<typename T> static inline T test(T const &, T const &, T const & maybe) {
        return maybe;
    }
};

template<bool b>
struct tribool_const {};
template<>
struct tribool_const<true>
{
    template<typename T> static inline T test(T const & yes, T const &, T const &) {
        return yes;
    }
};
template<>
struct tribool_const<false>
{
    template<typename T> static inline T test(T const &, T const & no, T const &) {
        return no;
    }
};
/*}}}*/
/* So many things are used in common for many small sieve routines that
 * it makes sense to gather them in a common object */
template<typename is_fragment = tribool_maybe> struct small_sieve_base {/*{{{*/
    int min_logI_logB;
    int logI;
    int N;
    unsigned int log_regions_per_line;
    unsigned int region_rank_in_line;
    bool last_region_in_line;
    unsigned int j0;
    unsigned int j1;
    int i0;
    // int i1;
    /* those are (1,0,0) in the standard case */
    int sublatm;
    int sublati0;
    int sublatj0;
    int row0_is_oddj;
    bool has_origin;
    inline int F() const { return 1 << min_logI_logB; }
    inline int I() const { return 1 << logI; }
    small_sieve_base(int logI, int N, sublat_t const & sublat)/*{{{*/
        : logI(logI), N(N)
    {
        unsigned int log_lines_per_region;
        min_logI_logB         = is_fragment::test(LOG_BUCKET_REGION, logI, MIN(LOG_BUCKET_REGION, logI));
        log_lines_per_region  = (LOG_BUCKET_REGION-min_logI_logB);
        log_regions_per_line  = (logI-min_logI_logB);
        unsigned int regions_per_line      = (1<<log_regions_per_line);
        region_rank_in_line   = (N&(regions_per_line-1));
        // last_region_in_line   = (region_rank_in_line==(regions_per_line-1));
        j0    = ((N>>log_regions_per_line)<<log_lines_per_region);
        j1    = (j0+(1<<log_lines_per_region));
        // I     = (1<<logI);
        i0    = ((region_rank_in_line<<LOG_BUCKET_REGION)-(1 << (logI-1)));
        // i1    = (i0+(1<<min_logI_logB));
        // row0_is_oddj  = ((j0*sublatm+sublatj0)&1);

#if 0
        log_regions_per_line = MAX(0, logI - LOG_BUCKET_REGION);
        regions_per_line = 1 << log_regions_per_line;           
        region_rank_in_line = N & (regions_per_line - 1);       
        last_region_in_line = region_rank_in_line == (regions_per_line - 1); 
        I = 1 << logI;
        i0 = (region_rank_in_line << LOG_BUCKET_REGION) - I/2;          
        i1 = i0 + (1 << MIN(LOG_BUCKET_REGION, logI));     
        /* those are (1,0,0) in the standard case */
        row0_is_oddj = (j0*sublatm + sublatj0) & 1;
#endif
        sublatm = sublat.m ? sublat.m : 1; 
        sublati0 = sublat.i0;       
        sublatj0 = sublat.j0;       

        bool has_haxis = !j0;                                               
        bool has_vaxis = region_rank_in_line == ((regions_per_line-1)/2);   
        has_origin = has_haxis && has_vaxis;              
    }/*}}}*/

    spos_t first_position_ordinary_prime(ssp_simple_t const & ssp, unsigned int dj = 0) const/*{{{*/
    {
        /* equation here: i-r*j = 0 mod p */

        /* Expanded wrt first row j0 and sublats: smallest pos0 s.t.:
         *  sublatm*(i0+pos0)+sublati0 - r*(j0*sublatm+sublatj0) = -k*p
         *  (p is coprime to sublatm).
         *
         * In non-sublat mode, this means:  (i0+pos0)-r*j0 = 0 mod p
         * so that pos0 = (r*j0 - i0) mod p where i0 is traditionally
         * negative (that helps for sign stuff, of course).
         *
         * So S = sublatm*pos0 must be such that:
         *
         * S = r*(j0*sublatm+sublatj0) - (sublatm*i0+sublati0) + k*p
         *
         * pos0 being an integer, of course. So there's only one
         * congruence class for k mod sublatm such that the above
         * happens. We start by computing it.
         *
         *  k:=((sublati0 - r * sublatj0) * invp_mod_m) mod sublatm;
         *
         * then we deduce pos0 as:
         *
         *  pos0:=j0 * r - i0 + (((r*sublatj0-sublati0)+k*p) div sublatm)
         *
         * and finally we take pos0 mod p.
         */
        spos_t x = (spos_t)(j0 + dj) * (spos_t)ssp.get_r() - i0;
        if (sublatm > 1) {
            ASSERT(ssp.get_p() % sublatm);
            /* alternative code. not clear it's better.
               ASSERT(sublatm == 2 || sublatm == 3 || sublatm == 6);
               uint64_t invp_mod_m = p;
               int k = ((sublati0 - r * sublatj0) * invp_mod_m) % sublatm;
               x += ((r * sublatj0 - sublati0) + k * p) / sublatm;
               */
            spos_t y = ssp.get_r() * sublatj0;
            for( ; y % sublatm != sublati0 ; y += ssp.get_p());
            x += (y - sublati0) / sublatm;
        }
        x = x % (spos_t)ssp.get_p();
        /* As long as i0 <= 0, which holds in the normal case where
         * logI <= LOG_BUCKET_REGION, we can be sure that x >= 0, so
         * we have no issue with the sign.  However, when i0 is a
         * positive number, some extra care is needed.
         */
        if (x < 0) x += ssp.get_p();
        return x;
    }/*}}}*/

    /* This return value is typically logI bits larger than for ordinary
     * primes, so it makes sense to return it as a 64-bit integer.
     *
     * (more precisely, if p is, say, a 25-bit prime, and logI is 20,
     * then we return 45 bits. note however that projective primes are
     * not typically that big -- but undoubtedly it may happen).
     *
     * Quite probably we would have an interest in doing those projective
     * primes another way, and not deal with artificially shifted values.
     */
    int64_t first_position_projective_prime(ssp_t const & ssp) const/*{{{*/
    {
        /* equation here: i == (j/g)*U (mod q) */
        /* we're super-non-critical, here.
         *
         * Note that the code below could actually cover the generic
         * case as well, if we adapt by letting g=1, q=p, U=r.
         */

        /*
         * Because of the sublat feature, (i0,j0) actually correspond
         * to "expanded" indices computed as:
         *      ii = i0 * sublatm + sublati0
         *      jj = j0 * sublatm + sublatj0
         */

        unsigned int jj = j0*sublatm + sublatj0;

        /* First question is to find the next multiple of g above jj.
         * This is done as follows */

        /* next multiple of g above j0 */
        unsigned int jjmod = jj % ssp.get_g();
        if (jjmod) jj += ssp.get_g();
        jj -= jjmod;

        /* All of the fragment above trivializes to a no-op in the
         * non-projective case (which has g=1)
         */

        /* Now we'd like to avoid row number 0 (so jj == 0). */
        /* The sieving code may do some special stuff to fill the
         * position (1,0). At least at some point it did */
        if (jj == 0) jj += ssp.get_g();

        // In sublat mode, we also need jj congruent to sublatj0 mod m.
        // XXX A very nasty situation: when ssp.g and sublatm are
        // not coprime, we may very well have an infinite loop here
        // (say we want to sieve only even lines and sublatj0=1,
        // sublatm=1. For the moment, we do SSP_DISCARD_SUBLAT
        // whenever p and sublatm have a common divisor. If we want
        // to be finer grain, we need to selectively discard some
        // small sieved primes depending on the sublattice we're
        // considering (or but ssdpos to ULONG_MAX ?).
        if (sublatm > 1) {
            for( ; int(jj % sublatm) != sublatj0 ; jj += ssp.get_g());
        }
        // Find the corresponding i
        int ii = int(jj/ssp.get_g())*int(ssp.get_U());

        if (sublatm > 1) {
            for( ; ii % sublatm != sublati0 ; ii += ssp.get_q());
        }
        // In the sublat mode, switch back to reduced convention
        // (exact divisions)
        if (sublatm > 1) {
            jj = (jj-sublatj0) / sublatm;
            ii = (ii-sublati0) / sublatm;
        }
        /* At this point we know that the point (ii,jj) is one of the
         * points to be sieved, and jj-j0 perhaps is within reach for
         * this bucket region (not always). How do we adjust with
         * respect to this region's i0 ?
         */

        /* Given the value of ii, we need to know the next multiple
         * of q which is relative to the current i0. So something
         * congruent to ii mod q, but little above i0.
         */
        /* when skipping to the next line, we want to adjust with
         * respect to the very first possible i0, so virtually with
         * i0=-I/2.
         */
        int i0ref = (j0 == jj) ? i0 : (-I()/2);
        int64_t x = (ii-i0ref) % (uint64_t)ssp.get_q();
        if (x < 0) x += ssp.get_q();
        if (jj > j0) {
            x -= region_rank_in_line << LOG_BUCKET_REGION;
            x += (((uint64_t)(jj - j0))<<logI);
        }
        return x;
    }/*}}}*/

    spos_t first_position_power_of_two(ssp_t const & ssp, unsigned int dj = 0) const/*{{{*/
    {
        /* equation here: i-r*j = 0 mod p, p a power of 2. */
        /* only difference with ordinary case is that we want to
         * sieve only odd lines.
         */
        // FIXME: this is probably broken with sublattices (well,
        // let's give it a try).
        /* For powers of 2, we sieve only odd lines (*) and 
         * ssdpos needs to point at line j=1. We assume
         * that in this case (si.I/2) % p == 0
         * (*) for lines with j even, we have a root mod the prime
         * power for i-j*r multiple of our power of 2, which means
         * i even too. Thus a useless report.
         */
        unsigned int j = j0 + dj;
        uint64_t jj = j*sublatm + sublatj0;
        // uint64_t ii = i0*sublatm + sublati0;
        /* next odd line */
        jj |= 1;
        int i0ref = (j == jj) ? i0 : (-I()/2);
        spos_t x = (spos_t)jj * (spos_t)ssp.get_r();
        if (sublatm > 1) {
            for( ; x % sublatm != sublati0 ; x += ssp.get_p());
            x = (x - sublati0) / sublatm;
        }
        x = (x - i0ref) & (ssp.get_p() - 1);
        if (x < 0) x += ssp.get_p();
        /* our target is position x in the bucket region which starts
         * at coordinates (i0ref, jj). How far is that from us ?
         */
        if (jj > j) {
            x -= region_rank_in_line << LOG_BUCKET_REGION;
            x += (((uint64_t)(jj - j0))<<logI);
            /* For the case of several bucket regions per line, it's
             * clear that this position will be outside the current
             * bucket region. Note that some special care is needed
             * when we want to incrementally update the position, but we
             * rarely (if ever) want to do that anyway.
             */
        }
        return x;
    }/*}}}*/

    /* This function returns the first location hit, relative to the start
       of the current *line fragment*. dj is the line number being sieved
       relative to the bucket region start. If a bucket region contains only
       one line, or only a fragment of a line, then dj must be 0. The line
       index jj being sieved must be divisible by ssp.g. Line jj=0 is not
       treated any different than non-zero lines. */
    int64_t first_position_in_line_fragment_projective_prime(ssp_t const & ssp, unsigned int dj = 0) const
    {
        const unsigned int j = j0 + dj;
        const unsigned int jj = j * sublatm + sublatj0;
        
        ASSERT (ssp.is_proj());
        ASSERT (jj % ssp.get_g() == 0);

        int64_t ii = int64_t(jj / ssp.get_g()) * int64_t(ssp.get_U());

        if (sublatm > 1) {
            for( ; ii % sublatm != sublati0 ; ii += ssp.get_q());
        }

        const int i = (sublatm == 1) ? ii : (ii - sublati0) / sublatm;

        int64_t x = (i - i0) % (uint64_t)ssp.get_q();
        if (x < 0) x += ssp.get_q();
        ASSERT_ALWAYS(0 <= x && x < ssp.get_q());
        return x;
    }

};/*}}}*/

/* {{{ overrun is actually a direct dependent of the <is_fragment>
 * parameter. If we wish to encapsulate it inside
 * small_sieve<is_fragment>, it could be
 * that the syntax deserves a simplification. */
template<bool b> class overrun_t {
    size_t x = 0;
    public:
    operator size_t() const { return x; }
    overrun_t operator=(size_t a) { x=a; return *this; }
};

template<> class overrun_t<false> {
    public:
        operator size_t() const { return 0; }
        inline overrun_t operator=(size_t) { return *this; }
};
/* }}} */

template<bool is_fragment>
struct small_sieve : public small_sieve_base<tribool_const<is_fragment>> {/*{{{*/
    typedef small_sieve_base<tribool_const<is_fragment>> super;
    std::vector<int> & positions;
    std::vector<ssp_simple_t> const& primes;
    size_t sorted_limit;
    std::list<size_t> sorted_subranges;
    std::vector<ssp_t> const& not_nice_primes;
    unsigned char * S;
    int nthreads;

    static const fbprime_t pattern2_size = 2 * sizeof(unsigned long);
    static const int test_divisibility = 0; /* very slow, but nice for debugging */

    /* This "index" field is actually the loop counter that is shared by
     * the various instantiations that are triggered by do_it<>.
     */
    size_t index = 0;

    small_sieve(std::vector<int>& positions,
            std::vector<ssp_simple_t> const & primes,
            std::vector<ssp_t> const & not_nice_primes,
            unsigned char*S, int logI,
            unsigned int N,
            sublat_t const & sublat,
            int nthreads
            )
        : super(logI, N, sublat),
            positions(positions),
            primes(primes), 
            not_nice_primes(not_nice_primes),
            S(S), nthreads(nthreads) {
                auto s = begin(primes);
                auto s0 = s;
                auto e = end(primes);
                for( ; e > s ; ) {
                    if ((e-s) < 32) {
                        /* we don't want to bother adding an extra
                         * control loop for a small bunch of primes 
                         */
                        break;
                    }
                    int c;
                    for(c = 1 ; c < (e-s) && !(s[c] < s[c-1]) ; c++);
                    if (c <= 16)
                        fprintf(stderr, "warning, the prime list looks really ugly\n");
                    /*
                    fprintf(stderr, "ssp entries [%zd..%zd[ (out of %zu) are sorted\n",
                            s-s0, s+c-s0, primes.size());
                            */
                    sorted_subranges.push_back((s+c) - s0);
                    s += c;
                }
                /*
                fprintf(stderr, "ssp entries [%zd..%zd[ (tail) do not need to be sorted\n",
                        s-s0, primes.size());
                        */
            }

    bool finished() const { return index == primes.size(); }
    bool finished_sorted_prefix() const { return index == sorted_limit; }

    /* this one is instantiated outside the class */
    inline void after_region_adjust(spos_t & p_pos, spos_t pos, overrun_t<is_fragment> const & overrun, ssp_simple_t const & ssp) const;

    /* most fields from the parent class must be redeclared here if we
     * want to use them transparently -- it's only a shorthand
     * convenience. We don't re-expose everything though, since some are
     * only seldom used.
     */
    using super::logI;
    using super::i0;
    using super::j0;
    using super::j1;
    using super::sublatj0;
    using super::I;
    using super::F;

    void handle_projective_prime(ssp_t const & ssp, where_am_I & w MAYBE_UNUSED) {/*{{{*/
        /* This code also covers projective powers of 2 */
        const fbprime_t q = ssp.get_q();
        const fbprime_t g = ssp.get_g();
        const size_t gI = (size_t)ssp.get_g() << logI;
        const fbprime_t U = ssp.get_U();
        const fbprime_t p MAYBE_UNUSED = g * q;
        WHERE_AM_I_UPDATE(w, p, p);
        const unsigned char logp = ssp.logp;
        /* Sieve the projective primes. We have
         *         p^index | fij(i,j)
         * for i,j such that
         *         i * g == j * U (mod p^index)
         * where g = p^l and gcd(U, p) = 1.
         * This hits only for g|j, then j = j' * g, and
         *         i == j' * U (mod p^(index-l)).
         * In every g-th line, we sieve the entries with
         *         i == (j/g)*U (mod q).
         * In ssd we have stored g, q = p^(index-l), U, and ssdpos so
         * that S + ssdpos is the next sieve entry that needs to be
         * sieved.  So if S + ssdpos is in the current bucket region,
         * we update all  S + ssdpos + n*q  where ssdpos + n*q < I,
         * then set ssdpos = ((ssdpos % I) + U) % q) + I * g.  */
        if (!test_divisibility && ssp.get_q() == 1)
        {
            /* q = 1, therefore U = 0, and we sieve all entries in lines
               with g|j, beginning with the line starting at S[ssdpos] */
            unsigned long logps;
            long_spos_t pos = super::first_position_projective_prime(ssp);

            // The following is for the case where p divides the norm
            // at the position (i,j) = (1,0).
            if (UNLIKELY(super::has_origin && pos == (long_spos_t) gI)) {
#ifdef TRACE_K
                if (trace_on_spot_Nx(w.N, (1-i0))) {
                    WHERE_AM_I_UPDATE(w, x, trace_Nx.x);
                    unsigned int v = logp;
                    sieve_increase_logging(S + w.x, v, w);
                }
#endif
                S[1 - i0] += logp;
            }
            // The event SSP_DISCARD might have occurred due to
            // the first row to sieve being larger than J. The row
            // number 0 must still be sieved in that case, but once
            // it's done, we can indeed skip the next part of
            // sieving.
            if (ssp.is_discarded_proj())
                return;
            ASSERT (ssp.get_U() == 0);
            ASSERT (pos % F() == 0);
            ASSERT (F() % (4 * sizeof (unsigned long)) == 0);
            for (size_t x = 0; x < sizeof (unsigned long); x++)
                ((unsigned char *)&logps)[x] = logp;
            unsigned int j = j0 + (pos >> logI);
            for( ; j < j1 ; j += g) {
                /* our loop is over line fragments that have a hit,
                 * and by the condition q=1 above we'll sieve them
                 * completely */
                unsigned long *S_ptr = (unsigned long *) (S + pos);
                unsigned long *S_end = (unsigned long *) (S + pos + F());
                unsigned long logps2 = logps;
                if (!(j&1)) {
                    /* j is even. We update only odd i-coordinates */
                    /* Use array indexing to avoid endianness issues. */
                    for (size_t x = 0; x < sizeof (unsigned long); x += 2)
                        ((unsigned char *)&logps2)[x] = 0;
                }
#ifdef TRACE_K
                if (trace_on_range_Nx(w.N, pos, pos + F())) {
                    WHERE_AM_I_UPDATE(w, x, trace_Nx.x);
                    unsigned int x = trace_Nx.x;
                    unsigned int index = x % I();
                    unsigned int v = (((unsigned char *)(&logps2))[index%sizeof(unsigned long)]);
                    sieve_increase_logging(S + w.x, v, w);
                }
#endif
                for( ; S_ptr < S_end ; S_ptr += 4) {
                    S_ptr[0] += logps2;
                    S_ptr[1] += logps2;
                    S_ptr[2] += logps2;
                    S_ptr[3] += logps2;
                }
                pos += gI;
            }
#if 0
            ssdpos[index] = pos - (1U << LOG_BUCKET_REGION);
#endif
        } else {
            /* q > 1, more general sieving code. */
            spos_t pos = super::first_position_projective_prime(ssp);
            const fbprime_t evenq = (q % 2 == 0) ? q : 2 * q;
            unsigned char * S_ptr = S;
            S_ptr += pos - (pos & (I() - 1U));
            unsigned int j = j0 + (pos >> logI);
            pos = pos & (I() - 1U);
            ASSERT (U < q);
            for( ; j < j1 ; S_ptr += gI, j+=g) {
                WHERE_AM_I_UPDATE(w, j, j - j0);
                unsigned int q_or_2q = q;
                int i = pos;
                if (!(j&1)) {
                    /* j even: sieve only odd i values */
                    if (i % 2 == 0) /* Make i odd */
                        i += q;
                    q_or_2q = evenq;
                }
                if ((i|j) & 1) { /* odd j, or not i and q both even */
                    for ( ; i < F(); i += q_or_2q) {
                        WHERE_AM_I_UPDATE(w, x, (S_ptr - S) + i);
                        sieve_increase (S_ptr + i, logp, w);
                    }
                }

                pos += U;
                if (pos >= (spos_t) q) pos -= q;
            }
#if 0
            ssdpos[index] = linestart + lineoffset - (1U << LOG_BUCKET_REGION);
#endif
        }
    }/*}}}*/
    void handle_power_of_2(ssp_t const & ssp, where_am_I & w MAYBE_UNUSED) {/*{{{*/
        /* Powers of 2 are treated separately */
        /* Don't sieve powers of 2 again that were pattern-sieved */
        const fbprime_t p = ssp.get_p();
        const fbprime_t r = ssp.get_r();
        WHERE_AM_I_UPDATE(w, p, p);

        if (ssp.is_pattern_sieved())
            return;

        const unsigned char logp = ssp.logp;
        unsigned char *S_ptr = S;

        int pos = super::first_position_power_of_two(ssp);

        unsigned int j = j0;
        /* Our encoding is that when the first row is even, the
         * pos we keep in the ssdpos array deliberately includes an
         * additional quantity I to mean ``next row''. Compensate
         * that, move on to an odd row, and proceed.
         *
         * With several bucket regions in a line, we adjust by only
         * (i1-i0).  Note that several bucket regions in a line also
         * means only one one line per bucket... So here we'll end up
         * subtracting I in several independent chunks, while I know
         * in advance that I could have done that subtraction all in
         * one go. But really, who cares, at the end of the day.
         */

        if (j % 2 == 0) {
            ASSERT(pos >= F());
            pos -= F(); S_ptr += F();
            j++;
        }
        if (j < j1) pos &= (p-1);
        for( ; j < j1 ; j+= 2) {
            for (int i = pos; i < F(); i += p) {
                WHERE_AM_I_UPDATE(w, x, ((size_t) (j-j0) << logI) + i);
                sieve_increase (S_ptr + i, logp, w);
            }
            // odd lines only.
            pos = (pos + (r << 1)) & (p - 1);
            S_ptr += I();
            S_ptr += I();
        }
#if 0
        /* see above. Because we do j+=2, we have either j==j1 or
         * j==j1+1 */
        if (j > j1) pos += I;
        ssdpos[index] = pos;
#endif
    }/*}}}*/
    template<typename even_code, typename odd_code, int bits_off>
    bool handle_nice_prime(ssp_simple_t const & ssp, spos_t & p_pos, where_am_I & w) {/*{{{*/
        const fbprime_t p = ssp.get_p();
        if (bits_off && (p >> (super::min_logI_logB + 1 - bits_off))) {
            /* time to move on to the next bit size; */
            return false;
        }

        spos_t pos = p_pos;

        const fbprime_t r = ssp.get_r();
        const unsigned char logp = ssp.logp;
        unsigned char * S0 = S;
        unsigned char * S1 = S + F();

        overrun_t<is_fragment> overrun;

        /* we sieve over the area [S0..S0+(i1-i0)] (F() is (i1-i0)),
         * which may actually be just a fragment of a line. After
         * that, if (i1-i0) is different from I, we'll break anyway.
         * So whether we add I or (i1-i0) to S0 does not matter much.
         */

        bool row0_even = (((j0&super::sublatm)+super::sublatj0) & 1) == 0;
        bool dj_row0_evenness = (super::sublatm & 1);
        bool even = row0_even;

        for(unsigned int j = j0; j < j1; j++) {
            WHERE_AM_I_UPDATE(w, j, j - j0);
            if (sublatj0 == 0 && j0 == 0 && j == 0) {
                /* A nice prime p hits in line j=0 only in locations
                   where p|i, so no need to sieve those. */
            } else if (even) {
                /* for j even, we sieve only odd pi, so step = 2p. */
                {
                    spos_t xpos = ((super::sublati0 + pos) & 1) ? pos : (pos+p);
                    overrun = even_code()(S0, S1, S0 - S, xpos, p+p, logp, w);
                }
            } else {
                overrun = odd_code()(S0, S1, S0 - S, pos, p, logp, w);
            }
            S0 += I();
            S1 += I();
            pos += r; if (pos >= (spos_t) p) pos -= p; 
            even ^= dj_row0_evenness;
        }
        after_region_adjust(p_pos, pos, overrun, ssp);
        return true;
    }/*}}}*/

    /* This function is responsible for small-sieving primes of a
     * specific bit size */
    template<typename even_code, typename odd_code, int bits_off>
        inline void handle_nice_primes(where_am_I & w MAYBE_UNUSED) /* {{{ */
        {
            /* here, we can sieve for primes p < 2 * F() / 2^bits_off,
             * (where F() is i1-i0 = 2^min(logI, logB)).
             *
             * meaning that the number of hits in a line is at least
             * floor(F() / p) = 2^(bits_off-1)
             *
             * Furthermore, if p >= 2 * F() / 2^(bits_off+1), we can also
             * say that the number of hits is at most 2^bits_off
             */

#ifdef HAVE_SSE41
            bool row0_even = (((j0&super::sublatm)+super::sublatj0) & 1) == 0;
            bool dj_row0_evenness = (super::sublatm & 1);

            for( ; index + 3 < sorted_limit ; index+=4) {
                /* find 4 index values with no special prime */
                ssp_simple_t const & ssp0(primes[index]);
                spos_t & p_pos0(positions[index]);
                ssp_simple_t const & ssp1(primes[index+1]);
                spos_t & p_pos1(positions[index+1]);
                ssp_simple_t const & ssp2(primes[index+2]);
                spos_t & p_pos2(positions[index+2]);
                ssp_simple_t const & ssp3(primes[index+3]);
                spos_t & p_pos3(positions[index+3]);


                const fbprime_t p0 = ssp0.get_p();
                const fbprime_t p1 = ssp1.get_p();
                const fbprime_t p2 = ssp2.get_p();
                const fbprime_t p3 = ssp3.get_p();

                if (bits_off && (p0 >> (super::min_logI_logB + 1 - bits_off))) {
                    /* time to move on to the next bit size; */
                    return;
                }
                if (bits_off && (p3 >> (super::min_logI_logB + 1 - bits_off))) {
                    break;
                }

                static_assert(std::is_same<spos_t, int32_t>::value, "spos_t must be an int32_t for the sse2 code to be valid");

                __m128i p = _mm_setr_epi32(p0, p1, p2, p3);
                __m128i r = _mm_setr_epi32(ssp0.get_r(), ssp1.get_r(), ssp2.get_r(), ssp3.get_r());
                __m128i pos = _mm_setr_epi32(p_pos0, p_pos1, p_pos2, p_pos3);
                const unsigned char logp0 = ssp0.logp;
                const unsigned char logp1 = ssp1.logp;
                const unsigned char logp2 = ssp2.logp;
                const unsigned char logp3 = ssp3.logp;

                unsigned char * S0 = S;
                unsigned char * S1 = S + F();

                overrun_t<is_fragment> overrun0;
                overrun_t<is_fragment> overrun1;
                overrun_t<is_fragment> overrun2;
                overrun_t<is_fragment> overrun3;

                /* we sieve over the area [S0..S0+(i1-i0)] (F() is (i1-i0)),
                 * which may actually be just a fragment of a line. After
                 * that, if (i1-i0) is different from I, we'll break anyway.
                 * So whether we add I or (i1-i0) to S0 does not matter much.
                 */
                __m128i ones = _mm_set1_epi32(1);
                __m128i sublati0 = _mm_set1_epi32(super::sublati0);
                bool even = row0_even;
                for(unsigned int j = j0 ; j < j1; j++) {
                    WHERE_AM_I_UPDATE(w, j, j - j0);
                    /* the if() branch here that the compiler and/or the
                     * branch predictor are smart enough to make its code
                     * reduce to almost zero */
                    if (sublatj0 == 0 && j0 == 0 && j == 0) {
                        /* A nice prime p hits in line j=0 only in locations
                           where p|i, so no need to sieve those. */
                    } else if (even) {
                        /* for j even, we sieve only odd pi, so step = 2p. */
                        __m128i xpos =
                            _mm_add_epi32(pos,
                                    _mm_and_si128(p,
                                        _mm_cmplt_epi32(
                                            _mm_and_si128(
                                                _mm_add_epi32(sublati0, pos),
                                                ones),
                                            ones)
                                        )
                                    )
                            ;
                        WHERE_AM_I_UPDATE(w, p, p0);
                        overrun0 = even_code()(S0, S1, S0 - S, _mm_extract_epi32(xpos, 0), p0+p0, logp0, w);
                        WHERE_AM_I_UPDATE(w, p, p1);
                        overrun1 = even_code()(S0, S1, S0 - S, _mm_extract_epi32(xpos, 1), p1+p1, logp1, w);
                        WHERE_AM_I_UPDATE(w, p, p2);
                        overrun2 = even_code()(S0, S1, S0 - S, _mm_extract_epi32(xpos, 2), p2+p2, logp2, w);
                        WHERE_AM_I_UPDATE(w, p, p3);
                        overrun3 = even_code()(S0, S1, S0 - S, _mm_extract_epi32(xpos, 3), p3+p3, logp3, w);
                    } else {
                        WHERE_AM_I_UPDATE(w, p, p0);
                        overrun0 = odd_code()(S0, S1, S0 - S, _mm_extract_epi32(pos, 0), p0, logp0, w);
                        WHERE_AM_I_UPDATE(w, p, p1);
                        overrun1 = odd_code()(S0, S1, S0 - S, _mm_extract_epi32(pos, 1), p1, logp1, w);
                        WHERE_AM_I_UPDATE(w, p, p2);
                        overrun2 = odd_code()(S0, S1, S0 - S, _mm_extract_epi32(pos, 2), p2, logp2, w);
                        WHERE_AM_I_UPDATE(w, p, p3);
                        overrun3 = odd_code()(S0, S1, S0 - S, _mm_extract_epi32(pos, 3), p3, logp3, w);
                    }
                    S0 += I();
                    S1 += I();
                    pos = _mm_add_epi32(pos, r);
                    pos = _mm_sub_epi32(pos, _mm_andnot_si128(_mm_cmplt_epi32(pos, p), p));
                    even ^= dj_row0_evenness;
                }
                after_region_adjust(p_pos0, _mm_extract_epi32(pos, 0), overrun0, ssp0);
                after_region_adjust(p_pos1, _mm_extract_epi32(pos, 1), overrun1, ssp1);
                after_region_adjust(p_pos2, _mm_extract_epi32(pos, 2), overrun2, ssp2);
                after_region_adjust(p_pos3, _mm_extract_epi32(pos, 3), overrun3, ssp3);
            }
#endif

            for( ; index < sorted_limit ; index++) {
                ssp_simple_t const & ssp(primes[index]);
                spos_t & p_pos(positions[index]);
                const fbprime_t p = ssp.get_p();
                if (bits_off && (p >> (super::min_logI_logB + 1 - bits_off))) {
                    /* time to move on to the next bit size; */
                    return;
                }
                WHERE_AM_I_UPDATE(w, p, p);
                handle_nice_prime<even_code, odd_code, bits_off>(ssp, p_pos, w);
            }

        }/*}}}*/
    private:
    /* {{{ template machinery to make one single function out of several */
    /* we'll now craft all the specific do_it functions into one big
     * function. Because we're playing tricks with types and lists of
     * types and such, we need to work with partial specializations at
     * the class level, which is admittedly messy. */
    template<typename T, int max_bits_off = INT_MAX>
        struct do_it
        {
            void operator()(small_sieve<is_fragment> & SS, where_am_I &) {
                /* default, should be at end of list. We require that we
                 * are done processing, at this point. */
                ASSERT_ALWAYS(SS.finished_sorted_prefix());
            }
        };

    /* optimization: do not split into pieces when we have several times
     * the same code anyway. */
    template<typename E0, typename O0, int b0, int b1, typename T, int bn>
        struct do_it<choice_list_car<E0,O0,b0,
                     choice_list_car<E0,O0,b1,
                    T>>, bn>
        {
            static_assert(b0 > b1, "choice list is in wrong order");
            void operator()(small_sieve<is_fragment> & SS, where_am_I & w) {
                /*
                SS.handle_nice_primes<E0, O0, b1>(w);
                do_it<T>()(SS, w);
                */
                do_it<choice_list_car<E0,O0,b1, T>, bn>()(SS, w);
            }
        };
    template<typename E0, typename O0, int b0, int bn>
        struct is_compatible_for_range {
            static_assert(bn > b0, "choice list is in wrong order");
            static const int value =
                E0::template is_compatible<bn-1>::value && 
                O0::template is_compatible<bn>::value && 
                is_compatible_for_range<E0, O0, b0, bn-1>::value;
        };
    template<typename E0, typename O0, int b0>
        struct is_compatible_for_range<E0, O0, b0, b0> {
            static const int value =
                E0::template is_compatible<b0-1>::value && 
                O0::template is_compatible<b0>::value;
        };
    template<typename E0, typename O0, int b0>
        struct is_compatible_for_range<E0, O0, b0, INT_MAX> {
            static const int value =
                E0::template is_compatible<INT_MAX>::value && 
                O0::template is_compatible<INT_MAX>::value &&
                is_compatible_for_range<E0, O0, b0, b0 + 5>::value;
        };

    template<typename E0, typename O0, int b0, typename T, int bn>
        struct do_it<choice_list_car<E0,O0,b0,T>, bn>
        {
            static_assert(is_compatible_for_range<E0, O0, b0, bn>::value, "Cannot use these two code fragments for primes of the current size");
            void operator()(small_sieve<is_fragment> & SS, where_am_I & w) {
                SS.handle_nice_primes<E0, O0, b0>(w);
                do_it<T, b0-1>()(SS, w);
            }
        };
    /* }}} */

    public:
    void do_pattern_sieve(where_am_I &);

    void normal_sieve(where_am_I & w) {
        for(size_t s : sorted_subranges) {
            sorted_limit = s;
            /* This function will eventually call handle_nice_primes on
             * sub-ranges of the set of small primes. */
            typedef make_best_choice_list<12>::type choices;
            small_sieve<is_fragment>::do_it<choices>()(*this, w);
        }

        /* This is for the tail of the list. We typically have powers,
         * here. These are ordinary, nice, simple prime powers, but the
         * only catch is that these don't get resieved (because the prime
         * itself was already divided out, either via trial division or
         * earlier resieving. By handling them here, we benefit from the
         * ssdpos table. */
        for( ; index < primes.size() ; index++) {
            ssp_simple_t const & ssp(primes[index]);
            spos_t & p_pos(positions[index]);
            WHERE_AM_I_UPDATE(w, p, ssp.get_p());
            typedef default_smallsieve_inner_loop even_code;
            typedef default_smallsieve_inner_loop odd_code;
            handle_nice_prime<even_code, odd_code, 0>(ssp, p_pos, w);
        }
    }

    void exceptional_sieve(where_am_I & w) {
        /* a priori we'll never have "nice" primes here, but we're not
         * forced to rule it out completely, given that we have the code
         * available at hand. The only glitch is that we're storing the
         * start positions *only* for the primes in the ssps array. */


        for(auto const & ssp : not_nice_primes) {
#if 0
            spos_t & p_pos(positions[index]);
            if (ssp.is_nice()) {
                typedef assembly_generic_oldloop even_code;
                typedef assembly_generic_oldloop odd_code;
                handle_nice_prime<even_code, odd_code, 0>(ssp, p_pos, w);
            } else
#endif
            if (ssp.is_pattern_sieved()) {
                /* This ssp is pattern-sieved, nothing to do here */
            } else if (ssp.is_proj()) {
                handle_projective_prime(ssp, w);
            } else if (ssp.is_pow2()) {
                handle_power_of_2(ssp, w);
            } else {
                /* I don't think we can end up here.  */
                ASSERT_ALWAYS(0);
            }
        }
    }

};/*}}}*/

/* {{{ specific instantiations for the after-bucket-region adjustments */
template<>
void small_sieve<true>::after_region_adjust(int & p_pos, int pos, overrun_t<true> const & overrun, ssp_simple_t const & ssp) const
{
    const fbprime_t r = ssp.get_r();
    const fbprime_t p = ssp.get_p();
    /* quick notes for incremental adjustment in case I>B (B =
     * LOG_BUCKET_REGION).
     *
     * Let q = 2^(I-B).  Let N = a*q+b, and N'=N+nthreads=a'*q+b' ;
     * N' is the next bucket region we'll handle.
     *
     * Let nthreads = u*q+v
     *
     * The row increase is dj = (N' div q) - (N div q) = a'-a
     * The fragment increase is di = (N' mod q) - (N mod q) = b'-b
     *
     * Of course we have -q < b'-b < q
     *
     * dj can be written as (N'-N-(b'-b)) div q, which is an exact
     * division. we rewrite that as:
     *
     * dj = u + (v - (b'-b)) div q
     *
     * where the division is again exact. Now (v-(b'-b))
     * satisfies:
     * -q < v-(b'-b) < 2*q-1
     *
     * so that the quotient may only be 0 or 1.
     *
     * It is 1 if and only if v >= q + b'-b, which sounds like a
     * reasonable thing to check.
     */
    /* available stuff, for computing the next position:
     * p_pos was the position of the first hit on the first line
     *       in this bucket.
     * pos is the position of the first hit on the next line just
     *       above this bucket (possibly +p if we just sieved an
     *       even line -- this is a catch, by the way).
     * overrun is the position of the first hit on the last line
     *       a bucket that would be on the right of this one
     *       (even if we're at the end of a line of buckets. In
     *       effect, (overrun-p_pos) is congruent to B mod p when
     *       the bucket is a single line fragment of size B.
     */
    int N1 = N + nthreads;
    int dj = (N1>>log_regions_per_line) - j0;
    unsigned int regions_per_line      = (1<<log_regions_per_line);
    int di = (N1&(regions_per_line-1)) - (N&(regions_per_line-1));
    /* Note that B_mod p is not reduced. It may be <0, and
     * may also be >= p if we sieved with 2p because of even j
     *
     * (0 <= overrun < 2p), and (0 <= pos < p), so -p < B_mod_p < 2p
     */
    ASSERT(overrun < (size_t) 2*p);
    ASSERT(p_pos < (spos_t) p);
    int B_mod_p = overrun - p_pos;
    /* FIXME: we may avoid some of the cost for the modular
     * reduction, here. Having a mod operation in this place
     * seems to be a fairly terrible idea.
     *
     * dj is either always the same thing, or that same thing +1.
     * di is within a small interval (albeit a centered one).
     * 
     * It seems feasible to get by with a fixed number of
     * conditional subtractions.
     */
    pos = (p_pos + B_mod_p * di + dj * r) % p;
    if (pos < 0) pos += p;
    p_pos = pos;
}

template<>
void small_sieve<false>::after_region_adjust(int & p_pos, int pos, overrun_t<false> const &, ssp_simple_t const & ssp) const
{
    /* skip stride */
    const fbprime_t p = ssp.get_p();
    pos += ssp.get_offset();
    if (pos >= (spos_t) p) pos -= p;
    p_pos = pos;
}
/*}}}*/
/*}}}*/


#endif	/* LAS_SMALLSIEVE_GLUE_HPP_ */
