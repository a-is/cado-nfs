#ifndef ARITH_CONCRETE_BASE_HPP_
#define ARITH_CONCRETE_BASE_HPP_

#define ALIGNMENT_ON_ALL_BWC_VECTORS    64

/* See also MINIMUM_ITEMS_IN_BWC_CHUNKS in balancing.hpp */

/* all concrete instantiations derive from this */

struct arith_concrete_base {
    struct elt {};
};

#endif	/* ARITH_CONCRETE_BASE_HPP_ */
