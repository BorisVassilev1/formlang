#pragma once

#include <climits>
#include <ranges>

#include "cfg.h"
#include "datastructures.hpp"

namespace ll1g {

using fl::CFG;
using fl::Production;
using fl::unordered_map;
using fl::unordered_set;

template <class _Symbol>
class LL1Grammar : public CFG<_Symbol> {
   public:
	LL1Grammar(const _Symbol &start) : CFG<_Symbol>(start, _Symbol::eof) {}
};

template <class _Symbol, class G>
concept grammar = std::is_base_of_v<LL1Grammar<_Symbol>, std::remove_cvref_t<G>>;

template <class _Symbol, class G>
concept single_symbol = std::is_same_v<_Symbol, std::remove_cvref_t<G>>;

template <class _Symbol, class G>
concept single_word = std::is_same_v<std::vector<_Symbol>, std::remove_cvref_t<G>> ||
					  std::is_same_v<Production<_Symbol>, std::remove_cvref_t<G>>;

template <class _Symbol, class G>
concept convertible_to_subgrammar = grammar<_Symbol, G> || single_symbol<_Symbol, G> || single_word<_Symbol, G>;

template <class _Symbol>
class Epsilon : public LL1Grammar<_Symbol> {
   public:
	Epsilon(const _Symbol &start) : LL1Grammar<_Symbol>(start) {
		this->addRule(start, Production<_Symbol>({}, {}, false));
	}
};

template <class _Symbol>
class Word : public LL1Grammar<_Symbol> {
   public:
	Word(const _Symbol &start, const std::vector<_Symbol> &word, const std::vector<bool> &ignore, bool spill = false)
		: LL1Grammar<_Symbol>(start) {
		this->terminals.insert(word.begin(), word.end());
		this->addRule(start, Production<_Symbol>(word, ignore));
		if (spill) this->getNonTerminalData(start).upwardSpillThreshold = INT_MAX;
	}
	Word(const _Symbol &start, const std::vector<_Symbol> &word, bool spill = false)
		: Word(start, word, std::vector<bool>(word.size(), false), spill) {}
};

template <class _Symbol>
class Optional : public LL1Grammar<_Symbol> {
   public:
	template <typename G>
		requires convertible_to_subgrammar<_Symbol, G>
	Optional(const _Symbol &start, G &&g) : LL1Grammar<_Symbol>(start) {
		if constexpr (single_symbol<_Symbol, G>) {
			this->terminals.insert(g);
			this->addRule(start, {_Symbol(g)});
		} else if constexpr (single_word<_Symbol, G>) {
			this->terminals.insert(g.begin(), g.end());
			this->addRule(start, g);
		} else {
			this->terminals.insert(g.terminals.begin(), g.terminals.end());
			this->nonTerminals.insert(g.nonTerminals.begin(), g.nonTerminals.end());
			this->nonTerminalData.insert(g.nonTerminalData.begin(), g.nonTerminalData.end());
			this->rules.insert(g.rules.begin(), g.rules.end());
			this->addRule(start, {g.start});
		}

		this->addRule(start, {});
		this->getNonTerminalData(start).upwardSpillThreshold = -1;
	}
};

template <class _Symbol>
class Seq : public LL1Grammar<_Symbol> {
   public:
	template <typename... Grammars>
		requires((grammar<_Symbol, Grammars> || single_symbol<_Symbol, Grammars>) && ...)
	Seq(const _Symbol &start, std::vector<bool> ignore, Grammars &&...gs) : LL1Grammar<_Symbol>(start) {
		assert(sizeof...(gs) == ignore.size() && "Ignore vector size must match number of grammars");
		auto params = std::vector<_Symbol>{[&]() {
			if constexpr (single_symbol<_Symbol, Grammars>) {
				this->terminals.insert(gs);
				return gs;
			} else {
				this->terminals.insert(gs.terminals.begin(), gs.terminals.end());
				this->nonTerminals.insert(gs.nonTerminals.begin(), gs.nonTerminals.end());
				this->nonTerminalData.insert(gs.nonTerminalData.begin(), gs.nonTerminalData.end());
				this->rules.insert(gs.rules.begin(), gs.rules.end());
				return gs.start;
			}
		}()...};
		this->addRule(start, {params, ignore});
		this->getNonTerminalData(start).upwardSpillThreshold = -1;
	}

	template <typename... Grammars>
		requires((grammar<_Symbol, Grammars> || single_symbol<_Symbol, Grammars>) && ...)
	Seq(const _Symbol &start, Grammars &&...gs)
		: Seq(start, std::vector<bool>(sizeof...(gs), false), std::forward<Grammars>(gs)...) {}
};

template <class _Symbol>
class Choice : public LL1Grammar<_Symbol> {
   public:
	template <typename... Grammars>
		requires(convertible_to_subgrammar<_Symbol, Grammars> && ...)
	Choice(const _Symbol &start, Grammars &&...gs) : LL1Grammar<_Symbol>(start) {
		static_assert(sizeof...(gs) > 0, "Choice must have at least one grammar");
		(
			[&]() {
				if constexpr (single_symbol<_Symbol, Grammars>) {
					this->terminals.insert(gs);
					this->addRule(start, {_Symbol(gs)});
				} else if constexpr (single_word<_Symbol, Grammars>) {
					this->terminals.insert(gs.begin(), gs.end());
					this->addRule(start, gs);
				} else {
					this->terminals.insert(gs.terminals.begin(), gs.terminals.end());
					this->nonTerminals.insert(gs.nonTerminals.begin(), gs.nonTerminals.end());
					this->nonTerminalData.insert(gs.nonTerminalData.begin(), gs.nonTerminalData.end());
					this->rules.insert(gs.rules.begin(), gs.rules.end());
					this->addRule(start, {gs.start});
				}
			}(),
			...);
	}
};

template <class _Symbol>
class Repeat : public LL1Grammar<_Symbol> {
   public:
	template <typename G>
		requires convertible_to_subgrammar<_Symbol, G>
	Repeat(const _Symbol &start, G &&g, int spillThreshold = -1) : LL1Grammar<_Symbol>(start) {
		if constexpr (std::is_same_v<_Symbol, std::remove_cvref_t<G>>) {
			this->terminals.insert(g);
			this->addRule(start, {_Symbol(g), start});
		} else if constexpr (single_word<_Symbol, G>) {
			this->terminals.insert(g.begin(), g.end());
			auto word = g;
			word.push_back(start);
			this->addRule(start, word);
		} else {
			this->terminals.insert(g.terminals.begin(), g.terminals.end());
			this->nonTerminals.insert(g.nonTerminals.begin(), g.nonTerminals.end());
			this->nonTerminalData.insert(g.nonTerminalData.begin(), g.nonTerminalData.end());
			this->rules.insert(g.rules.begin(), g.rules.end());
			this->addRule(start, {g.start, start});
		}
		this->addRule(start, {});
		this->getNonTerminalData(start).upwardSpillThreshold = spillThreshold;
	}
};

template <class _Symbol>
class RepeatChoice : public LL1Grammar<_Symbol> {
   public:
	template <typename... Grammars>
		requires(convertible_to_subgrammar<_Symbol, Grammars> && ...)
	RepeatChoice(const _Symbol &start, int spillThreshold, Grammars &&...gs) : LL1Grammar<_Symbol>(start) {
		static_assert(sizeof...(gs) > 0, "RepeatChoice must have at least one grammar");
		(
			[&]() {
				if constexpr (single_symbol<_Symbol, Grammars>) {
					this->terminals.insert(gs);
					this->addRule(start, {_Symbol(gs), start});
				} else if constexpr (single_word<_Symbol, Grammars>) {
					this->terminals.insert(gs.begin(), gs.end());
					auto word = gs;
					word.push_back(start);
					this->addRule(start, word);
				} else {
					this->terminals.insert(gs.terminals.begin(), gs.terminals.end());
					this->nonTerminals.insert(gs.nonTerminals.begin(), gs.nonTerminals.end());
					this->nonTerminalData.insert(gs.nonTerminalData.begin(), gs.nonTerminalData.end());
					this->rules.insert(gs.rules.begin(), gs.rules.end());
					this->addRule(start, {gs.start, start});
				}
			}(),
			...);
		this->addRule(start, {});
		this->getNonTerminalData(start).upwardSpillThreshold = spillThreshold;
	}

	template <typename... Grammars>
		requires((grammar<_Symbol, Grammars> || single_symbol<_Symbol, Grammars> || single_word<_Symbol, Grammars>) &&
				 ...)
	RepeatChoice(const _Symbol &start, Grammars &&...gs) : RepeatChoice(start, -1, std::forward<Grammars>(gs)...) {}
};

template <class _Symbol>
class Combine : public LL1Grammar<_Symbol> {
   public:
	template <class StartGrammar, class... Grammars>
		requires(grammar<_Symbol, Grammars> && ...)
	Combine(StartGrammar &&startGrammar, Grammars &&...gs) : LL1Grammar<_Symbol>(startGrammar.start) {
		this->terminals.insert(startGrammar.terminals.begin(), startGrammar.terminals.end());
		this->nonTerminals.insert(startGrammar.nonTerminals.begin(), startGrammar.nonTerminals.end());
		this->nonTerminalData.insert(startGrammar.nonTerminalData.begin(), startGrammar.nonTerminalData.end());
		this->rules.insert(startGrammar.rules.begin(), startGrammar.rules.end());

		(this->terminals.insert(gs.terminals.begin(), gs.terminals.end()), ...);
		(this->nonTerminals.insert(gs.nonTerminals.begin(), gs.nonTerminals.end()), ...);
		(this->nonTerminalData.insert(gs.nonTerminalData.begin(), gs.nonTerminalData.end()), ...);
		(this->rules.insert(gs.rules.begin(), gs.rules.end()), ...);

		this->terminals = this->terminals |
						  std::views::filter([this](const _Symbol &l) { return !this->nonTerminals.contains(l); }) |
						  std::ranges::to<unordered_set<_Symbol>>();
	}
};

template <class _Symbol, class... Grammars>
Combine(LL1Grammar<_Symbol> &&, Grammars &&...) -> Combine<_Symbol>;
template <class _Symbol, class... Grammars>
Combine(const LL1Grammar<_Symbol> &, Grammars &&...) -> Combine<_Symbol>;
};	   // namespace ll1g
