#pragma once
#include <cassert>
#include <queue>

#include "concepts.hpp"
#include "pipes.hpp"
#include "utils.h"
#include "wordset.hpp"
#include "TotalSSFT.hpp"
#include "hashing.hpp"

namespace fl {

// direct construction of a SSFT from a set of replace rules with single-letter begin and end markers
//
// rules are all in the form _α_β_ -> _αβ_, where '_' is the marker and α, β are words in the alphabet
// the left and right '_' serve as left and right context so intersection of occurences is allowed within the
// contexts.
//
// the transducer is total, so every state has transitions with each letter.
template <fl::symbol Symbol, size_t alphabetSize = Symbol::size>
class ReplaceWithMarkerSSFT : public TotalSSFT<Symbol, alphabetSize> {
   public:
	using State		 = unsigned int;
	using WordID	 = UniqueWordSet<Symbol>::WordID;
	using Map		 = TotalSSFT<Symbol, alphabetSize>::Map;
	using Transition = TotalSSFT<Symbol, alphabetSize>::Transition;

	/// @brief A rule _<left>_<right>_ -> _<left><right>_
	struct Rule {
		std::vector<Symbol> left;
		std::vector<Symbol> right;
	};

	struct RuleMetadata {
		std::vector<Symbol> match;
		size_t				markerIndex;
		WordID				rightHalfID;

		RuleMetadata(Rule &&rule, const Symbol &marker, UniqueWordSet<Symbol> &words)
			: match(std::move(rule.left)), markerIndex(match.size()) {
			match.push_back(marker);
			match.insert(match.end(), rule.right.begin(), rule.right.end());
			match.push_back(marker);
			rightHalfID = words.addWord(std::span{match.begin() + markerIndex, match.end()});
		}
		auto size() const { return match.size() - 1; }

		auto operator<=>(const RuleMetadata &other) const {
			// compare lexicographically, ignoring the last marker because it should not be part of the minimal
			// automaton
			return std::lexicographical_compare(match.begin(), match.end() - 1, other.match.begin(),
												other.match.end() - 1);
		}

		WordID output(UniqueWordSet<Symbol> &words, size_t offset) const {
			// assert(offset < match.size());
			if (offset < markerIndex) return words.addWord(std::span{&match[offset], 1});
			else return 0;	   // epsilon
		}
		WordID color(UniqueWordSet<Symbol> &words) const {
			return words.addWord(std::span{match.begin() + markerIndex + 1, match.end()});
		}

		/// words must be the same as the one used to construct the RuleMetadata
		WordID delay(UniqueWordSet<Symbol> &words, size_t offset) const {
			if (offset < markerIndex) return 0;
			int	   len	  = std::max(0, (int)(offset - markerIndex));
			WordID result = words.addSubWord(rightHalfID, 0, len);
			return result;
		}
	};

   private:
	const Symbol marker;
	bool		 minimize = true;

	using TotalSSFT<Symbol, alphabetSize>::N;
	using TotalSSFT<Symbol, alphabetSize>::transitions;
	using TotalSSFT<Symbol, alphabetSize>::output;
	using TotalSSFT<Symbol, alphabetSize>::words;

	struct TemporaryStateData {
		WordID								 color;
		std::array<Transition, alphabetSize> transitions;
		auto								&operator[](size_t index) { return transitions[index]; }
		auto								 operator[](size_t index) const { return transitions[index]; }
		auto								 begin() { return transitions.begin(); }
		auto								 end() { return transitions.end(); }
	};

	struct StateDataView {
		WordID										color;
		const std::array<Transition, alphabetSize> *transitions;

		constexpr StateDataView(const TemporaryStateData &s) noexcept : color(s.color), transitions(&s.transitions) {}
		constexpr StateDataView(WordID c, const std::array<Transition, alphabetSize> &t) noexcept
			: color(c), transitions(&t) {}
	};

	struct hash {
		constexpr hash()	 = default;
		using is_transparent = void;
		constexpr size_t operator()(const TemporaryStateData &s) const {
			return fl::hash<WordID>()(s.color) ^ fl::hash<std::array<Transition, alphabetSize>>()(s.transitions);
		}
		constexpr size_t operator()(const StateDataView &s) const {
			return fl::hash<WordID>()(s.color) ^ fl::hash<std::array<Transition, alphabetSize>>()(*s.transitions);
		}
	};
	struct equal {
		constexpr equal()	 = default;
		using is_transparent = void;
		constexpr bool operator()(const StateDataView &a, const StateDataView &b) const {
			return a.color == b.color && *a.transitions == *b.transitions;
		}
	};

	struct TemporaryData {
		std::unordered_map<TemporaryStateData, State, hash, equal> minimizedStates;
		std::vector<TemporaryStateData>							   unminimizedStates;	  /// treat as a stack
		RuleMetadata											  *prevRuleMeta = nullptr;

		UniqueWordSet<Symbol> words;	  /// store the output words for each state in minimizedStates
		std::vector<WordID>	  delays;	  /// output for each state in minimizedStates

		void newState(WordID color) {
			unminimizedStates.push_back({color, {}});
			for (auto &t : unminimizedStates.back())
				t = {0, -1u};
		}
		auto popState() {
			assert(!unminimizedStates.empty());
			auto transitions = unminimizedStates.back();
			unminimizedStates.pop_back();
			return transitions;
		}
	};

	unsigned int newState() {
		if (transitions.size() <= N) {
			transitions.push_back({});
			for (auto &t : transitions.back())
				t = {0, -1u};
			output.push_back(0);	 // epsilon
			assert(transitions.size() == output.size() && transitions.size() == N + 1);
		}
		return N++;
	}

	void fillFailTransitions(State initial, State trieStart, TemporaryData &tempData) {
		for (Symbol l = 0; l < Symbol::size; ++l) {
			if (l == marker) continue;
			// self-loop for all letters except marker
			transitions[initial][size_t(l)] = {words.addWord(std::span{&l, 1}), initial};
		}
		std::vector<State> fail(N, -1u);
		fail[initial]	= initial;
		fail[trieStart] = initial;
		std::queue<State> bfsQueue;
		std::vector<bool> visited(N, false);
		bfsQueue.push(initial);
		while (!bfsQueue.empty()) {
			State state = bfsQueue.front();
			bfsQueue.pop();
			if (visited[state]) continue;
			visited[state] = true;
			assert(fail[state] != -1u);

			for (Symbol l = 0; l < Symbol::size; ++l) {
				const auto &[outputID, next] = transitions[state][size_t(l)];
				if (next != -1u) {
					bfsQueue.push(next);
					if (fail[next] == -1u) {
						State f	   = fail[state];
						fail[next] = transitions[f][size_t(l)].next;
					}
				} else {
					State to						   = transitions[fail[state]][size_t(l)].next;
					transitions[state][size_t(l)].next = to;
					std::vector<Symbol> failOutput;
					// assert output[fail[state]].size() is a suffix of output[state].size()

					std::span<const Symbol> delayTo	   = tempData.words[tempData.delays[to]];
					std::span<const Symbol> delayState = tempData.words[tempData.delays[state]];
					int						cutoff	   = delayTo.size();
					failOutput.insert(failOutput.end(), delayState.begin(), delayState.end() - std::max(0, cutoff - 1));
					if (cutoff <= 0) failOutput.push_back(l);
					transitions[state][size_t(l)].outputID = words.addWord(failOutput);
				}
			}
		}
	}

	size_t longestCommonPrefix(std::span<const Symbol> a, std::span<const Symbol> b) const {
		size_t len = std::min(a.size(), b.size());
		for (size_t i = 0; i < len; ++i) {
			if (a[i] != b[i]) return i;
		}
		return len;
	}

	void insertTemporary(const RuleMetadata &ruleMeta, TemporaryData &tempData) {
		assert(tempData.unminimizedStates.size() <= ruleMeta.size() + 1);
		if (tempData.unminimizedStates.size() == ruleMeta.size() + 1) {
			// the last state is already created, just set its color
			assert(tempData.unminimizedStates.back().color == 0);
			tempData.unminimizedStates.back().color = ruleMeta.color(tempData.words);
			return;
		}

		while (tempData.unminimizedStates.size() < ruleMeta.size()) {
			tempData.newState(0);
		}
		tempData.newState(ruleMeta.color(tempData.words));
		assert(tempData.unminimizedStates.size() >= ruleMeta.size() + 1);
	}

	void popTemporaryUntil(int until, TemporaryData &tempData) {
		assert(until >= -1);
		State		prevState	 = -1;
		const auto &prevRuleMeta = *tempData.prevRuleMeta;

		while (tempData.unminimizedStates.size() > (size_t)until + 1) {
			auto		 newData = tempData.popState();
			const size_t offset	 = tempData.unminimizedStates.size();	  // index of the popped state

			if (offset < prevRuleMeta.size()) {		// the deepest state has no spine successor
				auto &[outputID, next] = newData.transitions[size_t(prevRuleMeta.match[offset])];
				next				   = prevState;
				outputID			   = tempData.prevRuleMeta->output(words, offset);
			}

			State state;
			auto  it = tempData.minimizedStates.find(StateDataView{newData.color, newData.transitions});
			if (minimize && it != tempData.minimizedStates.end()) {
				state = it->second;
			} else {
				state = newState();
				tempData.delays.push_back(prevRuleMeta.delay(tempData.words, offset));
				assert(tempData.delays.size() == N);
				this->transitions[state] = newData.transitions;
				// this->output[state]		 = newData.color;
				if (minimize) tempData.minimizedStates.emplace(std::move(newData), state);
			}
			prevState = state;
		}

		if (until >= 0) {
			auto &[outputID, next] = tempData.unminimizedStates.back().transitions[size_t(prevRuleMeta.match[until])];
			next				   = prevState;
			outputID			   = tempData.prevRuleMeta->output(words, until);
		}

		assert(tempData.unminimizedStates.size() == (size_t)until + 1);
	}

   public:
	ReplaceWithMarkerSSFT(std::vector<Rule> &&rules, const Symbol &marker, bool minimize = true)
		: marker(marker), minimize(minimize) {
		// build a trie of the left parts of the rules

		TemporaryData tempData;

		State initial = newState();
		tempData.delays.push_back(0);
		assert(tempData.delays.size() == N);

		std::vector<RuleMetadata> sortedRules;
		// <left><marker><right> for each rule
		for (auto &rule : rules) {
			sortedRules.emplace_back(std::move(rule), marker, tempData.words);
		}
		// sort lexicographically
		std::sort(sortedRules.begin(), sortedRules.end(), [](const RuleMetadata &a, const RuleMetadata &b) {
			return std::lexicographical_compare(a.match.begin(), a.match.end(), b.match.begin(), b.match.end());
		});

		insertTemporary(sortedRules[0], tempData);
		tempData.prevRuleMeta = &sortedRules[0];
		for (size_t i = 1; i < sortedRules.size(); ++i) {
			auto &ruleMeta = sortedRules[i];

			// std::cerr << "Adding rule: ";
			// for (const auto &l : ruleMeta.match)
			//	std::cerr << l;
			// std::cerr << std::endl;

			size_t lcp = longestCommonPrefix(ruleMeta.match, tempData.prevRuleMeta->match);
			// pop states until we reach the lcp
			popTemporaryUntil(lcp, tempData);
			insertTemporary(ruleMeta, tempData);

			tempData.prevRuleMeta = &ruleMeta;

			// draw(tempData);
		}

		popTemporaryUntil(-1, tempData);

		// place psi-function and transitions on matching states
		State trieStart = N - 1;
		for (const auto &ruleMeta : sortedRules) {
			State state = trieStart;
			for (size_t i = 0; i < ruleMeta.size(); ++i) {
				auto &[outputID, next] = transitions[state][size_t(ruleMeta.match[i])];
				assert(next != -1u);
				if (i > 1 && ruleMeta.match[i - 1] == marker) { output[state] = words.addWord(std::span{&marker, 1}); }
				state = next;
			}
			output[state]					   = ruleMeta.color(words);
			transitions[state][size_t(marker)] = {output[state], trieStart};
		}

		transitions[initial][size_t(marker)] = {words.addWord(std::span{&marker, 1}), trieStart};
		// draw(tempData);

		fillFailTransitions(initial, trieStart, tempData);
	}

	void draw(TemporaryData &tempData) {
		ShellProcess p("dot -Tsvg > a.svg && feh ./a.svg");
		printConstruction(p.in(), tempData);
		p.in() << std::endl;
		p.in().close();
		p.wait();
		auto out = getString(p.out()), err = getString(p.err());
		if (!out.empty()) std::cout << out << std::endl;
		if (!err.empty()) std::cout << err << std::endl;
	}

	void printConstruction(std::ostream &out, TemporaryData &tempData) {
		out << "digraph ReplaceWithMarkerSSFT {\n";
		out << "  rankdir=LR;\n";
		out << "  node [shape=circle];\n";
		out << "  init [label=\"N=" << N << "\", shape=square];\n";
		out << "  init -> 0;\n";	 // initial state
		for (State s = 0; s < N; ++s) {
			out << "  " << s << " [shape=" << (output[s] != 0 ? "doublecircle" : "circle") << ", label=\"" << s << ": ";
			if (output[s] != 0) {
				for (const auto &letter : words[output[s]]) {
					out << letter;
				}
			}
			if (tempData.delays[s] != 0) {
				out << "\n(delay=";
				for (const auto &letter : tempData.words[tempData.delays[s]]) {
					out << letter;
				}
				out << ")";
			}
			out << "\"];\n";	 // final States with output

			for (Symbol l = 0; l < Symbol::size; ++l) {
				const auto &[outputID, next] = transitions[s][size_t(l)];
				if (next != -1u) {
					out << "  " << s << " -> " << next << " [label=\"<" << l << ", ";
					for (const auto &letter : words[outputID]) {
						out << letter;
					}
					out << ">\"];\n";
				}
			}
		}
		// add the unminimized states
		for (State s = 0; s < tempData.unminimizedStates.size(); ++s) {
			out << "  " << N + s << " [shape=circle, style=dashed];\n";
			for (Symbol l = 0; l < Symbol::size; ++l) {
				const auto &[outputID, next] = tempData.unminimizedStates[s][size_t(l)];
				if (next != -1u) {
					out << "  " << N + s << " -> " << (next < N ? next : N + next - N) << " [label=\"<" << l << ", ";
					for (const auto &letter : words[outputID]) {
						out << letter;
					}
					out << ">\"];\n";
				}
			}
			// print implicit transition with prevRuleMeta
			if (s < tempData.unminimizedStates.size() - 1) {
				out << "  " << N + s << " -> " << (N + s + 1) << " [label=\"<" << tempData.prevRuleMeta->match[s]
					<< ", ";
				WordID outputID = tempData.prevRuleMeta->output(words, s);
				for (const auto &letter : words[outputID]) {
					out << letter;
				}
				out << ">\", style=dashed];\n";
			}
		}
		out << "}\n";
	}

	ReplaceWithMarkerSSFT(std::istream &in, const Symbol &marker)
		: fl::TotalSSFT<Symbol, alphabetSize>(in), marker(marker) {}

	ReplaceWithMarkerSSFT(const ReplaceWithMarkerSSFT &)			= default;
	ReplaceWithMarkerSSFT(ReplaceWithMarkerSSFT &&)					= default;
	ReplaceWithMarkerSSFT &operator=(const ReplaceWithMarkerSSFT &) = default;
	ReplaceWithMarkerSSFT &operator=(ReplaceWithMarkerSSFT &&)		= default;
};

}	  // namespace fl
