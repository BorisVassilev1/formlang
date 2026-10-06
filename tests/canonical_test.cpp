#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "../doctest.h"

#include <string>
#include <vector>

#include "canonical.hpp"
#include "letter.hpp"
#include "total_ssft.hpp"

using namespace fl;

namespace {

auto toSym(const char *s) { return fl::toSymbol<Letter>(s); }

std::string word(const auto &m, const auto &v) {
	auto g = get<1>(m).gen(v);
	return std::string(g.begin(), g.end());
}

// A 4-letter total SSFT keeps the test alphabet small while still exercising
// TotalSSFST (the only concrete SSFST type in this codebase that satisfies
// FST_with_arcs, since it alone provides per-state Transitions(q)).
using SSFST_t = TotalSSFST<Letter, 4>;

/// runs the transducer on `input`, returning the full output (initial output,
/// every transition's output, and the final output), the same way f() does.
std::string run(const SSFST_t &t, const std::vector<Letter> &input) {
	SSFST_t::State s = 0;
	const auto	  &m = t.GetMonoid();
	std::string	   out = word(m, t.InitialOutput());
	for (auto l : input) {
		auto [outw, next] = *t.Transition(s, l);
		out += word(m, outw);
		s = next;
	}
	out += word(m, t.Psi(s));
	return out;
}

/// Builds a transducer with a self-loop cycle at the initial state, and a Psi/transition
/// mix engineered so that pushing forces sigma(0) = "m" (shared by every alternative) while
/// sigma(1) = "z" and sigma(2) = "q" come purely from each state's own final output (their
/// own self loops all emit "" and so don't constrain anything further):
///
///  state0 (Psi="m"):
///    letter0 --"mx"--> state1
///    letter1 --"mq"--> state2
///    letter2 --"m"-->  state0  (self loop)
///    letter3 --"m"-->  state0  (self loop)
///  state1 (Psi="z"): all 4 letters self-loop on "" (never leaves)
///  state2 (Psi="q"): all 4 letters self-loop on "" (never leaves)
SSFST_t buildSelfLoopExample() {
	SSFST_t t;
	auto	s0 = t.NewState();
	auto	s1 = t.NewState();
	auto	s2 = t.NewState();

	using Value = SSFST_t::Monoid::Value;
	auto   &m	= t.GetMonoid();
	auto	w	= [&](const char *s) { return get<1>(m).from(toSym(s)); };

	t.AddTransition(s0, Value{Letter('\0'), w("mx")}, s1);
	t.AddTransition(s0, Value{Letter('\1'), w("mq")}, s2);
	t.AddTransition(s0, Value{Letter('\2'), w("m")}, s0);
	t.AddTransition(s0, Value{Letter('\3'), w("m")}, s0);
	for (char c = 0; c < 4; ++c) t.AddTransition(s1, Value{Letter(c), w("")}, s1);
	for (char c = 0; c < 4; ++c) t.AddTransition(s2, Value{Letter(c), w("")}, s2);

	t.SetPsi(s0, w("m"));
	t.SetPsi(s1, w("z"));
	t.SetPsi(s2, w("q"));
	return t;
}

/// Builds a transducer whose push values are mutually recursive across a genuine multi-hop
/// (0 -> 1 -> 0) cycle, not just self-loops, so sigma(0) and sigma(1) each depend on the
/// other. Also includes a self-loop (letter1 at state1, weight "zz") whose own weight
/// conflicts with the value otherwise suggested by Psi(1), which forces sigma(1) all the
/// way down to the identity -- a case easy to get wrong by hand (see the comment in the test
/// below), which is exactly why this case is checked mechanically here.
SSFST_t buildTwoCycleExample() {
	SSFST_t t;
	auto	s0 = t.NewState();
	auto	s1 = t.NewState();

	using Value = SSFST_t::Monoid::Value;
	auto   &m	= t.GetMonoid();
	auto	w	= [&](const char *s) { return get<1>(m).from(toSym(s)); };

	t.AddTransition(s0, Value{Letter('\0'), w("ab")}, s1);
	t.AddTransition(s0, Value{Letter('\1'), w("ac")}, s1);
	t.AddTransition(s0, Value{Letter('\2'), w("a")}, s0);
	t.AddTransition(s0, Value{Letter('\3'), w("a")}, s0);
	t.AddTransition(s1, Value{Letter('\0'), w("ab")}, s0);
	t.AddTransition(s1, Value{Letter('\1'), w("zz")}, s1);
	t.AddTransition(s1, Value{Letter('\2'), w("")}, s1);
	t.AddTransition(s1, Value{Letter('\3'), w("")}, s1);

	t.SetPsi(s0, w("a"));
	t.SetPsi(s1, w("ab"));
	return t;
}

const std::vector<std::vector<Letter>> &selfLoopInputs() {
	static const std::vector<std::vector<Letter>> inputs = {
		{},
		{Letter('\0')},
		{Letter('\1')},
		{Letter('\2')},
		{Letter('\2'), Letter('\2'), Letter('\0')},
		{Letter('\0'), Letter('\0')},	  // state1 self loop
		{Letter('\1'), Letter('\3')},	  // state2 self loop
	};
	return inputs;
}

const std::vector<std::vector<Letter>> &twoCycleInputs() {
	static const std::vector<std::vector<Letter>> inputs = {
		{},
		{Letter('\0')},
		{Letter('\0'), Letter('\0')},
		{Letter('\0'), Letter('\0'), Letter('\0')},
		{Letter('\1')},
		{Letter('\0'), Letter('\1')},
		{Letter('\2'), Letter('\0'), Letter('\1'), Letter('\1')},
	};
	return inputs;
}

}	  // namespace

// ---------------------------------------------------------------------------
// isCanonical() -- regression test for a bug found while validating
// canonicalizeFST(): the per-state loop used to `return false` as soon as the
// running gcp hit the identity, even when more transitions remained to be
// examined, which made ANY final state with empty final output and at least
// one outgoing transition register as non-canonical regardless of whether it
// genuinely was. The fix changed that early exit from `return false` to
// `break` (nothing more to learn once the running gcp is already identity,
// but that is not itself a violation).
// ---------------------------------------------------------------------------

TEST_SUITE("isCanonical") {

	TEST_CASE("a state whose Psi and every transition output are already empty is canonical") {
		SSFST_t t;
		auto	s0 = t.NewState();
		auto   &m	= t.GetMonoid();
		for (char c = 0; c < 4; ++c)
			t.AddTransition(s0, SSFST_t::Monoid::Value{Letter(c), get<1>(m).identity}, s0);
		t.SetPsi(s0, get<1>(m).identity);

		CHECK(isCanonical(t));
	}

	TEST_CASE("a state whose transitions and Psi share a non-empty common prefix is not canonical") {
		// Psi("xy") and transition0's "xz" only share "x": the gcp of the whole set is "x",
		// not empty, so the state still has something left to push.
		SSFST_t t;
		auto	s0 = t.NewState();
		auto   &m	= t.GetMonoid();
		t.AddTransition(s0, SSFST_t::Monoid::Value{Letter('\0'), get<1>(m).from(toSym("xz"))}, s0);
		for (char c = 1; c < 4; ++c)
			t.AddTransition(s0, SSFST_t::Monoid::Value{Letter(c), get<1>(m).from(toSym("xy"))}, s0);
		t.SetPsi(s0, get<1>(m).from(toSym("xy")));

		CHECK_FALSE(isCanonical(t));
	}
}

// ---------------------------------------------------------------------------
// canonicalizeFST()
// ---------------------------------------------------------------------------

TEST_SUITE("canonicalizeFST") {

	TEST_CASE("pushes a shared prefix through a self-loop cycle at the initial state") {
		SSFST_t orig = buildSelfLoopExample();
		CHECK_FALSE(isCanonical(orig));

		SSFST_t canon = canonicalizeFST(orig);
		CHECK(isCanonical(canon));

		// sigma(0) = gcp("m", "mx"+sigma(1), "mq"+sigma(2), "m"+sigma(0), "m"+sigma(0)) = "m",
		// so "m" should land in the initial output and be stripped from every label at state 0.
		const auto &m = canon.GetMonoid();
		CHECK(word(m, canon.InitialOutput()) == "m");
		CHECK(word(m, canon.Psi(0)) == "");

		for (const auto &input : selfLoopInputs()) CHECK(run(canon, input) == run(orig, input));
	}

	TEST_CASE("is idempotent on an already-canonical transducer") {
		SSFST_t canon  = canonicalizeFST(buildSelfLoopExample());
		SSFST_t canon2 = canonicalizeFST(canon);

		CHECK(isCanonical(canon2));
		for (const auto &input : selfLoopInputs()) CHECK(run(canon2, input) == run(canon, input));
	}

	TEST_CASE("resolves mutually-recursive push values across a multi-hop cycle") {
		SSFST_t orig = buildTwoCycleExample();
		CHECK_FALSE(isCanonical(orig));

		SSFST_t canon = canonicalizeFST(orig);
		CHECK(isCanonical(canon));

		for (const auto &input : twoCycleInputs()) CHECK(run(canon, input) == run(orig, input));
	}
}
