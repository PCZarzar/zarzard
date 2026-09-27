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
#include "FlashcardDeckOptions.h"
#include "FlashcardFolderIndex.h"
#include "FlashcardHistory.h"
#include "FlashcardLinearSession.h"
#include "FlashcardMemory.h"
#include "FlashcardPeriods.h"
#include "FlashcardPlanner.h"
#include "FlashcardSrs.h"
#include "FlashcardStatsScan.h"
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

  // With new cards left in their share: most due first (5, 2), then new-only decks by most new (4, 3)
  auto sorted = entries;
  for (auto& e : sorted) e.newToday = 10;
  FlashcardCatalog::sortForSpaced(sorted);
  std::vector<std::string> names;
  for (const auto& e : sorted) names.push_back(e.name);
  CHECK_EQ(names, (std::vector<std::string>{"103.1 comandos", "104.2 filesystems", "105 shell", "avulso"}));
  // Share used up: new-only decks are up to date and go below, by name
  sorted = entries;
  FlashcardCatalog::sortForSpaced(sorted);
  CHECK_EQ(FlashcardCatalog::isPending(sorted[2]), false);
  CHECK_EQ(sorted[2].name, std::string("105 shell"));
  CHECK_EQ(sorted[3].name, std::string("avulso"));
  // Decks over the card limit are never pending
  FlashcardCatalog::Entry big = entries[1];
  big.truncated = true;
  big.newToday = 10;
  CHECK_EQ(FlashcardCatalog::isPending(big), false);
  // Stand-by decks are never pending; Reviews only still has its due cards
  FlashcardCatalog::Entry parked = entries[1];
  parked.state = FlashcardDeckState::StandBy;
  CHECK_EQ(FlashcardCatalog::isPending(parked), false);
  parked.state = FlashcardDeckState::ReviewsOnly;
  CHECK_EQ(FlashcardCatalog::isPending(parked), true);
  // describe() gives name and folder from the path alone
  FlashcardCatalog::Entry d;
  FlashcardCatalog::describe("/flashcard/LPIC 102/105 shell.csv", d);
  CHECK_EQ(d.name, std::string("105 shell"));
  CHECK_EQ(d.folder, std::string("LPIC 102"));
  FlashcardCatalog::describe("/flashcard/avulso.csv", d);
  CHECK_EQ(d.folder, std::string(""));
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

// ─── Spaced planning ─────────────────────────────────────────────────────────

static FlashcardPlanner::PlanDeck pd(const std::string& path, uint8_t share, uint16_t fresh,
                                     FlashcardDeckState state = FlashcardDeckState::Active) {
  FlashcardPlanner::PlanDeck d;
  d.path = path;
  d.share = share;
  d.newCards = fresh;
  d.state = state;
  return d;
}

static uint16_t allotOf(const std::vector<FlashcardPlanner::Allot>& a, const std::string& path) {
  for (const auto& x : a)
    if (x.path == path) return x.allot;
  return 0;
}

static uint32_t allotSum(const std::vector<FlashcardPlanner::Allot>& a) {
  uint32_t s = 0;
  for (const auto& x : a) s += x.allot;
  return s;
}

static void testPlannerSplit() {
  using FlashcardPlanner::split;
  // 5 decks x share 5, total 30: the 5 left over are spread, 6 each
  std::vector<FlashcardPlanner::PlanDeck> five;
  for (int i = 0; i < 5; ++i) five.push_back(pd("d" + std::to_string(i), 5, 100));
  auto a = split(five, {}, 30, 0, 1);
  CHECK_EQ(allotSum(a), (uint32_t)30);
  for (int i = 0; i < 5; ++i) CHECK_EQ(allotOf(a, "d" + std::to_string(i)), (uint16_t)6);
  // Total equal to the shares: exactly the shares
  a = split(five, {}, 25, 0, 1);
  for (int i = 0; i < 5; ++i) CHECK_EQ(allotOf(a, "d" + std::to_string(i)), (uint16_t)5);
  // 10 decks x 5 = 50 > 30: cut in proportion, 3 each
  std::vector<FlashcardPlanner::PlanDeck> ten;
  for (int i = 0; i < 10; ++i) ten.push_back(pd("t" + std::to_string(i), 5, 100));
  a = split(ten, {}, 30, 0, 7);
  for (int i = 0; i < 10; ++i) CHECK_EQ(allotOf(a, "t" + std::to_string(i)), (uint16_t)3);
  // Remainder not divisible: 32 over 10 → two decks get 4, never more than one apart
  a = split(ten, {}, 32, 0, 7);
  CHECK_EQ(allotSum(a), (uint32_t)32);
  int fours = 0;
  for (int i = 0; i < 10; ++i) {
    const uint16_t v = allotOf(a, "t" + std::to_string(i));
    CHECK_EQ(v == 3 || v == 4, true);
    fours += v == 4;
  }
  CHECK_EQ(fours, 2);
  // Ties are broken by the seed: some seed gives a different pair
  bool differs = false;
  for (uint32_t seed = 2; seed < 40 && !differs; ++seed) {
    const auto b = split(ten, {}, 32, 0, seed);
    for (int i = 0; i < 10; ++i) differs |= allotOf(b, "t" + std::to_string(i)) != allotOf(a, "t" + std::to_string(i));
  }
  CHECK_EQ(differs, true);
  // English share 10 + four LPIC share 5, total 40: 13 and 7, 7, 7, 6
  std::vector<FlashcardPlanner::PlanDeck> mix = {pd("en", 10, 600), pd("l1", 5, 100), pd("l2", 5, 100),
                                                 pd("l3", 5, 100), pd("l4", 5, 100)};
  a = split(mix, {}, 40, 0, 3);
  CHECK_EQ(allotOf(a, "en"), (uint16_t)13);
  std::vector<uint16_t> l = {allotOf(a, "l1"), allotOf(a, "l2"), allotOf(a, "l3"), allotOf(a, "l4")};
  std::sort(l.begin(), l.end());
  CHECK_EQ(l, (std::vector<uint16_t>{6, 7, 7, 7}));
  // Proportional without favouring the bigger share (Sainte-Laguë, not D'Hondt):
  // shares 5 and 1, total 4 → 3 and 1 (D'Hondt would give 4 and 0)
  std::vector<FlashcardPlanner::PlanDeck> skew = {pd("big", 5, 100), pd("small", 1, 100)};
  for (uint32_t seed = 1; seed < 6; ++seed) {
    a = split(skew, {}, 4, 0, seed);
    CHECK_EQ(allotOf(a, "big"), (uint16_t)3);
    CHECK_EQ(allotOf(a, "small"), (uint16_t)1);
  }
  // A deck running out of new cards: its part goes to the others
  std::vector<FlashcardPlanner::PlanDeck> cap = {pd("a", 5, 2), pd("b", 5, 100), pd("c", 5, 100)};
  a = split(cap, {}, 30, 0, 1);
  CHECK_EQ(allotOf(a, "a"), (uint16_t)2);
  CHECK_EQ(allotOf(a, "b"), (uint16_t)14);
  CHECK_EQ(allotOf(a, "c"), (uint16_t)14);
  // Not enough new cards anywhere: everything that exists, no more
  std::vector<FlashcardPlanner::PlanDeck> few = {pd("a", 5, 2), pd("b", 5, 3)};
  a = split(few, {}, 30, 0, 1);
  CHECK_EQ(allotSum(a), (uint32_t)5);
  // Stand-by, Reviews only, over the limit and decks without new cards get nothing
  auto trunc = pd("tr", 5, 50);
  trunc.spacedOk = false;
  std::vector<FlashcardPlanner::PlanDeck> states = {pd("act", 5, 50), pd("sb", 5, 50, FlashcardDeckState::StandBy),
                                                    pd("ro", 5, 50, FlashcardDeckState::ReviewsOnly), trunc,
                                                    pd("empty", 5, 0)};
  a = split(states, {}, 20, 0, 1);
  CHECK_EQ(allotOf(a, "act"), (uint16_t)20);
  CHECK_EQ(a.size(), (size_t)1);
  CHECK_EQ(FlashcardPlanner::shareSum(states), (uint32_t)5);
  // Same day, new split: what was used stays, the total is respected
  std::vector<FlashcardPlanner::Allot> prev = {{"d0", 6, 6}, {"d1", 6, 2}, {"gone", 3, 3}};
  a = split(five, prev, 30, 11, 1);
  CHECK_EQ(allotSum(a), (uint32_t)30);
  uint32_t used = 0;
  for (const auto& x : a) {
    used += x.used;
    CHECK_EQ(x.allot >= x.used, true);
  }
  CHECK_EQ(used, (uint32_t)11);
  CHECK_EQ(allotOf(a, "gone"), (uint16_t)3);  // a deck no longer listed keeps its count
  // Fair over the day: d0 already had 6, so the others catch up first
  CHECK_EQ(allotOf(a, "d0"), (uint16_t)6);
  // Global counter above the sum of used (cards from before the split): counted too
  a = split(five, {}, 30, 28, 1);
  CHECK_EQ(allotSum(a), (uint32_t)2);
  a = split(five, {}, 30, 40, 1);  // total lowered below what was already done
  CHECK_EQ(allotSum(a), (uint32_t)0);
  CHECK_EQ(FlashcardPlanner::remaining(a, "d0"), (uint16_t)0);
  CHECK_EQ(FlashcardPlanner::remaining(a, "nope"), (uint16_t)0);
  // Default state: never studied → stand-by; any studied card → Active
  DeckStats fresh;
  fresh.totalCards = 300;
  fresh.newCards = 300;
  CHECK_EQ(FlashcardPlanner::defaultState(fresh) == FlashcardDeckState::StandBy, true);
  fresh.newCards = 299;
  CHECK_EQ(FlashcardPlanner::defaultState(fresh) == FlashcardDeckState::Active, true);
  CHECK_EQ(FlashcardPlanner::nextState(FlashcardDeckState::Active) == FlashcardDeckState::ReviewsOnly, true);
  CHECK_EQ(FlashcardPlanner::nextState(FlashcardDeckState::ReviewsOnly) == FlashcardDeckState::StandBy, true);
  CHECK_EQ(FlashcardPlanner::nextState(FlashcardDeckState::StandBy) == FlashcardDeckState::Active, true);
}

static void resetOptions() {
  JsonDocument empty;
  FLASHCARD_DECK_OPTIONS.fromJson(empty.as<JsonVariantConst>());
}

static void testDeckOptions(const std::string& root) {
  auto& o = FLASHCARD_DECK_OPTIONS;
  std::filesystem::remove_all(root + "/.crosspoint");
  resetOptions();
  DeckStats studied;
  studied.totalCards = 10;
  studied.newCards = 4;
  DeckStats unseen;
  unseen.totalCards = 10;
  unseen.newCards = 10;
  CHECK_EQ(o.stateFor("/flashcard/a.csv", studied) == FlashcardDeckState::Active, true);
  CHECK_EQ(o.stateFor("/flashcard/b.csv", unseen) == FlashcardDeckState::StandBy, true);
  CHECK_EQ(o.shareFor("/flashcard/a.csv"), FlashcardPlanner::DEFAULT_SHARE);
  o.set("/flashcard/b.csv", FlashcardDeckState::Active, 12);
  o.set("/flashcard/c.csv", FlashcardDeckState::ReviewsOnly, 200);  // clamped
  CHECK_EQ(o.stateFor("/flashcard/b.csv", unseen) == FlashcardDeckState::Active, true);
  CHECK_EQ(o.shareFor("/flashcard/b.csv"), (uint8_t)12);
  CHECK_EQ(o.shareFor("/flashcard/c.csv"), FlashcardPlanner::SHARE_MAX);

  const uint32_t day = 9700;
  std::vector<FlashcardPlanner::PlanDeck> decks = {pd("/flashcard/a.csv", 5, 100), pd("/flashcard/b.csv", 12, 100)};
  CHECK_EQ(o.refreshSplit(decks, day, 17, 0, 5), true);
  CHECK_EQ(o.refreshSplit(decks, day, 17, 0, 5), false);  // already done today
  CHECK_EQ(o.newRemaining("/flashcard/a.csv", day), (uint16_t)5);
  CHECK_EQ(o.newRemaining("/flashcard/b.csv", day), (uint16_t)12);
  CHECK_EQ(o.newRemaining("/flashcard/a.csv", day + 1), (uint16_t)0);  // no split for that day
  CHECK_EQ(o.newRemaining("/flashcard/a.csv", 0), (uint16_t)0);
  o.recordNew("/flashcard/a.csv", day);
  o.recordNew("/flashcard/a.csv", day);
  CHECK_EQ(o.newRemaining("/flashcard/a.csv", day), (uint16_t)3);
  o.recordLinearPass("/flashcard/a.csv", 17, 20);
  o.recordLinearPass("/flashcard/a.csv", 2, 3);
  o.recordLinearPass("/flashcard/z.csv", 0, 0);  // empty pass: ignored

  // Save, forget, load: everything comes back
  CHECK_EQ(o.saveToFile(), true);
  resetOptions();
  CHECK_EQ(o.shareFor("/flashcard/b.csv"), FlashcardPlanner::DEFAULT_SHARE);
  CHECK_EQ(o.loadFromFile(), true);
  CHECK_EQ(o.shareFor("/flashcard/b.csv"), (uint8_t)12);
  CHECK_EQ(o.stateFor("/flashcard/c.csv", studied) == FlashcardDeckState::ReviewsOnly, true);
  CHECK_EQ(o.newRemaining("/flashcard/a.csv", day), (uint16_t)3);
  const auto* lin = o.linearResult("/flashcard/a.csv");
  CHECK_EQ(lin != nullptr, true);
  if (lin) {
    CHECK_EQ(lin->passes, (uint16_t)2);
    CHECK_EQ(lin->lastKnownPct, (uint8_t)67);
  }
  CHECK_EQ(o.linearResult("/flashcard/z.csv") == nullptr, true);

  // A changed choice redoes the split the same day, keeping what was used
  o.set("/flashcard/b.csv", FlashcardDeckState::StandBy, 12);
  decks[1].state = FlashcardDeckState::StandBy;
  CHECK_EQ(o.refreshSplit(decks, day, 17, 2, 5), true);
  CHECK_EQ(o.newRemaining("/flashcard/a.csv", day), (uint16_t)15);  // 17 - 2 used, all to a
  CHECK_EQ(o.newRemaining("/flashcard/b.csv", day), (uint16_t)0);
  // Setting the same values again changes nothing
  CHECK_EQ(o.refreshSplit(decks, day, 17, 2, 5), false);
  o.set("/flashcard/b.csv", FlashcardDeckState::StandBy, 12);
  CHECK_EQ(o.refreshSplit(decks, day, 17, 2, 5), false);
  // Total changed in Settings: invalidate, same day
  o.invalidateSplit();
  CHECK_EQ(o.refreshSplit(decks, day, 10, 2, 5), true);
  CHECK_EQ(o.newRemaining("/flashcard/a.csv", day), (uint16_t)8);
  // Next day: starts over
  CHECK_EQ(o.refreshSplit(decks, day + 1, 10, 0, 5), true);
  CHECK_EQ(o.newRemaining("/flashcard/a.csv", day + 1), (uint16_t)10);
  // Clock not set: no split
  CHECK_EQ(o.refreshSplit(decks, 0, 10, 0, 5), false);

  // Prune drops decks that are gone
  CHECK_EQ(o.prune({"/flashcard/a.csv", "/flashcard/b.csv"}), true);
  CHECK_EQ(o.shareFor("/flashcard/c.csv"), FlashcardPlanner::DEFAULT_SHARE);
  CHECK_EQ(o.prune({"/flashcard/a.csv", "/flashcard/b.csv"}), false);
  CHECK_EQ(o.prune({"/flashcard/b.csv"}), true);  // a: only a linear result left
  CHECK_EQ(o.linearResult("/flashcard/a.csv") == nullptr, true);

  // Damaged or unknown files do not load and leave defaults
  std::ofstream(root + "/.crosspoint/flashcard_decks.json") << "{not json";
  CHECK_EQ(o.loadFromFile(), false);
  std::ofstream(root + "/.crosspoint/flashcard_decks.json") << "{\"v\":99,\"decks\":[{\"p\":\"/x\",\"s\":1}]}";
  CHECK_EQ(o.loadFromFile(), false);
  CHECK_EQ(o.getOptions().empty(), true);
  // Out-of-range values are clamped
  std::ofstream(root + "/.crosspoint/flashcard_decks.json")
      << "{\"v\":1,\"decks\":[{\"p\":\"/x\",\"s\":9,\"n\":0}],\"linear\":[{\"p\":\"/x\",\"c\":1,\"k\":250}],"
         "\"split\":[{\"p\":\"/x\",\"a\":1,\"u\":5}],\"splitDay\":5,\"splitValid\":true}";
  CHECK_EQ(o.loadFromFile(), true);
  CHECK_EQ(o.stateFor("/x", studied) == FlashcardDeckState::StandBy, true);
  CHECK_EQ(o.shareFor("/x"), FlashcardPlanner::SHARE_MIN);
  CHECK_EQ(o.linearResult("/x")->lastKnownPct, (uint8_t)100);
  CHECK_EQ(o.newRemaining("/x", 5), (uint16_t)0);  // allot raised to used
  resetOptions();
  std::filesystem::remove_all(root + "/.crosspoint");
}

static void testCatalogPlan(const std::string& root) {
  namespace fs = std::filesystem;
  const std::string fc = root + "/flashcard";
  fs::remove_all(fc);
  fs::create_directories(fc + "/English");
  auto put = [&](const std::string& rel, int cards, int studied) {
    std::string csv = "card_id,front_content,back_content,sr_due,sr_interval,sr_ease\n";
    for (int i = 1; i <= cards; ++i)
      csv += std::to_string(i) + ",f,b," + (i <= studied ? "01/01/2026,3,250" : ",,") + "\n";
    std::ofstream(fc + "/" + rel, std::ios::binary) << csv;
  };
  put("lpic.csv", 20, 5);         // studied → Active by default
  put("English/g1.csv", 300, 0);  // never studied → stand-by
  put("English/g2.csv", 300, 0);
  resetOptions();
  const uint32_t today = FlashcardSrs::parseDateStr("27/09/2026");
  std::vector<FlashcardCatalog::Entry> entries;
  for (const auto& p : FlashcardCatalog::allDecks()) {
    FlashcardCatalog::Entry e;
    CHECK_EQ(FlashcardCatalog::load(p, today, e), true);
    entries.push_back(e);
  }
  CHECK_EQ(FlashcardCatalog::applyPlan(entries, FLASHCARD_DECK_OPTIONS, today, 30, 0, 1), true);
  CHECK_EQ(entries[0].state == FlashcardDeckState::Active, true);
  CHECK_EQ(entries[1].state == FlashcardDeckState::StandBy, true);
  CHECK_EQ(entries[0].newToday, (uint16_t)15);  // only 15 new cards left in it
  CHECK_EQ(entries[1].newToday, (uint16_t)0);
  // Activating English: the 30 are shared again (15 + 15 left for 2 decks of share 5)
  FLASHCARD_DECK_OPTIONS.set(entries[1].path, FlashcardDeckState::Active, 5);
  FLASHCARD_DECK_OPTIONS.set(entries[2].path, FlashcardDeckState::Active, 5);
  CHECK_EQ(FlashcardCatalog::applyPlan(entries, FLASHCARD_DECK_OPTIONS, today, 30, 0, 1), true);
  CHECK_EQ(entries[0].newToday + entries[1].newToday + entries[2].newToday, 30);
  CHECK_EQ(entries[0].newToday, (uint16_t)10);
  resetOptions();
  fs::remove_all(fc);
}

// ─── History ─────────────────────────────────────────────────────────────────

static uint32_t fileSize(const std::string& p) {
  std::error_code ec;
  const auto n = std::filesystem::file_size(p, ec);
  return ec ? 0 : (uint32_t)n;
}

static void testHistory(const std::string& root) {
  namespace fs = std::filesystem;
  const std::string file = root + FlashcardHistory::PATH;
  fs::remove_all(root + "/.crosspoint");
  FlashcardHistory::resetForTest();

  // Encoding round trip; damage is detected
  FlashcardDayRecord r;
  r.day = 9700;
  r.again = 1;
  r.hard = 2;
  r.good = 300;
  r.easy = 4;
  r.newCards = 5;
  r.reviewCards = 6;
  r.reviewCorrect = 7;
  r.matureReviews = 8;
  r.matureCorrect = 9;
  r.linearCards = 65535;
  r.spacedSeconds = 11;
  r.linearSeconds = 12;
  uint8_t buf[FlashcardHistory::RECORD_BYTES];
  FlashcardHistory::encode(r, buf);
  FlashcardDayRecord back;
  CHECK_EQ(FlashcardHistory::decode(buf, back), true);
  CHECK_EQ(back.good, (uint16_t)300);
  CHECK_EQ(back.linearCards, (uint16_t)65535);
  CHECK_EQ(back.linearSeconds, (uint16_t)12);
  CHECK_EQ(back.cards(), (uint32_t)(307 + 65535));
  buf[5] ^= 0x10;
  CHECK_EQ(FlashcardHistory::decode(buf, back), false);
  FlashcardDayRecord zero;
  FlashcardHistory::encode(zero, buf);
  CHECK_EQ(FlashcardHistory::decode(buf, back), false);  // day 0 is never valid
  // Saturating sums
  FlashcardDayRecord big = r;
  big.add(r);
  CHECK_EQ(big.linearCards, (uint16_t)65535);
  CHECK_EQ(big.good, (uint16_t)600);

  // Nothing is recorded without a date
  FlashcardHistory::recordLinear(0, 5);
  CHECK_EQ(FlashcardHistory::flush(), true);
  CHECK_EQ(fs::exists(file), false);

  const uint32_t d1 = FlashcardSrs::parseDateStr("21/09/2026");  // Monday
  // New card: counted as new, not in retention. Review card missed / right. Mature card.
  FlashcardHistory::recordSpaced(d1, SrsRating::Again, true, 0, 12);
  FlashcardHistory::recordSpaced(d1, SrsRating::Good, false, 0, 500);  // relearning: interval 0, time capped
  FlashcardHistory::recordSpaced(d1, SrsRating::Again, false, 3, 5);
  FlashcardHistory::recordSpaced(d1, SrsRating::Good, false, 4, 5);
  FlashcardHistory::recordSpaced(d1, SrsRating::Easy, false, 30, 5);
  FlashcardHistory::recordLinear(d1, 7);
  CHECK_EQ(FlashcardHistory::flush(), true);
  CHECK_EQ(fileSize(file), (uint32_t)FlashcardHistory::RECORD_BYTES);
  auto recs = FlashcardHistory::readRange(d1, d1);
  CHECK_EQ(recs.size(), (size_t)1);
  if (!recs.empty()) {
    const auto& x = recs[0];
    CHECK_EQ(x.spaced(), (uint32_t)5);
    CHECK_EQ(x.again, (uint16_t)2);
    CHECK_EQ(x.newCards, (uint16_t)1);
    CHECK_EQ(x.reviewCards, (uint16_t)3);  // interval >= 1 before the rating
    CHECK_EQ(x.reviewCorrect, (uint16_t)2);
    CHECK_EQ(x.matureReviews, (uint16_t)1);
    CHECK_EQ(x.matureCorrect, (uint16_t)1);
    CHECK_EQ(x.linearCards, (uint16_t)1);
    CHECK_EQ(x.spacedSeconds, (uint16_t)(12 + 60 + 15));
    CHECK_EQ(x.linearSeconds, (uint16_t)7);
  }

  // Later the same day (device restarted): counts continue, still one record
  FlashcardHistory::resetForTest();
  FlashcardHistory::recordLinear(d1, 3);
  CHECK_EQ(FlashcardHistory::flush(), true);
  CHECK_EQ(fileSize(file), (uint32_t)FlashcardHistory::RECORD_BYTES);
  recs = FlashcardHistory::readRange(d1, d1);
  CHECK_EQ(recs.empty() ? 0u : (unsigned)recs[0].linearCards, 2u);
  CHECK_EQ(recs.empty() ? 0u : (unsigned)recs[0].spaced(), 5u);

  // Next days append; unflushed RAM state is visible to readRange
  for (uint32_t d = d1 + 1; d <= d1 + 5; ++d) FlashcardHistory::recordLinear(d, 1);  // Tue..Sat
  CHECK_EQ(FlashcardHistory::readRange(d1, d1 + 10).size(), (size_t)6);
  CHECK_EQ(FlashcardHistory::flush(), true);
  CHECK_EQ(fileSize(file), (uint32_t)FlashcardHistory::RECORD_BYTES * 6);
  CHECK_EQ(FlashcardHistory::readRange(d1 + 2, d1 + 3).size(), (size_t)2);
  CHECK_EQ(FlashcardHistory::readRange(d1 + 3, d1 + 2).size(), (size_t)0);

  // Streaks: Sunday (d1+6) is a free day with no study, Monday (d1+7) studied
  const uint8_t sundayFree = 1;
  FlashcardHistory::recordLinear(d1 + 7, 1);
  CHECK_EQ(FlashcardHistory::flush(), true);
  auto sum = FlashcardHistory::summarize(d1 + 7, sundayFree);
  CHECK_EQ(sum.currentStreak, (uint16_t)7);
  CHECK_EQ(sum.longestStreak, (uint16_t)7);
  CHECK_EQ(sum.firstDay, d1);
  CHECK_EQ(sum.bestDay, d1);
  CHECK_EQ(sum.bestDayCards, (uint32_t)7);
  // Without the free day the gap breaks it
  sum = FlashcardHistory::summarize(d1 + 7, 0);
  CHECK_EQ(sum.currentStreak, (uint16_t)1);
  CHECK_EQ(sum.longestStreak, (uint16_t)6);
  // Today not studied yet: the streak up to yesterday still counts
  sum = FlashcardHistory::summarize(d1 + 8, 0);
  CHECK_EQ(sum.currentStreak, (uint16_t)1);
  // Two days without study: gone
  sum = FlashcardHistory::summarize(d1 + 9, 0);
  CHECK_EQ(sum.currentStreak, (uint16_t)0);
  CHECK_EQ(sum.longestStreak, (uint16_t)6);

  // Clock set back: an earlier day recorded after later ones still reads in order and merges
  FlashcardHistory::recordLinear(d1 + 2, 1);
  CHECK_EQ(FlashcardHistory::flush(), true);
  recs = FlashcardHistory::readRange(d1, d1 + 10);
  CHECK_EQ(recs.size(), (size_t)7);
  bool ascending = true;
  for (size_t i = 1; i < recs.size(); ++i) ascending &= recs[i - 1].day < recs[i].day;
  CHECK_EQ(ascending, true);
  CHECK_EQ(FlashcardHistory::readRange(d1 + 2, d1 + 2)[0].linearCards, (uint16_t)2);

  // A torn write at the end (partial record) is ignored and dropped by the next flush
  const uint32_t before = fileSize(file);
  {
    std::ofstream(file, std::ios::binary | std::ios::app) << "garbage";
  }
  CHECK_EQ(FlashcardHistory::readRange(d1, d1 + 10).size(), (size_t)7);
  FlashcardHistory::resetForTest();
  FlashcardHistory::recordLinear(d1 + 20, 1);
  CHECK_EQ(FlashcardHistory::flush(), true);
  CHECK_EQ(fileSize(file), before + (uint32_t)FlashcardHistory::RECORD_BYTES);
  CHECK_EQ(FlashcardHistory::readRange(d1, d1 + 30).size(), (size_t)8);
  // A damaged record in the middle is skipped, the others survive
  {
    std::fstream f(file, std::ios::binary | std::ios::in | std::ios::out);
    f.seekp(FlashcardHistory::RECORD_BYTES + 3);
    f.put('\x5A');
  }
  CHECK_EQ(FlashcardHistory::readRange(d1, d1 + 30).size(), (size_t)7);
  // Interrupted flush (only the .tmp left): recovered
  fs::rename(file, file + ".tmp");
  CHECK_EQ(FlashcardHistory::readRange(d1, d1 + 30).size(), (size_t)7);
  CHECK_EQ(fs::exists(file), true);
  FlashcardHistory::resetForTest();
  fs::remove_all(root + "/.crosspoint");
}

static void testPeriods() {
  using namespace FlashcardPeriods;
  const auto D = [](const char* s) { return FlashcardSrs::parseDateStr(s); };
  CHECK_EQ(weekStart(D("27/09/2026")), D("21/09/2026"));  // Sunday → Monday before
  CHECK_EQ(weekStart(D("21/09/2026")), D("21/09/2026"));
  CHECK_EQ(weekStart(D("22/09/2026")), D("21/09/2026"));
  CHECK_EQ(monthStart(D("27/09/2026")), D("01/09/2026"));
  CHECK_EQ(monthStart(D("01/03/2028")), D("01/03/2028"));
  CHECK_EQ(nextStart(Scale::Months, D("01/01/2026")), D("01/02/2026"));
  CHECK_EQ(nextStart(Scale::Months, D("01/02/2026")), D("01/03/2026"));
  CHECK_EQ(nextStart(Scale::Months, D("01/02/2028")), D("01/03/2028"));  // leap year
  CHECK_EQ(nextStart(Scale::Months, D("01/12/2026")), D("01/01/2027"));
  CHECK_EQ(previousStart(Scale::Months, D("01/03/2026")), D("01/02/2026"));
  CHECK_EQ(previousStart(Scale::Months, D("01/01/2027")), D("01/12/2026"));
  CHECK_EQ(previousStart(Scale::Weeks, D("21/09/2026")), D("14/09/2026"));

  const uint32_t today = D("27/09/2026");
  auto b = buckets(Scale::Days, today, 30, 0);
  CHECK_EQ(b.size(), (size_t)30);
  CHECK_EQ(b.back().start, today);
  CHECK_EQ(b.front().start, today - 29);
  b = buckets(Scale::Days, today, 30, 1);
  CHECK_EQ(b.back().end, today - 30);
  b = buckets(Scale::Weeks, today, 12, 0);
  CHECK_EQ(b.back().start, D("21/09/2026"));
  CHECK_EQ(b.back().end, D("27/09/2026"));
  bool contiguous = true;
  for (size_t i = 1; i < b.size(); ++i) contiguous &= b[i].start == b[i - 1].end + 1;
  CHECK_EQ(contiguous, true);
  b = buckets(Scale::Months, today, 12, 0);
  CHECK_EQ(b.back().start, D("01/09/2026"));
  CHECK_EQ(b.back().end, D("30/09/2026"));
  CHECK_EQ(b.front().start, D("01/10/2025"));
  b = buckets(Scale::Months, today, 12, 1);
  CHECK_EQ(b.back().start, D("01/09/2025"));
  CHECK_EQ(b.back().end, D("30/09/2025"));

  std::vector<FlashcardDayRecord> recs(3);
  recs[0].day = D("31/08/2026");
  recs[0].good = 4;
  recs[1].day = D("01/09/2026");
  recs[1].good = 2;
  recs[2].day = D("27/09/2026");
  recs[2].linearCards = 3;
  b = buckets(Scale::Months, today, 2, 0);
  fill(b, recs);
  CHECK_EQ(b[0].sum.cards(), (uint32_t)4);
  CHECK_EQ(b[1].sum.cards(), (uint32_t)5);
  CHECK_EQ(sum(recs, D("01/09/2026"), today).cards(), (uint32_t)5);
  CHECK_EQ(percent(2, 3), 67);
  CHECK_EQ(percent(0, 5), 0);
  CHECK_EQ(percent(1, 0), -1);
}

static void testStatsScan(const std::string& root) {
  namespace fs = std::filesystem;
  const std::string fc = root + "/flashcard";
  fs::remove_all(fc);
  fs::create_directories(fc);
  const uint32_t today = FlashcardSrs::parseDateStr("27/09/2026");
  char due[16];
  auto date = [&](uint32_t d) {
    FlashcardSrs::formatDateStr(d, due, sizeof(due));
    return std::string(due);
  };
  std::string csv = "card_id,front_content,back_content,sr_due,sr_interval,sr_ease\n";
  csv += "1,new card,b,,,\n";
  csv += "2,overdue,b," + date(today - 2) + ",3,250\n";
  csv += "3,mature,b," + date(today + 5) + ",30,250\n";
  csv += "4,\"worst/nline \\ hint \\\",\"answer/nhere\"," + date(today + 1) + ",1,130\n";
  csv += "5,edge,b," + date(today + 40) + ",2,150\n";
  csv += "6,fine,b," + date(today) + ",2,160\n";
  std::ofstream(fc + "/deck.csv", std::ios::binary) << csv;

  uint32_t forecast[FlashcardStatsScan::FORECAST_DAYS] = {};
  std::vector<FlashcardHardCard> hard;
  FlashcardStatsScan::DeckResult r;
  CHECK_EQ(FlashcardStatsScan::scanDeck("/flashcard/deck.csv", today, r, forecast, &hard), true);
  CHECK_EQ(r.phases.newCards, (uint32_t)1);
  CHECK_EQ(r.phases.mature, (uint32_t)1);
  CHECK_EQ(r.phases.learning, (uint32_t)4);
  CHECK_EQ(forecast[0], (uint32_t)2);  // overdue + due today
  CHECK_EQ(forecast[1], (uint32_t)1);
  CHECK_EQ(forecast[5], (uint32_t)1);
  // Same counts as the list scan
  DeckStats st;
  bool trunc = false;
  CHECK_EQ(FlashcardDeck::scanStats("/flashcard/deck.csv", today, st, trunc), true);
  CHECK_EQ(st.totalCards, r.stats.totalCards);
  CHECK_EQ(st.newCards, r.stats.newCards);
  CHECK_EQ(st.dueCards, r.stats.dueCards);
  // Hard cards: ease <= 150, worst first; display text decoded
  CHECK_EQ(hard.size(), (size_t)2);
  if (hard.size() == 2) {
    CHECK_EQ(hard[0].ease, (uint16_t)130);
    CHECK_EQ(hard[0].front, std::string("worst line"));
    CHECK_EQ(hard[0].back, std::string("answer here"));
    CHECK_EQ(hard[0].deckName, std::string("deck"));
    CHECK_EQ(hard[1].front, std::string("edge"));
    // The positions read back through the folder index
    std::vector<FlashcardFolderIndex::CardRef> refs;
    for (const auto& h : hard) refs.push_back({h.path, h.offset});
    FlashcardFolderIndex idx;
    CHECK_EQ(idx.buildFromRefs(refs), true);
    CHECK_EQ(idx.size(), (size_t)2);
    FlashcardCard c;
    CHECK_EQ(idx.loadCard(0, c), true);
    CHECK_EQ(c.frontContent, std::string("worst/nline \\ hint \\"));
    const uint32_t fp = idx.fingerprint();
    FlashcardFolderIndex again;
    again.buildFromRefs(refs);
    CHECK_EQ(again.fingerprint(), fp);
    // A position that no longer holds a card is skipped
    refs.push_back({"/flashcard/deck.csv", 999999});
    refs.push_back({"/flashcard/missing.csv", 0});
    FlashcardFolderIndex stale;
    CHECK_EQ(stale.buildFromRefs(refs), true);
    CHECK_EQ(stale.size(), (size_t)2);
    FlashcardFolderIndex none;
    CHECK_EQ(none.buildFromRefs({}), false);
  }

  // At most MAX_HARD, the worst ones, across decks
  std::string many = "card_id,front_content,back_content,sr_due,sr_interval,sr_ease\n";
  for (int i = 1; i <= 60; ++i) {
    many += std::to_string(i) + ",h" + std::to_string(i) + ",b," + date(today) + ",1," +
            std::to_string(130 + (i % 21)) + "\n";
  }
  std::ofstream(fc + "/many.csv", std::ios::binary) << many;
  hard = FlashcardStatsScan::collectHard({"/flashcard/deck.csv", "/flashcard/many.csv"});
  CHECK_EQ(hard.size(), FlashcardStatsScan::MAX_HARD);
  bool ordered = true;
  for (size_t i = 1; i < hard.size(); ++i) ordered &= !FlashcardStatsScan::harder(hard[i], hard[i - 1]);
  CHECK_EQ(ordered, true);
  CHECK_EQ(hard.front().ease, (uint16_t)130);

  // Short text never ends inside a UTF-8 character
  std::string accents;
  for (int i = 0; i < 100; ++i) accents += "\xC3\xA9";
  const std::string cut = FlashcardStatsScan::shortText(accents);
  CHECK_EQ(cut.size() <= FlashcardStatsScan::TEXT_BYTES, true);
  CHECK_EQ(cut.size() % 2, (size_t)0);
  CHECK_EQ(FlashcardStatsScan::shortText("a\nb\tc"), std::string("a b c"));
  fs::remove_all(fc);
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
  testPlannerSplit();
  testDeckOptions(Storage.root);
  testCatalogPlan(Storage.root);
  testHistory(Storage.root);
  testPeriods();
  testStatsScan(Storage.root);
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
