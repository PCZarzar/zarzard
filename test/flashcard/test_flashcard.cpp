// Host-side tests for flashcard logic that does not need the device.
// Build and run: bash test/flashcard/run.sh
#include <HalStorage.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "FlashcardCatalog.h"
#include "FlashcardClock.h"
#include "FlashcardDeck.h"
#include "FlashcardFolderIndex.h"
#include "FlashcardLinearSession.h"
#include "FlashcardMemory.h"
#include "FlashcardSrs.h"
#include "reference_parser.h"

extern size_t g_testLargestFreeBlock;

static int failures = 0;
static int checks = 0;

#define CHECK_EQ(actual, expected)                             \
  do {                                                         \
    ++checks;                                                  \
    const auto& a_ = (actual);                                 \
    const auto& e_ = (expected);                               \
    if (!(a_ == e_)) {                                         \
      ++failures;                                              \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #actual); \
    }                                                          \
  } while (0)

static FlashcardCard front(const std::string& s) {
  FlashcardCard c;
  c.frontContent = s;
  return c;
}

static void testFrontParser() {
  // Legacy hint syntax unchanged
  CHECK_EQ(front("Capital of France \\ Europe \\").frontMain(), std::string("Capital of France"));
  CHECK_EQ(front("Capital of France \\ Europe \\").frontHint(), std::string("Europe"));
  CHECK_EQ(front("No hint here").frontMain(), std::string("No hint here"));
  CHECK_EQ(front("No hint here").frontHint(), std::string(""));
  CHECK_EQ(front("Line/nbreak \\ a/nb \\").frontMain(), std::string("Line\nbreak"));
  CHECK_EQ(front("Line/nbreak \\ a/nb \\").frontHint(), std::string("a\nb"));

  // Escaped backslash
  CHECK_EQ(front("grep '\\\\.conf$'").frontMain(), std::string("grep '\\.conf$'"));
  CHECK_EQ(front("grep '\\\\.conf$'").frontHint(), std::string(""));
  CHECK_EQ(front("a \\\\ b").frontMain(), std::string("a \\ b"));          // "\\ " is not an opener
  CHECK_EQ(front("x \\ h \\\\ y \\").frontHint(), std::string("h \\ y"));  // " \\" is not a closer

  // Stray backslashes render literally instead of truncating
  CHECK_EQ(front("C:\\Windows").frontMain(), std::string("C:\\Windows"));
  CHECK_EQ(front("open \\ never closed").frontMain(), std::string("open \\ never closed"));
  CHECK_EQ(front("open \\ never closed").frontHint(), std::string(""));
  CHECK_EQ(front("ends with \\").frontMain(), std::string("ends with \\"));
}

static void testLiteralSlash() {
  // "\/" keeps a path's "/n" from becoming a line break
  CHECK_EQ(front("O que controla /etc\\/nsswitch.conf?").frontMain(),
           std::string("O que controla /etc/nsswitch.conf?"));
  CHECK_EQ(front("Shell de /sbin\\/nologin \\ dica /mnt\\/nfs \\").frontMain(), std::string("Shell de /sbin/nologin"));
  CHECK_EQ(front("Shell de /sbin\\/nologin \\ dica /mnt\\/nfs \\").frontHint(), std::string("dica /mnt/nfs"));
  CHECK_EQ(FlashcardDeck::decodeNewlines("/usr\\/share\\/nano"), std::string("/usr/share/nano"));
  // Unescaped /n is still a newline, other backslashes are untouched
  CHECK_EQ(FlashcardDeck::decodeNewlines("Linha 1/nLinha 2"), std::string("Linha 1\nLinha 2"));
  CHECK_EQ(FlashcardDeck::decodeNewlines("a\\.b"), std::string("a\\.b"));
}

static void testTestDeck(const std::string& deckPath) {
  FlashcardDeck deck;
  CHECK_EQ(deck.loadFromCsv(deckPath.c_str()), true);
  const auto& c = deck.getCards();
  CHECK_EQ(c.size(), (size_t)7);
  if (c.size() != 7) return;
  CHECK_EQ(c[0].frontMain(), std::string("Contar as linhas de um arquivo"));
  CHECK_EQ(c[0].frontHint(), std::string("coreutils"));
  CHECK_EQ(c[4].frontMain(), std::string("Filtrar linhas terminadas em .conf com grep '\\.conf$'"));
  CHECK_EQ(c[4].frontHint(), std::string(""));
  CHECK_EQ(c[5].frontMain(), std::string("Regex com barra: grep '\\.conf$'"));
  CHECK_EQ(c[5].frontHint(), std::string("escape do ponto"));
  CHECK_EQ(c[5].backContent, std::string("`grep '\\.conf$' arquivos.txt`"));
  CHECK_EQ(c[6].frontMain(), std::string("Quebra\nde linha na frente"));
}

static std::vector<size_t> iota(size_t n) {
  std::vector<size_t> v(n);
  for (size_t i = 0; i < n; ++i) v[i] = i;
  return v;
}

static void testLinearSession() {
  // Shuffle off: CSV order
  FlashcardLinearSession s;
  s.start(7, false, 123);
  CHECK_EQ(s.pass(), (uint16_t)1);
  CHECK_EQ(s.passQueue(), iota(7));
  CHECK_EQ(s.shownPosition(), (size_t)1);

  // Sort: 0,3 missed; 1,5 unsure; rest known
  const LinearPile marks[7] = {LinearPile::Missed, LinearPile::Unsure, LinearPile::Known, LinearPile::Missed,
                               LinearPile::Known,  LinearPile::Unsure, LinearPile::Known};
  for (auto m : marks) {
    CHECK_EQ(s.passDone(), false);
    s.mark(m);
  }
  CHECK_EQ(s.passDone(), true);
  CHECK_EQ(s.missedCount(), (size_t)2);
  CHECK_EQ(s.unsureCount(), (size_t)2);
  CHECK_EQ(s.knownCount(), (size_t)3);
  CHECK_EQ(s.canRepeat(), true);

  // Repeat brings exactly missed + unsure, no duplicates within the pass
  s.startRepeatPass(5);
  CHECK_EQ(s.pass(), (uint16_t)2);
  CHECK_EQ(s.passQueue(), (std::vector<size_t>{0, 1, 3, 5}));
  CHECK_EQ(s.missedCount() + s.unsureCount() + s.knownCount(), (size_t)0);
  for (int i = 0; i < 4; ++i) s.mark(LinearPile::Known);
  CHECK_EQ(s.canRepeat(), false);  // option hidden when both piles empty

  s.startFullPass(9);
  CHECK_EQ(s.pass(), (uint16_t)3);
  CHECK_EQ(s.passQueue(), iota(7));

  // Shuffle on: permutation of all cards, differs between passes/seeds
  FlashcardLinearSession r;
  r.start(50, true, 0xC0FFEE);
  auto p1 = r.passQueue();
  auto sorted = p1;
  std::sort(sorted.begin(), sorted.end());
  CHECK_EQ(sorted, iota(50));
  CHECK_EQ(p1 == iota(50), false);
  r.startFullPass(0xBEEF);
  CHECK_EQ(r.passQueue() == p1, false);
}

// Mirrors FlashcardReviewActivity (Spaced): quota -> queue -> rate -> count.
static size_t runSpacedDeck(FlashcardDeck& deck, uint32_t today, uint8_t perDay, uint16_t& count, uint32_t& date) {
  const uint16_t avail = FlashcardDeck::newCardsAvailable(perDay, count, date, today);
  auto queue = deck.buildReviewQueue(today, (uint8_t)avail, 250);
  size_t newShown = 0;
  for (size_t idx : queue) {
    auto& card = deck.getCards()[idx];
    const bool wasNew = card.isNew();
    deck.updateCard(idx, FlashcardSrs::review(card.srs, SrsRating::Good, today));
    if (wasNew) {
      ++newShown;
      if (today != 0) ++count;
    }
  }
  return newShown;
}

static FlashcardDeck newDeck(size_t n) {
  FlashcardDeck d;
  for (size_t i = 0; i < n; ++i) {
    FlashcardCard c;
    c.id = (uint16_t)(i + 1);
    d.getCards().push_back(c);
  }
  return d;
}

static void testDailyNewLimit() {
  uint16_t count = 7;
  uint32_t date = 9000;  // stale day from before
  const uint32_t today = 9500;
  FlashcardDeck a = newDeck(20), b = newDeck(20);
  const size_t shownA = runSpacedDeck(a, today, 10, count, date);
  const size_t shownB = runSpacedDeck(b, today, 10, count, date);
  CHECK_EQ(shownA + shownB, (size_t)10);  // acceptance 5: 10 in total, not 20
  CHECK_EQ(shownA, (size_t)10);
  CHECK_EQ(count, (uint16_t)10);
  CHECK_EQ(date, today);
  // Reopening the same day brings nothing new
  CHECK_EQ(runSpacedDeck(b, today, 10, count, date), (size_t)0);
  // Next day: counter resets
  CHECK_EQ(runSpacedDeck(b, today + 1, 10, count, date), (size_t)10);
  CHECK_EQ(count, (uint16_t)10);
  // Lowering the limit below today's count never goes negative
  CHECK_EQ(FlashcardDeck::newCardsAvailable(5, count, date, today + 1), (uint16_t)0);
  // Clock not set: old per-open behaviour, counter untouched
  uint16_t c0 = 3;
  uint32_t d0 = 1234;
  CHECK_EQ(FlashcardDeck::newCardsAvailable(10, c0, d0, 0), (uint16_t)10);
  CHECK_EQ(c0, (uint16_t)3);
  CHECK_EQ(d0, (uint32_t)1234);
}

static DeckStats st(uint16_t due, uint16_t fresh) {
  DeckStats d;
  d.dueCards = due;
  d.newCards = fresh;
  d.totalCards = due + fresh;
  return d;
}

static void testSpacedRound() {
  // 0: due, 1: nothing, 2: only new, 3: due+new, 4: only new
  std::vector<DeckStats> stats = {st(3, 0), st(0, 0), st(0, 5), st(2, 4), st(0, 1)};
  std::vector<bool> visited(stats.size(), false);
  // Start manually on deck 3, quota 10: round goes 4, 0, 2 (wraps), then ends
  int cur = 3;
  visited[cur] = true;
  std::vector<int> order;
  uint16_t quota = 10;
  for (int guard = 0; guard < 10; ++guard) {
    const int next = FlashcardDeck::nextSpacedDeck(stats, visited, cur, quota);
    if (next < 0) break;
    CHECK_EQ((bool)visited[next], false);  // never repeats a deck
    order.push_back(next);
    visited[next] = true;
    cur = next;
  }
  CHECK_EQ(order, (std::vector<int>{4, 0, 2}));                             // deck 1 has nothing → skipped
  CHECK_EQ(FlashcardDeck::nextSpacedDeck(stats, visited, cur, quota), -1);  // All caught up

  // Quota exhausted: decks with only new cards no longer qualify
  std::vector<bool> fresh(stats.size(), false);
  fresh[0] = true;
  CHECK_EQ(FlashcardDeck::nextSpacedDeck(stats, fresh, 0, 0), 3);
  fresh[3] = true;
  CHECK_EQ(FlashcardDeck::nextSpacedDeck(stats, fresh, 3, 0), -1);

  // N = due + new allowed, capped by max reviews
  CHECK_EQ(FlashcardDeck::spacedSessionSize(st(2, 4), 3, 250), (uint16_t)5);
  CHECK_EQ(FlashcardDeck::spacedSessionSize(st(2, 4), 0, 250), (uint16_t)2);
  CHECK_EQ(FlashcardDeck::spacedSessionSize(st(300, 4), 10, 250), (uint16_t)250);
}

static void testClock() {
  using FlashcardClock::State;
  const int64_t t2026_09_24_02h = 1790215200;  // 2026-09-24T02:00:00Z
  // UTC-3: 23:00 on the 23rd; UTC: the 24th; one hour later in UTC-3 still the 23rd
  CHECK_EQ(FlashcardClock::dayIndex(t2026_09_24_02h, -180), FlashcardSrs::parseDateStr("23/09/2026"));
  CHECK_EQ(FlashcardClock::dayIndex(t2026_09_24_02h, 0), FlashcardSrs::parseDateStr("24/09/2026"));
  CHECK_EQ(FlashcardClock::dayIndex(t2026_09_24_02h + 3599, -180), FlashcardSrs::parseDateStr("23/09/2026"));
  CHECK_EQ(FlashcardClock::dayIndex(t2026_09_24_02h + 3600, -180), FlashcardSrs::parseDateStr("24/09/2026"));
  CHECK_EQ(FlashcardClock::dayIndex(t2026_09_24_02h + 3 * 3600, -180), FlashcardSrs::parseDateStr("24/09/2026"));
  // Unset clock (1970 after power-on reset) is day 0 territory
  CHECK_EQ(FlashcardClock::evaluate(5000, 0), State::Unset);
  CHECK_EQ(FlashcardClock::evaluate(5000, 1790215200), State::Unset);
  CHECK_EQ(FlashcardClock::evaluate(FlashcardClock::MIN_VALID_UNIX - 1, 1790215200), State::Unset);
  CHECK_EQ(FlashcardClock::dayIndex(0, -180), (uint32_t)0);
  // Synced now / 2 days ago: Ok; 4 days ago or never: Stale; clock behind sync by > 1 day: Stale
  CHECK_EQ(FlashcardClock::evaluate(t2026_09_24_02h, (uint32_t)t2026_09_24_02h), State::Ok);
  CHECK_EQ(FlashcardClock::evaluate(t2026_09_24_02h + 2 * 86400, (uint32_t)t2026_09_24_02h), State::Ok);
  CHECK_EQ(FlashcardClock::evaluate(t2026_09_24_02h + 4 * 86400, (uint32_t)t2026_09_24_02h), State::Stale);
  CHECK_EQ(FlashcardClock::evaluate(t2026_09_24_02h, 0), State::Stale);
  CHECK_EQ(FlashcardClock::evaluate(t2026_09_24_02h - 2 * 86400, (uint32_t)t2026_09_24_02h), State::Stale);
}

static std::string slurp(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

static void testSaveAndRecovery(const std::string& root) {
  namespace fs = std::filesystem;
  fs::create_directories(root + "/flashcard");
  fs::copy_file(root + "/lpic_test.csv", root + "/flashcard/lpic.csv", fs::copy_options::overwrite_existing);

  // Save twice: the second save hits "rename fails because destination exists"
  FlashcardDeck d;
  CHECK_EQ(d.loadFromCsv("/flashcard/lpic.csv"), true);
  const uint32_t today = FlashcardSrs::parseDateStr("24/09/2026");
  d.updateCard(0, FlashcardSrs::review(d.getCards()[0].srs, SrsRating::Good, today));
  CHECK_EQ(d.saveToCsv("/flashcard/lpic.csv"), true);
  d.updateCard(1, FlashcardSrs::review(d.getCards()[1].srs, SrsRating::Easy, today));
  CHECK_EQ(d.saveToCsv("/flashcard/lpic.csv"), true);
  CHECK_EQ(fs::exists(root + "/flashcard/lpic.csv.tmp"), false);
  FlashcardDeck r;
  CHECK_EQ(r.loadFromCsv("/flashcard/lpic.csv"), true);
  CHECK_EQ(r.getCards().size(), (size_t)7);
  if (r.getCards().size() == 7) {
    CHECK_EQ(r.getCards()[0].srs.dueDate, today + 1);
    CHECK_EQ(r.getCards()[0].srs.interval, (uint16_t)1);
    CHECK_EQ(r.getCards()[1].srs.dueDate, today + 4);
    CHECK_EQ(r.getCards()[1].srs.ease, (uint16_t)265);
    CHECK_EQ(r.getCards()[2].isNew(), true);
    // Content round-trips byte for byte (quotes, backslashes, /n)
    CHECK_EQ(r.getCards()[5].frontContent, d.getCards()[5].frontContent);
    CHECK_EQ(r.getCards()[5].backContent, std::string("`grep '\\.conf$' arquivos.txt`"));
  }

  // No NUL byte is ever written, and a file that has one still loads
  CHECK_EQ(slurp(root + "/flashcard/lpic.csv").find('\0'), std::string::npos);
  {
    std::string withNul = slurp(root + "/flashcard/lpic.csv");
    withNul.insert(withNul.find('\n') + 1, 1, '\0');
    std::ofstream(root + "/flashcard/nul.csv", std::ios::binary) << withNul;
    FlashcardDeck n;
    CHECK_EQ(n.loadFromCsv("/flashcard/nul.csv"), true);
    CHECK_EQ(n.getCards().size(), (size_t)7);
    fs::remove(root + "/flashcard/nul.csv");
  }

  // Interrupted save: only the .tmp survived -> listDecks restores it
  const std::string full = slurp(root + "/flashcard/lpic.csv");
  fs::rename(root + "/flashcard/lpic.csv", root + "/flashcard/lpic.csv.tmp");
  auto decks = FlashcardDeck::listDecks();
  CHECK_EQ(decks, (std::vector<std::string>{"/flashcard/lpic.csv"}));
  CHECK_EQ(slurp(root + "/flashcard/lpic.csv"), full);
  CHECK_EQ(fs::exists(root + "/flashcard/lpic.csv.tmp"), false);

  // Save after a failed rename (only the .tmp left): restores it, then saves
  {
    FlashcardDeck d2;
    CHECK_EQ(d2.loadFromCsv("/flashcard/lpic.csv"), true);
    fs::rename(root + "/flashcard/lpic.csv", root + "/flashcard/lpic.csv.tmp");
    CHECK_EQ(d2.saveToCsv("/flashcard/lpic.csv"), true);
    CHECK_EQ(fs::exists(root + "/flashcard/lpic.csv.tmp"), false);
    CHECK_EQ(slurp(root + "/flashcard/lpic.csv"), full);
  }

  // Over the card limit: loads truncated and is never written back
  {
    std::string big = "card_id,front_content,back_content,sr_due,sr_interval,sr_ease\n";
    for (int i = 1; i <= 501; ++i) big += std::to_string(i) + ",f" + std::to_string(i) + ",b,,,\n";
    {
      std::ofstream(root + "/flashcard/big.csv", std::ios::binary) << big;
    }
    FlashcardDeck b;
    CHECK_EQ(b.loadFromCsv("/flashcard/big.csv"), true);
    CHECK_EQ(b.isTruncated(), true);
    CHECK_EQ(b.getCards().size(), MAX_CARDS_PER_DECK);
    CHECK_EQ(b.saveToCsv("/flashcard/big.csv"), false);
    CHECK_EQ(slurp(root + "/flashcard/big.csv"), big);
    FlashcardDeck small;
    CHECK_EQ(small.loadFromCsv("/flashcard/lpic.csv"), true);
    CHECK_EQ(small.isTruncated(), false);
    fs::remove(root + "/flashcard/big.csv");
  }

  // A stale .tmp next to an existing CSV is left alone (the CSV wins)
  {
    std::ofstream(root + "/flashcard/lpic.csv.tmp") << "garbage";
  }
  decks = FlashcardDeck::listDecks();
  CHECK_EQ(decks, (std::vector<std::string>{"/flashcard/lpic.csv"}));
  CHECK_EQ(slurp(root + "/flashcard/lpic.csv"), full);
  fs::remove(root + "/flashcard/lpic.csv.tmp");
}

static void testLinearResume() {
  // Pass 1 of 45 cards, shuffled, stopped after 29 marks (card 30 on screen)
  FlashcardLinearSession s;
  s.start(45, true, 77);
  for (int i = 0; i < 29; ++i)
    s.mark(i % 3 == 0 ? LinearPile::Missed : i % 3 == 1 ? LinearPile::Unsure : LinearPile::Known);
  const auto st = s.exportState();
  CHECK_EQ(s.shownPosition(), (size_t)30);

  FlashcardLinearSession r;
  CHECK_EQ(r.restore(st, 45, true), true);
  CHECK_EQ(r.pass(), (uint16_t)1);
  CHECK_EQ(r.shownPosition(), (size_t)30);
  CHECK_EQ(r.current(), s.current());      // same card on screen
  CHECK_EQ(r.passQueue(), s.passQueue());  // same shuffled order
  CHECK_EQ(r.missedCount() + r.unsureCount() + r.knownCount(), (size_t)29);
  for (int i = 0; i < 16; ++i) r.mark(LinearPile::Known);
  CHECK_EQ(r.passDone(), true);
  CHECK_EQ(r.missedCount(), s.missedCount());

  // Rejected: deck shrank, index out of range, duplicate, position past end, piles inconsistent
  FlashcardLinearSession x;
  CHECK_EQ(x.restore(st, 40, true), false);
  auto bad = st;
  bad.queue[0] = 99;
  CHECK_EQ(x.restore(bad, 45, true), false);
  bad = st;
  bad.queue[1] = bad.queue[0];
  CHECK_EQ(x.restore(bad, 45, true), false);
  bad = st;
  bad.position = 46;
  CHECK_EQ(x.restore(bad, 45, true), false);
  bad = st;
  bad.known.push_back(bad.queue[40]);  // a card not yet shown cannot be in a pile
  CHECK_EQ(x.restore(bad, 45, true), false);
  bad = st;
  bad.pass = 0;
  CHECK_EQ(x.restore(bad, 45, true), false);
  CHECK_EQ(x.pass(), (uint16_t)0);  // failed restores leave the session untouched

  // Finished pass (summary) restores as done
  FlashcardLinearSession done;
  done.start(7, false, 1);
  for (int i = 0; i < 7; ++i) done.mark(LinearPile::Missed);
  FlashcardLinearSession d2;
  CHECK_EQ(d2.restore(done.exportState(), 7, false), true);
  CHECK_EQ(d2.passDone(), true);
  CHECK_EQ(d2.canRepeat(), true);
}

static void testFingerprint(const std::string& root) {
  FlashcardDeck a, b;
  CHECK_EQ(a.loadFromCsv("/lpic_test.csv"), true);
  CHECK_EQ(b.loadFromCsv("/lpic_test.csv"), true);
  CHECK_EQ(a.contentFingerprint(), b.contentFingerprint());
  // SRS changes (Spaced reviews in between) keep the fingerprint
  b.updateCard(0, FlashcardSrs::review(b.getCards()[0].srs, SrsRating::Good, 9000));
  CHECK_EQ(a.contentFingerprint(), b.contentFingerprint());
  // Editing a card's text changes it
  b.getCards()[3].backContent += "!";
  CHECK_EQ(a.contentFingerprint() == b.contentFingerprint(), false);
  (void)root;
}

static void testCatalog(const std::string& root) {
  namespace fs = std::filesystem;
  const std::string fc = root + "/flashcard";
  fs::remove_all(fc);
  fs::create_directories(fc + "/LPIC 101");
  fs::create_directories(fc + "/LPIC 102");
  fs::create_directories(fc + "/.hidden");
  auto put = [&](const std::string& rel, int cards, bool dueAll) {
    std::string csv = "card_id,front_content,back_content,sr_due,sr_interval,sr_ease\n";
    for (int i = 1; i <= cards; ++i) csv += std::to_string(i) + ",f,b," + (dueAll ? "01/01/2026,3,250" : ",,") + "\n";
    std::ofstream(fc + "/" + rel, std::ios::binary) << csv;
  };
  put("avulso.csv", 3, false);
  put("LPIC 101/103.1 comandos.csv", 5, true);
  put("LPIC 101/104.2 filesystems.csv", 2, true);
  put("LPIC 102/105 shell.csv", 4, false);
  put(".hidden/x.csv", 1, true);                                   // hidden folder: ignored
  std::ofstream(fc + "/LPIC 101/._103.1 comandos.csv") << "junk";  // macOS resource fork: ignored
  std::ofstream(fc + "/notas.txt") << "x";                         // not a deck

  CHECK_EQ(FlashcardDeck::listFolders(), (std::vector<std::string>{"LPIC 101", "LPIC 102"}));
  CHECK_EQ(FlashcardCatalog::decksIn(""), (std::vector<std::string>{"/flashcard/avulso.csv"}));
  CHECK_EQ(FlashcardCatalog::decksIn("LPIC 101"),
           (std::vector<std::string>{"/flashcard/LPIC 101/103.1 comandos.csv",
                                     "/flashcard/LPIC 101/104.2 filesystems.csv"}));
  const auto all = FlashcardCatalog::allDecks();
  CHECK_EQ(all.size(), (size_t)4);

  const uint32_t today = FlashcardSrs::parseDateStr("24/09/2026");
  std::vector<FlashcardCatalog::Entry> entries;
  for (const auto& p : all) {
    FlashcardCatalog::Entry e;
    CHECK_EQ(FlashcardCatalog::load(p, today, e), true);
    entries.push_back(e);
  }
  CHECK_EQ(entries[0].folder, std::string(""));
  CHECK_EQ(entries[1].folder, std::string("LPIC 101"));
  CHECK_EQ(entries[1].name, std::string("103.1 comandos"));
  CHECK_EQ(entries[1].stats.dueCards, (uint16_t)5);

  // With new-card quota: most due first (5, 2), then new-only decks by most new (4, 3)
  auto sorted = entries;
  FlashcardCatalog::sortForSpaced(sorted, 10);
  std::vector<std::string> names;
  for (const auto& e : sorted) names.push_back(e.name);
  CHECK_EQ(names, (std::vector<std::string>{"103.1 comandos", "104.2 filesystems", "105 shell", "avulso"}));
  // Quota used up: new-only decks are up to date and go below, by name
  sorted = entries;
  FlashcardCatalog::sortForSpaced(sorted, 0);
  CHECK_EQ(FlashcardCatalog::isPending(sorted[2], 0), false);
  CHECK_EQ(sorted[2].name, std::string("105 shell"));
  CHECK_EQ(sorted[3].name, std::string("avulso"));
  // Decks over the card limit are never pending
  FlashcardCatalog::Entry big = entries[1];
  big.truncated = true;
  CHECK_EQ(FlashcardCatalog::isPending(big, 10), false);
  fs::remove_all(fc);
}

static void testWholeFolder(const std::string& root) {
  namespace fs = std::filesystem;
  const std::string dir = root + "/flashcard/LPIC 101";
  fs::create_directories(dir);
  auto put = [&](const std::string& name, int from, int count) {
    std::string csv = "card_id,front_content,back_content,sr_due,sr_interval,sr_ease\n";
    for (int i = from; i < from + count; ++i)
      csv += std::to_string(i) + ",\"f" + std::to_string(i) + ", x\",b" + std::to_string(i) + ",,,\n";
    std::ofstream(dir + "/" + name, std::ios::binary) << csv;
    return csv;
  };
  const std::string a = put("a.csv", 1, 3);
  const std::string b = put("b.csv", 100, 4);
  const auto paths = FlashcardDeck::listDecks("/flashcard/LPIC 101");

  FlashcardFolderIndex idx;
  CHECK_EQ(idx.build(paths), true);
  CHECK_EQ(idx.size(), (size_t)7);
  CHECK_EQ(idx.isTruncated(), false);
  FlashcardCard c;
  CHECK_EQ(idx.loadCard(0, c), true);
  CHECK_EQ(c.frontContent, std::string("f1, x"));
  CHECK_EQ(idx.loadCard(6, c), true);
  CHECK_EQ(c.frontContent, std::string("f103, x"));
  CHECK_EQ(c.backContent, std::string("b103"));
  CHECK_EQ(idx.loadCard(7, c), false);
  // The CSVs are only read
  CHECK_EQ(slurp(dir + "/a.csv"), a);
  CHECK_EQ(slurp(dir + "/b.csv"), b);
  // Stable fingerprint across builds (Continue works), changes when a card changes
  FlashcardFolderIndex idx2;
  idx2.build(paths);
  CHECK_EQ(idx.fingerprint(), idx2.fingerprint());
  put("b.csv", 100, 5);
  FlashcardFolderIndex idx3;
  idx3.build(FlashcardDeck::listDecks("/flashcard/LPIC 101"));
  CHECK_EQ(idx.fingerprint() == idx3.fingerprint(), false);
  CHECK_EQ(idx3.size(), (size_t)8);

  // Folders larger than one deck's limit: all cards are indexed (600 > 500)
  put("c.csv", 1000, 600);
  FlashcardFolderIndex big;
  CHECK_EQ(big.build(FlashcardDeck::listDecks("/flashcard/LPIC 101")), true);
  CHECK_EQ(big.size(), (size_t)608);
  CHECK_EQ(big.isTruncated(), false);
  CHECK_EQ(big.loadCard(607, c), true);
  CHECK_EQ(c.frontContent, std::string("f1599, x"));
  // Shuffled pass over all of them
  FlashcardLinearSession s;
  s.start(big.size(), true, 99);
  auto q = s.passQueue();
  std::sort(q.begin(), q.end());
  CHECK_EQ(q, iota(608));
  // Low memory: the index stops early instead of failing
  g_testLargestFreeBlock = FLASHCARD_HEAP_RESERVE + 64 * 8;
  FlashcardFolderIndex low;
  const bool lowOk = low.build(FlashcardDeck::listDecks("/flashcard/LPIC 101"));
  g_testLargestFreeBlock = SIZE_MAX;
  CHECK_EQ(lowOk, true);
  CHECK_EQ(low.isTruncated(), true);
  CHECK_EQ(low.size() < (size_t)608, true);
  // No readable deck: fails
  FlashcardFolderIndex none;
  CHECK_EQ(none.build({"/flashcard/missing.csv"}), false);
  fs::remove_all(root + "/flashcard/LPIC 101");
}

// Random CSV text: quotes, doubled quotes, commas, CR/LF inside and outside
// quotes, blank lines, stray NULs, missing and extra fields, long fields that
// cross the reader's 512-byte buffer.
static std::string randomCsv(std::mt19937& rng, int records) {
  auto pick = [&](int n) { return (int)(rng() % (unsigned)n); };
  const char* pieces[] = {"a", "b", "ã", "ç", ",", "\"", "\"\"", "\n", "\r\n", " ", "\\", "/n", "x", "\0", "é"};
  std::string out = pick(4) == 0 ? "\xEF\xBB\xBF" : "";
  out += "card_id,front_content,back_content,sr_due,sr_interval,sr_ease\n";
  for (int r = 0; r < records; ++r) {
    if (pick(8) == 0) out += pick(2) ? "\n" : "\r\n";  // blank line
    const int fields = 1 + pick(8);
    for (int f = 0; f < fields; ++f) {
      if (f) out += ',';
      std::string v;
      if (f == 0 && pick(3)) {
        v = std::to_string(pick(70000));
      } else if (f == 3 && pick(2)) {
        v = std::to_string(1 + pick(28)) + "/" + std::to_string(1 + pick(12)) + "/20" + std::to_string(20 + pick(10));
      } else if ((f == 4 || f == 5) && pick(2)) {
        v = std::to_string(pick(500));
      } else {
        const int len = pick(10) == 0 ? 300 + pick(900) : pick(12);
        for (int k = 0; k < len; ++k) {
          const int pi = pick(15);
          v += (pi == 13) ? std::string(1, '\0') : std::string(pieces[pi]);
        }
      }
      if (pick(2)) {
        std::string q = "\"";
        for (char ch : v) q += (ch == '"') ? std::string("\"\"") : std::string(1, ch);
        q += "\"";
        if (pick(20) == 0) q += "tail";  // garbage after the closing quote
        out += q;
      } else {
        for (char ch : v)
          if (ch != ',' && ch != '\n' && ch != '\r' && ch != '"') out += ch;
      }
    }
    out += pick(3) == 0 ? "\r\n" : "\n";
  }
  if (pick(3) == 0) out.pop_back();  // no final newline
  return out;
}

static void testStreamingParserMatchesReference(const std::string& root) {
  std::mt19937 rng(20260927);
  int compared = 0;
  for (int t = 0; t < 1500; ++t) {
    const int records = (t % 50 == 0) ? 480 + (int)(rng() % 60) : (int)(rng() % 40);
    const std::string csv = randomCsv(rng, records);
    {
      std::ofstream(root + "/fuzz.csv", std::ios::binary) << csv;
    }
    bool refTrunc = false;
    const auto ref = refParse(csv, MAX_CARDS_PER_DECK, refTrunc);
    FlashcardDeck d;
    const bool ok = d.loadFromCsv("/fuzz.csv");
    // The only intended difference: an empty file fails to load.
    if (!ok) {
      CHECK_EQ(csv.empty(), true);
      continue;
    }
    ++compared;
    const auto& cards = d.getCards();
    CHECK_EQ(cards.size(), ref.size());
    CHECK_EQ(d.isTruncated(), refTrunc);
    bool same = cards.size() == ref.size();
    for (size_t i = 0; same && i < cards.size(); ++i) {
      const auto& r = ref[i];
      SrsState srs;
      if (!r.due.empty() && !r.interval.empty() && !r.ease.empty()) {
        srs.dueDate = FlashcardSrs::parseDateStr(r.due.c_str());
        srs.interval = (uint16_t)atoi(r.interval.c_str());
        srs.ease = (uint16_t)std::clamp(atoi(r.ease.c_str()) & 0xFFFF, 130, 400);
      }
      same = cards[i].id == r.id && cards[i].frontContent == r.front && cards[i].backContent == r.back &&
             cards[i].srs.dueDate == srs.dueDate && cards[i].srs.interval == srs.interval &&
             cards[i].srs.ease == srs.ease;
      if (!same) printf("  mismatch in case %d card %zu\n", t, i);
    }
    CHECK_EQ(same, true);
    // Stats scan (no cards kept) agrees with the loaded deck
    const uint32_t today = FlashcardSrs::parseDateStr("15/06/2025");
    DeckStats st;
    bool trunc = false;
    CHECK_EQ(FlashcardDeck::scanStats("/fuzz.csv", today, st, trunc), true);
    const DeckStats want = d.getStats(today);
    CHECK_EQ(st.totalCards, want.totalCards);
    CHECK_EQ(st.newCards, want.newCards);
    CHECK_EQ(st.dueCards, want.dueCards);
    CHECK_EQ(trunc, d.isTruncated());
    // The folder index reads back the same text for every card
    if (!d.isTruncated() && t % 10 == 0) {
      FlashcardFolderIndex idx;
      if (!cards.empty()) {
        CHECK_EQ(idx.build({"/fuzz.csv"}), true);
        CHECK_EQ(idx.size(), cards.size());
        FlashcardCard c;
        bool readBack = true;
        for (size_t i = 0; readBack && i < cards.size(); ++i)
          readBack =
              idx.loadCard(i, c) && c.frontContent == cards[i].frontContent && c.backContent == cards[i].backContent;
        CHECK_EQ(readBack, true);
      }
    }
  }
  CHECK_EQ(compared > 1400, true);
  std::filesystem::remove(root + "/fuzz.csv");
}

static void testLowMemory(const std::string& root) {
  // 300 cards like the English decks; with too little memory the load fails cleanly.
  std::string csv = "card_id,front_content,back_content,sr_due,sr_interval,sr_ease\n";
  for (int i = 1; i <= 300; ++i)
    csv += std::to_string(i) + ",\"What are you going to do? " + std::to_string(i) + "\",\"I am going to study.\",,,\n";
  {
    std::ofstream(root + "/english.csv", std::ios::binary) << csv;
  }
  g_testLargestFreeBlock = 10 * 1024;
  FlashcardDeck d;
  CHECK_EQ(d.loadFromCsv("/english.csv"), false);
  CHECK_EQ(d.outOfMemory(), true);
  CHECK_EQ(d.getCards().empty(), true);
  // Stats need no memory for cards
  DeckStats st;
  bool trunc = false;
  CHECK_EQ(FlashcardDeck::scanStats("/english.csv", 1, st, trunc), true);
  CHECK_EQ(st.totalCards, (uint16_t)300);
  CHECK_EQ(st.newCards, (uint16_t)300);
  g_testLargestFreeBlock = SIZE_MAX;
  CHECK_EQ(d.loadFromCsv("/english.csv"), true);
  CHECK_EQ(d.getCards().size(), (size_t)300);
  CHECK_EQ(d.outOfMemory(), false);
  // Strings are held at their exact size (no doubling slack)
  size_t slack = 0;
  for (const auto& c : d.getCards())
    slack += (c.frontContent.capacity() - c.frontContent.size()) + (c.backContent.capacity() - c.backContent.size());
  CHECK_EQ(slack < 300 * 16, true);
  std::filesystem::remove(root + "/english.csv");
}

static void testFreeDays() {
  using FlashcardClock::avoidFreeDays;
  using FlashcardClock::weekday;
  const uint32_t thu = FlashcardSrs::parseDateStr("24/09/2026");  // a Thursday
  const uint32_t sat = thu + 2, sun = thu + 3, mon = thu + 4, fri = thu + 1;
  CHECK_EQ(weekday(thu), (uint8_t)4);
  CHECK_EQ(weekday(sun), (uint8_t)0);
  CHECK_EQ(weekday(0), (uint8_t)6);  // 2000-01-01 was a Saturday
  constexpr uint8_t SUNDAY = 1u << 0;
  constexpr uint8_t SATURDAY = 1u << 6;

  // No free days: untouched
  CHECK_EQ(avoidFreeDays(sun, thu, 3, 0), sun);
  // Not a free day: untouched
  CHECK_EQ(avoidFreeDays(mon, thu, 4, SUNDAY), mon);
  // Short interval landing on Sunday: later (Monday), never the same day as the review
  CHECK_EQ(avoidFreeDays(sun, sat, 1, SUNDAY), mon);
  CHECK_EQ(avoidFreeDays(sun, fri, 2, SUNDAY), mon);
  // Interval >= 3: one day early (Saturday)
  CHECK_EQ(avoidFreeDays(sun, thu, 3, SUNDAY), sat);
  CHECK_EQ(avoidFreeDays(sun, sun - 30, 30, SUNDAY), sat);
  // Saturday + Sunday free: interval 30 may go 2 days early (Friday); interval 4 goes to Monday
  CHECK_EQ(avoidFreeDays(sun, sun - 30, 30, SATURDAY | SUNDAY), fri);
  CHECK_EQ(avoidFreeDays(sun, sun - 4, 4, SATURDAY | SUNDAY), mon);
  // Again (due today) and past dates are never moved, even on a free day
  CHECK_EQ(avoidFreeDays(sun, sun, 0, SUNDAY), sun);
  // Every day free is ignored (at least one study day must exist)
  CHECK_EQ(avoidFreeDays(sun, thu, 3, 0x7F), sun);
  // Result is never a free day and never before or on the review day, for many cases
  for (uint32_t today = thu; today < thu + 14; ++today) {
    for (uint16_t iv = 1; iv <= 40; ++iv) {
      for (uint8_t mask : {SUNDAY, (uint8_t)(SATURDAY | SUNDAY), (uint8_t)0x3E}) {
        const uint32_t due = avoidFreeDays(today + iv, today, iv, mask);
        CHECK_EQ(FlashcardClock::isFreeDay(due, mask), false);
        CHECK_EQ(due > today, true);
      }
    }
  }
}

int main(int argc, char** argv) {
  Storage.root = argc > 1 ? argv[1] : ".";
  testFrontParser();
  testLiteralSlash();
  testLinearResume();
  testCatalog(Storage.root);
  testFreeDays();
  testWholeFolder(Storage.root);
  testStreamingParserMatchesReference(Storage.root);
  testLowMemory(Storage.root);
  testFingerprint(Storage.root);
  testTestDeck("/lpic_test.csv");
  testLinearSession();
  testDailyNewLimit();
  testSpacedRound();
  testClock();
  testSaveAndRecovery(Storage.root);
  printf("%d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
