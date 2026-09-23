#pragma once

#include <array>
#include <cstddef>
#include <istream>
#include <ostream>
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
// are both InterningMonoid<Symbol> (interning string pools) can use `Slots =
// {0, 0}` over `Owned = std::tuple<InterningMonoid<Symbol>>` so both tapes
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

	static constexpr std::size_t					   NumTapes = sizeof...(Slots);
	static constexpr std::array<std::size_t, NumTapes> slots{Slots...};

   public:
	using Value = std::tuple<typename ElemAt<Slots>::Value...>;

	// Constrained (rather than an unconditional catch-all) so it doesn't
	// out-compete a more specific constructor -- e.g. an exact-match single
	// std::stringstream& argument would otherwise beat the istream&
	// constructor below (which needs a derived-to-base conversion) in
	// overload resolution, get selected, and then fail deep inside trying to
	// construct a multi-element tuple from one stream.
	template <class... Args>
		requires std::constructible_from<OwnedTuple, Args...>
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

	// const, matching InterningMonoid::compact() const: the owned monoids
	// mutate their own pools through their own `mutable` members, same as
	// InterningMonoid does, so this doesn't need non-const access to `owned`
	// -- and it must be const, since compactable_monoid checks callability
	// through a `const M&`.
	template <std::size_t I>
	void compactTape(const auto &...liveRanges) const {
		if constexpr (fl::compactable_monoid<ElemAt<slots[I]>>) {
			std::get<slots[I]>(owned).compact(
				std::views::transform(liveRanges, [](const Value &v) { return std::get<I>(v); })...);
		}
	}

	template <std::size_t... Is>
	void compactImpl(std::index_sequence<Is...>, const auto &...liveRanges) const {
		(compactTape<Is>(liveRanges...), ...);
	}

   public:
	constexpr auto mul(auto a, auto b) const { return mulImpl(a, b, std::make_index_sequence<NumTapes>{}); }
	constexpr auto invMul(auto a, auto b) const { return invMulImpl(a, b, std::make_index_sequence<NumTapes>{}); }
	constexpr auto gen(auto a) const { return genImpl(a, std::make_index_sequence<NumTapes>{}); }
	constexpr bool equal(auto a, auto b) const { return equalImpl(a, b, std::make_index_sequence<NumTapes>{}); }
	constexpr std::size_t hash(auto a) const { return hashImpl(a, std::make_index_sequence<NumTapes>{}); }

	/// copy a value from another (structurally identical) SharedCartesianMonoid's
	/// tapes into this one's, tape by tape -- e.g. re-interning words produced by
	/// a different InterningMonoid pool into this one's pool.
	constexpr Value own(const SharedCartesianMonoid &src, auto a) const {
		return ownImpl(src, a, std::make_index_sequence<NumTapes>{});
	}

	template <std::size_t I>
	constexpr const auto &getMonoid() const {
		return std::get<slots[I]>(owned);
	}

	// tries to compact all owned monoids
	template <std::ranges::input_range... Ranges>
		requires((fl::range_of<Ranges, Value> && ...) && (fl::compactable_monoid<OwnedTs> || ...))
	void compact(Ranges &&...liveRanges) const {
		compactImpl(std::make_index_sequence<NumTapes>{}, liveRanges...);
	}

	// Serializes each physically-owned monoid instance exactly once (by
	// `owned`, not by tape/slot -- two tapes sharing one instance via a
	// repeated slot, e.g. DiagonalMonoid, must not have it written twice).
	// Unlike compact(), this needs EVERY owned monoid to be serializable, not
	// just some: a tape silently skipped here couldn't be reconstructed on
	// read, so there's no "some tapes" escape hatch the way compact() has.
	const SharedCartesianMonoid &serialize(std::ostream &out) const
		requires(fl::serializable_monoid<OwnedTs> && ...)
	{
		std::apply([&](const auto &...m) { (m.serialize(out), ...); }, owned);
		return *this;
	}

	// Reads back `owned` in the same order serialize() wrote it. The braced
	// init-list (not a parenthesized call) is load-bearing: list-init
	// sequences each element's construction left to right, guaranteeing the
	// per-monoid reads happen in the same order they were written in, which
	// ordinary function-argument evaluation order would NOT guarantee.
	explicit SharedCartesianMonoid(std::istream &in)
		requires(fl::serializable_monoid<OwnedTs> && ...)
		: owned{OwnedTs(in)...} {}
};

// Today's default: one independent monoid instance per tape, no sharing --
// e.g. CartesianMonoid<InterningMonoid<char>, InterningMonoid<char>> owns two
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
// DiagonalMonoid<InterningMonoid<Symbol>> shares one string pool between the
// input and output tapes of an FST, instead of duplicating it.
template <class M>
using DiagonalMonoid = SharedCartesianMonoid<std::tuple<M>, 0, 0>;

};	   // namespace fl

#include "IntegerMonoid.hpp"
#include "InterningMonoid.hpp"

static_assert(fl::monoid<fl::CartesianMonoid<fl::IntegerMonoid<>, fl::IntegerMonoid<>, fl::IntegerMonoid<>>>);
static_assert(fl::monoid<fl::CartesianMonoid<fl::InterningMonoid<char> &, fl::InterningMonoid<char>>>);
static_assert(fl::monoid<fl::DiagonalMonoid<fl::InterningMonoid<char>>>);

/// cartesian product of free monoids is not itself a free monoid, because (a, Ɛ) and (Ɛ, a) commute
static_assert(!fl::free_monoid<fl::CartesianMonoid<fl::IntegerMonoid<>, fl::IntegerMonoid<>, fl::IntegerMonoid<>>>);
