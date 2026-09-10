#pragma once

#include <cstddef>
#include <functional>
#include <span>
#include <string_view>

#include "concepts.hpp"

namespace fl {
template <class A>
struct hash {
	constexpr hash() = default;
	constexpr size_t operator()(const A &x) const { return std::hash<A>()(x); }
};

template <symbol Symbol>
struct hash<Symbol> {
	constexpr hash() = default;
	constexpr size_t operator()(const Symbol &x) const { return std::hash<size_t>()((size_t)x); }
};

template <symbol Symbol>
struct hash<std::span<Symbol>> {
	constexpr hash() = default;
	constexpr size_t operator()(const std::span<Symbol> &x) const {
		return std::hash<std::string_view>()(std::string_view(reinterpret_cast<const char *>(x.data()), x.size() * sizeof(Symbol)));
	}
};

template <class A, size_t N>
struct hash<std::array<A, N>> {
	constexpr hash() = default;
	constexpr size_t operator()(const std::array<A, N> &x) const {
		return std::hash<std::string_view>()(
			std::string_view(reinterpret_cast<const char *>(x.data()), x.size() * sizeof(A)));
	}
};

template <class Symbol>
struct hash<std::vector<Symbol>> {
	constexpr hash() = default;
	constexpr size_t operator()(const std::vector<Symbol> &x) const {
		return std::hash<std::string_view>()(std::string_view(reinterpret_cast<const char *>(x.data()), x.size() * sizeof(Symbol)));
	}
};

template <state State>
	requires(not std::same_as<State, size_t> and not symbol<State>)
struct hash<State> {
	constexpr hash() = default;
	constexpr size_t operator()(const State &x) const { return hash<size_t>()(x); }
};

template <class... Args>
struct hash<std::tuple<Args...>> {
	constexpr hash() = default;
	constexpr std::size_t operator()(const std::tuple<Args...> &t) const {
		return [&]<std::size_t... p>(std::index_sequence<p...>) {
			return ((fl::hash<NthTypeOf<p, Args...>>{}(std::get<p>(t))) ^ ...);
		}(std::make_index_sequence<std::tuple_size_v<std::tuple<Args...>>>{});
	}
};

}	  // namespace fl
