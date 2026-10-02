#pragma once

#include <typeinfo>
#include <vector>
#include <string>
#include <stdexcept>

#include "regex_parser.hpp"
#include "fst.hpp"
#include "interning_monoid.hpp"
#include "concepts.hpp"

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
		this->addTransition(*this->qFirsts.begin(), get<0>(this->monoid).from(word1), get<1>(this->monoid).from(word2),
							1);
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
		this->addTransition(*this->qFirsts.begin(), get<0>(this->monoid).from(word1), get<1>(this->monoid).from(word2),
							1);
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
}	  // namespace fl
