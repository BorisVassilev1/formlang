#pragma once

#include <cstddef>
#include <queue>
#include <type_traits>
#include <utility>
#include <vector>

#include "transducer_concepts.hpp"

namespace fl {
/// tests if the subsequential transducer is canonical
template <FST_with_arcs Transducer>
bool isCanonical(const Transducer &t) {
	using State = typename Transducer::State;

	const auto &monoid = t.GetMonoid();

	const auto &m = get<1>(monoid);
	// get_output_t<Transducer> i;

	for (State s = 0; s < t.Size(); ++s) {
		bool haveCandidate = t.IsFinal(s);

		auto gcp = m.widen(m.identity);
		if (haveCandidate) { gcp = t.Psi(s); }

		for (const auto &[letter, to] : t.Transitions(s)) {
			if (haveCandidate && m.equal(gcp, m.identity)) break;	  // already maximally reduced
			const auto &[a, out] = letter;
			if (!haveCandidate) {
				gcp			  = out;
				haveCandidate = true;
				continue;
			}
			gcp = m.gcp(gcp, out);
		}

		if (haveCandidate && !m.equal(gcp, m.identity)) return false;
	}
	return true;
}

namespace detail {
template <typename TOut, typename T1>
using out_or_t = std::conditional_t<std::is_void_v<TOut>, T1, TOut>;
}	  // namespace detail

/// Brings a subsequential transducer into canonical ("onward") form: for every state q, the
/// greatest common prefix (gcp) of q's outgoing transition outputs -- together with q's own
/// final output Psi(q), if q is final -- is pulled out of q and pushed back towards its
/// predecessors (ultimately into the overall initial output), so that this gcp becomes empty
/// at every state. That is exactly the invariant isCanonical() checks.
///
/// Mirrors the two-phase algorithm from Mihov & Maletti's subsequential-transducer
/// minimization (same one implemented, in C(m), by mso1_SSFSTI/canonical_SSFSTI in ssfsti.cm):
/// a push value sigma(q) is computed for every state q, equal to the gcp of
/// {lambda(q,a).sigma(delta(q,a)) : a in Sigma} \/ {Psi(q) if q is final}, and every label
/// leaving q gets sigma(q) stripped off its front (the stripped prefix lands on the successor
/// side of the edge, i.e. recursively inside sigma of q's predecessors, or in the initial
/// output for q = the start state).
///
/// sigma is a least fixed point over a graph that can have cycles, so it is computed in two
/// phases instead of a single top-down pass:
///  1. an arbitrary witness completion -- one concrete output word obtained by following some
///     path from q to a final state -- is found for every co-accessible state, via a BFS over
///     the reversed transition relation starting at a virtual sink (reached from every final
///     state q via a (Psi(q), sink) edge). BFS visits each state exactly once, so it terminates
///     regardless of cycles.
///  2. sigma is then obtained by a Dijkstra-style relaxation: repeatedly take the
///     not-yet-finalized state p with the shortest current gcp estimate, freeze
///     sigma(p) := estimate(p), and refine every predecessor p' of p by intersecting (gcp-ing)
///     p''s current estimate with edge-label(p',p).sigma(p). Prepending a label to a value
///     never shortens it, so -- exactly as in an ordinary shortest-distance computation -- the
///     globally smallest remaining estimate can never be reduced further by a not-yet-finalized
///     state, and is safe to freeze. The estimate is seeded (phase 1's output) by directly
///     gcp-ing together -- using the fixed witnesses -- all of a state's own outgoing edges at
///     once, which already resolves any mismatch between a state's own immediate alternatives
///     before the relaxation phase has to propagate anything.
template <class TOut = void, class T>
	requires SSFST<T> && FST_with_arcs<T> && FSA_builder<detail::out_or_t<TOut, T>> &&
			 SSFST_builder<detail::out_or_t<TOut, T>>
auto canonicalizeFST(const T &fst) {
	using State		   = typename T::State;
	using OutputMonoid = get_output_t<T>;
	using OutValue	   = typename OutputMonoid::Value;

	using Result = detail::out_or_t<TOut, T>;
	using Value	 = typename Result::Monoid::Value;

	Result result;

	const std::size_t n = fst.Size();
	if (n == 0) return result;

	const auto &outMonoid = get<1>(fst.GetMonoid());

	// forward/reverse adjacency of the "output relation": every real transition, plus a
	// virtual (Psi(q), sink) edge out of every final state q into a virtual sink state `n`.
	const State											  sink = State(n);
	std::vector<std::vector<std::pair<OutValue, State>>> forwardEdges(n);
	std::vector<std::vector<std::pair<OutValue, State>>> reverseEdges(n + 1);

	for (State q = 0; q < n; ++q) {
		for (const auto &[label, to] : fst.Transitions(q)) {
			const auto &outw = std::get<1>(label);
			forwardEdges[q].push_back({outw, to});
			reverseEdges[to].push_back({outw, q});
		}
		if (fst.IsFinal(q)) {
			OutValue psi = fst.Psi(q);
			forwardEdges[q].push_back({psi, sink});
			reverseEdges[n].push_back({psi, q});
		}
	}

	// Phase 1: an arbitrary witness completion for every co-accessible state, via BFS over the
	// reversed relation starting at the sink.
	std::vector<bool>	   hasWitness(n + 1, false);
	std::vector<OutValue> witness(n + 1, outMonoid.identity);
	hasWitness[n] = true;
	std::queue<State> bfs;
	bfs.push(sink);
	while (!bfs.empty()) {
		State q = bfs.front();
		bfs.pop();
		for (const auto &[w, p] : reverseEdges[q]) {
			if (hasWitness[p]) continue;
			hasWitness[p] = true;
			witness[p]	  = outMonoid.mul(w, witness[q]);
			bfs.push(p);
		}
	}

	// Phase 2a: seed every state's estimate with the gcp of all its own outgoing edges at once
	// (using the fixed witnesses from phase 1).
	std::vector<bool>	   hasEstimate(n, false);
	std::vector<OutValue> estimate(n, outMonoid.identity);
	for (State q = 0; q < n; ++q) {
		if (!hasWitness[q]) continue;	  // dead state: not co-accessible, nothing to push
		for (const auto &[w, to] : forwardEdges[q]) {
			OutValue candidate = outMonoid.mul(w, witness[to]);
			estimate[q]		   = hasEstimate[q] ? outMonoid.gcp(estimate[q], candidate) : candidate;
			hasEstimate[q]	   = true;
		}
	}

	// Phase 2b: Dijkstra-style relaxation over the gcp semiring.
	std::vector<bool>	   finalized(n, false);
	std::vector<OutValue> sigma(n, outMonoid.identity);

	using QItem = std::pair<std::size_t, State>;
	std::priority_queue<QItem, std::vector<QItem>, std::greater<>> pq;
	for (State q = 0; q < n; ++q)
		if (hasEstimate[q]) pq.push({outMonoid.size(estimate[q]), q});

	while (!pq.empty()) {
		auto [len, q] = pq.top();
		pq.pop();
		if (finalized[q] || len != outMonoid.size(estimate[q])) continue;	  // stale entry
		finalized[q] = true;
		sigma[q]	 = estimate[q];

		for (const auto &[w, p] : reverseEdges[q]) {
			if (p >= n || finalized[p]) continue;	  // reverseEdges[q] for q<n never contains the sink, but be safe
			OutValue candidate = outMonoid.mul(w, sigma[q]);
			OutValue refined	= hasEstimate[p] ? outMonoid.gcp(estimate[p], candidate) : candidate;
			if (!hasEstimate[p] || !outMonoid.equal(refined, estimate[p])) {
				estimate[p]	  = refined;
				hasEstimate[p] = true;
				pq.push({outMonoid.size(estimate[p]), p});
			}
		}
	}

	// Re-build the transducer with sigma(q) stripped from the front of every label leaving q
	// (and, symmetrically, sigma(0) prepended to the overall initial output).
	for (State q = 0; q < n; ++q)
		result.NewState();
	result.AddInitial(State(0));

	auto &resultOutMonoid = get<1>(result.GetMonoid());

	for (State q = 0; q < n; ++q) {
		for (const auto &[label, to] : fst.Transitions(q)) {
			const auto &sym   = std::get<0>(label);
			const auto &outw  = std::get<1>(label);
			OutValue	newOut = outMonoid.invMul(sigma[q], outMonoid.mul(outw, sigma[to]));
			result.AddTransition(q, Value{sym, resultOutMonoid.own(outMonoid, newOut)}, to);
		}
		if (fst.IsFinal(q)) {
			result.AddFinal(q);
			OutValue newPsi = outMonoid.invMul(sigma[q], fst.Psi(q));
			result.SetPsi(q, resultOutMonoid.own(outMonoid, newPsi));
		}
	}

	if constexpr (SSFSTI_builder<Result>) {
		if constexpr (SSFSTI<T>) {
			OutValue newInitOut = outMonoid.mul(fst.InitialOutput(), sigma[0]);
			result.SetInitialOutput(resultOutMonoid.own(outMonoid, newInitOut));
		} else {
			result.SetInitialOutput(resultOutMonoid.own(outMonoid, sigma[0]));
		}
	}

	return result;
}

}	  // namespace fl
