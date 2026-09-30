module;

#include <type_traits>
#include <variant>

export module formlang:canonical;

import :transducer_concepts;

export namespace fl {
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
			if (haveCandidate && m.equal(gcp, m.identity)) return false;
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
}	  // namespace fl
