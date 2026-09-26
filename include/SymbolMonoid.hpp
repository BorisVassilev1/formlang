#pragma once

#include <cassert>
#include <istream>
#include <ostream>
#include <type_traits>
#include <utility>
#include "concepts.hpp"
#include "hashing.hpp"

namespace fl {

/// symbol does not require a constexpr epsilon, but if it has one, we can use it to make SymbolMonoid::identity
/// constexpr
template <class T>
concept has_constexpr_eps = requires { typename std::integral_constant<decltype(T::val), T::val>; };

template <class T>
struct eps_holder {
	static inline const auto identity = T::eps;		// fallback
};
template <has_constexpr_eps T>
struct eps_holder<T> {
	static constexpr auto identity = T::eps;
};

template <symbol _Symbol>
class SymbolMonoid : eps_holder<_Symbol> {
   public:
	using Symbol = _Symbol;
	using Value	 = Symbol;

	using eps_holder<Symbol>::identity;

	// Only length-<=1 words are representable as a Value here, so mul/invMul
	// are only ever total when at least one side is the identity -- the
	// out-of-domain case (two real symbols) is a precondition violation, not
	// a well-formedness question, so it's an assert (checked when actually
	// called/evaluated), not a static_assert (which would fire merely from
	// this function's declaration being instantiated, e.g. by a concept
	// check or by CartesianMonoid composing this tape with others).
	constexpr Value mul(Value a, Value b) const {
		if (a == identity) return b;
		if (b == identity) return a;
		assert(false && "SymbolMonoid::mul: both operands are non-identity symbols");
		std::unreachable();		// optimizer hint only, never load-bearing: assert() above is what actually guards this
	}
	constexpr Value invMul(Value a, Value b) const {
		if (a == identity) return b;
		if (a == b) return identity;
		assert(false && "SymbolMonoid::invMul: a is not a prefix of b");
		std::unreachable();		// optimizer hint only, never load-bearing: assert() above is what actually guards this
	}
	// gen() must return the empty range for identity (eps is the empty word)
	// and a length-1 range otherwise. A std::array<Symbol,1> can't shrink to
	// length 0, and a std::span can't safely point back into the by-value `a`
	// parameter -- CartesianMonoid::gen() takes its tuple argument by value
	// and extracts per-tape references from that local copy before calling
	// each tape's gen(), so a span into `a` would dangle the moment it
	// returns. SymbolWord owns its one possible symbol by value instead.
	class SymbolWord {
		Symbol sym;
		bool   nonEmpty;

	   public:
		constexpr SymbolWord(Symbol sym, bool nonEmpty) : sym(sym), nonEmpty(nonEmpty) {}
		constexpr const Symbol *begin() const { return nonEmpty ? &sym : nullptr; }
		constexpr const Symbol *end() const { return nonEmpty ? &sym + 1 : nullptr; }
		constexpr std::size_t	size() const { return nonEmpty; }
	};

	constexpr SymbolWord  gen(const Value &a) const { return {a, a != identity}; }
	constexpr bool		  equal(Value a, Value b) const { return a == b; }
	constexpr std::size_t hash(Value a) const { return fl::hash<Value>{}(a); }

	template <class T>
	constexpr Value own(const SymbolMonoid<T> &, SymbolMonoid<T>::Value a) const {
		return T(a);
	}

	constexpr Value from(range_of<Symbol> auto &&r) const {
		auto it = std::ranges::begin(r);
		if (it == std::ranges::end(r)) return identity;
		Symbol s = *it++;
		assert(it == std::ranges::end(r) && "SymbolMonoid::from: input range has length > 1");
		if (it == std::ranges::end(r)) return s;
		std::unreachable();
		return s;
	}

	constexpr Value sub(Value a, std::size_t start, std::size_t len) const {
		assert(start <= 1 && start + len <= 1 && "SymbolMonoid::sub: input range has length > 1");
		if (len == 0) return identity;
		return a;
	}

	constexpr std::size_t size(Value a) const { return a == identity ? 0 : 1; }
	constexpr Value		  gcp(Value a, Value b) const {
		if (a == identity) return identity;
		if (b == identity) return identity;
		if (a == b) return a;
		assert(false && "SymbolMonoid::gcp: a and b are different non-identity symbols");
		std::unreachable();
	}

	constexpr std::size_t C() const { return 1; }

	constexpr Value p(Value a) const { return a; }

	// SymbolMonoid holds no state of its own -- Value is a bare Symbol, so
	// there's nothing to write/read here. Exists only so SymbolMonoid composes
	// under a CartesianMonoid (e.g. ExpandedFST::Monoid) alongside a stateful
	// output tape that does need serialize()/deserialize() on every tape.
	SymbolMonoid() = default;
	const SymbolMonoid &serialize(std::ostream &) const { return *this; }
	explicit SymbolMonoid(std::istream &) {}
};

};	   // namespace fl

#include "letter.hpp"
static_assert(fl::monoid<fl::SymbolMonoid<fl::Letter>>);
static_assert(fl::free_monoid<fl::SymbolMonoid<fl::Letter>>);
static_assert(fl::printable_monoid<fl::SymbolMonoid<fl::Letter>>);
