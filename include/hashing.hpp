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
		return std::hash<std::string_view>()(
			std::string_view(reinterpret_cast<const char *>(x.data()), x.size() * sizeof(Symbol)));
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
		return std::hash<std::string_view>()(
			std::string_view(reinterpret_cast<const char *>(x.data()), x.size() * sizeof(Symbol)));
	}
};

template <state State>
	requires(not std::same_as<State, size_t> and not symbol<State>)
struct hash<State> {
	constexpr hash() = default;
	constexpr size_t operator()(const State &x) const { return hash<size_t>()(x); }
};

// boost::hash_combine, see
// https://www.boost.org/doc/libs/1_86_0/libs/container_hash/doc/html/hash.html#notes_hash_combine
constexpr void hash_combine(std::size_t &seed, std::size_t value) {
	seed ^= value + 0x9e3779b9 + (seed << 6) + (seed >> 2);
}

template <class T>
constexpr void hash_combine(std::size_t &seed, const T &v) {
	hash_combine(seed, fl::hash<T>{}(v));
}

template <class... Args>
struct hash<std::tuple<Args...>> {
	constexpr hash() = default;
	constexpr std::size_t operator()(const std::tuple<Args...> &t) const {
		return [&]<std::size_t... p>(std::index_sequence<p...>) {
			std::size_t seed = 0;
			(hash_combine(seed, std::get<p>(t)), ...);
			return seed;
		}(std::make_index_sequence<std::tuple_size_v<std::tuple<Args...>>>{});
	}
};

template <monoid M>
struct monoid_hash {
	const M *monoid;
	constexpr monoid_hash(const M *monoid) : monoid(monoid) {}
	constexpr std::size_t operator()(const typename M::Value &v) const { return monoid->hash(v); }
};

template <monoid M>
struct monoid_equal {
	const M *monoid;
	constexpr monoid_equal(const M *monoid) : monoid(monoid) {}
	constexpr bool operator()(const typename M::Value &a, const typename M::Value &b) const {
		return monoid->equal(a, b);
	}
};

template <class... Hashes>
struct cartesian_hash {
	std::tuple<Hashes...> hashes;
	template <class... H>
	constexpr cartesian_hash(H &&...hashes) : hashes(std::forward<H>(hashes)...) {}
	template <class... Args>
	constexpr std::size_t operator()(const std::tuple<Args...> &t) const {
		return [&]<std::size_t... p>(std::index_sequence<p...>) {
			std::size_t seed = 0;
			(hash_combine(seed, std::get<p>(hashes)(std::get<p>(t))), ...);
			return seed;
		}(std::make_index_sequence<std::tuple_size_v<std::tuple<Args...>>>{});
	}
};

template <class... Equals>
struct cartesian_equal {
	std::tuple<Equals...> equals;
	template <class... E>
	constexpr cartesian_equal(E &&...equals) : equals(std::forward<E>(equals)...) {}
	template <class... Args>
	constexpr bool operator()(const std::tuple<Args...> &a, const std::tuple<Args...> &b) const {
		return [&]<std::size_t... p>(std::index_sequence<p...>) {
			return (... && std::get<p>(equals)(std::get<p>(a), std::get<p>(b)));
		}(std::make_index_sequence<std::tuple_size_v<std::tuple<Args...>>>{});
	}
};

}	  // namespace fl
