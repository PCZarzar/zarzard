#pragma once

// Keeps the flashcard date across deep sleep on boards without an RTC chip
// (X4). Before sleeping, the wall time and the RTC counter are saved in RTC
// memory; after a wake that lost the system clock, the time is rebuilt from
// the counter (FlashcardClock::restoreAfterSleep). Each boot also appends one
// line to /.crosspoint/fc_clock_log.txt so a lost clock can be diagnosed.
namespace FlashcardClockKeeper {

// Call right before entering deep sleep.
void beforeSleep();
// Call early in setup(), before anything reads the time.
void afterBoot();
// Call once the SD card is ready: writes what afterBoot() saw.
void logBoot();

}  // namespace FlashcardClockKeeper
