#pragma once

#include <cstdint>
#include <istream>
#include <ostream>
#include "concepts.hpp"

namespace fl {

template <std::integral int_type = uint64_t>
class IntegerMonoid {
   public:
	using Value	 = int_type;
	using Symbol = Value;	  // really just '1'

	static constexpr Value identity = 0;
	constexpr Value		   mul(Value a, Value b) const { return a + b; }
	constexpr Value		   invMul(Value a, Value b) const { return b - a; }
	constexpr auto		   gen(Value a) const { return std::ranges::repeat_view(int_type(1), a); }
	constexpr bool		   equal(Value a, Value b) const { return a == b; }
	constexpr std::size_t  hash(Value a) const { return std::hash<Value>{}(a); }

	template <class T>
	constexpr Value own(const IntegerMonoid<T> &, IntegerMonoid<T>::Value a) const {
		return T(a);
	}

	// IntegerMonoid holds no state of its own -- Value is self-contained, so
	// there's nothing to write/read here. Exists only so IntegerMonoid
	// composes under a CartesianMonoid alongside stateful tapes that do need
	// serialize()/deserialize() on every tape.
	IntegerMonoid() = default;
	const IntegerMonoid &serialize(std::ostream &) const { return *this; }
	explicit IntegerMonoid(std::istream &) {}
};

}	  // namespace fl

static_assert(fl::monoid<fl::IntegerMonoid<>>);
static_assert(fl::free_monoid<fl::IntegerMonoid<>>);
