// Host-side tests for flashcard logic that does not need the device.
// Build and run: bash test/flashcard/run.sh
#include <HalStorage.h>

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "FlashcardCatalog.h"
#include "FlashcardClock.h"
#include "FlashcardDeck.h"
#include "FlashcardLinearSession.h"
#include "FlashcardSrs.h"

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
    for (int i = from; i < from + count; ++i) csv += std::to_string(i) + ",f" + std::to_string(i) + ",b,,,\n";
    std::ofstream(dir + "/" + name, std::ios::binary) << csv;
    return csv;
  };
  const std::string a = put("a.csv", 1, 3);
  const std::string b = put("b.csv", 100, 4);
  const auto paths = FlashcardDeck::listDecks("/flashcard/LPIC 101");

  FlashcardDeck m;
  CHECK_EQ(m.loadMerged(paths, "LPIC 101", "/flashcard/LPIC 101/"), true);
  CHECK_EQ(m.getCards().size(), (size_t)7);
  CHECK_EQ(m.getName(), std::string("LPIC 101"));
  CHECK_EQ(m.isTruncated(), false);
  CHECK_EQ(m.getCards()[0].frontContent, std::string("f1"));
  CHECK_EQ(m.getCards()[6].frontContent, std::string("f103"));
  CHECK_EQ(m.saveToCsv("/flashcard/LPIC 101/a.csv"), false);  // merged decks are read-only
  CHECK_EQ(slurp(dir + "/a.csv"), a);
  CHECK_EQ(slurp(dir + "/b.csv"), b);
  // Stable fingerprint across loads (Continue works), changes if a deck changes
  FlashcardDeck m2;
  m2.loadMerged(paths, "LPIC 101", "/flashcard/LPIC 101/");
  CHECK_EQ(m.contentFingerprint(), m2.contentFingerprint());

  // One pass over every card of the folder, shuffled
  FlashcardLinearSession s;
  s.start(m.getCards().size(), true, 4242);
  auto q = s.passQueue();
  std::sort(q.begin(), q.end());
  CHECK_EQ(q, iota(7));

  // Over the card limit: exactly MAX cards, flagged truncated
  put("c.csv", 1000, 498);
  FlashcardDeck big;
  CHECK_EQ(big.loadMerged(FlashcardDeck::listDecks("/flashcard/LPIC 101"), "LPIC 101", "/flashcard/LPIC 101/"), true);
  CHECK_EQ(big.getCards().size(), MAX_CARDS_PER_DECK);
  CHECK_EQ(big.isTruncated(), true);
  // Exactly at the limit is not truncated
  fs::remove(dir + "/c.csv");
  put("c.csv", 1000, 493);
  FlashcardDeck exact;
  exact.loadMerged(FlashcardDeck::listDecks("/flashcard/LPIC 101"), "LPIC 101", "/flashcard/LPIC 101/");
  CHECK_EQ(exact.getCards().size(), MAX_CARDS_PER_DECK);
  CHECK_EQ(exact.isTruncated(), false);
  // No readable deck: fails
  FlashcardDeck none;
  CHECK_EQ(none.loadMerged({"/flashcard/missing.csv"}, "x", "/flashcard/x/"), false);
  fs::remove_all(root + "/flashcard/LPIC 101");
}

int main(int argc, char** argv) {
  Storage.root = argc > 1 ? argv[1] : ".";
  testFrontParser();
  testLiteralSlash();
  testLinearResume();
  testCatalog(Storage.root);
  testWholeFolder(Storage.root);
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
