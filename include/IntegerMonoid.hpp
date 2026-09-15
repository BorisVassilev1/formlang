#pragma once

#include "concepts.hpp"

namespace fl {

template <class int_type = uint64_t>
class IntegerMonoid {
   public:
	using Value = int_type;

	static constexpr Value identity = 0;
	constexpr Value mul(Value a, Value b) const { return a + b; }
	constexpr Value invMul(Value a, Value b) const { return b - a; }
	constexpr Value gen(Value a) const { return a; }
	constexpr bool equal(Value a, Value b) const { return a == b; }
	constexpr std::size_t hash(Value a) const { return std::hash<Value>{}(a); }
};

}	  // namespace fl

static_assert(fl::monoid<fl::IntegerMonoid<>>);
static_assert(!fl::free_monoid<fl::IntegerMonoid<>>);
