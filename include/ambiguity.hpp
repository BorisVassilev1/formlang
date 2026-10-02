#pragma once

#include <stack>
#include <vector>

#include "transducer_concepts.hpp"

namespace fl {
// non-exported implementation details of testInfiniteAmbiguity (Tarjan SCC)
extern int				  foundat, sccIndex;
extern std::vector<int>  scc;
extern std::vector<int>  disc, low;	 // init disc to -1
extern std::vector<bool> onstack;		 // init to 0

template <FST_with_arcs T>
void tarjan(int u, const T &fst) {
	static std::stack<int> st;

	disc[u] = low[u] = foundat++;
	st.push(u);
	onstack[u] = true;
	// auto [it, end] = fst.transitions.equal_range(u);
	auto r = fst.Transitions(u);
	for (const auto &[label, i] : r) {
		if (!fst.template isIdentityOnTape<0>(label)) continue;
		if (disc[i] == -1) {
			tarjan(i, fst);
			low[u] = std::min(low[u], low[i]);
		} else if (onstack[i]) low[u] = std::min(low[u], disc[i]);
	}
	if (disc[u] == low[u]) {
		while (1) {
			int v = st.top();
			st.pop();
			onstack[v] = false;
			scc[v]	   = sccIndex;
			if (u == v) break;
		}
		++sccIndex;
	}
}
}	  // namespace fl

namespace fl {
template <FST_with_arcs T>
bool testInfiniteAmbiguity(const T &fst) {
	// tarjan algorithm to find strongly connected components
	// we search in the subgraph with transitions only <\varepsilon, w>

	if (fst.transitions.empty()) return false;

	disc.assign(fst.N, -1);
	low.assign(fst.N, -1);
	onstack.assign(fst.N, false);
	scc.assign(fst.N, -1);

	tarjan(0, fst);

	for (const auto &[k, label, i] : fst.Transitions()) {
		// transition is (\varepsilon, w) with w non-empty
		if (!fst.template isIdentityOnTape<0>(label) || fst.template isIdentityOnTape<1>(label)) continue;
		if (i == k) return true;
		if (i != k && scc[i] == scc[k] && scc[i] != -1) {
			return true;	 // found a cycle in the epsilon transitions
		}
	}

	return false;
}
}	  // namespace fl
