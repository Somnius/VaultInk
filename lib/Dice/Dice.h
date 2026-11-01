#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

// Platform-free dice engine for the VaultInk dice roller: polyhedral notation plus the pool
// systems Lef plays. No heap, no exceptions, no Arduino/ESP includes. Every roll function takes
// an Rng and returns a Result with every die that was thrown (at most MAX_DICE), its flags and
// the numeric outcome; the UI does all the text. oWoD and V5 follow Lef's ShadowRealms rules
// (backend/services/wod_dice.py, v5_dice.py) exactly.
namespace dice {

constexpr int MAX_DICE = 64;
constexpr int MAX_EXPLODE = 20;  // extra dice an exploding roll may add

// ---- random numbers ----

// Injectable source so tests can script exact dice.
class Rng {
 public:
  virtual ~Rng() = default;
  // Uniform 1..sides (sides >= 1).
  virtual int roll(int sides) = 0;
};

// xorshift32: 4 bytes of state, seed 0 is remapped so the stream never sticks at zero.
class Xorshift32 final : public Rng {
 public:
  explicit Xorshift32(uint32_t seed = 0x9E3779B9u);
  uint32_t next();
  // Rejection sampling, no modulo bias.
  int roll(int sides) override;

 private:
  uint32_t state;
};

// ---- results ----

enum class System : uint8_t {
  Polyhedral,
  Wod,
  V5,
  V5Rouse,
  Cod,
  Fate,
  Pbta,
  Blades,
  Coc,
  Shadowrun,
  Savage,
  YearZero,
  Cyberpunk,
};

// Per-die flags (bitmask in Die::flags).
enum DieFlag : uint8_t {
  FLAG_NONE = 0,
  FLAG_HUNGER = 1 << 0,    // V5 Hunger die
  FLAG_KEPT = 1 << 1,      // counts toward the total (kh/kl, Blades, CoC tens, Savage chain)
  FLAG_EXPLODED = 1 << 2,  // this die triggered an extra die (max face, 10-again, specialty 10)
  FLAG_CRIT = 1 << 3,      // part of a critical (V5 10, Blades 6, CoC 01, Cyberpunk 10)
  FLAG_REROLLED = 1 << 4,  // replaced by a reroll (Willpower, push, rote); value is the new one
  FLAG_EXTRA = 1 << 5,     // added by a rule (explosion, specialty, bonus/penalty die, wild die)
  FLAG_ONE = 1 << 6,       // a 1 with a rule attached (oWoD cancel, Shadowrun glitch, Cyberpunk)
  FLAG_WILD = 1 << 7,      // Savage Worlds wild die
};

struct Die {
  int16_t value = 0;   // face as shown; Fate -1/0/+1; CoC tens die 0..90
  uint16_t sides = 0;  // 3 for Fate; 10 for the CoC tens die too
  uint8_t flags = FLAG_NONE;

  bool has(DieFlag f) const { return (flags & f) != 0; }
};

// The UI maps each key to a translated label.
enum class Outcome : uint8_t {
  None,  // nothing to judge (plain polyhedral sum, Cyberpunk with no DV)
  Success,
  Failure,
  Botch,               // oWoD
  ExceptionalSuccess,  // oWoD 4 net, CoD 5+
  PhenomenalSuccess,   // oWoD 5+
  CriticalSuccess,     // V5 pair of 10s, Blades two 6s, Cyberpunk 10, nat 20 helper
  MessyCritical,       // V5
  BestialFailure,      // V5
  TotalFailure,        // V5 zero successes
  DramaticFailure,     // CoD chance die 1
  CriticalFailure,     // Savage snake eyes, Cyberpunk 1, nat 1 helper
  StrongHit,           // PbtA 10+
  WeakHit,             // PbtA 7-9
  Miss,                // PbtA 6-
  Full,                // Blades 6
  Partial,             // Blades 4-5
  Bad,                 // Blades 1-3
  Critical,            // CoC 01
  Extreme,             // CoC <= skill/5
  Hard,                // CoC <= skill/2
  Regular,             // CoC <= skill
  Fumble,              // CoC 100 (96+ under 50)
  Glitch,              // Shadowrun glitch with hits
  CriticalGlitch,      // Shadowrun glitch with no hits
  Raise,               // Savage Worlds success with 1+ raises
};

// Stable snake_case key for an Outcome, e.g. "messy_critical" (translation lookup).
const char* outcomeKey(Outcome o);

enum class ParseError : uint8_t {
  None,
  Empty,
  BadSyntax,
  TooManyDice,  // count > MAX_DICE or 0
  BadSides,     // sides < 2 or > 1000
  BadKeep,      // keep 0 or > count
  BadModifier,  // more than 4 digits
  BadParams,    // a system parameter out of range (difficulty, skill, trait die, wrong prev Result)
};

struct Result {
  System system = System::Polyhedral;
  bool ok = true;  // false on a parse error or bad params; the error field says why
  ParseError error = ParseError::None;

  std::array<Die, MAX_DICE> dice{};
  uint8_t count = 0;

  Outcome outcome = Outcome::None;
  int32_t total = 0;       // sum of kept dice + modifier; CoC the d100; Cyberpunk result; Rouse hunger after
  int16_t modifier = 0;    // flat bonus/stat/skill used
  int16_t target = 0;      // difficulty / TN / DV / Savage target / CoC skill
  int16_t successes = 0;   // net successes or hits (pool systems)
  int16_t ones = 0;        // 1s with meaning: oWoD cancels, V5 Hunger 1s, Shadowrun, Year Zero banes
  int16_t pairs = 0;       // V5 pairs of 10s
  int16_t margin = 0;      // successes - difficulty (V5), raises (Savage), total - DV (Cyberpunk)
  bool truncated = false;  // dice were dropped because MAX_DICE/MAX_EXPLODE was reached

  // Adds a die; returns false (and sets truncated) when full.
  bool add(int value, int sides, uint8_t flags);
};

// ---- 1. polyhedral ----

struct PolyParams {
  int count = 1;
  int sides = 20;
  int keep = 0;          // 0 = keep all
  bool keepHigh = true;  // kh / kl
  int modifier = 0;
  bool explode = false;  // "!" : a max face adds another die (capped at MAX_EXPLODE)
};

// "NdM[!][kh K|kl K][+C|-C]": 2d6+3, d20, 4d6kh3, d100, d%, 3d6!, 2d20kl1-1. Case and spaces are
// ignored. Returns false and sets err on a bad string (params are left unspecified).
bool parseNotation(const char* text, PolyParams& out, ParseError& err);

Result rollPolyhedral(Rng& rng, const PolyParams& p);
// Parse + roll; a parse error gives ok=false with the error, no dice.
Result rollNotation(Rng& rng, const char* text);
// 2d20 keep high / low, + modifier. Natural 20 / 1 on the kept die set CriticalSuccess / CriticalFailure.
Result rollAdvantage(Rng& rng, int modifier);
Result rollDisadvantage(Rng& rng, int modifier);

// ---- 2. oWoD classic / Revised Storyteller (wod_dice.py) ----

struct WodParams {
  int pool = 1;                   // 1..MAX_DICE
  int difficulty = 6;             // 2..10
  bool specialty = false;         // 10s add a die (chained); extra dice carry FLAG_EXTRA
  bool willpower = false;         // +1 automatic success; never botches
  bool rerollOnesCancel = false;  // Mage: a 1 on a specialty die cancels too
};

// successes = net (after 1s cancel, floor 0, + willpower); ones = 1s in the base pool.
// Outcome: Botch, Failure, Success (1-3), ExceptionalSuccess (4), PhenomenalSuccess (5+).
Result rollWod(Rng& rng, const WodParams& p);

// ---- 3. Vampire V5 (v5_dice.py) ----

struct V5Params {
  int pool = 1;        // 1..MAX_DICE
  int hunger = 0;      // 0..5; min(hunger, pool) dice are Hunger dice (they come last)
  int difficulty = 1;  // 0..10 successes needed; 0 successes never wins
};

// successes include +2 per pair of 10s; pairs; margin = successes - difficulty; ones = Hunger 1s.
// Outcome: MessyCritical, CriticalSuccess, Success, BestialFailure, TotalFailure, Failure.
Result rollV5(Rng& rng, const V5Params& p);
// Willpower: reroll up to 3 normal (non-Hunger) dice that failed (< 6), lowest first, once.
// Rerolled dice get FLAG_REROLLED. prev must be a V5 result; returns it unchanged when nothing qualifies.
Result rerollV5Willpower(Rng& rng, const Result& prev);
// Rouse check: one Hunger die, 6+ succeeds. total = Hunger after; margin = 1 when failing at Hunger 5.
Result rollRouse(Rng& rng, int hungerBefore);

// ---- 4. Chronicles of Darkness ----

struct CodParams {
  int pool = 1;       // <= 0 rolls a chance die
  int again = 10;     // 10, 9, 8 or 0 (none): dice >= again add a die
  bool rote = false;  // failed dice of the first roll are rerolled once
};

// 8+ succeeds. Chance die: 10 = one success, 1 = DramaticFailure, no again. 5+ = ExceptionalSuccess.
Result rollCod(Rng& rng, const CodParams& p);

// ---- 5. Fate / Fudge ----

// 4dF (-1/0/+1 each) + modifier; total in modifier-4 .. modifier+4. Outcome None.
Result rollFate(Rng& rng, int modifier);

// ---- 6. Powered by the Apocalypse ----

// 2d6 + stat: 10+ StrongHit, 7-9 WeakHit, 6- Miss.
Result rollPbta(Rng& rng, int stat);

// ---- 7. Blades in the Dark ----

// d6 pool, highest kept: 6 Full, 4-5 Partial, 1-3 Bad; two 6s CriticalSuccess. Pool 0: two dice,
// lowest kept, no critical. total = the kept value.
Result rollBlades(Rng& rng, int pool);

// ---- 8. Call of Cthulhu 7e ----

struct CocParams {
  int skill = 50;       // 1..100
  int bonusDice = 0;    // extra tens dice, keep the lowest tens
  int penaltyDice = 0;  // extra tens dice, keep the highest; bonus and penalty cancel
};

// Dice: tens dice (value 0..90, the kept one flagged) then the units die (0..9). total = 1..100.
// Outcome: Critical (01), Extreme (<= skill/5), Hard (<= skill/2), Regular (<= skill),
// Fumble (100; 96-99 when skill < 50), else Failure. target = skill.
Result rollCoc(Rng& rng, const CocParams& p);

// ---- 9. Shadowrun ----

// d6 pool: 5-6 hit. Glitch when 1s >= half the pool; CriticalGlitch when also no hits.
// successes = hits, ones = 1s.
Result rollShadowrun(Rng& rng, int pool);

// ---- 10. Savage Worlds ----

struct SavageParams {
  int traitSides = 6;  // d4..d12
  int modifier = 0;
  bool wildDie = true;  // Wild Cards add a d6
  int target = 4;
};

// Trait die and wild die each ace (explode on max). Kept chain flagged; total = best + modifier;
// margin = raises. Outcome: CriticalFailure (both first dice 1), Failure, Success, Raise.
Result rollSavage(Rng& rng, const SavageParams& p);

// ---- 11. Year Zero ----

// d6 pool: 6 = success, 1 = bane (ones). Outcome Success/Failure.
Result rollYearZero(Rng& rng, int pool);
// Push: reroll every die that is neither 6 nor 1 (FLAG_REROLLED), recount.
Result pushYearZero(Rng& rng, const Result& prev);

// ---- 12. Cyberpunk Red ----

struct CyberpunkParams {
  int modifier = 0;  // stat + skill
  int dv = 0;        // difficulty value; 0 = none (Outcome None unless 10/1)
};

// d10: a 10 adds a d10 (FLAG_CRIT), a 1 subtracts a d10 (stored negative, FLAG_ONE on the 1).
// total = dice + modifier. With a DV: Success when total > dv (ties lose), else Failure.
// Without one: CriticalSuccess / CriticalFailure / None.
Result rollCyberpunk(Rng& rng, const CyberpunkParams& p);

}  // namespace dice
