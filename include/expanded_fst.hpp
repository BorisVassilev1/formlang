#pragma once

#include <ctime>
#include <limits>
#include <map>
#include <queue>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <cassert>
#include <ostream>
#include <ranges>
#include <stack>
#include <vector>

#include "concepts.hpp"
#include "cartesian_monoid.hpp"
#include "dbg.hpp"
#include "symbol_monoid.hpp"
#include "transducer_concepts.hpp"
#include "hashing.hpp"
#include "datastructures.hpp"

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

	// the set of (epsilon, x) \in the relation. A plain vector, content-deduplicated by
	// addFEps() below -- kept small in practice, and (unlike unordered_set) its elements
	// can be renumbered in place by InterningMonoid::compact().
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

	State NewState() { return N++; }

	void AddTransition(State from, const Value &label, State to) { transitions.insert({from, {label, to}}); }
	void AddInitial(State q) { qFirsts.insert(q); }
	void AddFinal(State q) { qFinals.insert(q); }

	template <class T>
		requires free_monoid<M>
	void addTransition(State from, InSymbol a, T &&b, State to) {
		AddTransition(from, Value{a, get<1>(monoid).from(b)}, to);
	}

	/// re-creates a value that was produced by a different (but structurally
	/// identical) monoid instance inside this FST's own monoid -- see
	/// SparseFST::reintern.
	Value reintern(const Monoid &src, const Value &v) {
		const auto &[a, b] = v;
		return Value(a, get<1>(monoid).own(get<1>(src), b));
	}

	void addFEps(OutValue v) {
		for (const auto &existing : f_eps) {
			if (get<1>(monoid).equal(existing, v)) return;
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
			out << "  " << from << " -> " << to << " [label=\"";
			out << print_if_can(monoid, label);
			out << "\"];\n";
		}
		out << "}\n";
	}

	void printInfo(std::ostream &out) const {
		out << "Expanded FST: |Q| = " << N << ", |Δ| = " << transitions.size() << ", |F| = " << qFinals.size() << "\n";
	}

	const Monoid &GetMonoid() const { return monoid; }
	Monoid		 &GetMonoid()		&{ return monoid; }
	Monoid		&&GetMonoid()	   &&{ return std::move(monoid); }

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
				   const auto &[label, to] = pair.second;
				   return std::make_tuple(pair.first, label, to);
			   });
	}

	auto &RawTransitions() & { return transitions; }
	auto  RawTransitions()	&&{ return std::move(transitions); }

	/// mutable, storage-backed view of every transition's output value -- see
	/// TotalSSFST::TransitionValues() for why this can't go through Transitions().
	auto TransitionValues() & {
		return transitions | std::views::values |
			   std::views::transform([](auto &labelAndTo) -> auto & { return std::get<1>(std::get<0>(labelAndTo)); });
	}
};

template <symbol Symbol, monoid M>
void statFSA(const ExpandedFST<Symbol, M> &fsa, std::ostream &out = std::cout) {
	fsa.printInfo(out);
}

/// mutable view of the output-tape value carried by every transition. The input tape
/// (always a plain SymbolMonoid, never pooled) is never what compact() needs to touch --
/// only the output tape's InterningMonoid is ever compactable here -- so this projects
/// straight down to it, letting callers hand it directly to the output monoid's compact()
/// alongside f_eps (see the call sites below), rather than going through
/// SharedCartesianMonoid::compact()'s generic per-tape dispatch.
template <class Map>
auto transitionValues(Map &transitions) {
	return transitions | std::views::values |
		   std::views::transform([](auto &labelAndTo) -> auto & { return std::get<1>(std::get<0>(labelAndTo)); });
}

/// Pseudo-determinization of any real-time FST
template <FST_with_arcs T>
auto pseudoDeterminizeFST(const T &fst) {
	using Symbol   = typename get_input_t<T>::Symbol;
	using M		   = get_output_t<T>;
	using FSA_t	   = ExpandedFST<Symbol, M>;
	using Monoid   = typename FSA_t::Monoid;
	using InState  = typename T::State;
	using OutState = typename FSA_t::State;
	using Value	   = typename Monoid::Value;

	using BigState = std::vector<InState>;

	FSA_t dfa;	   // starts with a fresh, empty pool of its own
	if constexpr (requires { fst.f_eps; }) {
		for (const auto &v : fst.f_eps) {
			dfa.addFEps(get<1>(dfa.monoid).own(get<1>(fst.GetMonoid()), v));
		}
	}

	std::vector<std::reference_wrapper<const BigState>> states;
	fl::unordered_map<BigState, OutState>				state_map;
	std::queue<OutState>								queue;

	auto getStateID = [&](BigState &&bs) -> std::pair<OutState, bool> {
		if constexpr (!T::sorted_arcs) std::ranges::sort(bs);
		bs.erase(std::unique(bs.begin(), bs.end()), bs.end());

		auto it = state_map.find(bs);
		if (it == state_map.end()) {
			OutState new_id = dfa.NewState();
			for (const auto &s : bs) {
				if (fst.IsFinal(s)) {
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

	auto [initial_state, _] = getStateID(BigState{std::from_range, fst.Initial()});
	queue.push(initial_state);
	dfa.qFirsts.insert(initial_state);

	std::unordered_map<Value, BigState, monoid_hash<Monoid>, monoid_equal<Monoid>> current_transitions(
		0, monoid_hash<Monoid>(&dfa.monoid), monoid_equal<Monoid>(&dfa.monoid));

	while (!queue.empty()) {
		OutState current = queue.front();
		queue.pop();
		const BigState &current_bs = states[current];

		current_transitions.clear();

		for (const auto &q : current_bs) {
			auto outTransitions = fst.Transitions(q);
			for (const auto &[label, to] : outTransitions) {
				current_transitions[label].push_back(to);
			}
		}

		for (const auto &[sigma, next_bs] : current_transitions) {
			auto [next_state, is_new] = getStateID(BigState(next_bs));
			dfa.AddTransition(current, dfa.reintern(fst.GetMonoid(), sigma), next_state);
			if (is_new) { queue.push(next_state); }
		}
	}

	// the subset construction only ever visits states reachable from
	// fst.Initial() (the BFS over `queue`) -- pool entries that were only
	// referenced by transitions out of unreached states are now dead.
	if constexpr (compactable_monoid<Monoid>) {
		get<1>(dfa.monoid).compact(transitionValues(dfa.transitions), dfa.f_eps);
	}

	return dfa;
}

/// Reverses any FST
template <class T>
	requires FST<T>
auto reverseFST(const T &fst) {
	using Symbol   = typename get_input_t<T>::Symbol;
	using M		   = get_output_t<T>;
	using FSA_t	   = ExpandedFST<Symbol, M>;
	using OutState = typename FSA_t::State;

	FSA_t rev;
	rev.N = fst.Size();

	for (const auto &q : fst.Initial())
		rev.qFinals.insert(OutState(q));
	for (const auto &q : fst.Final())
		rev.qFirsts.insert(OutState(q));

	for (const auto &[from, label, to] : fst.Transitions()) {
		rev.AddTransition(OutState(to), rev.reintern(fst.GetMonoid(), label), OutState(from));
	}

	if constexpr (requires { fst.f_eps; }) {	 /// TODO: this is a hack
		// f(eps) doesn't depend on transition direction, just re-interned into rev's own pool
		for (const auto &v : fst.f_eps) {
			rev.addFEps(get<1>(rev.monoid).own(get<1>(fst.GetMonoid()), v));
		}
	}

	if constexpr (compactable_monoid<typename FSA_t::Monoid>) {
		get<1>(rev.monoid).compact(transitionValues(rev.transitions), rev.f_eps);
	}

	return rev;
}

// Pseudo-minimization
template <class T>
	requires FST<T>
auto pseudoMinimizeFST_slow(const T &fst) {
	return pseudoDeterminizeFST(reverseFST(pseudoDeterminizeFST(reverseFST(fst))));
}

namespace detail {

template <typename TOut, typename T1>
using out_or_t = std::conditional_t<std::is_void_v<TOut>, T1, TOut>;

}	  // namespace detail
/// Crochemore-style pseudo-minimization by partition refinement. The input must be pseudo-deterministic
/// (at most one successor per state and (symbol, output) label), as pseudoDeterminizeFST produces.
/// Equivalent states -- same finality and same labelled successor blocks -- are merged.
template <class TOut = void, FST_with_arcs T>
	requires FSA_builder<detail::out_or_t<TOut, T>>
auto crochemorePseudoMinimizeFST(const T &fst) {
	using Monoid = typename T::Monoid;
	using Value	 = typename Monoid::Value;

	using Result					= detail::out_or_t<TOut, T>;
	constexpr std::size_t none		= std::numeric_limits<std::size_t>::max();
	const auto			 &fstMonoid = fst.GetMonoid();

	std::vector<std::size_t>	   local(fst.Size(), none);
	std::vector<typename T::State> states;
	std::queue<typename T::State>  bfs;
	for (const auto &q : fst.Initial()) {
		if (local[q] != none) continue;
		local[q] = states.size();
		states.push_back(q);
		bfs.push(q);
	}
	while (!bfs.empty()) {
		auto q = bfs.front();
		bfs.pop();
		for (const auto &[label, to] : fst.Transitions(q)) {
			if (local[to] != none) continue;
			local[to] = states.size();
			states.push_back(to);
			bfs.push(to);
		}
	}

	std::unordered_map<Value, std::size_t, monoid_hash<Monoid>, monoid_equal<Monoid>> labelIds(
		0, monoid_hash<Monoid>(&fstMonoid), monoid_equal<Monoid>(&fstMonoid));
	std::vector<Value>											  labels;
	std::vector<std::vector<std::pair<std::size_t, std::size_t>>> edges(states.size());		// (label, local target)
	for (std::size_t i = 0; i < states.size(); ++i) {
		for (const auto &[label, to] : fst.Transitions(states[i])) {
			auto [it, fresh] = labelIds.try_emplace(label, labels.size());
			if (fresh) labels.push_back(label);
			edges[i].push_back({it->second, local[to]});
		}
		if constexpr (!T::sorted_arcs) std::sort(edges[i].begin(), edges[i].end());
		if constexpr (dbg::enabled)
			for (std::size_t k = 1; k < edges[i].size(); ++k) {
				if (edges[i][k].first == edges[i][k - 1].first && edges[i][k].second != edges[i][k - 1].second)
					throw std::invalid_argument("crochemorePseudoMinimizeFST: automaton is not pseudo-deterministic");
			}
		edges[i].erase(std::unique(edges[i].begin(), edges[i].end()), edges[i].end());
	}

	std::vector<std::size_t> block(states.size());
	if constexpr (SSFST<T>) {
		// Two states with identical transition structure but different final output (Psi)
		// still produce a different language, so they must never merge -- Psi joins IsFinal
		// as part of the initial seed that partition refinement is forbidden from crossing.
		using PsiOutputMonoid = get_output_t<T>;
		using PsiValue		  = typename PsiOutputMonoid::Value;
		const auto &outMonoid = get<1>(fstMonoid);
		std::unordered_map<PsiValue, std::size_t, monoid_hash<PsiOutputMonoid>, monoid_equal<PsiOutputMonoid>> psiIds(
			0, monoid_hash<PsiOutputMonoid>(&outMonoid), monoid_equal<PsiOutputMonoid>(&outMonoid));
		for (std::size_t i = 0; i < states.size(); ++i) {
			if (!fst.IsFinal(states[i])) {
				block[i] = 0;
				continue;
			}
			auto [it, _] = psiIds.try_emplace(fst.Psi(states[i]), psiIds.size());
			block[i]	 = 1 + it->second;
		}
	} else {
		for (std::size_t i = 0; i < states.size(); ++i)
			block[i] = fst.IsFinal(states[i]) ? 1 : 0;
	}

	std::size_t blockCount = 0;
	while (true) {
		using Signature = std::pair<std::size_t, std::vector<std::pair<std::size_t, std::size_t>>>;
		std::map<Signature, std::size_t> signatureIds;
		std::vector<std::size_t>		 next(states.size());
		for (std::size_t i = 0; i < states.size(); ++i) {
			std::vector<std::pair<std::size_t, std::size_t>> successors;
			successors.reserve(edges[i].size());
			for (const auto &[label, to] : edges[i])
				successors.push_back({label, block[to]});
			auto [it, _] = signatureIds.try_emplace({block[i], std::move(successors)}, signatureIds.size());
			next[i]		 = it->second;
		}
		block				 = std::move(next);
		std::size_t newCount = signatureIds.size();
		if (newCount == blockCount) break;
		blockCount = newCount;
	}

	Result					 result;
	std::vector<std::size_t> representative(blockCount, none);
	for (std::size_t i = 0; i < states.size(); ++i) {
		if (representative[block[i]] == none) representative[block[i]] = i;
	}
	for (std::size_t b = 0; b < blockCount; ++b)
		result.NewState();

	for (std::size_t b = 0; b < blockCount; ++b) {
		std::size_t i = representative[b];
		if (fst.IsFinal(states[i])) {
			result.AddFinal(b);
			if constexpr (SSFST<T> && SSFST_builder<Result>)
				result.SetPsi(b, get<1>(result.GetMonoid()).own(get<1>(fstMonoid), fst.Psi(states[i])));
		}
		for (const auto &[label, to] : edges[i]) {
			result.AddTransition(b, result.GetMonoid().own(fstMonoid, labels[label]), block[to]);
		}
	}
	for (const auto &q : fst.Initial())
		result.AddInitial(block[local[q]]);

	if constexpr (requires { fst.f_eps; }) {	 // TODO: this is a hack
		for (const auto &v : fst.f_eps)
			result.addFEps(get<1>(result.monoid).own(get<1>(fstMonoid), v));
	}

	if constexpr (SSFSTI<T> && SSFSTI_builder<Result>)
		result.SetInitialOutput(get<1>(result.GetMonoid()).own(get<1>(fstMonoid), fst.InitialOutput()));

	if constexpr (compactable_monoid<typename Result::Monoid>) {
		if constexpr (requires { result.f_eps; })
			get<1>(result.GetMonoid()).compact(result.TransitionValues(), result.f_eps);
		else if constexpr (SSFSTI_builder<Result>)
			get<1>(result.GetMonoid())
				.compact(result.TransitionValues(), result.PsiValues(), result.InitialOutputSpan());
		else if constexpr (SSFST_builder<Result>)
			get<1>(result.GetMonoid()).compact(result.TransitionValues(), result.PsiValues());
		else get<1>(result.GetMonoid()).compact(result.TransitionValues());
	}

	return result;
}

template <FST_with_arcs T>
	requires FSA_builder<T>
auto pseudoMinimizeFST(const T &fst) {
	return crochemorePseudoMinimizeFST<T>(fst);
}

template <FSA T>
auto realtimeFSTAuto(T &&fst) {
	return realtimeFST<ExpandedFST<typename get_input_t<T>::Symbol, get_output_t<T>>>(std::forward<T>(fst));
}

}	  // namespace fl
