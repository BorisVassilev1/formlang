#pragma once
#include "cfg.h"
#include "utils.h"

namespace fl {

/**
 * @brief An Earley Parser for Context-Free Grammars
 *
 * @tparam Symbol
 */
template <symbol Symbol>
class EarleyParser {
	CFG<Symbol> grammar;

   public:
	// struct DottedRule {
	//	Symbol				 lhs;
	//	std::vector<Symbol> *rhs;
	//	int					 dotPos;
	//	int					 j;
	// };

	bool expect_eof	  = false;
	bool enable_print = false;

	using DottedRule = std::tuple<Symbol, std::vector<Symbol> *, int, int>;
	EarleyParser(const CFG<Symbol> &g) : grammar(g) {}

	auto C(const std::vector<fl::unordered_set<DottedRule>> &R, fl::unordered_set<DottedRule> &R_ip, int i) {
		fl::unordered_set<DottedRule> &C_s	 = R_ip;
		bool						   added = true;

		// auto &terminals	   = grammar.terminals;
		auto &nonTerminals = grammar.nonTerminals;
		auto &rules		   = grammar.rules;

		static fl::unordered_set<DottedRule> C_s1;
		while (added) {
			added = false;
			C_s1.clear();
			// std::cout << C_s << std::endl;
			for (auto &[A, w, dotPos, j] : C_s) {
				auto &β1Xβ2 = *w;
				if (dotPos < (int)β1Xβ2.size() && nonTerminals.contains(β1Xβ2[dotPos])) {
					//  { (X → •β, i) | ∃ (A → β1•Xβ2, j) ∈ C(s) and X → β ∈ P }
					auto &X = β1Xβ2[dotPos];
					auto  r = rules.equal_range(X);
					for (auto &[_X, β] : RangeFromPair(r)) {
						C_s1.insert(DottedRule{X, &β.rhs, 0, i});
					}

					// { (A → β1X•β2, j) | ∃ (A → β1X•β2, j) ∈ C(s) and (X → β•, i) ∈ C(s) }
					for (auto &[X_, w, dotPos_, i_] : C_s) {
						auto &β = *w;
						if (X_ == X && dotPos_ == (int)β.size() && i_ == i) {
							//
							C_s1.insert({A, &β1Xβ2, dotPos + 1, j});
						}
					}
				}
			}

			std::size_t prevSize = C_s.size();
			C_s.insert(C_s1.begin(), C_s1.end());
			if (C_s.size() > prevSize) { added = true; }
			C_s1.clear();

			// {(A → β1X•β2, j) | има β и k < i : ((A → β1•Xβ2, j) ∈ Rk и (X → β•, k) ∈ C(s))}
			for (auto &[X, w, dotPos, k] : C_s) {
				auto &β = *w;
				if (dotPos == (int)β.size()) {
					// search in R_k
					for (auto &[A, w, dotPos, j] : R[k]) {
						auto &β1Xβ2 = *w;
						if (dotPos < (int)β1Xβ2.size() && X == β1Xβ2[dotPos]) {
							C_s1.insert({A, &β1Xβ2, dotPos + 1, j});
						}
					}
				}
			}

			prevSize = C_s.size();
			C_s.insert(C_s1.begin(), C_s1.end());
			if (C_s.size() > prevSize) { added = true; }
		};
		return C_s;
	}

	bool recognize(const std::vector<Symbol> &word) {
		std::vector<fl::unordered_set<DottedRule>> R(word.size() + 1);
		std::vector<fl::unordered_set<DottedRule>> Rp(word.size() + 1);

		auto r = grammar.rules.equal_range(grammar.start);
		for (auto &[S, β] : RangeFromPair(r)) {
			Rp[0].insert(DottedRule{S, &β.rhs, 0, 0});
		}
		if (enable_print) {
			std::cout << "R'[0] = ";
			print(Rp[0]);
			std::cout << std::endl;
		}

		R[0] = C(R, Rp[0], 0);

		if (enable_print) {
			std::cout << "R[0] = ";
			print(R[0]);
			std::cout << std::endl;
		}

		int size = word.size() - expect_eof;

		for (int i = 0; i < size; ++i) {
			if (enable_print) { std::cout << "i = " << i << std::endl; }

			// R′[i+1] = {(A → β1a • β2, j) | (A → β1 • aβ2, j) ∈ Ri и a = ai+1}
			for (auto &[A, w, dotPos, j] : R[i]) {
				auto &β1aβ2 = *w;
				if (dotPos < (int)β1aβ2.size() && β1aβ2[dotPos] == word[i]) {
					Rp[i + 1].insert({A, &β1aβ2, dotPos + 1, j});
				}
			}
			if (Rp[i + 1].size() == 0) {
				std::cout << "failed: " << i << " " << word[i] << std::endl;
				print(R[i]);
				std::cout << std::endl;
			}

			// Ri+1 = Cε( R, R[i+1], i + 1)
			R[i + 1] = C(R, Rp[i + 1], i + 1);

			if (enable_print) {
				std::cout << "R'[" << i + 1 << "] = ";
				print(Rp[i + 1]);
				std::cout << std::endl;
				std::cout << "R[" << i + 1 << "] = ";
				print(R[i + 1]);
				std::cout << std::endl;
			}
		}

		for (auto &[A, w, dotPos, j] : R[size]) {
			auto &β = *w;
			if (dotPos == (int)β.size() && A == grammar.start) return true;
		}
		return false;
	}

	auto &print(const fl::unordered_set<DottedRule> &R, std::ostream &out = std::cout) {
		out << "(";
		int i = 0;
		for (auto &r : R) {
			if (i > 0) out << ", ";
			print(r, out);
			++i;
		}
		out << ")";
		return out;
	}

	auto &print(const DottedRule &r, std::ostream &out = std::cout) {
		auto &[A, w, dotPos, j] = r;
		out << "(" << A << " -> ";
		auto &β = *w;
		for (int i = 0; i < (int)β.size(); ++i) {
			if (i == dotPos) out << "•";
			out << β[i];
		}
		if (dotPos == (int)β.size()) out << "•";
		std::cout << ", " << j << ")";
		return out;
	}
};
}	  // namespace fl
