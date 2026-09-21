#pragma once
#include <concepts>
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
	{ m.identity } -> std::same_as<const typename M::Value &>;
	{ m.equal(a, b) } -> std::convertible_to<bool>;
	{ m.hash(a) } -> std::convertible_to<std::size_t>;
	{ m.mul(a, b) } -> std::convertible_to<typename M::Value>;
	{ m.invMul(a, b) } -> std::convertible_to<typename M::Value>;
	{ m.own(m, a) } -> std::convertible_to<typename M::Value>;	   // copy a value from another monoid
};

template <class R, class T>
concept range_of = std::ranges::forward_range<R> && std::same_as<std::ranges::range_value_t<R>, T>;

template <class M>
concept free_monoid = monoid<M> && requires(const M &m, const M::Value &a) {
	typename M::Symbol;
	// requires symbol<typename M::Symbol>;
	{ m.gen(a) } -> range_of<typename M::Symbol>;
};

template <class T>
concept OStreamable = requires(std::ostream &os, const T &t) {
	{ os << t } -> std::convertible_to<std::ostream &>;
};

/// monoidal Finite state automaton
template <class T>
concept FSA = monoid<typename T::Monoid> && state<typename T::State> && requires(T t, typename T::State s) {
	// properties of the transition table
	{ T::deterministic } -> std::convertible_to<bool>;
	{ T::sorted_arcs } -> std::convertible_to<bool>; // are arcs comparable as arrays

	// methods
	{ t.start() } -> std::same_as<typename T::State>;
	// sorted or not, deterministic or not, the transitions are always a range of tuples (Symbol, State)
	{ t.transitions(s) } -> range_of<std::tuple<typename T::Monoid::Symbol, typename T::State>>;
	{ t.isFinal(s) } -> std::convertible_to<bool>;
	{ t.size() } -> std::convertible_to<std::size_t>;
};

template <class T>
concept FST = FSA<T> && requires() {
	{ T::Monoid } -> free_monoid; // this is weak, but good for now
};

template <class T>
concept SSFST = FST<T> && requires(T t, typename T::State s, typename T::Monoid::Symbol l) {
	{ t.transitions(s) } -> std::ranges::random_access_range;
	{ t.psi(s) } -> std::same_as<std::span<const typename T::Monoid::Symbol>>;
};

//template <class T>
//concept SSFST =							//
//	symbol<typename T::Letter_t> &&		//
//	state<typename T::State> &&			//
//	requires(T t, typename T::Letter_t l, T::State s) {
//		{
//			&T::step
//		} -> std::same_as<std::pair<std::span<const typename T::Letter_t>, bool> (T::*)(typename T::State &,
//																						typename T::Letter_t) const>;
//		{ &T::psi } -> std::same_as<std::span<const typename T::Letter_t> (T::*)(typename T::State) const>;
//		{ &T::isFinal } -> std::same_as<bool (T::*)(typename T::State) const>;
//		{ &T::initial } -> std::same_as<typename T::State (T::*)() const>;
//		{ &T::size } -> std::same_as<std::size_t (T::*)() const>;
//	};

template <class T>
concept SSFSTI = SSFST<T> && requires(T t) {
	{ &T::initialOutput } -> std::same_as<std::span<const typename T::Letter_t> (T::*)() const>;
};


}	  // namespace fl
