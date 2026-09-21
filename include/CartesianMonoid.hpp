#pragma once

#include <array>
#include <cstddef>
#include <tuple>
#include <type_traits>
#include <utility>

#include "concepts.hpp"
#include "hashing.hpp"

namespace fl {

// A cartesian monoid combining several "tapes", where each tape is backed by
// one of the monoid instances physically stored in `Owned` (a std::tuple),
// and `Slots...` says, per tape, *which* element of `Owned` that tape uses.
// Tapes that repeat the same slot index share one physical monoid instance
// instead of each owning an independent copy -- e.g. an FST whose two tapes
// are both KleeneMonoid<Symbol> (interning string pools) can use `Slots =
// {0, 0}` over `Owned = std::tuple<KleeneMonoid<Symbol>>` so both tapes
// intern into the same pool, instead of duplicating it.
//
// The tape -> instance mapping is a compile-time constant, so getMonoid<I>()
// always re-derives its reference by indexing into `owned`; nothing is
// cached or aliased at the object level, so the implicitly-generated
// copy/move constructors and assignment operators (which just copy/move
// `owned`, an ordinary tuple of value types) are correct as-is -- there is
// no pointer/reference state to fix up after a copy or move.
template <class Owned, std::size_t... Slots>
class SharedCartesianMonoid;	 // primary template intentionally undefined; Owned must be a std::tuple

template <class... OwnedTs, std::size_t... Slots>
	requires(fl::monoid<std::remove_cvref_t<OwnedTs>> && ...) && (sizeof...(Slots) >= 1)
class SharedCartesianMonoid<std::tuple<OwnedTs...>, Slots...> {
	using OwnedTuple = std::tuple<OwnedTs...>;

	template <std::size_t I>
	using ElemAt = std::remove_cvref_t<std::tuple_element_t<I, OwnedTuple>>;

	OwnedTuple owned;

	static constexpr std::size_t				   NumTapes = sizeof...(Slots);
	static constexpr std::array<std::size_t, NumTapes> slots{Slots...};

   public:
	using Value = std::tuple<typename ElemAt<Slots>::Value...>;

	template <class... Args>
	constexpr SharedCartesianMonoid(Args &&...args) : owned(std::forward<Args>(args)...) {}

	static constexpr Value identity{ElemAt<Slots>::identity...};

   private:
	template <std::size_t... Is>
	constexpr auto mulImpl(auto a, auto b, std::index_sequence<Is...>) const {
		return Value(std::get<slots[Is]>(owned).mul(std::get<Is>(a), std::get<Is>(b))...);
	}
	template <std::size_t... Is>
	constexpr auto invMulImpl(auto a, auto b, std::index_sequence<Is...>) const {
		return Value(std::get<slots[Is]>(owned).invMul(std::get<Is>(a), std::get<Is>(b))...);
	}
	template <std::size_t... Is>
	constexpr auto genImpl(auto a, std::index_sequence<Is...>) const {
		return std::tuple(std::get<slots[Is]>(owned).gen(std::get<Is>(a))...);
	}
	template <std::size_t... Is>
	constexpr bool equalImpl(auto a, auto b, std::index_sequence<Is...>) const {
		return (... && std::get<slots[Is]>(owned).equal(std::get<Is>(a), std::get<Is>(b)));
	}
	template <std::size_t... Is>
	constexpr std::size_t hashImpl(auto a, std::index_sequence<Is...>) const {
		std::size_t seed = 0;
		(hash_combine(seed, std::get<slots[Is]>(owned).hash(std::get<Is>(a))), ...);
		return seed;
	}
	template <std::size_t... Is>
	constexpr Value ownImpl(const SharedCartesianMonoid &src, auto a, std::index_sequence<Is...>) const {
		return Value(std::get<slots[Is]>(owned).own(std::get<slots[Is]>(src.owned), std::get<Is>(a))...);
	}

   public:
	constexpr auto mul(auto a, auto b) const { return mulImpl(a, b, std::make_index_sequence<NumTapes>{}); }
	constexpr auto invMul(auto a, auto b) const { return invMulImpl(a, b, std::make_index_sequence<NumTapes>{}); }
	constexpr auto gen(auto a) const { return genImpl(a, std::make_index_sequence<NumTapes>{}); }
	constexpr bool equal(auto a, auto b) const { return equalImpl(a, b, std::make_index_sequence<NumTapes>{}); }
	constexpr std::size_t hash(auto a) const { return hashImpl(a, std::make_index_sequence<NumTapes>{}); }

	/// copy a value from another (structurally identical) SharedCartesianMonoid's
	/// tapes into this one's, tape by tape -- e.g. re-interning words produced by
	/// a different KleeneMonoid pool into this one's pool.
	constexpr Value own(const SharedCartesianMonoid &src, auto a) const {
		return ownImpl(src, a, std::make_index_sequence<NumTapes>{});
	}

	template <std::size_t I>
	constexpr const auto &getMonoid() const {
		return std::get<slots[I]>(owned);
	}
};

// Today's default: one independent monoid instance per tape, no sharing --
// e.g. CartesianMonoid<KleeneMonoid<char>, KleeneMonoid<char>> owns two
// separate string pools, one per tape.
namespace detail {
template <class TupleOfTypes, class Seq>
struct IdentitySlots;
template <class... Ts, std::size_t... Is>
struct IdentitySlots<std::tuple<Ts...>, std::index_sequence<Is...>> {
	using type = SharedCartesianMonoid<std::tuple<Ts...>, Is...>;
};
}	  // namespace detail

template <class... Ts>
using CartesianMonoid = typename detail::IdentitySlots<std::tuple<Ts...>, std::index_sequence_for<Ts...>>::type;

// Both tapes backed by the SAME monoid instance -- e.g.
// DiagonalMonoid<KleeneMonoid<Symbol>> shares one string pool between the
// input and output tapes of an FST, instead of duplicating it.
template <class M>
using DiagonalMonoid = SharedCartesianMonoid<std::tuple<M>, 0, 0>;

};	   // namespace fl

#include "IntegerMonoid.hpp"
#include "KleeneMonoid.hpp"

static_assert(fl::monoid<fl::CartesianMonoid<fl::IntegerMonoid<>, fl::IntegerMonoid<>, fl::IntegerMonoid<>>>);
static_assert(fl::monoid<fl::CartesianMonoid<fl::KleeneMonoid<char> &, fl::KleeneMonoid<char>>>);
static_assert(fl::monoid<fl::DiagonalMonoid<fl::KleeneMonoid<char>>>);
