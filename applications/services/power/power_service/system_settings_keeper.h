/**
 * @file system_settings_keeper.h
 * @brief The System settings the clock chip holds, kept in a file as well.
 *
 * Left-handed mode, stealth mode, the units, time and date formats, the sleep method, the
 * detailed file names, the log options and Debug live in the RTC's backup registers, not in a
 * settings file: they survive restarts and updates, and are lost with the registers when the
 * battery runs completely flat (or the registers are otherwise wiped). They are written to
 * /int/.system.settings at each start and before each power off or restart, and put back from
 * it when the registers come up empty. A factory reset clears both, so it still resets them.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/** At start-up, after the storage service: put the settings back if the registers lost them,
 * then keep a copy of what they hold */
void system_settings_keeper_start(void);

/** Before a power off or a restart: keep a copy of what the registers hold */
void system_settings_keeper_save(void);

#ifdef __cplusplus
}
#endif
