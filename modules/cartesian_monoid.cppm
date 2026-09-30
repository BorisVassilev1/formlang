module;

#include <array>
#include <cstddef>
#include <istream>
#include <ostream>
#include <tuple>
#include <type_traits>
#include <utility>
#include <ranges>
#include <iterator>
#include <format>

export module formlang:cartesian_monoid;

import :concepts;
import :formatting;
import :hashing;

export namespace fl {

// A cartesian product of monoids, where some of the monoids may be shared between the different slots of the product.
//
// ! this is not a free monoid, even if all components are free monoids.
// ! it is compactable if any of the components are compactable
// ! it is serializable if any of the components are serializable
// ! it is printable if all of the components are printable
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

	template <std::size_t... Is>
	constexpr auto widenImpl(auto a, std::index_sequence<Is...>) const {
		return std::tuple(std::get<slots[Is]>(owned).widen(std::get<Is>(a))...);
	}

	// const, matching InterningMonoid::compact() const: the owned monoids
	// mutate their own pools through their own `mutable` members, same as
	// InterningMonoid does, so this doesn't need non-const access to `owned`
	// -- and it must be const, since compactable_monoid checks callability
	// through a `const M&`.
	//
	// Iterates over the physically OWNED monoids (index J into `owned`), not
	// over the tapes: a monoid shared by several tapes (e.g. DiagonalMonoid,
	// slots = {0, 0}) must see every tape's live values in a SINGLE compact()
	// call, since compact() treats anything absent from the ranges it's given
	// as dead and reclaims it right away -- calling it once per tape would
	// have each call reclaim the other tape(s)' still-live entries out from
	// under it.
	template <std::size_t J, std::size_t I>
	constexpr auto projectTapeIfOwned(auto &...liveRanges) const {
		if constexpr (slots[I] == J) {
			return std::make_tuple(
				std::views::transform(liveRanges, [](Value &v) -> auto & { return std::get<I>(v); })...);
		} else {
			return std::tuple<>{};
		}
	}

	template <std::size_t J, std::size_t... Is>
	void compactOwned(std::index_sequence<Is...>, auto &...liveRanges) const {
		if constexpr (fl::compactable_monoid<ElemAt<J>>) {
			auto allRanges = std::tuple_cat(projectTapeIfOwned<J, Is>(liveRanges...)...);
			if constexpr (std::tuple_size_v<decltype(allRanges)> > 0) {
				std::apply([&](auto &...ranges) { std::get<J>(owned).compact(ranges...); }, allRanges);
			}
		}
	}

	template <std::size_t... Js>
	void compactImpl(std::index_sequence<Js...>, auto &...liveRanges) const {
		(compactOwned<Js>(std::make_index_sequence<NumTapes>{}, liveRanges...), ...);
	}

	template <std::size_t I>
	constexpr const auto &getMonoid() const {
		return std::get<slots[I]>(owned);
	}

	template <std::size_t I>
	constexpr decltype(auto) deserizeTape(std::istream &in) const {
		if constexpr (fl::serializable_monoid<ElemAt<I>>) {
			return ElemAt<I>{in};
		} else {
			return ElemAt<I>{};
		}
	}
	template <std::size_t... Is>
	constexpr auto deserializeImpl(std::istream &in, std::index_sequence<Is...>) const {
		return std::tuple(deserizeTape<Is>(in)...);
	}

	template <std::size_t I>
	constexpr auto serializeTape(std::ostream &out) const {
		if constexpr (fl::serializable_monoid<ElemAt<I>>) { std::get<I>(owned).serialize(out); }
	}
	template <std::size_t... Is>
	constexpr void serializeImpl(std::ostream &out, std::index_sequence<Is...>) const {
		(serializeTape<Is>(out), ...);
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
	constexpr auto widen(auto a) const { return widenImpl(a, std::make_index_sequence<NumTapes>{}); }

	// tries to compact all owned monoids. Renumbers ids on every tape backed by a
	// compactable monoid, writing the new ids back into the given ranges -- see
	// InterningMonoid::compact().
	template <std::ranges::forward_range... Ranges>
		requires((std::same_as<std::ranges::range_value_t<Ranges>, Value> &&
				  std::indirectly_writable<std::ranges::iterator_t<Ranges>, Value>) &&
				 ...) &&
				(fl::compactable_monoid<OwnedTs> || ...)
	void compact(Ranges &&...liveRanges) const {
		compactImpl(std::make_index_sequence<sizeof...(OwnedTs)>{}, liveRanges...);
	}

	CartesianMonoidElementPrinter<std::tuple<OwnedTs...>, Slots...> p(const Value &v) const
		requires(fl::printable_monoid<OwnedTs> && ...);

	const SharedCartesianMonoid &serialize(std::ostream &out) const
		requires(fl::serializable_monoid<OwnedTs> || ...)
	{
		serializeImpl(out, std::make_index_sequence<sizeof...(OwnedTs)>{});
		return *this;
	}

	explicit SharedCartesianMonoid(std::istream &in)
		requires(fl::serializable_monoid<OwnedTs> || ...)
		: owned{deserializeImpl(in, std::make_index_sequence<sizeof...(OwnedTs)>{})} {}

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
			CartesianMonoidElementPrinter<std::tuple<OwnedTs...>, Slots...> SharedCartesianMonoid<
				std::tuple<OwnedTs...>,
				Slots...>::p(const typename SharedCartesianMonoid<std::tuple<OwnedTs...>, Slots...>::Value &v) const
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

export template <class... OwnedTs, std::size_t... Slots>
	requires(fl::monoid<std::remove_cvref_t<OwnedTs>> && ...) && (sizeof...(Slots) >= 1)
struct std::formatter<fl::CartesianMonoidElementPrinter<std::tuple<OwnedTs...>, Slots...>, char>
	: fl::ostream_formatter {};

export template <class C>
	requires std::derived_from<C, fl::CartesianMonoidBase>
struct std::tuple_size<C> : std::integral_constant<std::size_t, C::NumTapes> {};

export template <std::size_t I, class C>
	requires std::derived_from<C, fl::CartesianMonoidBase>
struct std::tuple_element<I, C> {
	using type = decltype(fl::get<I>(std::declval<C>()));
};
