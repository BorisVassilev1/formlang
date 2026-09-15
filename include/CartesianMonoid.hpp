#pragma once

#include "concepts.hpp"
namespace fl {

template <class... Ts>
	requires(fl::monoid<Ts> && ...)
class CartesianMonoid {
	std::tuple<Ts...> monoids;

   public:
	using Value = std::tuple<typename Ts::Value...>;

	static constexpr Value identity = std::tuple<typename Ts::Value...>(Ts::identity...);
	constexpr auto		   mul(auto a, auto b) const {
		return [&]<std::size_t... Is>(std::index_sequence<Is...>) {
			return std::tuple<typename Ts::Value...>(std::get<Is>(monoids).mul(std::get<Is>(a), std::get<Is>(b))...);
		}(std::index_sequence_for<Ts...>());
	}

	constexpr auto invMul(auto a, auto b) const {
		return [&]<std::size_t... Is>(std::index_sequence<Is...>) {
			return std::tuple<typename Ts::Value...>(std::get<Is>(monoids).invMul(std::get<Is>(a), std::get<Is>(b))...);
		}(std::index_sequence_for<Ts...>());
	}
	constexpr auto gen(auto a) const {
		return [&]<std::size_t... Is>(std::index_sequence<Is...>) {
			return std::tuple<typename Ts::Value...>(std::get<Is>(monoids).gen(std::get<Is>(a))...);
		}(std::index_sequence_for<Ts...>());
	}

	constexpr bool equal(auto a, auto b) const {
		return [&]<std::size_t... Is>(std::index_sequence<Is...>) {
			return (... && std::get<Is>(monoids).equal(std::get<Is>(a), std::get<Is>(b)));
		}(std::index_sequence_for<Ts...>());
	}
	constexpr std::size_t hash(auto a) const {
		return [&]<std::size_t... p>(std::index_sequence<p...>) {
			std::size_t seed = 0;
			(hash_combine(seed, std::get<p>(monoids).hash(std::get<p>(a))), ...);
			return seed;
		}(std::index_sequence_for<Ts...>());
	}

	template <std::size_t I>
	constexpr auto getMonoid() const {
		return std::get<I>(monoids);
	}
};

};	   // namespace fl

#include "IntegerMonoid.hpp"

static_assert(fl::monoid<fl::CartesianMonoid<fl::IntegerMonoid<>, fl::IntegerMonoid<>, fl::IntegerMonoid<>>>);
