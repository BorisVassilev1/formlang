#pragma once

#include <type_traits>
#include "CartesianMonoid.hpp"
#include "concepts.hpp"

namespace fl {

/// A monoidal Finite-State Automaton: transitions are labeled by elements of
/// an arbitrary monoid T::Monoid
template <class T>
concept FSA = monoid<typename T::Monoid> && state<typename T::State> && requires(T t, typename T::State s) {
	// properties of the transition table
	{ T::deterministic } -> std::convertible_to<bool>;

	// exposes its monoid instance
	{ t.GetMonoid() } -> std::same_as<const typename T::Monoid &>;

	// methods
	{ t.Initial() } -> range_of<typename T::State>;
	{ t.IsInitial(s) } -> std::convertible_to<bool>;
	{ t.Final() } -> range_of<typename T::State>;
	{ t.IsFinal(s) } -> std::convertible_to<bool>;
	{ t.Size() } -> std::convertible_to<std::size_t>;

	{ t.Transitions() } -> range_of<std::tuple<typename T::State, typename T::Monoid::Value, typename T::State>>;
};

template <class T>
concept FSA_with_arcs = FSA<T> && requires(T t, typename T::State s, typename T::Monoid::Value v) {
	{ T::sorted_arcs } -> std::convertible_to<bool>;	 // are arcs comparable as arrays
	// sorted or not, deterministic or not, the transitions are always a range of tuples (Value, State)
	{ t.Transitions(s) } -> range_of<std::tuple<typename T::Monoid::Value, typename T::State>>;
};

// get_input and get_output type traits ------------------

template <class T>
struct get_input;

template <cartesian_monoid M>
	requires(std::tuple_size_v<M> == 2)
struct get_input<M> {
	using type = std::remove_cvref_t<std::tuple_element_t<0, M>>;
};

template <class T>
	requires(std::tuple_size_v<typename T::Monoid> == 2)
struct get_input<T> {
	using type = std::remove_cvref_t<std::tuple_element_t<0, typename T::Monoid>>;
};

template <class T>
struct get_output;

template <cartesian_monoid M>
	requires(std::tuple_size_v<M> == 2)
struct get_output<M> {
	using type = std::remove_cvref_t<std::tuple_element_t<1, M>>;
};

template <class T>
	requires(std::tuple_size_v<typename T::Monoid> == 2)
struct get_output<T> {
	using type = std::remove_cvref_t<std::tuple_element_t<1, typename T::Monoid>>;
};

// _t extensions ----------------

template <class T>
using get_input_t = typename get_input<T>::type;

template <class T>
using get_output_t = typename get_output<T>::type;

/// A Finite-State Transducer: an FSA over the product of two free monoids
template <class T>
concept FST = FSA<T> &&											//
			  std::tuple_size_v<typename T::Monoid> == 2 &&		//
			  free_monoid<get_input_t<T>> &&					//
			  free_monoid<get_output_t<T>>;

template <class T>
concept FST_with_arcs = FST<T> && FSA_with_arcs<T>;

/// A Finite-State Transducer that can be traversed by consuming input symbols
template <class T>
concept FST_traversable = FST<T> && requires(T t, typename T::State s, typename get_input_t<T>::Symbol l) {
	{
		t.Transition(s, l)
	} -> std::convertible_to<std::optional<std::tuple<typename get_output_t<T>::Value, typename T::State>>>;
};

/// A Subsequential Finite-State Transducer: a deterministic, real-time FST --
/// transitions(s) is indexable directly by a T::InputMonoid::Symbol.
template <class T>
concept SSFST =		//
	FST<T> &&		//
	requires(T t, typename T::State s, typename get_input_t<T>::Symbol l) {
		{ T::deterministic == true };
		//{ t.Transitions(s)[l] } -> std::convertible_to<std::tuple<typename T::Monoid::Value, typename T::State>>;
		{ t.Psi(s) } -> std::convertible_to<typename get_output_t<T>::Value>;
	};

template <class T>
concept SSFST_traversable = SSFST<T> && FST_traversable<T>;

/// An SSFST that also emits output before consuming the first symbol (the
/// start state carries its own Psi-value).
template <class T>
concept SSFSTI = SSFST<T> && requires(const T t) {
	{ t.InitialOutput() } -> std::convertible_to<typename T::OutputMonoid::Value>;
};

template <class T>
concept SSFSTI_traversable = SSFSTI<T> && FST_traversable<T>;

}	  // namespace fl
