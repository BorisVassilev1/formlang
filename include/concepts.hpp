#pragma once
#include <compare>
#include <concepts>
#include <iosfwd>
#include <ranges>
#include <span>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <vector>

namespace fl {

/// a concept for classes that can be Symbol in a DPDA<State, Symbol>
template <class L>
concept symbol = requires() {
	{ L::eps } -> std::same_as<const L &>;
	{ L::eof } -> std::same_as<const L &>;
	{ L::size } -> std::convertible_to<const std::size_t &>;
	{ L() } -> std::same_as<L>;
	{ ++std::declval<L &>() } -> std::same_as<L>;
	std::is_convertible_v<L, std::size_t>;
	not std::is_fundamental_v<L>;
};

/// a concept for classes that can be State in a DPDA<State, Symbol>
template <class S>
concept state = requires(std::size_t i) {
	std::is_convertible_v<S, std::size_t>;
	{ new S(i) };
	{ new S() };
	not std::is_fundamental_v<S>;
};

template <int N, typename... Ts>
using NthTypeOf = typename std::tuple_element<N, std::tuple<Ts...>>::type;

template <symbol Symbol, std::ranges::viewable_range T>
std::vector<Symbol> toSymbol(T &&t) {
	return std::vector<Symbol>(std::begin(t), std::end(t));
}

template <symbol Symbol>
std::vector<Symbol> toSymbol(const char *s) {
	return toSymbol<Symbol>(std::string_view(s));
}

template <class M>
concept monoid = requires(const M &m, const M::Value &a, const M::Value &b) {
	typename M::Value;
	requires std::semiregular<typename M::Value>;
	requires std::default_initializable<typename M::Value>;
	{ a < b } -> std::convertible_to<bool>;
	{ m.identity } -> std::same_as<const typename M::Value &>;
	{ m.equal(a, b) } -> std::convertible_to<bool>;
	{ m.hash(a) } -> std::convertible_to<std::size_t>;
	{ m.mul(a, b) } -> std::convertible_to<typename M::Value>;
	{ m.invMul(a, b) } -> std::convertible_to<typename M::Value>;
	{ m.own(m, a) } -> std::convertible_to<typename M::Value>;	   // copy a value from another monoid
};

template <class M>
concept printable_monoid = monoid<M> && requires(const M &m, const M::Value &a, std::ostream &out) {
	{ out << m.p(a) } -> std::convertible_to<std::ostream &>;
};

auto print_if_can(const auto &m, const auto &a) {
	if constexpr (printable_monoid<std::decay_t<decltype(m)>>) {
		return m.p(a);
	} else {
		return "unprintable";
	}
}

template <class R, class T>
concept range_of = std::ranges::forward_range<R> && std::same_as<std::ranges::range_value_t<R>, T>;

template <class M>
concept free_monoid = monoid<M> && requires(const M &m, const M::Value &a) {
	typename M::Symbol;
	// requires symbol<typename M::Symbol>;
	{ m.gen(a) } -> range_of<typename M::Symbol>;		   /// a range of the generators of a value
	{ m.size(a) } -> std::convertible_to<std::size_t>;	   /// the number of generators in a value
	{ m.from(std::span<const typename M::Symbol>()) } -> std::same_as<typename M::Value>;
	{ m.sub(a, 0, 1) } -> std::convertible_to<typename M::Value>;
	{ m.gcp(a, a) } -> std::convertible_to<typename M::Value>;
	{ m.C() } -> std::convertible_to<std::size_t>;	   /// the longest word in the monoid TODO: bad??
};

/// for monoids that pool their valuesconcept
template <class M>
concept compactable_monoid = monoid<M> && requires(const M &m, const std::vector<typename M::Value> &live) {
	// this should be variadic
	{ m.compact(live) } -> std::same_as<void>;
	{ m.compact(live, live) } -> std::same_as<void>;
	{ m.compact(live, live, live) } -> std::same_as<void>;
};

template <class M>
concept serializable_monoid = monoid<M> && requires(const M &m, std::ostream &out, std::istream &in) {
	{ m.serialize(out) } -> std::same_as<const M &>;
	{ M(in) } -> std::same_as<M>;
};

template <class T>
concept OStreamable = requires(std::ostream &os, const T &t) {
	{ os << t } -> std::convertible_to<std::ostream &>;
};

}	  // namespace fl
