#pragma once
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

/// for monoids that pool their values
template <class M>
concept compactable_monoid = monoid<M> && requires(const M &m, const std::vector<typename M::Value> &live) {
	{ m.compact(live) } -> std::same_as<void>;
	{ m.compact(live, live) } -> std::same_as<void>;
	{ m.compact(live, live, live) } -> std::same_as<void>;
};

/// for monoids that own persistent state needing its own save/load -- e.g. an
/// InterningMonoid's pool. Stateless monoids (IntegerMonoid, SymbolMonoid)
/// have nothing of their own to persist, but still implement this trivially
/// so they compose under a CartesianMonoid that mixes stateful and stateless
/// tapes: unlike compactable_monoid (an optional optimization, fine to skip
/// per-tape), serialize/deserialize must be complete for every tape or the
/// whole monoid can't be reconstructed, so this can't have a "some tapes"
/// escape hatch the way CartesianMonoid::compact() does.
template <class M>
concept serializable_monoid = monoid<M> && requires(const M &m, std::ostream &out, std::istream &in) {
	{ m.serialize(out) } -> std::same_as<const M &>;
	{ M(in) } -> std::same_as<M>;
};

template <class T>
concept OStreamable = requires(std::ostream &os, const T &t) {
	{ os << t } -> std::convertible_to<std::ostream &>;
};

/// A monoidal Finite-State Automaton: transitions are labeled by elements of
/// an arbitrary monoid T::Monoid
template <class T>
concept FSA = monoid<typename T::Monoid> && state<typename T::State> && requires(T t, typename T::State s) {
	// properties of the transition table
	{ T::deterministic } -> std::convertible_to<bool>;
	{ T::sorted_arcs } -> std::convertible_to<bool>;	 // are arcs comparable as arrays

	// exposes its monoid instance
	{ t.GetMonoid() } -> std::same_as<const typename T::Monoid &>;

	// methods
	{ t.Initial() } -> range_of<typename T::State>;
	{ t.IsInitial(s) } -> std::convertible_to<bool>;
	{ t.Final() } -> range_of<typename T::State>;
	{ t.IsFinal(s) } -> std::convertible_to<bool>;
	{ t.Size() } -> std::convertible_to<std::size_t>;
	// sorted or not, deterministic or not, the transitions are always a range of tuples (Value, State)
	{ t.Transitions(s) } -> range_of<std::tuple<typename T::Monoid::Value, typename T::State>>;
};

/// A Finite-State Transducer (Mihov & Schulz): an FSA over the product of two
/// free monoids, T::InputMonoid (Sigma*) and T::OutputMonoid (Delta*).
/// Sigma* x Delta* is a monoid (T::Monoid, e.g. CartesianMonoid<I,O>) but
/// generally not itself free -- u and v can differ in length on the same
/// transition -- so freeness is required per tape, not on T::Monoid.
template <class T>
concept FST = FSA<T> && free_monoid<typename T::InputMonoid> && free_monoid<typename T::OutputMonoid> &&
			  std::same_as<typename T::Monoid::Value,
						   std::tuple<typename T::InputMonoid::Value, typename T::OutputMonoid::Value>>;

/// A Subsequential Finite-State Transducer: a deterministic, real-time FST --
/// transitions(s) is indexable directly by a T::InputMonoid::Symbol.
template <class T>
concept SSFST = FST<T> && requires(T t, typename T::State s, typename T::InputMonoid::Symbol l) {
	{ T::deterministic == true };
	{ t.Transitions(s)[l] } -> std::convertible_to<std::tuple<typename T::Monoid::Value, typename T::State>>;
	{ t.Psi(s) } -> std::convertible_to<typename T::OutputMonoid::Value>;
};

/// An SSFST that also emits output before consuming the first symbol (the
/// start state carries its own Psi-value).
template <class T>
concept SSFSTI = SSFST<T> && requires(const T t) {
	{ t.InitialOutput() } -> std::convertible_to<typename T::OutputMonoid::Value>;
};

}	  // namespace fl
