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
	const Monoid &GetMonoid() const { return monoid; }
};

template <free_monoid I, monoid M>
inline void saveFSA(const SparseFST<I, M> &fsa, const std::string &filename) {
	std::ofstream out(filename);
	if (!out.is_open()) { throw std::runtime_error("Could not open file " + filename + " for writing."); }
	fsa.print(out);
	out.close();
}

template <free_monoid I, monoid M>
auto trimFSA(SparseFST<I, M> &&fsa) {
	if (fsa.qFinals.empty()) {
		fsa.N		= 0;
		fsa.qFirsts = {0};
		fsa.transitions.clear();
		return std::move(fsa);
	}
	using State = SparseFST<I, M>::State;
	std::vector<bool> visited_back(fsa.N, false);
	std::vector<bool> visited_forw(fsa.N, false);

	{
		auto						   &forwardTransitions = fsa.transitions;
		std::vector<std::vector<State>> backwardTransitions;
		backwardTransitions.resize(fsa.N);
		for (const auto &[from, value] : forwardTransitions) {
			const auto &[label, to] = value;
			backwardTransitions[to].push_back(from);
		}

		std::vector<State> stack;
		for (const auto &final : fsa.qFinals) {
			visited_back[final] = true;
			stack.push_back(final);
		}
		while (!stack.empty()) {
			State current = stack.back();
			stack.pop_back();
			for (const auto &next : backwardTransitions[current]) {
				if (!visited_back[next]) {
					visited_back[next] = true;
					stack.push_back(next);
				}
			}
		}

		for (const auto &first : fsa.qFirsts) {
			visited_forw[first] = true;		// mark initial states as visited
			stack.push_back(first);
		}
		while (!stack.empty()) {
			State current = stack.back();
			stack.pop_back();
			auto [i1, i2] = forwardTransitions.equal_range(current);
			for (const auto &[_, value] : std::ranges::subrange(i1, i2)) {
				const auto &[label, to] = value;
				if (!visited_forw[to]) {
					visited_forw[to] = true;
					stack.push_back(to);
				}
			}
		}
	}
	int				   cnt = 0;
	std::vector<State> new_map(fsa.N, -1);
	for (unsigned int i = 0; i < fsa.N; ++i) {
		if (visited_back[i] && visited_forw[i]) { new_map[i] = cnt++; }
	}
	SparseFST<I, M> new_fsa;
	new_fsa.monoid = std::move(
		fsa.monoid);	 // word pool entries stay valid regardless of which states/transitions survive trimming
	new_fsa.N = cnt;
	new_fsa.qFirsts.reserve(fsa.qFirsts.size());
	for (const auto &q : fsa.qFirsts) {
		if (new_map[q] != -1u) { new_fsa.qFirsts.insert(new_map[q]); }
	}

	new_fsa.qFinals.reserve(fsa.qFinals.size());
	for (const auto &q : fsa.qFinals) {
		if (new_map[q] != -1u) { new_fsa.qFinals.insert(new_map[q]); }
	}

	for (const auto &[from, value] : fsa.transitions) {
		const auto &[label, to] = value;
		if (new_map[from] != -1u && new_map[to] != -1u) {
			new_fsa.transitions.insert({new_map[from], {label, new_map[to]}});
		}
	}

	return std::move(new_fsa);
}

/// gets rid of (epsilon, epsilon) transitions preserving the language of the FST.
template <free_monoid I, monoid M>
auto removeEpsilonFST(SparseFST<I, M> &&fsa) {
	using State	 = typename SparseFST<I, M>::State;
	using Monoid = typename SparseFST<I, M>::Monoid;

	std::stack<State>				stack;
	std::vector<bool>				visited(fsa.N, false);
	std::vector<std::vector<State>> closure(fsa.N);

	for (State i = 0; i < fsa.N; ++i) {
		stack.push(i);
		visited[i] = true;
		while (!stack.empty()) {
			State current = stack.top();
			stack.pop();

			auto [i1, i2] = fsa.transitions.equal_range(current);
			for (const auto &[_, value] : std::ranges::subrange(i1, i2)) {
				const auto &[label, to] = value;
				if (fsa.monoid.equal(label, Monoid::identity) && !visited[to]) {	 // epsilon transition
					stack.push(to);
					visited[to] = true;
					closure[i].push_back(to);
				}
			}
		}

		visited.assign(fsa.N, false);
	}

	std::erase_if(fsa.transitions, [&fsa](const auto &pair) {
		const auto &[from, value] = pair;
		const auto &[label, to]	  = value;
		return fsa.monoid.equal(label, Monoid::identity);	  // remove epsilon transitions
	});

	typename SparseFST<I, M>::Map new_transitions;
	new_transitions.insert(fsa.transitions.begin(), fsa.transitions.end());
	for (const auto &[from, value] : fsa.transitions) {
		const auto &[label, to] = value;
		assert(!fsa.monoid.equal(label, Monoid::identity));
		for (const auto &next : closure[to]) {
			new_transitions.insert({from, {label, next}});
		}
	}

	unordered_set<State> new_qFirsts;
	for (const State &i : fsa.qFirsts) {
		new_qFirsts.insert(i);
		for (const State &j : closure[i]) {
			new_qFirsts.insert(j);
		}
	}
	fsa.qFirsts = std::move(new_qFirsts);

	fsa.transitions = std::move(new_transitions);
	return std::move(fsa);
}
}	  // namespace fl
