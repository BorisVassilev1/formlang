#pragma once

#include <stack>
#include "transducer_concepts.hpp"
#include "trim.hpp"

namespace fl {

namespace detail {

template <typename TOut, typename T1>
using out_or_t = std::conditional_t<std::is_void_v<TOut>, T1, TOut>;

}	  // namespace detail

/// expands the transducer. in the case where output is longer than input,
/// it will leave a whole excess word on the output tape to save space.
template <class TOut = void, FST T>
	requires(FSA_builder<detail::out_or_t<TOut, T>> &&
			 std::same_as<typename get_input_t<detail::out_or_t<TOut, T>>::Symbol,
						  typename get_input_t<detail::out_or_t<TOut, T>>::Value>)
auto expandFST(T &&fst) {
	using I		 = get_input_t<T>;
	using Symbol = I::Symbol;
	using Result = detail::out_or_t<TOut, T>;

	Result expanded;
	using State		  = Result::State;
	using OutValue	  = Result::OutValue;
	using ResultValue = Result::Monoid::Value;

	expanded.N		 = fst.Size();
	expanded.qFirsts = std::move(fst.Initial());
	expanded.qFinals = std::move(fst.Final());

	// const auto &inMonoid  = get<0>(expanded.GetMonoid());
	const auto &outMonoid = get<1>(expanded.GetMonoid());

	for (const auto &[from, label, to] : fst.Transitions()) {
		auto [w1, w2] = fst.GetMonoid().gen(label);
		if (fst.template isIdentityOnTape<0>(label)) {
			OutValue new_val = outMonoid.own(get<1>(fst.monoid), std::get<1>(label));
			expanded.AddTransition(from, ResultValue{Symbol::eps, new_val}, to);
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
				State		next  = (i1 == w1.end()) ? to : expanded.NewState();
				OutValue	w2val = outMonoid.from(std::span{&b, 1});
				expanded.AddTransition(prev, ResultValue{a, w2val}, next);
				prev = next;
			}
			OutValue w2val = outMonoid.from(std::ranges::subrange(i2, w2.end()));
			expanded.AddTransition(prev, ResultValue{*i1, w2val}, to);
		} else {
			auto i1 = w1.begin();
			auto i2 = w2.begin();
			while (i2 != w2.end()) {
				const auto &a	  = *i1++;
				const auto &b	  = *i2++;
				State		next  = (i2 == w2.end() && i1 == w1.end()) ? to : expanded.NewState();
				OutValue	w2val = outMonoid.from(std::span{&b, 1});
				expanded.AddTransition(prev, ResultValue{a, w2val}, next);
				prev = next;
			}
			while (i1 != w1.end()) {
				const auto &a	 = *i1++;
				State		next = (i1 == w1.end()) ? to : expanded.NewState();
				expanded.AddTransition(prev, ResultValue{a, outMonoid.identity}, next);
				prev = next;
			}
		}
	}

	return expanded;
}

// https://lml.bas.bg/~stoyan/finite-state-techniques.pdf#theorem.4.4.8
// template <symbol Symbol, monoid M>
template <FST_builder T>
	requires(FST_with_arcs<T> && std::same_as<typename get_input_t<T>::Symbol, typename get_input_t<T>::Value>)
auto removeUpperEpsilonFST(T &&fsa) {
	using Value		= T::Monoid::Value;
	using InSymbol	= get_input_t<T>::Symbol;
	using OutSymbol = get_output_t<T>::Symbol;
	using State		= T::State;

	auto &outMonoid = get<1>(fsa.monoid);

	std::stack<int>														stack;
	std::vector<bool>													visited(fsa.N, false);
	std::vector<std::vector<std::tuple<State, std::vector<OutSymbol>>>> closure(fsa.N);		// TODO: this is slow

	for (State i = 0; i < fsa.N; ++i) {
		stack.push(0);
		visited[i] = true;
		closure[i].push_back({i, {}});	   // add the state itself with an empty word
		while (!stack.empty()) {
			auto p			  = stack.top();
			auto [current, u] = closure[i][p];
			stack.pop();

			for (const auto &[label, to] : fsa.Transitions(current)) {
				const auto &[w1, w2] = label;
				if (w1 == InSymbol::eps && !visited[to]) {	   // epsilon transition
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

	std::remove_cvref_t<T> new_transitions_host;
	for (const auto &[from, label, to] : fsa.Transitions()) {
		const auto &[w1, w2] = label;
		if (w1 == InSymbol::eps) continue;	   // skip epsilon transitions
		new_transitions_host.AddTransition(from, label, to);
	}

	fsa.RawTransitions().clear();
	for (State q1 = 0; q1 < fsa.N; ++q1) {
		for (const auto &[q_, u] : closure[q1]) {
			for (const auto &[label, q__] : new_transitions_host.Transitions(q_)) {
				const auto &[sigma, w2] = label;
				auto v					= outMonoid.gen(w2);
				for (const auto &[q2, w] : closure[q__]) {
					auto new_word = u;
					new_word.insert(new_word.end(), v.begin(), v.end());
					new_word.insert(new_word.end(), w.begin(), w.end());
					auto new_val = outMonoid.from(new_word);
					/// new_transitions_host is not a valid FST because its labels are not in its monoid.
					fsa.AddTransition(q1, Value{sigma, new_val}, q2);
				}
			}
		}
	}
	for (const auto &t : new_transitions_host.Transitions()) {
		const auto &[from, label, to] = t;
		fsa.AddTransition(from, label, to);
	}

	// the epsilon-labeled transitions erased above may have been the only
	// reference to some pool entries (e.g. the output word carried along a
	// now-removed epsilon hop) -- reclaim them.
	if constexpr (compactable_monoid<typename T::Monoid>) {
		get<1>(fsa.monoid).compact(transitionValues(fsa.transitions), fsa.f_eps);
	}

	return std::move(fsa);
}

template <class TOut = void, FSA T>
	requires(FSA_builder<detail::out_or_t<TOut, T>> &&
			 std::same_as<typename get_input_t<detail::out_or_t<TOut, T>>::Symbol,
						  typename get_input_t<detail::out_or_t<TOut, T>>::Value>)
auto realtimeFST(T &&fst) {
	return trimFSA(removeUpperEpsilonFST(expandFST<TOut>(removeEpsilonFST(trimFSA(std::move(fst))))));
}

}	  // namespace fl
