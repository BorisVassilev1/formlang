#pragma once

#include <algorithm>
#include <ranges>
#include <stack>

#include "transducer_concepts.hpp"

namespace fl {

namespace detail {

template <typename TOut, typename T1>
using out_or_t = std::conditional_t<std::is_void_v<TOut>, T1, TOut>;

}	  // namespace detail

/// gets rid of (epsilon, epsilon) transitions preserving the language of the FST.
template <FST_builder T>
auto removeEpsilonFST(T &&fsa) {
	using State	 = T::State;
	using Monoid = T::Monoid;
	using State	 = T::State;

	std::stack<State>				stack;
	std::vector<bool>				visited(fsa.Size(), false);
	std::vector<std::vector<State>> closure(fsa.Size());

	for (State i = 0; i < fsa.Size(); ++i) {
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

		visited.assign(fsa.Size(), false);
	}

	std::remove_cvref_t<T> new_transitions_host;
	for (const auto &[from, label, to] : fsa.Transitions()) {
		if (fsa.GetMonoid().equal(label, fsa.GetMonoid().identity)) continue;	  // skip epsilon transitions
		new_transitions_host.addTransition(from, label, to);
		for (const auto &next : closure[to]) {
			new_transitions_host.addTransition(from, label, next);
		}
	}
	fsa.RawTransitions() = std::move(new_transitions_host).RawTransitions();

	for (const State &i : fsa.qFirsts) {
		for (const State &j : closure[i]) {
			fsa.AddInitial(j);
		}
	}

	return std::move(fsa);
}

/// Trims the FSA
// template <symbol Symbol, monoid M>
template <class TOut = void, FSA_with_arcs T>
	requires FSA_builder<detail::out_or_t<TOut, T>>
auto trimFSA(T &&fsa) {
	using TState = T::State;
	using Result = detail::out_or_t<TOut, T>;
	using RState = typename Result::State;

	if (fsa.Final().size() == 0) {
		Result new_fsa;
		return std::move(new_fsa);
	}

	std::vector<bool> visited_back(fsa.Size(), false);
	std::vector<bool> visited_forw(fsa.Size(), false);

	{
		// auto							&forwardTransitions = fsa.transitions;
		std::vector<std::vector<TState>> backwardTransitions;
		backwardTransitions.resize(fsa.Size());
		for (const auto &[from, label, to] : fsa.Transitions()) {
			backwardTransitions[to].push_back(from);
		}

		std::vector<TState> stack;
		if (fsa.Final().size() != fsa.Size()) {
			for (const auto &final : fsa.Final()) {
				visited_back[final] = true;
				stack.push_back(final);
			}
			while (!stack.empty()) {
				TState current = stack.back();
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

		for (const auto &first : fsa.Initial()) {
			visited_forw[first] = true;		// mark initial states as visited
			stack.push_back(first);
		}
		while (!stack.empty()) {
			TState current = stack.back();
			stack.pop_back();
			for (const auto &[label, to] : fsa.Transitions(current)) {
				if (!visited_forw[to]) {
					visited_forw[to] = true;
					stack.push_back(to);
				}
			}
		}
	}

	Result				new_fsa;
	std::size_t			cnt = 0;
	std::vector<RState> new_map(fsa.Size(), -1);
	for (unsigned int i = 0; i < fsa.Size(); ++i) {
		if (visited_back[i] && visited_forw[i]) { new_map[i] = new_fsa.NewState(); }
	}

	if (cnt == fsa.Size()) {
		fsa.CompactLabels();
		return std::move(fsa);
	}

	for (const auto &q : fsa.Initial()) {
		if (new_map[q] != -1u) { new_fsa.AddInitial(new_map[q]); }
	}
	for (const auto &q : fsa.Final()) {
		if (new_map[q] != -1u) { new_fsa.AddFinal(new_map[q]); }
	}

	new_fsa.GetMonoid() = std::move(fsa.GetMonoid());	  /// should preserve values
	if constexpr (requires { fsa.f_eps; }) { new_fsa.f_eps = std::move(fsa.f_eps); }

	for (const auto &[from, label, to] : fsa.Transitions()) {
		if (new_map[from] != -1u && new_map[to] != -1u) { new_fsa.AddTransition(new_map[from], label, new_map[to]); }
	}

	if constexpr (SSFST<T>) {
		for (const auto &q : fsa.Final())
			if (new_map[q] != -1u) new_fsa.SetPsi(new_map[q], fsa.Psi(q));
		if constexpr (SSFSTI<T>) new_fsa.SetInitialOutput(fsa.InitialOutput());
	}

	/// the monoid can compact itself better than we can compact it by
	/// just reinserting the transitions
	new_fsa.CompactLabels();

	return std::move(new_fsa);
}
}	  // namespace fl
