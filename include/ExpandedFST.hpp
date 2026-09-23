#pragma once

#include <ctime>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <cassert>
#include <ranges>

#include "FST.hpp"
#include "concepts.hpp"
#include "CartesianMonoid.hpp"
#include "SymbolMonoid.hpp"
#include "utils.h"
#include "debug.hpp"

namespace fl {

/// Expanded FST
///		(FST with only single letter or epsilon on the input tape)
///	If the input tape is never epsilon, then this is a realtime FST (non-deterministic)
template <symbol _InSymbol, monoid M>
class ExpandedFST {
   public:
	constexpr static bool deterministic = false;
	constexpr static bool sorted_arcs	= false;

	using InSymbol	   = _InSymbol;
	using InputMonoid  = SymbolMonoid<InSymbol>;
	using OutputMonoid = M;
	using OutValue	   = typename OutputMonoid::Value;
	using State		   = unsigned int;
	using Monoid	   = CartesianMonoid<InputMonoid, OutputMonoid>;
	using Value		   = typename Monoid::Value;

	using Map = unordered_multimap<State, std::tuple<Value, State>, fl::hash<State>>;

	unsigned int						  N = 0;
	unordered_set<State, fl::hash<State>> qFirsts;
	unordered_set<State, fl::hash<State>> qFinals;

	Monoid monoid;	   // owns the (trivial) input-symbol tape and the output tape's pool
	Map	   transitions;

	/// Mihov & Schulz's f(eps): the output(s) reachable by the empty input
	/// word at the level of the whole FST -- only meaningful once
	/// epsilon-input transitions have been eliminated (removeUpperEpsilonFST).
	/// A plain vector deduped via OutputMonoid::equal, since OutValue isn't
	/// guaranteed hashable/comparable with std::hash/operator== (e.g. an
	/// InterningMonoid::WordId has neither) -- only the monoid instance that
	/// owns it knows how to compare two of its values.
	std::vector<OutValue> f_eps{};

	constexpr ExpandedFST()
		requires(std::is_default_constructible_v<Monoid>)
		: N(0), monoid() {}

	constexpr ExpandedFST(const ExpandedFST &)			  = default;
	constexpr ExpandedFST(ExpandedFST &&)				  = default;
	constexpr ExpandedFST &operator=(const ExpandedFST &) = default;
	constexpr ExpandedFST &operator=(ExpandedFST &&)	  = default;

	explicit constexpr ExpandedFST(Monoid &&m) : N(0), monoid(std::move(m)) {}
	explicit constexpr ExpandedFST(const Monoid &m) : N(0), monoid(m) {}

	State newState() { return N++; }

	void addTransition(State from, InSymbol a, OutValue b, State to) { transitions.insert({from, {Value(a, b), to}}); }
	void addTransition(State from, const Value &label, State to) { transitions.insert({from, {label, to}}); }

	/// re-creates a value that was produced by a different (but structurally
	/// identical) monoid instance inside this FST's own monoid -- see
	/// SparseFST::reintern.
	Value reintern(const Monoid &src, const Value &v) {
		const auto &[a, b] = v;
		return Value(a, monoid.template getMonoid<1>().own(src.template getMonoid<1>(), b));
	}

	/// insert v into f_eps if it isn't already there (dedup via the owning monoid's equal())
	void addFEps(OutValue v) {
		auto &outMonoid = monoid.template getMonoid<1>();
		for (const auto &existing : f_eps) {
			if (outMonoid.equal(existing, v)) return;
		}
		f_eps.push_back(std::move(v));
	}

	void print(std::ostream &out) const {
		// print in DOT
		out << "digraph TFSA {\n";
		out << "  rankdir=LR;\n";
		out << "  node [shape=circle];\n";
		out << "  init [label=\"N=" << this->N << "\", shape=square];\n";
		for (const auto &i : qFinals) {
			out << "  " << i << " [shape=doublecircle];\n";		// final States
		}
		for (const auto &i : qFirsts) {
			out << "  init -> " << i << " [style=dotted];\n";	  // initial states
		}
		for (const auto &[from, value] : transitions) {
			const auto &[label, to] = value;
			auto [w1, w2]			= monoid.gen(label);
			out << "  " << from << " -> " << to << " [label=\"<";
			if constexpr (OStreamable<InSymbol>) {
				for (const auto &c : w1)
					out << c;
			} else {
				out << "unprintable";
			}
			out << ", ";
			if constexpr (OStreamable<typename OutputMonoid::Symbol>) {
				for (const auto &c : w2)
					out << c;
			} else {
				out << "unprintable";
			}
			out << ">\"];\n";
		}
		out << "}\n";
	}

	const auto &Initial() const { return qFirsts; }
	bool		IsInitial(State q) const { return qFirsts.contains(q); }
	const auto &Final() const { return qFinals; }
	bool		IsFinal(State q) const { return qFinals.contains(q); }
	std::size_t Size() const { return N; }
	auto		Transitions(State q) const {
		auto [begin, end] = transitions.equal_range(q);
		return std::ranges::subrange(begin, end) | std::views::values;
	}
};

/// extracts the label (the full Monoid::Value tuple) out of every (label, to)
/// entry in a transitions map, for feeding into Monoid::compact() as one of
/// its live-value ranges.
template <class Map>
auto transitionValues(const Map &transitions) {
	return transitions | std::views::values |
		   std::views::transform([](const auto &labelAndTo) { return std::get<0>(labelAndTo); });
}

/// f_eps only ever holds OutputMonoid::Value (there's no meaningful input
/// symbol for "the output of the empty input word"), but Monoid::compact()
/// needs ranges of the FULL Monoid::Value tuple so it can slice per tape --
/// pairs each one with a placeholder input symbol (never read: compact() only
/// ever touches the tapes that are actually compactable, and InputMonoid is
/// SymbolMonoid, which never is).
template <symbol Symbol, monoid M>
auto fEpsAsValues(const ExpandedFST<Symbol, M> &fsa) {
	using Value = typename ExpandedFST<Symbol, M>::Value;
	return fsa.f_eps | std::views::transform([](const auto &v) { return Value(Symbol::eps, v); });
}

/// splits every (possibly multi-symbol-in, multi-symbol-out) transition of a
/// SparseFST into a chain of single-input-symbol (or epsilon-input)
/// transitions, greedily attaching output symbols one at a time to the
/// input-symbol hop they line up with, and dumping any leftover output (when
/// |w1| < |w2|) or leftover input (when |w1| > |w2|) onto the last hop.
template <symbol Symbol>
auto expandFST(StringFST<Symbol> &&fst) {
	ExpandedFST<Symbol, InterningMonoid<Symbol>> expanded;
	using State	   = typename ExpandedFST<Symbol, InterningMonoid<Symbol>>::State;
	using OutValue = typename ExpandedFST<Symbol, InterningMonoid<Symbol>>::OutValue;

	expanded.N		 = fst.N;
	expanded.qFirsts = std::move(fst.qFirsts);
	expanded.qFinals = std::move(fst.qFinals);

	auto &outMonoid = expanded.monoid.template getMonoid<1>();

	for (const auto &[from, value] : fst.transitions) {
		const auto &[label, to] = value;
		auto [w1, w2]			= fst.monoid.gen(label);
		if (fst.template isIdentityOnTape<0>(label)) {
			OutValue new_val = outMonoid.own(fst.monoid.template getMonoid<1>(), std::get<1>(label));
			expanded.addTransition(from, Symbol::eps, new_val, to);
			continue;
		}
		State prev = from;

		if (w1.size() < w2.size()) {	 // |w1| < |w2|
			assert(w1.size() > 0);
			for (unsigned int i = 0; i < w1.size() - 1; ++i) {
				const auto &a	  = w1[i];
				const auto &b	  = w2[i];
				State		next  = expanded.newState();
				OutValue	w2val = outMonoid.create(std::span{&b, &b + 1});
				expanded.addTransition(prev, a, w2val, next);
				prev = next;
			}
			OutValue w2val = outMonoid.create(std::span{w2.data() + w1.size() - 1, w2.size() - w1.size() + 1});
			expanded.addTransition(prev, w1.back(), w2val, to);
		} else {
			for (unsigned int i = 0; i < w2.size(); ++i) {
				const auto &a	  = w1[i];
				const auto &b	  = w2[i];
				State		next  = (i == w1.size() - 1) ? to : expanded.newState();
				OutValue	w2val = outMonoid.create(std::span{&b, &b + 1});
				expanded.addTransition(prev, a, w2val, next);
				prev = next;
			}
			for (unsigned int i = w2.size(); i < w1.size(); ++i) {
				const auto &a	 = w1[i];
				State		next = (i == w1.size() - 1) ? to : expanded.newState();
				expanded.addTransition(prev, a, InterningMonoid<Symbol>::identity, next);
				prev = next;
			}
		}
	}

	return expanded;
}

template <symbol Symbol, monoid M>
void drawFSA(const ExpandedFST<Symbol, M> &fsa) {
	ShellProcess p("dot -Tsvg > a.svg && feh ./a.svg");
	fsa.print(p.in());
	p.in() << std::endl;
	p.in().close();
	p.wait();
	auto out = getString(p.out()), err = getString(p.err());
	if (!out.empty()) std::cout << out << std::endl;
	if (!err.empty()) std::cout << err << std::endl;
}

// https://lml.bas.bg/~stoyan/finite-state-techniques.pdf#theorem.4.4.8
template <symbol Symbol>
auto removeUpperEpsilonFST(ExpandedFST<Symbol, InterningMonoid<Symbol>> &&fsa) {
	using FSA_t	   = ExpandedFST<Symbol, InterningMonoid<Symbol>>;
	using State	   = typename FSA_t::State;
	using OutValue = typename FSA_t::OutValue;

	auto &outMonoid = fsa.monoid.template getMonoid<1>();

	std::stack<int>													 stack;
	std::vector<bool>												 visited(fsa.N, false);
	std::vector<std::vector<std::tuple<State, std::vector<Symbol>>>> closure(fsa.N);	 // TODO: this is slow

	for (State i = 0; i < fsa.N; ++i) {
		stack.push(0);
		visited[i] = true;
		closure[i].push_back({i, {}});	   // add the state itself with an empty word
		while (!stack.empty()) {
			auto p					 = stack.top();
			const auto &[current, u] = closure[i][p];
			stack.pop();

			auto [i1, i2] = fsa.transitions.equal_range(current);
			for (const auto &[_, value] : std::ranges::subrange(i1, i2)) {
				const auto &[label, to] = value;
				const auto &[w1, w2]	= label;
				if (w1 == Symbol::eps && !visited[to]) {	 // epsilon transition
					visited[to]	  = true;
					auto new_word = u;
					auto gw2	  = outMonoid.gen(w2);
					new_word.insert(new_word.end(), gw2.begin(), gw2.end());
					closure[i].push_back({to, std::move(new_word)});
					stack.push(closure[i].size() - 1);
				}
			}
		}

		visited.assign(fsa.N, false);
	}

	for (const auto &i : fsa.qFirsts) {
		for (const auto &[c, w] : closure[i]) {
			if (fsa.qFinals.contains(c)) {
				fsa.qFinals.insert(i);
				fsa.addFEps(outMonoid.create(w));	  // f(eps)
			}
		}
	}

	std::erase_if(fsa.transitions, [](const auto &pair) {
		const auto &[from, value] = pair;
		const auto &[label, to]	  = value;
		const auto &[w1, w2]	  = label;
		return w1 == Symbol::eps;	  // remove epsilon transitions
	});

	typename FSA_t::Map new_transitions;
	for (State q1 = 0; q1 < fsa.N; ++q1) {
		for (const auto &[q_, u] : closure[q1]) {
			auto [i1, i2] = fsa.transitions.equal_range(q_);
			for (const auto &[_, value] : std::ranges::subrange(i1, i2)) {
				const auto &[label, q__] = value;
				const auto &[sigma, w2]	 = label;
				auto v					 = outMonoid.gen(w2);
				for (const auto &[q2, w] : closure[q__]) {
					auto new_word = u;
					new_word.insert(new_word.end(), v.begin(), v.end());
					new_word.insert(new_word.end(), w.begin(), w.end());
					OutValue new_val = outMonoid.create(new_word);
					new_transitions.insert({q1, {typename FSA_t::Value(sigma, new_val), q2}});
				}
			}
		}
	}
	fsa.transitions = std::move(new_transitions);

	// the epsilon-labeled transitions erased above may have been the only
	// reference to some pool entries (e.g. the output word carried along a
	// now-removed epsilon hop) -- reclaim them.
	if constexpr (compactable_monoid<typename FSA_t::Monoid>) {
		fsa.monoid.compact(transitionValues(fsa.transitions), fEpsAsValues(fsa));
	}

	return std::move(fsa);
}

/// discards states unreachable from an initial state or that can't reach a
/// final state, and (when OutputMonoid supports it) compacts away whatever
/// pool entries only the discarded transitions referenced.
template <symbol Symbol, monoid M>
auto trimFSA(ExpandedFST<Symbol, M> &&fsa) {
	using State	 = typename ExpandedFST<Symbol, M>::State;
	using Monoid = typename ExpandedFST<Symbol, M>::Monoid;

	if (fsa.qFinals.empty()) {
		fsa.N		= 0;
		fsa.qFirsts = {0};
		fsa.transitions.clear();
		if constexpr (compactable_monoid<Monoid>) { fsa.monoid.compact(fEpsAsValues(fsa)); }
		return std::move(fsa);
	}

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
		if (fsa.qFinals.size() != fsa.N) {
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
		} else {
			std::fill(visited_back.begin(), visited_back.end(), true);
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

	std::size_t		   cnt = 0;
	std::vector<State> new_map(fsa.N, -1);
	for (unsigned int i = 0; i < fsa.N; ++i) {
		if (visited_back[i] && visited_forw[i]) { new_map[i] = cnt++; }
	}

	if (cnt == fsa.N) {
		if constexpr (compactable_monoid<Monoid>) {
			fsa.monoid.compact(transitionValues(fsa.transitions), fEpsAsValues(fsa));
		}
		return std::move(fsa);
	}

	ExpandedFST<Symbol, M> new_fsa;
	new_fsa.monoid = std::move(fsa.monoid);
	new_fsa.N	   = cnt;
	new_fsa.qFirsts.reserve(fsa.qFirsts.size());
	for (const auto &q : fsa.qFirsts) {
		if (new_map[q] != -1u) { new_fsa.qFirsts.insert(new_map[q]); }
	}

	new_fsa.qFinals.reserve(fsa.qFinals.size());
	for (const auto &q : fsa.qFinals) {
		if (new_map[q] != -1u) { new_fsa.qFinals.insert(new_map[q]); }
	}

	new_fsa.f_eps = std::move(fsa.f_eps);

	for (const auto &[from, value] : fsa.transitions) {
		const auto &[label, to] = value;
		if (new_map[from] != -1u && new_map[to] != -1u) {
			new_fsa.transitions.insert({new_map[from], {label, new_map[to]}});
		}
	}

	if constexpr (compactable_monoid<Monoid>) {
		new_fsa.monoid.compact(transitionValues(new_fsa.transitions), fEpsAsValues(new_fsa));
	}

	return std::move(new_fsa);
}

template <symbol Symbol>
auto realtimeFST(StringFST<Symbol> &&fst) {
	return trimFSA(removeUpperEpsilonFST(expandFST(removeEpsilonFST(trimFSA(std::move(fst))))));
}

/// Pseudo-determinization of an Expanded FST that has to be real-time
/// (Mihov & Schulz): merges states reachable by identical (input symbol,
/// output value) pairs into a single "big state" (a subset of the original
/// states), without factoring out common output prefixes -- that's the job
/// of a later, real determinization pass. Reuses fst's own monoid wholesale
/// (dfa.monoid = std::move(fst.monoid)) since no new words are created here,
/// only states are merged.
template <symbol Symbol, free_monoid M>
auto pseudoDeterminizeFST(ExpandedFST<Symbol, M> &&fst) {
	using FSA_t	 = ExpandedFST<Symbol, M>;
	using Monoid = typename FSA_t::Monoid;
	using State	 = typename FSA_t::State;

	using BigState = std::vector<State>;

	FSA_t dfa;
	dfa.monoid = std::move(fst.monoid);		// no new interning needed, only states are merged
	dfa.f_eps  = std::move(fst.f_eps);		// f(eps) is a property of the whole automaton, unaffected by state merging

	std::vector<std::reference_wrapper<const BigState>> states;
	fl::unordered_map<BigState, State>					state_map;
	std::queue<State>									queue;

	auto getStateID = [&](BigState &&bs) -> std::pair<State, bool> {
		std::ranges::sort(bs);
		bs.erase(std::unique(bs.begin(), bs.end()), bs.end());

		auto it = state_map.find(bs);
		if (it == state_map.end()) {
			State new_id = dfa.newState();
			for (const auto &s : bs) {
				if (fst.qFinals.contains(s)) {
					dfa.qFinals.insert(new_id);
					break;
				}
			}
			auto [it2, _] = state_map.emplace(std::move(bs), new_id);
			states.emplace_back(it2->first);
			return {new_id, true};
		} else {
			return {it->second, false};
		}
	};

	auto [initial_state, _] = getStateID(BigState{std::from_range, fst.qFirsts});
	queue.push(initial_state);
	dfa.qFirsts.insert(initial_state);

	std::unordered_map<typename Monoid::Value, BigState, monoid_hash<Monoid>, monoid_equal<Monoid>> current_transitions(
		0, monoid_hash<Monoid>(&dfa.monoid), monoid_equal<Monoid>(&dfa.monoid));

	while (!queue.empty()) {
		State current = queue.front();
		queue.pop();
		const BigState &current_bs = states[current];

		current_transitions.clear();

		for (const auto &q : current_bs) {
			auto [i1, i2] = fst.transitions.equal_range(q);
			for (const auto &[_, value] : std::ranges::subrange(i1, i2)) {
				const auto &[label, to] = value;
				current_transitions[label].push_back(to);
			}
		}

		for (const auto &[sigma, next_bs] : current_transitions) {
			auto [next_state, is_new] = getStateID(BigState(next_bs));
			dfa.addTransition(current, sigma, next_state);
			if (is_new) { queue.push(next_state); }
		}
	}

	// dbLog(dbg::LOG_DEBUG, "TFSA Pseudo-determinized FST has ", dfa.N, " states.");
	// drawFSA(dfa);

	// the subset construction only ever visits states reachable from
	// fst.qFirsts (the BFS over `queue`) -- pool entries that were only
	// referenced by transitions out of unreached states are now dead.
	if constexpr (compactable_monoid<Monoid>) { dfa.monoid.compact(transitionValues(dfa.transitions), fEpsAsValues(dfa)); }

	return dfa;
}
}	  // namespace fl
