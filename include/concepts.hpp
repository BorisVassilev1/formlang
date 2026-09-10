#pragma once
#include <concepts>
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

template <class T>
concept SSFST =							//
	symbol<typename T::Letter_t> &&		//
	state<typename T::State> &&			//
	requires(T t, typename T::Letter_t l, T::State s) {
		{
			&T::step
		} -> std::same_as<std::pair<std::span<const typename T::Letter_t>, bool> (T::*)(typename T::State &,
																						typename T::Letter_t) const>;
		{ &T::psi } -> std::same_as<std::span<const typename T::Letter_t> (T::*)(typename T::State) const>;
		{ &T::isFinal } -> std::same_as<bool (T::*)(typename T::State) const>;
		{ &T::initial } -> std::same_as<typename T::State (T::*)() const>;
		{ &T::size } -> std::same_as<std::size_t (T::*)() const>;
	};

template <class T>
concept SSFSTI = SSFST<T> && requires(T t) {
	{ &T::initialOutput } -> std::same_as<std::span<const typename T::Letter_t> (T::*)() const>;
};

}	  // namespace fl
