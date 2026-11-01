#include "Dice.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <numeric>

namespace dice {

namespace {

constexpr int D10 = 10;
constexpr int D6 = 6;
constexpr int MAX_SIDES = 1000;

bool isTen(const Die& d) { return d.value == 10; }
bool isOne(const Die& d) { return d.value == 1; }

template <typename Pred>
int countDice(const Result& r, Pred pred) {
  return static_cast<int>(std::count_if(r.dice.begin(), r.dice.begin() + r.count, pred));
}

int sumDice(const Result& r) {
  return std::accumulate(r.dice.begin(), r.dice.begin() + r.count, 0,
                         [](int acc, const Die& d) { return acc + d.value; });
}

int sumKept(const Result& r) {
  return std::accumulate(r.dice.begin(), r.dice.begin() + r.count, 0,
                         [](int acc, const Die& d) { return d.has(FLAG_KEPT) ? acc + d.value : acc; });
}

Result failed(System sys, ParseError err) {
  Result r;
  r.system = sys;
  r.ok = false;
  r.error = err;
  return r;
}

// Rolls one die and keeps adding while it shows its max face, up to MAX_EXPLODE extra dice.
// Returns the chain total. Each new die carries `flags` (+ FLAG_EXTRA after the first).
int rollChain(Rng& rng, Result& r, int sides, uint8_t flags, bool explode, int& extrasLeft) {
  int total = 0;
  uint8_t f = flags;
  for (;;) {
    const int v = rng.roll(sides);
    total += v;
    const bool again = explode && v == sides && extrasLeft > 0 && r.count < MAX_DICE;
    if (!r.add(v, sides, static_cast<uint8_t>(f | (again ? FLAG_EXPLODED : FLAG_NONE)))) {
      r.truncated = true;
      break;
    }
    if (!again) {
      if (explode && v == sides) r.truncated = true;
      break;
    }
    --extrasLeft;
    f = static_cast<uint8_t>(flags | FLAG_EXTRA);
  }
  return total;
}

// ---- notation parser helpers ----

struct Cursor {
  const char* p;
  char peek() const { return *p; }
  bool done() const { return *p == '\0'; }
  void skipSpaces() {
    while (*p == ' ' || *p == '\t') ++p;
  }
  // Up to 4 digits; false when none.
  bool number(int& out) {
    skipSpaces();
    if (std::isdigit(static_cast<unsigned char>(*p)) == 0) return false;
    int v = 0;
    int digits = 0;
    while (std::isdigit(static_cast<unsigned char>(*p)) != 0) {
      if (++digits > 4) return false;
      v = v * 10 + (*p - '0');
      ++p;
    }
    out = v;
    return true;
  }
  bool accept(char c) {
    skipSpaces();
    if (std::tolower(static_cast<unsigned char>(*p)) != c) return false;
    ++p;
    return true;
  }
};

}  // namespace

// ---- Rng ----

Xorshift32::Xorshift32(uint32_t seed) : state(seed == 0 ? 0x9E3779B9u : seed) {}

uint32_t Xorshift32::next() {
  uint32_t x = state;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  state = x;
  return x;
}

int Xorshift32::roll(int sides) {
  if (sides <= 1) return 1;
  const uint32_t n = static_cast<uint32_t>(sides);
  const uint32_t limit = (UINT32_MAX / n) * n;  // largest multiple of n that fits
  uint32_t x = next();
  while (x >= limit) x = next();
  return static_cast<int>(x % n) + 1;
}

// ---- Result ----

bool Result::add(const int value, const int sides, const uint8_t flags) {
  if (count >= MAX_DICE) {
    truncated = true;
    return false;
  }
  Die& d = dice[count++];
  d.value = static_cast<int16_t>(value);
  d.sides = static_cast<uint16_t>(sides);
  d.flags = flags;
  return true;
}

const char* outcomeKey(const Outcome o) {
  switch (o) {
    case Outcome::None:
      return "none";
    case Outcome::Success:
      return "success";
    case Outcome::Failure:
      return "failure";
    case Outcome::Botch:
      return "botch";
    case Outcome::ExceptionalSuccess:
      return "exceptional_success";
    case Outcome::PhenomenalSuccess:
      return "phenomenal_success";
    case Outcome::CriticalSuccess:
      return "critical_success";
    case Outcome::MessyCritical:
      return "messy_critical";
    case Outcome::BestialFailure:
      return "bestial_failure";
    case Outcome::TotalFailure:
      return "total_failure";
    case Outcome::DramaticFailure:
      return "dramatic_failure";
    case Outcome::CriticalFailure:
      return "critical_failure";
    case Outcome::StrongHit:
      return "strong_hit";
    case Outcome::WeakHit:
      return "weak_hit";
    case Outcome::Miss:
      return "miss";
    case Outcome::Full:
      return "full";
    case Outcome::Partial:
      return "partial";
    case Outcome::Bad:
      return "bad";
    case Outcome::Critical:
      return "critical";
    case Outcome::Extreme:
      return "extreme";
    case Outcome::Hard:
      return "hard";
    case Outcome::Regular:
      return "regular";
    case Outcome::Fumble:
      return "fumble";
    case Outcome::Glitch:
      return "glitch";
    case Outcome::CriticalGlitch:
      return "critical_glitch";
    case Outcome::Raise:
      return "raise";
  }
  return "none";
}

// ---- 1. polyhedral ----

bool parseNotation(const char* text, PolyParams& out, ParseError& err) {
  err = ParseError::None;
  if (text == nullptr) {
    err = ParseError::Empty;
    return false;
  }
  Cursor c{text};
  c.skipSpaces();
  if (c.done()) {
    err = ParseError::Empty;
    return false;
  }
  PolyParams p;
  p.count = 1;
  const bool hasCount = c.number(p.count);
  if (!c.accept('d')) {
    err = ParseError::BadSyntax;
    return false;
  }
  if (c.accept('%')) {
    p.sides = 100;
  } else if (!c.number(p.sides)) {
    err = ParseError::BadSyntax;
    return false;
  }
  if (hasCount && (p.count < 1 || p.count > MAX_DICE)) {
    err = ParseError::TooManyDice;
    return false;
  }
  if (p.sides < 2 || p.sides > MAX_SIDES) {
    err = ParseError::BadSides;
    return false;
  }
  p.explode = c.accept('!');
  if (c.accept('k')) {
    if (c.accept('h')) {
      p.keepHigh = true;
    } else if (c.accept('l')) {
      p.keepHigh = false;
    } else {
      err = ParseError::BadSyntax;
      return false;
    }
    if (!c.number(p.keep)) {
      err = ParseError::BadSyntax;
      return false;
    }
    if (p.keep < 1 || p.keep > p.count) {
      err = ParseError::BadKeep;
      return false;
    }
  }
  c.skipSpaces();
  if (c.peek() == '+' || c.peek() == '-') {
    const int sign = c.peek() == '-' ? -1 : 1;
    ++c.p;
    int m = 0;
    if (!c.number(m)) {
      err = ParseError::BadModifier;
      return false;
    }
    p.modifier = sign * m;
  }
  c.skipSpaces();
  if (!c.done()) {
    err = ParseError::BadSyntax;
    return false;
  }
  out = p;
  return true;
}

Result rollPolyhedral(Rng& rng, const PolyParams& p) {
  if (p.count < 1 || p.count > MAX_DICE) return failed(System::Polyhedral, ParseError::TooManyDice);
  if (p.sides < 2 || p.sides > MAX_SIDES) return failed(System::Polyhedral, ParseError::BadSides);
  if (p.keep < 0 || p.keep > p.count) return failed(System::Polyhedral, ParseError::BadKeep);

  Result r;
  r.system = System::Polyhedral;
  r.modifier = static_cast<int16_t>(p.modifier);
  int extras = MAX_EXPLODE;
  for (int i = 0; i < p.count; ++i) rollChain(rng, r, p.sides, FLAG_NONE, p.explode, extras);

  // Keep: every base die (FLAG_EXTRA excluded) is a candidate; its chain's extras follow it.
  if (p.keep == 0) {
    std::for_each(r.dice.begin(), r.dice.begin() + r.count, [](Die& d) { d.flags |= FLAG_KEPT; });
  } else {
    // Chain totals per base die, then mark the best `keep` chains.
    std::array<int, MAX_DICE> chainTotal{};
    std::array<uint8_t, MAX_DICE> chainStart{};
    int chains = 0;
    for (int i = 0; i < r.count; ++i) {
      if (!r.dice[i].has(FLAG_EXTRA)) chainStart[chains++] = static_cast<uint8_t>(i);
      chainTotal[chains - 1] += r.dice[i].value;
    }
    std::array<uint8_t, MAX_DICE> order{};
    std::iota(order.begin(), order.begin() + chains, static_cast<uint8_t>(0));
    std::stable_sort(order.begin(), order.begin() + chains, [&](uint8_t a, uint8_t b) {
      return p.keepHigh ? chainTotal[a] > chainTotal[b] : chainTotal[a] < chainTotal[b];
    });
    for (int k = 0; k < p.keep && k < chains; ++k) {
      const int start = chainStart[order[k]];
      for (int i = start; i < r.count && (i == start || r.dice[i].has(FLAG_EXTRA)); ++i) {
        r.dice[i].flags |= FLAG_KEPT;
      }
    }
  }
  r.total = sumKept(r) + p.modifier;
  return r;
}

Result rollNotation(Rng& rng, const char* text) {
  PolyParams p;
  ParseError err = ParseError::None;
  if (!parseNotation(text, p, err)) return failed(System::Polyhedral, err);
  return rollPolyhedral(rng, p);
}

namespace {
Result rollD20Pair(Rng& rng, const int modifier, const bool keepHigh) {
  PolyParams p;
  p.count = 2;
  p.sides = 20;
  p.keep = 1;
  p.keepHigh = keepHigh;
  p.modifier = modifier;
  Result r = rollPolyhedral(rng, p);
  const int kept = r.total - modifier;
  if (kept == 20) {
    r.outcome = Outcome::CriticalSuccess;
  } else if (kept == 1) {
    r.outcome = Outcome::CriticalFailure;
  }
  return r;
}
}  // namespace

Result rollAdvantage(Rng& rng, const int modifier) { return rollD20Pair(rng, modifier, true); }
Result rollDisadvantage(Rng& rng, const int modifier) { return rollD20Pair(rng, modifier, false); }

// ---- 2. oWoD ----

Result rollWod(Rng& rng, const WodParams& p) {
  if (p.pool < 1 || p.pool > MAX_DICE) return failed(System::Wod, ParseError::TooManyDice);
  if (p.difficulty < 2 || p.difficulty > D10) return failed(System::Wod, ParseError::BadParams);

  Result r;
  r.system = System::Wod;
  r.target = static_cast<int16_t>(p.difficulty);
  for (int i = 0; i < p.pool; ++i) {
    const int v = rng.roll(D10);
    uint8_t f = FLAG_NONE;
    if (v == 1) f |= FLAG_ONE;
    if (p.specialty && v == D10) f |= FLAG_EXPLODED;
    r.add(v, D10, f);
  }
  const int pool = r.count;
  const auto base = [&](auto pred) {
    return static_cast<int>(std::count_if(r.dice.begin(), r.dice.begin() + pool, pred));
  };
  const int rawSuccesses = base([&](const Die& d) { return d.value >= p.difficulty; });
  const int ones = base(isOne);

  // Specialty: one extra die per natural 10, each 10 on an extra die explodes again.
  int rerollSuccesses = 0;
  int rerollOnes = 0;
  if (p.specialty) {
    int pending = base(isTen);
    while (pending > 0 && r.count < MAX_DICE) {
      --pending;
      const int v = rng.roll(D10);
      uint8_t f = FLAG_EXTRA;
      if (v == D10) {
        ++pending;
        f |= FLAG_EXPLODED;
      }
      if (v == 1 && p.rerollOnesCancel) f |= FLAG_ONE;
      r.add(v, D10, f);
      if (v >= p.difficulty) ++rerollSuccesses;
      if (v == 1) ++rerollOnes;
    }
    if (pending > 0) r.truncated = true;
  }

  const int cancelling = ones + (p.rerollOnesCancel ? rerollOnes : 0);
  const int diceNet = std::max(0, rawSuccesses + rerollSuccesses - cancelling);
  const int net = diceNet + (p.willpower ? 1 : 0);
  const bool botch = !p.willpower && rawSuccesses == 0 && ones > 0;

  r.successes = static_cast<int16_t>(net);
  r.ones = static_cast<int16_t>(ones);
  r.total = net;
  if (botch) {
    r.outcome = Outcome::Botch;
  } else if (net == 0) {
    r.outcome = Outcome::Failure;
  } else if (net >= 5) {
    r.outcome = Outcome::PhenomenalSuccess;
  } else if (net == 4) {
    r.outcome = Outcome::ExceptionalSuccess;
  } else {
    r.outcome = Outcome::Success;
  }
  return r;
}

// ---- 3. V5 ----

namespace {
// Re-counts a V5 result from its dice (pure).
void resolveV5(Result& r) {
  const auto begin = r.dice.begin();
  const auto end = r.dice.begin() + r.count;
  const int base = static_cast<int>(std::count_if(begin, end, [](const Die& d) { return d.value >= 6; }));
  const int tensNormal =
      static_cast<int>(std::count_if(begin, end, [](const Die& d) { return isTen(d) && !d.has(FLAG_HUNGER); }));
  const int tensHunger =
      static_cast<int>(std::count_if(begin, end, [](const Die& d) { return isTen(d) && d.has(FLAG_HUNGER); }));
  const int hungerOnes =
      static_cast<int>(std::count_if(begin, end, [](const Die& d) { return isOne(d) && d.has(FLAG_HUNGER); }));
  const int pairs = (tensNormal + tensHunger) / 2;
  const int successes = base + 2 * pairs;
  const bool win = successes >= r.target && successes > 0;
  const bool critical = win && pairs >= 1;
  const bool messy = critical && tensHunger > 0;
  const bool bestial = !win && hungerOnes > 0;

  std::for_each(begin, end, [&](Die& d) {
    d.flags &= static_cast<uint8_t>(~(FLAG_CRIT | FLAG_ONE));
    if (isTen(d) && pairs > 0) d.flags |= FLAG_CRIT;
    if (isOne(d) && d.has(FLAG_HUNGER)) d.flags |= FLAG_ONE;
  });

  r.successes = static_cast<int16_t>(successes);
  r.pairs = static_cast<int16_t>(pairs);
  r.ones = static_cast<int16_t>(hungerOnes);
  r.margin = static_cast<int16_t>(successes - r.target);
  r.total = successes;
  if (win) {
    r.outcome = messy ? Outcome::MessyCritical : critical ? Outcome::CriticalSuccess : Outcome::Success;
  } else if (bestial) {
    r.outcome = Outcome::BestialFailure;
  } else if (successes == 0) {
    r.outcome = Outcome::TotalFailure;
  } else {
    r.outcome = Outcome::Failure;
  }
}
}  // namespace

Result rollV5(Rng& rng, const V5Params& p) {
  if (p.pool < 1 || p.pool > MAX_DICE) return failed(System::V5, ParseError::TooManyDice);
  if (p.difficulty < 0 || p.difficulty > D10) return failed(System::V5, ParseError::BadParams);

  Result r;
  r.system = System::V5;
  r.target = static_cast<int16_t>(p.difficulty);
  const int hunger = std::min(std::clamp(p.hunger, 0, 5), p.pool);
  for (int i = 0; i < p.pool - hunger; ++i) r.add(rng.roll(D10), D10, FLAG_NONE);
  for (int i = 0; i < hunger; ++i) r.add(rng.roll(D10), D10, FLAG_HUNGER);
  resolveV5(r);
  return r;
}

Result rerollV5Willpower(Rng& rng, const Result& prev) {
  if (prev.system != System::V5 || !prev.ok) return failed(System::V5, ParseError::BadParams);
  Result r = prev;
  // Pick up to 3 failing normal dice, lowest values first.
  std::array<uint8_t, MAX_DICE> idx{};
  int n = 0;
  for (int i = 0; i < r.count; ++i) {
    const Die& d = r.dice[i];
    if (!d.has(FLAG_HUNGER) && !d.has(FLAG_REROLLED) && d.value < 6) idx[n++] = static_cast<uint8_t>(i);
  }
  if (n == 0) return r;
  std::stable_sort(idx.begin(), idx.begin() + n,
                   [&](uint8_t a, uint8_t b) { return r.dice[a].value < r.dice[b].value; });
  const int rerolls = std::min(n, 3);
  for (int k = 0; k < rerolls; ++k) {
    Die& d = r.dice[idx[k]];
    d.value = static_cast<int16_t>(rng.roll(D10));
    d.flags |= FLAG_REROLLED;
  }
  resolveV5(r);
  return r;
}

Result rollRouse(Rng& rng, const int hungerBefore) {
  Result r;
  r.system = System::V5Rouse;
  const int h = std::clamp(hungerBefore, 0, 5);
  const int v = rng.roll(D10);
  const bool success = v >= 6;
  r.add(v, D10, static_cast<uint8_t>(FLAG_HUNGER | (v == 1 ? FLAG_ONE : FLAG_NONE)));
  r.target = static_cast<int16_t>(h);
  r.total = success ? h : std::min(5, h + 1);
  r.margin = static_cast<int16_t>((!success && h >= 5) ? 1 : 0);
  r.outcome = success ? Outcome::Success : Outcome::Failure;
  return r;
}

// ---- 4. Chronicles of Darkness ----

Result rollCod(Rng& rng, const CodParams& p) {
  if (p.pool > MAX_DICE) return failed(System::Cod, ParseError::TooManyDice);
  if (p.again != 0 && (p.again < 8 || p.again > D10)) return failed(System::Cod, ParseError::BadParams);

  Result r;
  r.system = System::Cod;
  r.target = 8;
  if (p.pool <= 0) {
    const int v = rng.roll(D10);
    r.add(v, D10, static_cast<uint8_t>(v == 1 ? FLAG_ONE : FLAG_NONE));
    r.successes = static_cast<int16_t>(v == D10 ? 1 : 0);
    r.total = r.successes;
    r.outcome = v == D10 ? Outcome::Success : v == 1 ? Outcome::DramaticFailure : Outcome::Failure;
    return r;
  }

  const int again = p.again == 0 ? D10 + 1 : p.again;  // 11 = never
  int extras = MAX_EXPLODE;
  // True when a die showing v earns an extra die (and takes one from the cap).
  const auto addAgain = [&](int v) {
    const bool more = v >= again && extras > 0 && r.count < MAX_DICE;
    if (more) --extras;
    return more;
  };
  // Rolling a chain of again-dice starting from an already-rolled value.
  const auto chainFrom = [&](int v, uint8_t flags) {
    bool more = addAgain(v);
    r.add(v, D10, static_cast<uint8_t>(flags | (more ? FLAG_EXPLODED : FLAG_NONE)));
    while (more) {
      const int nv = rng.roll(D10);
      more = addAgain(nv);
      r.add(nv, D10, static_cast<uint8_t>(FLAG_EXTRA | (more ? FLAG_EXPLODED : FLAG_NONE)));
    }
  };

  for (int i = 0; i < p.pool; ++i) {
    const int v = rng.roll(D10);
    if (p.rote && v < 8) {
      // Rote: a failed die is rolled again once; the new value replaces it (flagged rerolled).
      chainFrom(rng.roll(D10), FLAG_REROLLED);
    } else {
      chainFrom(v, FLAG_NONE);
    }
  }
  const int successes = countDice(r, [](const Die& d) { return d.value >= 8; });
  r.successes = static_cast<int16_t>(successes);
  r.total = successes;
  r.outcome = successes >= 5 ? Outcome::ExceptionalSuccess : successes > 0 ? Outcome::Success : Outcome::Failure;
  return r;
}

// ---- 5. Fate ----

Result rollFate(Rng& rng, const int modifier) {
  Result r;
  r.system = System::Fate;
  r.modifier = static_cast<int16_t>(modifier);
  for (int i = 0; i < 4; ++i) r.add(rng.roll(3) - 2, 3, FLAG_KEPT);
  r.total = sumDice(r) + modifier;
  return r;
}

// ---- 6. PbtA ----

Result rollPbta(Rng& rng, const int stat) {
  Result r;
  r.system = System::Pbta;
  r.modifier = static_cast<int16_t>(stat);
  r.add(rng.roll(D6), D6, FLAG_KEPT);
  r.add(rng.roll(D6), D6, FLAG_KEPT);
  r.total = sumDice(r) + stat;
  r.outcome = r.total >= 10 ? Outcome::StrongHit : r.total >= 7 ? Outcome::WeakHit : Outcome::Miss;
  return r;
}

// ---- 7. Blades ----

Result rollBlades(Rng& rng, const int pool) {
  if (pool < 0 || pool > MAX_DICE) return failed(System::Blades, ParseError::TooManyDice);
  Result r;
  r.system = System::Blades;
  const int n = pool == 0 ? 2 : pool;
  for (int i = 0; i < n; ++i) r.add(rng.roll(D6), D6, FLAG_NONE);
  const auto begin = r.dice.begin();
  const auto end = r.dice.begin() + r.count;
  const auto less = [](const Die& a, const Die& b) { return a.value < b.value; };
  Die& kept = pool == 0 ? *std::min_element(begin, end, less) : *std::max_element(begin, end, less);
  kept.flags |= FLAG_KEPT;
  const int sixes = countDice(r, [](const Die& d) { return d.value == D6; });
  r.total = kept.value;
  if (pool > 0 && sixes >= 2) {
    std::for_each(begin, end, [](Die& d) {
      if (d.value == D6) d.flags |= FLAG_CRIT;
    });
    r.outcome = Outcome::CriticalSuccess;
  } else if (kept.value == D6) {
    r.outcome = Outcome::Full;
  } else if (kept.value >= 4) {
    r.outcome = Outcome::Partial;
  } else {
    r.outcome = Outcome::Bad;
  }
  return r;
}

// ---- 8. Call of Cthulhu ----

Result rollCoc(Rng& rng, const CocParams& p) {
  if (p.skill < 1 || p.skill > 100) return failed(System::Coc, ParseError::BadParams);
  const int net = std::clamp(p.bonusDice, 0, MAX_DICE) - std::clamp(p.penaltyDice, 0, MAX_DICE);
  const int tensCount = std::min(1 + std::abs(net), MAX_DICE - 1);

  Result r;
  r.system = System::Coc;
  r.target = static_cast<int16_t>(p.skill);
  const int units = rng.roll(D10) - 1;  // 0..9
  for (int i = 0; i < tensCount; ++i) {
    r.add((rng.roll(D10) - 1) * 10, D10, static_cast<uint8_t>(i == 0 ? FLAG_NONE : FLAG_EXTRA));
  }
  // Value of a tens die combined with the units die (00 + 0 = 100).
  const auto value = [units](const Die& d) {
    const int v = d.value + units;
    return v == 0 ? 100 : v;
  };
  const auto begin = r.dice.begin();
  const auto end = r.dice.begin() + r.count;
  const auto less = [&](const Die& a, const Die& b) { return value(a) < value(b); };
  Die& kept = net >= 0 ? *std::min_element(begin, end, less) : *std::max_element(begin, end, less);
  kept.flags |= FLAG_KEPT;
  const int roll = value(kept);
  r.add(units, D10, FLAG_KEPT);
  r.total = roll;

  const int skill = p.skill;
  if (roll == 1) {
    kept.flags |= FLAG_CRIT;
    r.outcome = Outcome::Critical;
  } else if (roll == 100 || (skill < 50 && roll >= 96)) {
    r.outcome = Outcome::Fumble;
  } else if (roll <= skill / 5) {
    r.outcome = Outcome::Extreme;
  } else if (roll <= skill / 2) {
    r.outcome = Outcome::Hard;
  } else if (roll <= skill) {
    r.outcome = Outcome::Regular;
  } else {
    r.outcome = Outcome::Failure;
  }
  r.margin = static_cast<int16_t>(skill - roll);
  return r;
}

// ---- 9. Shadowrun ----

Result rollShadowrun(Rng& rng, const int pool) {
  if (pool < 1 || pool > MAX_DICE) return failed(System::Shadowrun, ParseError::TooManyDice);
  Result r;
  r.system = System::Shadowrun;
  for (int i = 0; i < pool; ++i) {
    const int v = rng.roll(D6);
    r.add(v, D6, static_cast<uint8_t>(v == 1 ? FLAG_ONE : FLAG_NONE));
  }
  const int hits = countDice(r, [](const Die& d) { return d.value >= 5; });
  const int ones = countDice(r, isOne);
  const bool glitch = ones * 2 >= pool;
  r.successes = static_cast<int16_t>(hits);
  r.ones = static_cast<int16_t>(ones);
  r.total = hits;
  if (glitch) {
    r.outcome = hits == 0 ? Outcome::CriticalGlitch : Outcome::Glitch;
  } else {
    r.outcome = hits > 0 ? Outcome::Success : Outcome::Failure;
  }
  return r;
}

// ---- 10. Savage Worlds ----

Result rollSavage(Rng& rng, const SavageParams& p) {
  if (p.traitSides < 4 || p.traitSides > 12) return failed(System::Savage, ParseError::BadParams);
  Result r;
  r.system = System::Savage;
  r.modifier = static_cast<int16_t>(p.modifier);
  r.target = static_cast<int16_t>(p.target);

  int extras = MAX_EXPLODE;
  const int traitStart = r.count;
  const int trait = rollChain(rng, r, p.traitSides, FLAG_NONE, true, extras);
  const int wildStart = r.count;
  const int wild = p.wildDie ? rollChain(rng, r, D6, FLAG_WILD, true, extras) : -1;

  const bool wildBetter = p.wildDie && wild > trait;
  const int best = wildBetter ? wild : trait;
  const int keptStart = wildBetter ? wildStart : traitStart;
  const int keptEnd = wildBetter ? r.count : wildStart;
  for (int i = keptStart; i < keptEnd; ++i) r.dice[i].flags |= FLAG_KEPT;

  r.total = best + p.modifier;
  const bool snakeEyes = p.wildDie && r.dice[traitStart].value == 1 && r.dice[wildStart].value == 1;
  const int raises = r.total >= p.target ? (r.total - p.target) / 4 : 0;
  r.margin = static_cast<int16_t>(raises);
  if (snakeEyes) {
    r.outcome = Outcome::CriticalFailure;
  } else if (r.total < p.target) {
    r.outcome = Outcome::Failure;
  } else {
    r.outcome = raises > 0 ? Outcome::Raise : Outcome::Success;
  }
  return r;
}

// ---- 11. Year Zero ----

namespace {
void resolveYearZero(Result& r) {
  const int successes = countDice(r, [](const Die& d) { return d.value == D6; });
  const int banes = countDice(r, isOne);
  std::for_each(r.dice.begin(), r.dice.begin() + r.count, [](Die& d) {
    d.flags &= static_cast<uint8_t>(~FLAG_ONE);
    if (isOne(d)) d.flags |= FLAG_ONE;
  });
  r.successes = static_cast<int16_t>(successes);
  r.ones = static_cast<int16_t>(banes);
  r.total = successes;
  r.outcome = successes > 0 ? Outcome::Success : Outcome::Failure;
}
}  // namespace

Result rollYearZero(Rng& rng, const int pool) {
  if (pool < 1 || pool > MAX_DICE) return failed(System::YearZero, ParseError::TooManyDice);
  Result r;
  r.system = System::YearZero;
  for (int i = 0; i < pool; ++i) r.add(rng.roll(D6), D6, FLAG_NONE);
  resolveYearZero(r);
  return r;
}

Result pushYearZero(Rng& rng, const Result& prev) {
  if (prev.system != System::YearZero || !prev.ok) return failed(System::YearZero, ParseError::BadParams);
  Result r = prev;
  std::for_each(r.dice.begin(), r.dice.begin() + r.count, [&](Die& d) {
    if (d.value != D6 && d.value != 1) {
      d.value = static_cast<int16_t>(rng.roll(D6));
      d.flags |= FLAG_REROLLED;
    }
  });
  resolveYearZero(r);
  return r;
}

// ---- 12. Cyberpunk Red ----

Result rollCyberpunk(Rng& rng, const CyberpunkParams& p) {
  Result r;
  r.system = System::Cyberpunk;
  r.modifier = static_cast<int16_t>(p.modifier);
  r.target = static_cast<int16_t>(p.dv);
  const int v = rng.roll(D10);
  int total = v;
  if (v == D10) {
    const int extra = rng.roll(D10);
    r.add(v, D10, FLAG_KEPT | FLAG_EXPLODED | FLAG_CRIT);
    r.add(extra, D10, FLAG_KEPT | FLAG_EXTRA);
    total += extra;
  } else if (v == 1) {
    const int extra = rng.roll(D10);
    r.add(v, D10, FLAG_KEPT | FLAG_ONE);
    r.add(-extra, D10, FLAG_KEPT | FLAG_EXTRA);
    total -= extra;
  } else {
    r.add(v, D10, FLAG_KEPT);
  }
  r.total = total + p.modifier;
  r.margin = static_cast<int16_t>(r.total - p.dv);
  if (p.dv > 0) {
    r.outcome = r.total > p.dv ? Outcome::Success : Outcome::Failure;
  } else {
    r.outcome = v == D10 ? Outcome::CriticalSuccess : v == 1 ? Outcome::CriticalFailure : Outcome::None;
  }
  return r;
}

}  // namespace dice
