module;

#include <ctime>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <cassert>
#include <ranges>
#include <stack>

export module formlang:expanded_fst;

import :concepts;
import :cartesian_monoid;
import :symbol_monoid;
import :transducer_concepts;
import :hashing;
import :datastructures;

export namespace fl {

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

	// the set  of (epslon, x) \in the relation
	std::unordered_set<OutValue, monoid_hash<OutputMonoid>, monoid_equal<OutputMonoid>> f_eps{};

	constexpr ExpandedFST()
		requires(std::is_default_constructible_v<Monoid>)
		: N(0),
		  monoid(),
		  f_eps(0, monoid_hash<OutputMonoid>(&get<1>(monoid)), monoid_equal<OutputMonoid>(&get<1>(monoid))) {}

	constexpr ExpandedFST(const ExpandedFST &)			  = default;
	constexpr ExpandedFST(ExpandedFST &&)				  = default;
	constexpr ExpandedFST &operator=(const ExpandedFST &) = default;
	constexpr ExpandedFST &operator=(ExpandedFST &&)	  = default;

	explicit constexpr ExpandedFST(Monoid &&m) : N(0), monoid(std::move(m)) {}
	explicit constexpr ExpandedFST(const Monoid &m) : N(0), monoid(m) {}

	State newState() { return N++; }

	void addTransition(State from, InSymbol a, OutValue b, State to) { transitions.insert({from, {Value(a, b), to}}); }
	void addTransition(State from, const Value &label, State to) { transitions.insert({from, {label, to}}); }

	template <class T>
		requires free_monoid<M>
	void addTransition(State from, InSymbol a, T &&b, State to) {
		addTransition(from, a, get<1>(monoid).from(b), to);
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
		f_eps.emplace(std::move(v));
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

	const auto &GetMonoid() const { return monoid; }

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
};

/// view of only the monoid elements
template <class Map>
auto transitionValues(const Map &transitions) {
	return transitions | std::views::values |
		   std::views::transform([](const auto &labelAndTo) { return std::get<0>(labelAndTo); });
}

/// view of only the monoid elements
template <symbol Symbol, monoid M>
auto fEpsAsValues(const ExpandedFST<Symbol, M> &fsa) {
	using Value = typename ExpandedFST<Symbol, M>::Value;
	return fsa.f_eps | std::views::transform([](const auto &v) { return Value(Symbol::eps, v); });
}

/// expands the transducer. in the case where output is longer than input,
/// it will leave a whole excess word on the output tape to save space.
template <FST T>
auto expandFST(T &&fst) {
	using I		 = get_input_t<T>;
	using M		 = get_output_t<T>;
	using Symbol = typename I::Symbol;

	ExpandedFST<Symbol, M> expanded;
	using State	   = typename ExpandedFST<Symbol, M>::State;
	using OutValue = typename ExpandedFST<Symbol, M>::OutValue;

	expanded.N		 = fst.Size();
	expanded.qFirsts = std::move(fst.Initial());
	expanded.qFinals = std::move(fst.Final());

	const auto &inMonoid  = get<0>(expanded.GetMonoid());
	const auto &outMonoid = get<1>(expanded.GetMonoid());

	for (const auto &[from, label, to] : fst.Transitions()) {
		auto [w1, w2] = fst.GetMonoid().gen(label);
		if (fst.template isIdentityOnTape<0>(label)) {
			OutValue new_val = outMonoid.own(get<1>(fst.monoid), std::get<1>(label));
			expanded.addTransition(from, Symbol::eps, new_val, to);
			continue;
		}
		State prev = from;

		if (w1.size() < w2.size()) {	 // |w1| < |w2|
			assert(w2.size() > 0);
			auto i1 = w1.begin();
			auto i2 = w2.begin();
			for (uint32_t i = 0; i < w1.size() - 1; ++i) {
				const auto &a	  = *i1++;
				const auto &b	  = *i2++;
				State		next  = (i1 == w1.end()) ? to : expanded.newState();
				OutValue	w2val = outMonoid.from(std::span{&b, 1});
				expanded.addTransition(prev, a, w2val, next);
				prev = next;
			}
			OutValue w2val = outMonoid.from(std::ranges::subrange(i2, w2.end()));
			expanded.addTransition(prev, *i1, w2val, to);
		} else {
			auto i1 = w1.begin();
			auto i2 = w2.begin();
			while (i2 != w2.end()) {
				const auto &a	  = *i1++;
				const auto &b	  = *i2++;
				State		next  = (i2 == w2.end() && i1 == w1.end()) ? to : expanded.newState();
				OutValue	w2val = outMonoid.from(std::span{&b, 1});
				expanded.addTransition(prev, a, w2val, next);
				prev = next;
			}
			while (i1 != w1.end()) {
				const auto &a	 = *i1++;
				State		next = (i1 == w1.end()) ? to : expanded.newState();
				expanded.addTransition(prev, a, outMonoid.identity, next);
				prev = next;
			}
		}
	}

	return expanded;
}

// https://lml.bas.bg/~stoyan/finite-state-techniques.pdf#theorem.4.4.8
template <symbol Symbol, monoid M>
auto removeUpperEpsilonFST(ExpandedFST<Symbol, M> &&fsa) {
	using FSA_t	   = ExpandedFST<Symbol, M>;
	using State	   = typename FSA_t::State;
	using OutValue = typename FSA_t::OutValue;

	auto &outMonoid = get<1>(fsa.monoid);

	std::stack<int>													 stack;
	std::vector<bool>												 visited(fsa.N, false);
	std::vector<std::vector<std::tuple<State, std::vector<Symbol>>>> closure(fsa.N);	 // TODO: this is slow

	for (State i = 0; i < fsa.N; ++i) {
		stack.push(0);
		visited[i] = true;
		closure[i].push_back({i, {}});	   // add the state itself with an empty word
		while (!stack.empty()) {
			auto p = stack.top();
			// copy, not reference: closure[i] gets push_back'ed to below (possibly
			// reallocating), which would leave a `const auto&` binding here dangling
			// once a state has more than one outgoing epsilon edge.
			auto [current, u] = closure[i][p];
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
				fsa.addFEps(outMonoid.from(w));		// f(eps)
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
					OutValue new_val = outMonoid.from(new_word);
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

/// Trims the FSA
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

template <FSA T>
auto realtimeFST(T &&fst) {
	return trimFSA(removeUpperEpsilonFST(expandFST(removeEpsilonFST(trimFSA(std::move(fst))))));
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
			OutState new_id = dfa.newState();
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
			dfa.addTransition(current, sigma, next_state);
			if (is_new) { queue.push(next_state); }
		}
	}

	// the subset construction only ever visits states reachable from
	// fst.Initial() (the BFS over `queue`) -- pool entries that were only
	// referenced by transitions out of unreached states are now dead.
	if constexpr (compactable_monoid<Monoid>) {
		dfa.monoid.compact(transitionValues(dfa.transitions), fEpsAsValues(dfa));
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
		rev.addTransition(OutState(to), rev.reintern(fst.GetMonoid(), label), OutState(from));
	}

	if constexpr (requires { fst.f_eps; }) {	 /// TODO: this is a hack
		// f(eps) doesn't depend on transition direction, just re-interned into rev's own pool
		for (const auto &v : fst.f_eps) {
			rev.addFEps(get<1>(rev.monoid).own(get<1>(fst.GetMonoid()), v));
		}
	}

	if constexpr (compactable_monoid<typename FSA_t::Monoid>) {
		rev.monoid.compact(transitionValues(rev.transitions), fEpsAsValues(rev));
	}

	return rev;
}

// Pseudo-minimization
template <class T>
	requires FST<T>
auto pseudoMinimizeFST(const T &fst) {
	return pseudoDeterminizeFST(reverseFST(pseudoDeterminizeFST(reverseFST(fst))));
}

}	  // namespace fl
