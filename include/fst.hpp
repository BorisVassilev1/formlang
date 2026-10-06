#pragma once

#include <cassert>
#include <fstream>
#include <stack>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <ostream>
#include <ranges>
#include <type_traits>
#include <stdexcept>
#include <typeinfo>

#include "pipes.hpp"
#include "cartesian_monoid.hpp"
#include "interning_monoid.hpp"
#include "transducer_concepts.hpp"
#include "concepts.hpp"
#include "hashing.hpp"
#include "datastructures.hpp"
#include "utils.hpp"

namespace fl {

// Classical Finite State Transducer (FST) class template
//
template <monoid I, monoid M>
class SparseFST {
   public:
	constexpr static bool deterministic = false;
	constexpr static bool sorted_arcs	= false;

	using Symbol	   = I::Symbol;
	using InputMonoid  = I;
	using OutputMonoid = M;
	using State		   = unsigned int;
	using Monoid	   = std::conditional_t<std::is_same_v<M, I>, DiagonalMonoid<I>, CartesianMonoid<I, M>>;

	using Value = typename Monoid::Value;
	using Map	= unordered_multimap<State, std::tuple<Value, State>, fl::hash<State>>;
	unsigned int						  N;
	unordered_set<State, fl::hash<State>> qFirsts;
	unordered_set<State, fl::hash<State>> qFinals;

	Monoid monoid;	   // owns the interning pools for both tapes
	Map	   transitions;

	constexpr SparseFST()
		requires(std::is_default_constructible_v<Monoid>)
		: N(0), monoid() {}

	constexpr SparseFST(const SparseFST &)			  = default;
	constexpr SparseFST(SparseFST &&)				  = default;
	constexpr SparseFST &operator=(const SparseFST &) = default;
	constexpr SparseFST &operator=(SparseFST &&)	  = default;

	explicit constexpr SparseFST(Monoid &&m) : N(0), monoid(std::move(m)) {}
	explicit constexpr SparseFST(const Monoid &m) : N(0), monoid(m) {}

	void addTransition(State from, const Value &label, State to) { transitions.insert({from, {std::move(label), to}}); }
	void addTransition(State from, const I::Value &in, const M::Value &out, State to) {
		transitions.insert({from, {{std::move(in), std::move(out)}, to}});
	}

	/// re-creates a value that was produced by a different (but structurally
	/// identical) monoid instance inside this FST's own monoid, so that
	/// transitions can be merged across FSTs built independently (e.g. when
	/// combining two automata into a union/concatenation).
	Value reintern(const Monoid &src, const Value &v) {
		auto [w1, w2] = src.gen(v);
		return Value(get<0>(monoid).from(w1), get<1>(monoid).from(w2));
	}

	/// whether the label is the identity of the I-th tape's monoid (i.e. that tape reads/writes epsilon)
	template <std::size_t i>
	bool isIdentityOnTape(const Value &v) const {
		return get<i>(monoid).equal(std::get<i>(v), std::get<i>(Monoid::identity));
	}

	/// total number of distinct words interned across both tapes' pools (diagnostic use only)
	std::size_t wordCount() const {
		bool isDiagonal = &get<0>(monoid) == &get<1>(monoid);
		return get<0>(monoid).totalWordCount() + (isDiagonal ? 0 : get<1>(monoid).totalWordCount());
	}

	void printInfo(std::ostream &out) const {
		out << "Finite State Transducer: |Q| = " << N << ", |Δ| = " << transitions.size()
			<< ", |F| = " << qFinals.size() << "\n";
		// if constexpr (requires { get<1>(monoid).totalWordCount(); })
		//	out << "Words stored: " << get<1>(monoid).totalWordCount() << "\n";
		// if constexpr (requires { get<1>(monoid).poolByteCount(); })
		//	out << "Words cache size: " << get<1>(monoid).poolByteCount() << "\n";
	}

	void print(std::ostream &out) const {
		// print in DOT
		out << "digraph FST {\n";
		out << "  rankdir=LR;\n";
		out << "  node [shape=circle];\n";
		out << "  init [label=\"N=" << this->N << "\", shape=square];\n";
		for (const int i : qFinals) {
			out << "  " << i << " [shape=doublecircle];\n";		// final States
		}
		for (const int i : qFirsts) {
			out << "  init -> " << i << " [style=dotted];\n";	  // initial states
		}
		for (const auto &[from, second] : transitions) {
			const auto &[label, to] = second;
			auto [w1, w2]			= monoid.gen(label);
			out << "  " << from << " -> " << to << " [label=\"<";
			for (const auto &c : w1)
				out << c;
			out << ", ";
			for (const auto &c : w2)
				out << c;
			out << ">\"];\n";
		}
		out << "}\n";
	}

	const auto &Initial() const { return qFirsts; }
	auto		Initial()		 &&{ return std::move(qFirsts); }
	bool		IsInitial(State q) const { return qFirsts.contains(q); }
	const auto &Final() const { return qFinals; }
	auto		Final()		   &&{ return std::move(qFinals); }
	bool		IsFinal(State q) const { return qFinals.contains(q); }
	std::size_t Size() const { return N; }
	auto		Transitions(State q) const {
		auto [begin, end] = transitions.equal_range(q);
		return std::ranges::subrange(begin, end) | std::views::values;
	}

	auto Transitions() const {
		return transitions | std::views::transform([](const auto &pair) {
				   const auto &[value, to] = pair.second;
				   return std::make_tuple(pair.first, value, to);
			   });
	}

	auto &RawTransitions() & { return transitions; }
	auto  RawTransitions()	&&{ return std::move(transitions); }

	const Monoid &GetMonoid() const { return monoid; }
	Monoid		 &GetMonoid()		&{ return monoid; }
	Monoid		&&GetMonoid()	   &&{ return std::move(monoid); }

	/////////////// builder interface //////////////////////
	State NewState() { return N++; }
	void  AddInitial(State state) { qFirsts.insert(state); }
	void  AddFinal(State state) { qFinals.insert(state); }
	void AddTransition(State from, const Value &label, State to) { transitions.insert({from, {std::move(label), to}}); }
};

template <free_monoid I, monoid M>
inline void saveFSA(const SparseFST<I, M> &fsa, const std::string &filename) {
	std::ofstream out(filename);
	if (!out.is_open()) { throw std::runtime_error("Could not open file " + filename + " for writing."); }
	fsa.print(out);
	out.close();
}

template <free_monoid I, monoid M>
inline void statFSA(const SparseFST<I, M> &fsa, std::ostream &out = std::cout) {
	fsa.printInfo(out);
}

}	  // namespace fl
