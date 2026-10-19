#pragma once

#include <array>
#include <bit>
#include <cassert>
#include <cstdint>
#include <optional>
#include <ranges>
#include <span>
#include <type_traits>
#include <vector>

#include "cartesian_monoid.hpp"
#include "concepts.hpp"
#include "interning_monoid.hpp"
#include "symbol_monoid.hpp"

namespace fl {

/// A Total Subsequential FST, just like TotalSSFST, but transitions whose
/// output happens to end in the symbol x being consumed -- q --x--> p labeled
/// (x, wx) -- only intern the prefix w; the trailing x is appended back on
/// demand instead of being stored (and interned) again for every such
/// transition. This is the common case for e.g. an expanded failure
/// transducer, where most of the fail-transitions simply pass the failing
/// letter through after some buffered prefix. Transitions whose output does
/// NOT end in x (e.g. because a match replaced it with something else
/// entirely) are stored verbatim, exactly as TotalSSFST would -- so this type
/// can represent anything TotalSSFST can, compression is purely an
/// opportunistic, per-transition storage optimization, detected automatically
/// in AddTransition, never a precondition callers have to satisfy.
///
/// The "is this transition compressed" flag isn't a separate field -- that
/// would cost a whole padded word per transition (bool + the padding needed
/// to keep Trans's alignment), wiping out a good chunk of the saving from
/// not interning the trailing letter. Instead it's packed into the top bit
/// of OutValue's own object representation: pool ids are allocated densely
/// from 0, so for any pool realistically smaller than 2^31 entries that bit
/// is otherwise always zero. This requires OutValue to be a 4-byte,
/// trivially-copyable handle (e.g. InterningMonoid<Symbol>::WordId) -- see
/// the static_assert below.
template <symbol Symbol, std::size_t alphabetSize = Symbol::size, free_monoid M = InterningMonoid<Symbol>>
	requires std::same_as<typename M::Symbol, Symbol>
class CompressedTotalSSFST {
   public:
	constexpr static bool		 deterministic = true;
	constexpr static bool		 sorted_arcs   = true;
	constexpr static std::size_t alphabet_size = alphabetSize;

	using State		   = unsigned int;
	using InputMonoid  = SymbolMonoid<Symbol>;
	using OutputMonoid = M;
	using Monoid	   = CartesianMonoid<SymbolMonoid<Symbol>, M>;

	using OutValue = typename OutputMonoid::Value;

	static_assert(sizeof(OutValue) == sizeof(uint32_t) && std::is_trivially_copyable_v<OutValue>,
				  "CompressedTotalSSFST packs its per-transition compression flag into the top bit of "
				  "OutValue's object representation, which requires OutValue to be a 4-byte, "
				  "trivially-copyable handle (e.g. InterningMonoid<Symbol>::WordId)");

	struct Trans {
		/// the compression flag lives in bit 31 of val's own representation --
		/// see IsCompressedFlag/CleanValue/PackValue. If set: the output is
		/// CleanValue(val) . x (x implicit, the array index). If clear: val
		/// (already clean) IS the full output, stored verbatim.
		OutValue val;
		State	 next;
		bool	 operator==(const Trans &other) const  = default;
		bool	 operator!=(const Trans &other) const  = default;
		bool	 operator<=>(const Trans &other) const = default;
	};
	using Map	   = std::vector<std::array<Trans, alphabetSize>>;
	using Letter_t = Symbol;

   protected:
	static constexpr uint32_t CompressedFlagBit = 0x8000'0000u;

	static uint32_t ToBits(OutValue v) { return std::bit_cast<uint32_t>(v); }
	static OutValue FromBits(uint32_t bits) { return std::bit_cast<OutValue>(bits); }

	static bool	 IsCompressedFlag(OutValue packed) { return (ToBits(packed) & CompressedFlagBit) != 0; }
	static OutValue CleanValue(OutValue packed) { return FromBits(ToBits(packed) & ~CompressedFlagBit); }
	static OutValue PackValue(OutValue clean, bool compressed) {
		uint32_t bits = ToBits(clean);
		assert((bits & CompressedFlagBit) == 0 &&
			   "CompressedTotalSSFST: pool id uses the top bit reserved for the compression flag -- pool grew "
			   "past 2^31 entries");
		if (compressed) bits |= CompressedFlagBit;
		return FromBits(bits);
	}

	unsigned int N = 0;

	Monoid monoid;
	/// transitions[from][letter] = Trans{val, to} -- val carries the
	/// compression flag packed into its own top bit, see above.
	Map transitions;
	/// the Ψ-function, output[state] = outputID (never flag-packed: there is
	/// no "consumed letter" at a final state to compress against).
	std::vector<OutValue> output;
	OutValue			  initialOut = OutputMonoid::identity;

	auto PsiValues() & { return std::span<OutValue>(output); }

	auto InitialOutputSpan() & { return std::span<OutValue>(&initialOut, 1); }

	/// true iff v's last generator is exactly x -- checked without interning
	/// anything new: sub()/gen() on an InterningMonoid-style free_monoid just
	/// read existing pool bytes. v must already be a clean (unpacked) value.
	bool EndsWithSymbol(OutValue v, Symbol x) const {
		const auto &outM = get<1>(monoid);
		std::size_t len	 = outM.size(v);
		if (len == 0) return false;
		auto tail = outM.gen(outM.sub(v, len - 1, 1));
		return *std::ranges::begin(tail) == x;
	}

	/// reconstructs the full output of a transition, interning w.x in the
	/// output monoid the first time this particular (w, x) pair is actually
	/// asked for through the FST interface -- or just returning the verbatim
	/// (clean) value if this transition wasn't compressible.
	OutValue FullOutput(const Trans &t, Symbol x) const {
		OutValue clean = CleanValue(t.val);
		if (!IsCompressedFlag(t.val)) return clean;
		const auto				   &outM = get<1>(monoid);
		const std::array<Symbol, 1> single{x};
		return OutValue(outM.mul(clean, outM.from(std::span<const Symbol>(single))));
	}

   public:
	CompressedTotalSSFST() = default;

	CompressedTotalSSFST(const CompressedTotalSSFST &) = default;
	CompressedTotalSSFST(CompressedTotalSSFST &&)		= default;

	CompressedTotalSSFST &operator=(const CompressedTotalSSFST &) = default;
	CompressedTotalSSFST &operator=(CompressedTotalSSFST &&)		= default;

	[[clang::always_inline]] inline constexpr std::size_t size() const { return N; }
	[[clang::always_inline]] inline constexpr std::size_t cacheCount() const {
		return get<1>(monoid).totalWordCount();	   // this is not guaranteed to work
	}
	[[clang::always_inline]] inline constexpr std::size_t cacheSize() const {
		return get<1>(monoid).poolByteCount();	   // this is not guaranteed to work
	}

	[[clang::always_inline]] inline OutValue			 Psi(State state) const { return output[state]; }
	[[clang::always_inline]] inline std::array<State, 1> Initial() const { return {0}; }
	[[clang::always_inline]] inline bool				 IsInitial(State state) const { return state == 0; }

	[[clang::always_inline]] inline auto InitialOutput() const { return initialOut; }

	[[clang::always_inline]] inline bool		IsFinal(State) const { return true; }
	[[clang::always_inline]] inline auto		Final() const { return std::ranges::iota_view(0u, N); }
	[[clang::always_inline]] inline std::size_t Size() const { return N; }

	[[clang::always_inline]] inline auto Transitions(State state) const {
		return std::views::enumerate(transitions[state]) | std::views::transform([this](const auto &p) {
				   const auto &[letter, transition] = p;
				   Symbol	   x						= Symbol(letter);
				   OutValue	   wx						= FullOutput(transition, x);
				   return std::make_tuple(typename Monoid::Value(x, wx), transition.next);
			   });
	}

	[[clang::always_inline]] inline std::optional<std::tuple<OutValue, State>> Transition(State  state,
																						  Symbol letter) const {
		const auto &t = transitions[state][size_t(letter)];
		return std::tuple{FullOutput(t, letter), t.next};
	}

	[[clang::always_inline]] inline auto Transitions() const {
		return std::views::iota(size_t(0), size_t(N * alphabetSize)) | std::views::transform([this](std::size_t i) {
				   State		s	 = i / alphabetSize;
				   Symbol		l	 = Symbol(i % alphabetSize);
				   const auto &t	 = transitions[s][size_t(l)];
				   return std::tuple(s, typename Monoid::Value{l, FullOutput(t, l)}, t.next);
			   });
	}

	[[clang::always_inline]] inline auto &RawTransitions() const { return transitions; }
	[[clang::always_inline]] inline auto  RawTransitions()	&&{ return std::move(transitions); }

	/// Unlike TotalSSFST, this can't just hand the monoid a live view straight
	/// into `transitions`: compact() reads/writes raw ids (v.id) to track
	/// liveness, and a flag bit packed into that same 32 bits would either be
	/// read back as part of a bogus, oversized id (crashing) or silently
	/// dropped when compact() overwrites the slot with a plain renumbered id.
	/// So: gather clean ids into scratch storage, let compact() do its thing,
	/// then scatter the renumbered ids back with each transition's original
	/// flag reapplied.
	void CompactLabels() {
		auto				  &outM = get<1>(monoid);
		std::vector<OutValue> cleanVals;
		cleanVals.reserve(std::size_t(N) * alphabetSize);
		for (const auto &row : transitions)
			for (const auto &t : row)
				cleanVals.push_back(CleanValue(t.val));

		outM.compact(cleanVals, PsiValues(), InitialOutputSpan());

		std::size_t i = 0;
		for (auto &row : transitions) {
			for (auto &t : row) {
				bool wasCompressed = IsCompressedFlag(t.val);
				t.val			   = PackValue(cleanVals[i++], wasCompressed);
			}
		}
	}

	const Monoid &GetMonoid() const { return monoid; }
	Monoid		 &GetMonoid()		&{ return monoid; }
	Monoid		&&GetMonoid()	   &&{ return std::move(monoid); }

	//////////////// builder interface //////////////////////

	State NewState() {
		State newState = N++;
		transitions.emplace_back();
		output.push_back(OutputMonoid::identity);
		return newState;
	}

	/// label.second is the FULL output (exactly like TotalSSFST::AddTransition
	/// expects): if it ends in the consumed letter, only the prefix before
	/// that trailing letter is stored (flagged compressed); otherwise the
	/// value is kept verbatim (flagged uncompressed), so this builder
	/// interface is a strict superset of TotalSSFST's -- any construction
	/// that targets TotalSSFST (composeFST, subsequentializeFST, ...) can
	/// target this type instead with no change in the set of representable
	/// transitions.
	void AddTransition(State from, Monoid::Value label, State to) {
		const auto &[letter, wx] = label;
		if (EndsWithSymbol(wx, letter)) {
			const auto &outM = get<1>(monoid);
			std::size_t len	 = outM.size(wx);
			OutValue	w	 = OutValue(outM.sub(wx, 0, len - 1));
			transitions[from][size_t(letter)] = {PackValue(w, true), to};
		} else {
			transitions[from][size_t(letter)] = {PackValue(wx, false), to};
		}
	}

	void AddInitial([[maybe_unused]] State state) { assert(state == 0); }	  /// only state 0 is initial
	void AddFinal([[maybe_unused]] State state) { assert(state < N); }		  /// all states are final
	void SetInitialOutput(OutValue v) { initialOut = std::move(v); }
	void SetPsi(State state, OutValue v) {
		assert(state < N);
		output[state] = std::move(v);
	}

	/// this can return some kind of weird range, but let's be reasonable
	void f(range_of<Symbol> auto input, std::vector<OutValue> &outputWord) const {
		State state = 0;
		outputWord.clear();
		outputWord.push_back(initialOut);
		for (const auto &letter : input) {
			const auto &t = transitions[state][size_t(letter)];
			state		  = t.next;
			outputWord.push_back(FullOutput(t, letter));
		}
		outputWord.push_back(output[state]);
	}

	/// The same as the other overload, but in some sense
	///	f(input) | get<1>(monoid).gen()
	/// For a compressed transition this never has to reconstruct w.x through
	/// the monoid: the trailing letter is appended to the output buffer
	/// directly, since it is already known without asking the pool. An
	/// uncompressed transition just emits its verbatim value, same as
	/// TotalSSFST would.
	void f(range_of<Symbol> auto input, std::vector<Symbol> &outputWord) const
		requires(!std::same_as<Symbol, OutValue>)	  // would clash
	{
		State state = 0;
		outputWord.clear();

		const auto &initial_out = get<1>(monoid).gen(initialOut);
		outputWord.insert(outputWord.end(), initial_out.begin(), initial_out.end());

		for (const auto &letter : input) {
			const auto &t	  = transitions[state][size_t(letter)];
			state			  = t.next;
			OutValue	clean = CleanValue(t.val);
			const auto &v	  = get<1>(monoid).gen(clean);
			outputWord.insert(outputWord.end(), v.begin(), v.end());
			if (IsCompressedFlag(t.val)) outputWord.push_back(letter);
		}
		const auto &final_out = get<1>(monoid).gen(output[state]);
		outputWord.insert(outputWord.end(), final_out.begin(), final_out.end());
	}

	std::vector<Symbol> f(std::span<const Symbol> input) const {
		std::vector<Symbol> outputWord;
		f(input, outputWord);
		return outputWord;
	}

	const CompressedTotalSSFST &serialize(std::ostream &out) const {
		out.write(reinterpret_cast<const char *>(&N), sizeof(N));
		monoid.serialize(out);
		out.write(reinterpret_cast<const char *>(transitions.data()), transitions.size() * sizeof(transitions[0]));
		out.write(reinterpret_cast<const char *>(output.data()), output.size() * sizeof(output[0]));
		return *this;
	}

	CompressedTotalSSFST(std::istream &in) {
		in.read(reinterpret_cast<char *>(&N), sizeof(N));
		monoid = Monoid(in);
		transitions.resize(N);
		output.resize(N);
		in.read(reinterpret_cast<char *>(transitions.data()), transitions.size() * sizeof(transitions[0]));
		in.read(reinterpret_cast<char *>(output.data()), output.size() * sizeof(output[0]));
	}

	const CompressedTotalSSFST &print(std::ostream &out) const {
		out << "digraph CompressedTotalSSFST {\n";
		out << "  rankdir=LR;\n";
		out << "  node [shape=circle];\n";
		out << "  init [label=\"N=" << N << "\", shape=square];\n";
		out << "  init -> 0;\n";	 // initial state
		for (State s = 0; s < N; ++s) {
			if (!get<1>(monoid).equal(output[s], OutputMonoid::identity)) {
				out << "  " << s << " [shape=doublecircle, label=\"";
				out << print_if_can(get<1>(monoid), output[s]);
				out << "\"];\n";								 // final States with output
			} else out << "  " << s << " [shape=circle];\n";	 // final States

			for (Symbol l : Symbol::all()) {
				const auto &t = transitions[s][size_t(l)];
				if (t.next != -1u) {
					out << "  " << s << " -> " << t.next << " [label=\"<" << l << ", ";
					out << print_if_can(get<1>(monoid), FullOutput(t, l));
					out << ">\"];\n";
				}
			}
		}
		out << "}\n";
		return *this;
	}
};

template <class Symbol, size_t alphabetSize>
void statFSA(const CompressedTotalSSFST<Symbol, alphabetSize> &fsa) {
	std::cout << "Subsequential Transtuder (compressed) : |Q| = " << fsa.size() << ", |Σ| = " << alphabetSize
			  << ", |Δ| = " << fsa.size() * alphabetSize << ", |strings| = " << fsa.cacheCount()
			  << ", total = " << fsa.cacheSize() << std::endl;
}

}	  // namespace fl
