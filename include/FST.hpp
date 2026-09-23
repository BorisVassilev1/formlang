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

#include "regexParser.hpp"
#include "pipes.hpp"
#include "regexParser.hpp"
#include "CartesianMonoid.hpp"
#include "InterningMonoid.hpp"

namespace fl {

// Classical Finite State Transducer (FST) class template
//
// A transition carries a single value of a cartesian monoid combining the two
// tapes (input/output words over Symbol) instead of an ad hoc pair of indices
// into a hand-rolled word pool: Monoid::Value plays the role that the old
// {StringID, StringID} tuple used to play, and InterningMonoid<Symbol> is the
// (interning) pool for each tape.
template <free_monoid I, monoid M>
class SparseFST {
   public:
	constexpr static bool deterministic = false;
	constexpr static bool sorted_arcs	= false;

	using Symbol = I::Symbol;
	using InputMonoid  = I;
	using OutputMonoid = M;
	using State	 = unsigned int;
	using Monoid = std::conditional_t<std::is_same_v<M, I>, DiagonalMonoid<I>, CartesianMonoid<I, M>>;

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

	// void addTransition(State from, std::vector<Symbol> &&w1, std::vector<Symbol> &&w2, State to) {
	//	auto v1 = monoid.template getMonoid<0>().create(std::span<const Symbol>(w1.data(), w1.size()));
	//	auto v2 = monoid.template getMonoid<1>().create(std::span<const Symbol>(w2.data(), w2.size()));
	//	transitions.insert({from, {Value(v1, v2), to}});
	// }

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
		return Value(monoid.template getMonoid<0>().create(w1), monoid.template getMonoid<1>().create(w2));
	}

	/// whether the label is the identity of the I-th tape's monoid (i.e. that tape reads/writes epsilon)
	template <std::size_t i>
	bool isIdentityOnTape(const Value &v) const {
		return monoid.template getMonoid<i>().equal(std::get<i>(v), std::get<i>(Monoid::identity));
	}

	/// total number of distinct words interned across both tapes' pools (diagnostic use only)
	std::size_t wordCount() const {
		return monoid.template getMonoid<0>().totalWordCount() + monoid.template getMonoid<1>().totalWordCount();
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

	const auto	&Initial() const { return qFirsts; }
	bool		 IsInitial(State q) const { return qFirsts.contains(q); }
	const auto & Final() const { return qFinals; }
	bool		 IsFinal(State q) const { return qFinals.contains(q); }
	std::size_t	 Size() const { return N; }
	auto Transitions(State q) const {
		auto [begin, end] = transitions.equal_range(q);
		return std::ranges::subrange(begin, end) | std::views::values;
	}
};
}	  // namespace fl

#include "letter.hpp"
static_assert(fl::FSA<fl::SparseFST<fl::InterningMonoid<fl::Letter>, fl::IntegerMonoid<>>>,
			  "SparseFST<IntegerMonoid<>> should satisfy the FSA concept");
static_assert(fl::FST<fl::SparseFST<fl::InterningMonoid<fl::Letter>, fl::IntegerMonoid<>>>,
			  "SparseFST<IntegerMonoid<>> should satisfy the FST concept");
static_assert(fl::FST<fl::SparseFST<fl::InterningMonoid<fl::Letter>, fl::InterningMonoid<fl::Letter>>>,
			  "SparseFST<InterningMonoid<>> should satisfy the FST concept");

namespace fl {

template <symbol Symbol>
using StringFST = SparseFST<InterningMonoid<Symbol>, InterningMonoid<Symbol>>;

// Berry-Sethi constructions

template <class Symbol>
class BS_FSA : public StringFST<Symbol> {
   public:
};

template <class Symbol>
class BS_WordFSA : public BS_FSA<Symbol> {
   public:
	BS_WordFSA(std::vector<Symbol> &&word1, std::vector<Symbol> &&word2) : BS_FSA<Symbol>() {
		this->N		  = 2;
		this->qFirsts = {0};
		this->qFinals = {1};
		this->addTransition(*this->qFirsts.begin(), this->monoid.template getMonoid<0>().create(word1),
							this->monoid.template getMonoid<1>().create(word2), 1);
	}
};

template <class Symbol>
class BS_UnionFSA : public BS_FSA<Symbol> {
   public:
	using State	 = StringFST<Symbol>::State;
	using Monoid = StringFST<Symbol>::Monoid;
	using Value	 = StringFST<Symbol>::Value;

	BS_UnionFSA(BS_FSA<Symbol> &&fst1, BS_FSA<Symbol> &&fst2) : BS_FSA<Symbol>() {
		if (fst1.qFinals.empty() && fst2.qFinals.empty()) {
			this->N		  = 0;
			this->qFirsts = {0};
			return;
		} else if (fst1.qFinals.empty()) {
			(StringFST<Symbol> &)(*this) = std::move(fst2);
			return;
		} else if (fst2.qFinals.empty()) {
			(StringFST<Symbol> &)(*this) = std::move(fst1);
			return;
		}

		bool canOptimizeFinals = true;
		{
			for (auto f : fst1.qFinals) {
				if (fst1.transitions.find(f) != fst1.transitions.end()) {
					canOptimizeFinals = false;
					break;
				}
			}
			for (auto f : fst2.qFinals) {
				if (fst2.transitions.find(f) != fst2.transitions.end()) {
					canOptimizeFinals = false;
					break;
				}
			}
		}

		this->N = fst1.N + fst2.N - 1;
		if (canOptimizeFinals) ++this->N;
		State newFinal = this->N - 1;
		this->qFirsts  = std::move(fst1.qFirsts);

		this->monoid	  = std::move(fst1.monoid);
		this->transitions = std::move(fst1.transitions);
		if (canOptimizeFinals)
			for (auto &[from, value] : this->transitions) {
				auto &[label, to] = value;
				if (fst1.qFinals.contains(to)) { to = newFinal; }
			}
		for (const auto &[from, value] : fst2.transitions) {
			const auto &[label, to] = value;
			State new_from			= from + fst1.N - 1;
			if (from == 0) new_from = 0;
			State new_to = to + fst1.N - 1;
			if (canOptimizeFinals && fst2.qFinals.contains(to)) new_to = newFinal;
			this->transitions.insert({new_from, {this->reintern(fst2.monoid, label), new_to}});
		}

		if (canOptimizeFinals) {
			this->qFinals = {newFinal};
		} else {
			this->qFinals = std::move(fst1.qFinals);
			for (const auto &q : fst2.qFinals) {
				this->qFinals.insert(q + fst1.N - 1);
			}
		}
	}
};

template <class Symbol>
class BS_ConcatFSA : public BS_FSA<Symbol> {
   public:
	using State	 = StringFST<Symbol>::State;
	using Monoid = StringFST<Symbol>::Monoid;
	using Value	 = StringFST<Symbol>::Value;

	BS_ConcatFSA(BS_FSA<Symbol> &&fsa1, BS_FSA<Symbol> &&fsa2) : BS_FSA<Symbol>() {
		//: BS_FSA<Symbol>(false) {
		if (fsa1.qFinals.empty() || fsa2.qFinals.empty()) {
			this->N		  = 0;
			this->qFirsts = {0};
			return;
		}
		this->N		  = fsa1.N + fsa2.N - 1;
		this->qFirsts = std::move(fsa1.qFirsts);

		// all transitions from fsa1
		this->monoid	  = std::move(fsa1.monoid);
		this->transitions = std::move(fsa1.transitions);
		// add transitions from fsa2, removing the initial state of fsa2
		auto fsa1_final = fsa1.qFinals.begin();
		for (const auto &[from, value] : fsa2.transitions) {
			const auto &[label, to] = value;
			State new_from			= from + fsa1.N - 1;
			if (from == 0) new_from = *fsa1_final;
			this->transitions.insert({new_from, {this->reintern(fsa2.monoid, label), to + fsa1.N - 1}});
		}

		++fsa1_final;
		for (; fsa1_final != fsa1.qFinals.end(); ++fsa1_final) {
			auto [i1, i2] = fsa2.transitions.equal_range(0);
			for (auto it = i1; it != i2; ++it) {
				const auto &[_, value]	= *it;
				const auto &[label, to] = value;
				this->transitions.insert({*fsa1_final, {this->reintern(fsa2.monoid, label), to + fsa1.N - 1}});
			}
		}

		if (fsa2.qFinals.contains(*fsa2.qFirsts.begin())) { this->qFinals = std::move(fsa1.qFinals); }
		this->qFinals.reserve(this->qFinals.size() + fsa2.qFinals.size());
		for (const auto &q : fsa2.qFinals) {
			this->qFinals.insert(q + fsa1.N - 1);
		}
	}
};

template <class Symbol>
class BS_KleeneStarFSA : public BS_FSA<Symbol> {
   public:
	BS_KleeneStarFSA(BS_FSA<Symbol> &&fsa, bool includeEpsilon = true) : BS_FSA<Symbol>() {
		if (fsa.qFinals.empty()) {
			this->N		  = 0;
			this->qFirsts = {0};
			if (includeEpsilon) this->qFinals = {0};
			return;
		}
		this->N		  = fsa.N;
		this->qFirsts = std::move(fsa.qFirsts);

		this->monoid	  = std::move(fsa.monoid);
		this->transitions = std::move(fsa.transitions);

		auto [i1, i2] = this->transitions.equal_range(0);
		typename StringFST<Symbol>::Map toAdd;
		for (auto it = i1; it != i2; ++it) {
			const auto &[_, value]	= *it;
			const auto &[label, to] = value;

			for (const auto &f : fsa.qFinals) {
				toAdd.insert({f, {label, to}});		// add transitions from final states to initial state
			}
		}
		this->transitions.insert(toAdd.begin(), toAdd.end());

		this->qFinals = std::move(fsa.qFinals);
		if (includeEpsilon) this->qFinals.insert(0);	 // add the new initial state as a final state
	}
};

template <class Symbol>
BS_FSA<Symbol> makeFSA_BerriSethi(rgx::Regex &regex) {
	using namespace rgx;
	// static int counter = 0;
	BS_FSA<Symbol> fsa;
	if (auto *r = dynamic_cast<TupleRegex<char> *>(&regex)) {
		fsa = BS_WordFSA<Symbol>(toSymbol<Symbol>(std::move(r->left)), toSymbol<Symbol>(std::move(r->right)));
	} else if (auto *r = dynamic_cast<UnionRegex *>(&regex)) {
		fsa = BS_UnionFSA<Symbol>(makeFSA_BerriSethi<Symbol>(*r->left), makeFSA_BerriSethi<Symbol>(*r->right));
	} else if (auto *r = dynamic_cast<ConcatRegex *>(&regex)) {
		fsa = BS_ConcatFSA<Symbol>(makeFSA_BerriSethi<Symbol>(*r->left), makeFSA_BerriSethi<Symbol>(*r->right));
	} else if (auto *r = dynamic_cast<KleeneStarRegex *>(&regex)) {
		fsa = BS_KleeneStarFSA<Symbol>(makeFSA_BerriSethi<Symbol>(*r->child), true);
	} else if (auto *r = dynamic_cast<KleenePlusRegex *>(&regex)) {
		fsa = BS_KleeneStarFSA<Symbol>(makeFSA_BerriSethi<Symbol>(*r->child), false);
	}
	return fsa;
}

// Thompson's construction

template <class Symbol>
class TH_WordFSA : public StringFST<Symbol> {
   public:
	TH_WordFSA(std::vector<Symbol> &&word1, std::vector<Symbol> &&word2) : StringFST<Symbol>() {
		this->N		  = 2;
		this->qFirsts = {0};
		this->qFinals = {1};
		this->addTransition(*this->qFirsts.begin(), this->monoid.template getMonoid<0>().create(word1),
							this->monoid.template getMonoid<1>().create(word2), 1);
	}
};

template <class Symbol>
class TH_UnionFSA : public StringFST<Symbol> {
   public:
	using Monoid = StringFST<Symbol>::Monoid;

	TH_UnionFSA(StringFST<Symbol> &&fst1, StringFST<Symbol> &&fst2) : StringFST<Symbol>() {
		if (fst1.qFinals.empty() && fst2.qFinals.empty()) {
			this->N		  = 0;
			this->qFirsts = {0};
			return;
		} else if (fst1.qFinals.empty()) {
			(StringFST<Symbol> &)(*this) = std::move(fst2);
			return;
		} else if (fst2.qFinals.empty()) {
			(StringFST<Symbol> &)(*this) = std::move(fst1);
			return;
		}

		this->N		  = fst1.N + fst2.N + 2;
		this->qFirsts = {this->N - 2};
		this->qFinals = {this->N - 1};

		this->monoid	  = std::move(fst1.monoid);
		this->transitions = std::move(fst1.transitions);
		for (const auto &[from, value] : fst2.transitions) {
			const auto &[label, to] = value;
			this->transitions.insert({from + fst1.N, {this->reintern(fst2.monoid, label), to + fst1.N}});
		}

		for (const auto &q : fst1.qFinals) {
			this->transitions.insert({q, {Monoid::identity, this->N - 1}});
		}
		for (const auto &q : fst2.qFinals) {
			this->transitions.insert({q + fst1.N, {Monoid::identity, this->N - 1}});
		}
		this->transitions.insert({*this->qFirsts.begin(), {Monoid::identity, *fst1.qFirsts.begin()}});
		this->transitions.insert({*this->qFirsts.begin(), {Monoid::identity, *fst2.qFirsts.begin() + fst1.N}});
	}
};

template <class Symbol>
class TH_ConcatFSA : public StringFST<Symbol> {
   public:
	using Monoid = StringFST<Symbol>::Monoid;

	TH_ConcatFSA(StringFST<Symbol> &&fst1, StringFST<Symbol> &&fst2) {
		if (fst1.qFinals.empty() || fst2.qFinals.empty()) {
			this->N		  = 0;
			this->qFirsts = {0};
			return;
		}

		this->N		  = fst1.N + fst2.N;
		this->qFirsts = {fst1.qFirsts};
		this->qFinals.reserve(fst2.qFinals.size());
		for (const auto &q : fst2.qFinals) {
			this->qFinals.insert(q + fst1.N);
		}

		this->monoid	  = std::move(fst1.monoid);
		this->transitions = std::move(fst1.transitions);
		for (const auto &[from, value] : fst2.transitions) {
			const auto &[label, to] = value;
			this->transitions.insert({from + fst1.N, {this->reintern(fst2.monoid, label), to + fst1.N}});
		}

		for (const auto &q : fst1.qFinals) {
			this->transitions.insert({q, {Monoid::identity, *fst2.qFirsts.begin() + fst1.N}});
		}
	}
};

template <class Symbol>
class TH_KleeneStarFSA : public StringFST<Symbol> {
   public:
	using Monoid = StringFST<Symbol>::Monoid;

	TH_KleeneStarFSA(StringFST<Symbol> &&fst, bool includeEpsilon = true) {
		if (fst.qFinals.empty()) {
			this->N		  = 0;
			this->qFirsts = {0};
			return;
		}

		this->N		  = fst.N + 2;
		this->qFirsts = {this->N - 2};
		this->qFinals = {this->N - 1};

		this->monoid	  = std::move(fst.monoid);
		this->transitions = std::move(fst.transitions);
		for (const auto &q : fst.qFinals) {
			this->transitions.insert({q, {Monoid::identity, this->N - 1}});
			this->transitions.insert({q, {Monoid::identity, *fst.qFirsts.begin()}});
		}
		this->transitions.insert({*this->qFirsts.begin(), {Monoid::identity, *fst.qFirsts.begin()}});
		if (includeEpsilon) this->transitions.insert({*this->qFirsts.begin(), {Monoid::identity, this->N - 1}});
	}
};

template <class Symbol>
StringFST<Symbol> makeFSA_Thompson(rgx::Regex &regex) {
	using namespace rgx;
	if (auto *r = dynamic_cast<TupleRegex<char> *>(&regex)) {
		return TH_WordFSA<Symbol>(toSymbol<Symbol>(std::move(r->left)), toSymbol<Symbol>(std::move(r->right)));
	} else if (auto *r = dynamic_cast<UnionRegex *>(&regex)) {
		return TH_UnionFSA<Symbol>(makeFSA_Thompson<Symbol>(*r->left), makeFSA_Thompson<Symbol>(*r->right));
	} else if (auto *r = dynamic_cast<ConcatRegex *>(&regex)) {
		return TH_ConcatFSA<Symbol>(makeFSA_Thompson<Symbol>(*r->left), makeFSA_Thompson<Symbol>(*r->right));
	} else if (auto *r = dynamic_cast<KleeneStarRegex *>(&regex)) {
		return TH_KleeneStarFSA<Symbol>(makeFSA_Thompson<Symbol>(*r->child), true);
	} else if (auto *r = dynamic_cast<KleenePlusRegex *>(&regex)) {
		return TH_KleeneStarFSA<Symbol>(makeFSA_Thompson<Symbol>(*r->child), false);
	}
	throw std::runtime_error("Unknown regex type for FSA construction: " + std::string(typeid(regex).name()));
}

template <class Symbol>
class StupidUnionFSA : public StringFST<Symbol> {
   public:
	StupidUnionFSA(StringFST<Symbol> &&fst1, StringFST<Symbol> &&fst2) {
		if (fst1.qFinals.empty() && fst2.qFinals.empty()) {
			this->N		  = 0;
			this->qFirsts = {0};
			return;
		} else if (fst1.qFinals.empty()) {
			(StringFST<Symbol> &)(*this) = std::move(fst2);
			return;
		} else if (fst2.qFinals.empty()) {
			(StringFST<Symbol> &)(*this) = std::move(fst1);
			return;
		}

		this->N		  = fst1.N + fst2.N;
		this->qFirsts = std::move(fst1.qFirsts);
		for (const auto &q : fst2.qFirsts) {
			this->qFirsts.insert(q + fst1.N);
		}
		this->qFinals = std::move(fst1.qFinals);
		for (const auto &q : fst2.qFinals) {
			this->qFinals.insert(q + fst1.N);
		}

		this->monoid	  = std::move(fst1.monoid);
		this->transitions = std::move(fst1.transitions);
		for (const auto &[from, value] : fst2.transitions) {
			const auto &[label, to] = value;
			this->transitions.insert({from + fst1.N, {this->reintern(fst2.monoid, label), to + fst1.N}});
		}
	}
};

template <class Symbol>
void drawFSA(const StringFST<Symbol> &fsa) {
	ShellProcess p("dot -Tsvg > a.svg && feh ./a.svg");
	fsa.print(p.in());
	p.in() << std::endl;
	p.in().close();
	p.wait();
	std::cout << getString(p.out()) << std::endl;
	std::cout << getString(p.err()) << std::endl;
}

template <class Symbol>
inline void saveFSA(const StringFST<Symbol> &fsa, const std::string &filename) {
	std::ofstream out(filename);
	if (!out.is_open()) { throw std::runtime_error("Could not open file " + filename + " for writing."); }
	fsa.print(out);
	out.close();
}

template <class Symbol>
auto trimFSA(StringFST<Symbol> &&fsa) {
	if (fsa.qFinals.empty()) {
		fsa.N		= 0;
		fsa.qFirsts = {0};
		fsa.transitions.clear();
		return std::move(fsa);
	}
	using State = StringFST<Symbol>::State;
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
	StringFST<Symbol> new_fsa;
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
template <class Symbol>
auto removeEpsilonFST(StringFST<Symbol> &&fsa) {
	using State	 = typename StringFST<Symbol>::State;
	using Monoid = typename StringFST<Symbol>::Monoid;

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

	typename StringFST<Symbol>::Map new_transitions;
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
