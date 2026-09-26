#pragma once

#include <array>
#include <cstddef>
#include <istream>
#include <ostream>
#include <tuple>
#include <type_traits>
#include <utility>

#include "concepts.hpp"
#include "formatting.hpp"
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

struct CartesianMonoidBase {};

template <class C>
concept cartesian_monoid = std::derived_from<std::remove_cvref_t<C>, CartesianMonoidBase>;

template <std::size_t I, cartesian_monoid C>
constexpr decltype(auto) get(C &&cartesian);

template <cartesian_monoid C>
constexpr decltype(auto) in(C &&cartesian);
template <cartesian_monoid C>
constexpr decltype(auto) out(C &&cartesian);

template <class OwnedTs, std::size_t... Slots>
	requires(sizeof...(Slots) >= 1)
struct CartesianMonoidElementPrinter;

template <class... OwnedTs, std::size_t... Slots>
	requires(fl::monoid<std::remove_cvref_t<OwnedTs>> && ...) && (sizeof...(Slots) >= 1)
class SharedCartesianMonoid<std::tuple<OwnedTs...>, Slots...> : public CartesianMonoidBase {
   private:
	using OwnedTuple = std::tuple<OwnedTs...>;

	template <std::size_t I>
	using ElemAt = std::remove_cvref_t<std::tuple_element_t<I, OwnedTuple>>;

	OwnedTuple owned;

   public:
	static constexpr std::size_t NumTapes = sizeof...(Slots);

   private:
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
		return std::tuple(std::get<slots[Is]>(owned).mul(std::get<Is>(a), std::get<Is>(b))...);
	}
	template <std::size_t... Is>
	constexpr auto invMulImpl(auto a, auto b, std::index_sequence<Is...>) const {
		return std::tuple(std::get<slots[Is]>(owned).invMul(std::get<Is>(a), std::get<Is>(b))...);
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
	constexpr auto ownImpl(const SharedCartesianMonoid &src, auto a, std::index_sequence<Is...>) const {
		return std::tuple(std::get<slots[Is]>(owned).own(std::get<slots[Is]>(src.owned), std::get<Is>(a))...);
	}

	template <std::size_t... Is>
	constexpr auto gcpImpl(auto a, auto b, std::index_sequence<Is...>) const {
		return std::tuple(std::get<slots[Is]>(owned).gcp(std::get<Is>(a), std::get<Is>(b))...);
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

	template <std::size_t I>
	constexpr const auto &getMonoid() const {
		return std::get<slots[I]>(owned);
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
	constexpr auto own(const SharedCartesianMonoid &src, auto a) const {
		return ownImpl(src, a, std::make_index_sequence<NumTapes>{});
	}

	constexpr auto gcp(auto a, auto b) const { return gcpImpl(a, b, std::make_index_sequence<NumTapes>{}); }

	// tries to compact all owned monoids
	template <std::ranges::input_range... Ranges>
		requires((fl::range_of<Ranges, Value> && ...) && (fl::compactable_monoid<OwnedTs> || ...))
	void compact(Ranges &&...liveRanges) const {
		compactImpl(std::make_index_sequence<NumTapes>{}, liveRanges...);
	}

	CartesianMonoidElementPrinter<std::tuple<OwnedTs...>, Slots...> p(const Value &v) const
		requires(fl::printable_monoid<OwnedTs> && ...);

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

	template <std::size_t I, cartesian_monoid C>
	friend constexpr decltype(auto) get(C &&);
};

template <class OwnedTs, std::size_t... Slots>
	requires(sizeof...(Slots) >= 1)
struct CartesianMonoidElementPrinter {
	const SharedCartesianMonoid<OwnedTs, Slots...>				   &m;
	const typename SharedCartesianMonoid<OwnedTs, Slots...>::Value &v;
};

template <class... OwnedTs, std::size_t... Slots>
	requires(fl::monoid<std::remove_cvref_t<OwnedTs>> && ...) &&
			(sizeof...(Slots) >= 1)
			CartesianMonoidElementPrinter<std::tuple<OwnedTs...>, Slots...> SharedCartesianMonoid<std::tuple<OwnedTs...>, Slots...>::p(
				const typename SharedCartesianMonoid<std::tuple<OwnedTs...>, Slots...>::Value &v) const
				requires(fl::printable_monoid<OwnedTs> && ...)
{
	return {*this, v};
}

template <class... OwnedTs, std::size_t... Slots>
std::ostream &operator<<(std::ostream &out, const CartesianMonoidElementPrinter<std::tuple<OwnedTs...>, Slots...> &p)
	requires(fl::printable_monoid<OwnedTs> && ...)
{
	out << "<";
	[&]<std::size_t... Is>(std::index_sequence<Is...>) {
		((out << (Is ? ", " : "") << print_if_can(get<Is>(p.m), std::get<Is>(p.v))), ...);
	}(std::make_index_sequence<sizeof...(Slots)>{});
	out << ">";
	return out;
}

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

template <std::size_t I, cartesian_monoid C>
constexpr decltype(auto) get(C &&cartesian) {
	return cartesian.template getMonoid<I>();
}

template <cartesian_monoid C>
	requires(C::NumTapes >= 1)
constexpr decltype(auto) in(C &&cartesian) {
	return get<0>(std::forward<C>(cartesian));
}
template <cartesian_monoid C>
	requires(C::NumTapes >= 2)
constexpr decltype(auto) out(C &&cartesian) {
	return get<1>(std::forward<C>(cartesian));
}

};	   // namespace fl

template <class... OwnedTs, std::size_t... Slots>
	requires(fl::monoid<std::remove_cvref_t<OwnedTs>> && ...) && (sizeof...(Slots) >= 1)
struct std::formatter<fl::CartesianMonoidElementPrinter<std::tuple<OwnedTs...>, Slots...>, char> : fl::ostream_formatter {};

template <class C>
	requires std::derived_from<C, fl::CartesianMonoidBase>
struct std::tuple_size<C> : std::integral_constant<std::size_t, C::NumTapes> {};

template <std::size_t I, class C>
	requires std::derived_from<C, fl::CartesianMonoidBase>
struct std::tuple_element<I, C> {
	using type = decltype(fl::get<I>(std::declval<C>()));
};

#include "IntegerMonoid.hpp"
#include "InterningMonoid.hpp"

static_assert(fl::monoid<fl::CartesianMonoid<fl::IntegerMonoid<>, fl::IntegerMonoid<>, fl::IntegerMonoid<>>>);
static_assert(fl::monoid<fl::CartesianMonoid<fl::InterningMonoid<char> &, fl::InterningMonoid<char>>>);
static_assert(fl::monoid<fl::DiagonalMonoid<fl::InterningMonoid<char>>>);

/// cartesian product of free monoids is not itself a free monoid, because (a, Ɛ) and (Ɛ, a) commute
static_assert(!fl::free_monoid<fl::CartesianMonoid<fl::IntegerMonoid<>, fl::IntegerMonoid<>, fl::IntegerMonoid<>>>);

static_assert(fl::printable_monoid<fl::CartesianMonoid<fl::IntegerMonoid<>, fl::IntegerMonoid<>, fl::IntegerMonoid<>>>);
