/**
 * @file furi_hal_rtc.h
 * Furi Hal RTC API
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#include <datetime/datetime.h>
#include <core/common_defines.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FuriHalRtcFlagDebug = (1 << 0),
    FuriHalRtcFlagStorageFormatInternal = (1 << 1),
    FuriHalRtcFlagLock = (1 << 2),
    FuriHalRtcFlagC2Update = (1 << 3),
    FuriHalRtcFlagHandOrient = (1 << 4),
    FuriHalRtcFlagLegacySleep = (1 << 5),
    FuriHalRtcFlagStealthMode = (1 << 6),
    FuriHalRtcFlagDetailedFilename = (1 << 7),
} FuriHalRtcFlag;

/** Development mode is on: a restart of the firmware's own, while unlocked and on USB, starts
 * unlocked (the desktop), and the desktop does not lock itself on USB */
#define FURI_HAL_RTC_DEVELOPMENT_ON          (1UL << 0)
/** Set by furi_hal_power_reset() (a crash, a reboot, an update), never by a power-on or a hard
 * reset (Left+Back), which run no code first; the desktop clears it as it starts */
#define FURI_HAL_RTC_DEVELOPMENT_OWN_RESTART (1UL << 1)

/* The alarm's options, FuriHalRtcRegisterAlarm: shared by the alarm that rings (from anywhere)
 * and the Clock app that sets them. Zero, a register never set, is the alarm as it always was:
 * every day, a 9 minute snooze, ringing for 10 minutes, with sound, vibration and light */
#define FURI_HAL_RTC_ALARM_SKIP_DAYS    0x7FUL /**< bit d (Monday 0 .. Sunday 6): not that day */
#define FURI_HAL_RTC_ALARM_SNOOZE_SHIFT 8 /**< 5 bits: minutes, 0 is 9 */
#define FURI_HAL_RTC_ALARM_RING_SHIFT   13 /**< 5 bits: minutes it rings, 0 is 10 */
#define FURI_HAL_RTC_ALARM_SILENT       (1UL << 18) /**< no sound */
#define FURI_HAL_RTC_ALARM_NO_VIBRO     (1UL << 19) /**< no vibration */
#define FURI_HAL_RTC_ALARM_NO_LIGHT     (1UL << 20) /**< the screen does not flash */
#define FURI_HAL_RTC_ALARM_GENTLE       (1UL << 21) /**< the sound starts soft and grows */

/** Snooze minutes of the options */
static inline uint8_t furi_hal_rtc_alarm_snooze_minutes(uint32_t options) {
    const uint8_t minutes = (options >> FURI_HAL_RTC_ALARM_SNOOZE_SHIFT) & 0x1F;
    return minutes ? minutes : 9;
}

/** Minutes the alarm rings before it gives up */
static inline uint8_t furi_hal_rtc_alarm_ring_minutes(uint32_t options) {
    const uint8_t minutes = (options >> FURI_HAL_RTC_ALARM_RING_SHIFT) & 0x1F;
    return minutes ? minutes : 10;
}

/** Every day skipped: it rings once, at its next time, and turns itself off */
static inline bool furi_hal_rtc_alarm_once(uint32_t options) {
    return (options & FURI_HAL_RTC_ALARM_SKIP_DAYS) == FURI_HAL_RTC_ALARM_SKIP_DAYS;
}

/** Whether it rings on a weekday (DateTime's: 1 Monday .. 7 Sunday) */
static inline bool furi_hal_rtc_alarm_rings_on(uint32_t options, uint8_t weekday) {
    if(furi_hal_rtc_alarm_once(options)) return true;
    return weekday < 1 || weekday > 7 || !(options & (1UL << (weekday - 1)));
}

typedef enum {
    FuriHalRtcBootModeNormal = 0, /**< Normal boot mode, default value */
    FuriHalRtcBootModeDfu, /**< Boot to DFU (MCU bootloader by ST) */
    FuriHalRtcBootModePreUpdate, /**< Boot to Update, pre update */
    FuriHalRtcBootModeUpdate, /**< Boot to Update, main */
    FuriHalRtcBootModePostUpdate, /**< Boot to Update, post update */
} FuriHalRtcBootMode;

typedef enum {
    FuriHalRtcHeapTrackModeNone = 0, /**< Disable allocation tracking */
    FuriHalRtcHeapTrackModeMain, /**< Enable allocation tracking for main application thread */
    FuriHalRtcHeapTrackModeTree, /**< Enable allocation tracking for main and children application threads */
    FuriHalRtcHeapTrackModeAll, /**< Enable allocation tracking for all threads */
} FuriHalRtcHeapTrackMode;

typedef enum {
    FuriHalRtcRegisterHeader, /**< RTC structure header */
    FuriHalRtcRegisterSystem, /**< Various system bits */
    FuriHalRtcRegisterVersion, /**< Pointer to Version */
    FuriHalRtcRegisterLfsFingerprint FURI_DEPRECATED, /**< LFS geometry fingerprint */
    FuriHalRtcRegisterFaultData, /**< Pointer to last fault message */
    FuriHalRtcRegisterPinFails, /**< Failed PINs count */
    /* Index of FS directory entry corresponding to FW update to be applied */
    FuriHalRtcRegisterUpdateFolderFSIndex,
    FuriHalRtcRegisterPinValue, /**< Encoded value of the currently set PIN */
    /* Steps of the update that runs from RAM (it cannot write to the SD card): reported to the
     * SD card log by the firmware after the update */
    FuriHalRtcRegisterUpdateTrace, /**< Current update: marker, starts, last step, detail */
    FuriHalRtcRegisterUpdateTraceStart, /**< Current update: RTC time of its first step */
    FuriHalRtcRegisterUpdateTracePrevious, /**< An earlier update that did not finish */
    FuriHalRtcRegisterBootGuard, /**< Starts since an update that did not settle; rollback */
    /* Set once the System settings these registers hold are also kept in a file; zero again
     * when the registers are wiped (a flat battery), so they are put back from the file */
    FuriHalRtcRegisterSettingsKept,
    /** Development mode (FURI_HAL_RTC_DEVELOPMENT_*): whether it is on, and whether the last
     * restart was the firmware's own */
    FuriHalRtcRegisterDevelopment,
    /** The alarm's options (FURI_HAL_RTC_ALARM_*): its days, snooze, how long it rings, how */
    FuriHalRtcRegisterAlarm,

    FuriHalRtcRegisterMAX, /**< Service value, do not use */
} FuriHalRtcRegister;

typedef enum {
    FuriHalRtcLocaleUnitsMetric = 0x0, /**< Metric measurement units */
    FuriHalRtcLocaleUnitsImperial = 0x1, /**< Imperial measurement units */
} FuriHalRtcLocaleUnits;

typedef enum {
    FuriHalRtcLocaleTimeFormat24h = 0x0, /**< 24-hour format */
    FuriHalRtcLocaleTimeFormat12h = 0x1, /**< 12-hour format */
} FuriHalRtcLocaleTimeFormat;

typedef enum {
    FuriHalRtcLocaleDateFormatDMY = 0x0, /**< Day/Month/Year */
    FuriHalRtcLocaleDateFormatMDY = 0x1, /**< Month/Day/Year */
    FuriHalRtcLocaleDateFormatYMD = 0x2, /**< Year/Month/Day */
} FuriHalRtcLocaleDateFormat;

typedef enum {
    FuriHalRtcLogDeviceUsart = 0x0, /**< Default: USART */
    FuriHalRtcLogDeviceLpuart = 0x1, /**< Default: LPUART */
    FuriHalRtcLogDeviceReserved = 0x2, /**< Reserved for future use */
    FuriHalRtcLogDeviceNone = 0x3, /**< None, disable serial logging */
} FuriHalRtcLogDevice;

typedef enum {
    FuriHalRtcLogBaudRate230400 = 0x0, /**< 230400 baud */
    FuriHalRtcLogBaudRate9600 = 0x1, /**< 9600 baud */
    FuriHalRtcLogBaudRate38400 = 0x2, /**< 38400 baud */
    FuriHalRtcLogBaudRate57600 = 0x3, /**< 57600 baud */
    FuriHalRtcLogBaudRate115200 = 0x4, /**< 115200 baud */
    FuriHalRtcLogBaudRate460800 = 0x5, /**< 460800 baud */
    FuriHalRtcLogBaudRate921600 = 0x6, /**< 921600 baud */
    FuriHalRtcLogBaudRate1843200 = 0x7, /**< 1843200 baud */
} FuriHalRtcLogBaudRate;

/** Early initialization */
void furi_hal_rtc_init_early(void);

/** Early de-initialization */
void furi_hal_rtc_deinit_early(void);

/** Initialize RTC subsystem */
void furi_hal_rtc_init(void);

/** Prepare system for shutdown
 *
 * This function must be called before system sent to transport mode(power off).
 * FlipperZero implementation configures and enables ALARM output on pin PC13
 * (Back button). This allows the system to wake-up charger from transport mode.
 */
void furi_hal_rtc_prepare_for_shutdown(void);

/** Force sync shadow registers */
void furi_hal_rtc_sync_shadow(void);

/** Reset ALL RTC registers content */
void furi_hal_rtc_reset_registers(void);

/** Get RTC register content
 *
 * @param[in]  reg   The register identifier
 *
 * @return     content of the register
 */
uint32_t furi_hal_rtc_get_register(FuriHalRtcRegister reg);

/** Set register content
 *
 * @param[in]  reg    The register identifier
 * @param[in]  value  The value to store into register
 */
void furi_hal_rtc_set_register(FuriHalRtcRegister reg, uint32_t value);

/** Set Log Level value
 *
 * @param[in]  level  The level to store
 */
void furi_hal_rtc_set_log_level(uint8_t level);

/** Get Log Level value
 *
 * @return     The Log Level value
 */
uint8_t furi_hal_rtc_get_log_level(void);

/** Set logging device
 *
 * @param[in]  device  The device
 */
void furi_hal_rtc_set_log_device(FuriHalRtcLogDevice device);

/** Get logging device
 *
 * @return     The furi hal rtc log device.
 */
FuriHalRtcLogDevice furi_hal_rtc_get_log_device(void);

/** Set logging baud rate
 *
 * @param[in]  baud_rate  The baud rate
 */
void furi_hal_rtc_set_log_baud_rate(FuriHalRtcLogBaudRate baud_rate);

/** Get logging baud rate
 *
 * @return     The furi hal rtc log baud rate.
 */
FuriHalRtcLogBaudRate furi_hal_rtc_get_log_baud_rate(void);

/** Set RTC Flag
 *
 * @param[in]  flag  The flag to set
 */
void furi_hal_rtc_set_flag(FuriHalRtcFlag flag);

/** Reset RTC Flag
 *
 * @param[in]  flag  The flag to reset
 */
void furi_hal_rtc_reset_flag(FuriHalRtcFlag flag);

/** Check if RTC Flag is set
 *
 * @param[in]  flag  The flag to check
 *
 * @return     true if set
 */
bool furi_hal_rtc_is_flag_set(FuriHalRtcFlag flag);

/** Set RTC boot mode
 *
 * @param[in]  mode  The mode to set
 */
void furi_hal_rtc_set_boot_mode(FuriHalRtcBootMode mode);

/** Get RTC boot mode
 *
 * @return     The RTC boot mode.
 */
FuriHalRtcBootMode furi_hal_rtc_get_boot_mode(void);

/** Set Heap Track mode
 *
 * @param[in]  mode  The mode to set
 */
void furi_hal_rtc_set_heap_track_mode(FuriHalRtcHeapTrackMode mode);

/** Get RTC Heap Track mode
 *
 * @return     The RTC heap track mode.
 */
FuriHalRtcHeapTrackMode furi_hal_rtc_get_heap_track_mode(void);

/** Set locale units
 *
 * @param[in]  value  The RTC Locale Units
 */
void furi_hal_rtc_set_locale_units(FuriHalRtcLocaleUnits value);

/** Get RTC Locale Units
 *
 * @return     The RTC Locale Units.
 */
FuriHalRtcLocaleUnits furi_hal_rtc_get_locale_units(void);

/** Set RTC Locale Time Format
 *
 * @param[in]  value  The RTC Locale Time Format
 */
void furi_hal_rtc_set_locale_timeformat(FuriHalRtcLocaleTimeFormat value);

/** Get RTC Locale Time Format
 *
 * @return     The RTC Locale Time Format.
 */
FuriHalRtcLocaleTimeFormat furi_hal_rtc_get_locale_timeformat(void);

/** Set RTC Locale Date Format
 *
 * @param[in]  value  The RTC Locale Date Format
 */
void furi_hal_rtc_set_locale_dateformat(FuriHalRtcLocaleDateFormat value);

/** Get RTC Locale Date Format
 *
 * @return     The RTC Locale Date Format
 */
FuriHalRtcLocaleDateFormat furi_hal_rtc_get_locale_dateformat(void);

/** Set RTC Date Time
 *
 * @param      datetime  The date time to set
 */
void furi_hal_rtc_set_datetime(DateTime* datetime);

/** Get RTC Date Time
 *
 * @param      datetime  The datetime
 */
void furi_hal_rtc_get_datetime(DateTime* datetime);

/** Set alarm
 *
 * @param[in]  datetime  The date time to set or NULL if time change is not needed
 * @param[in]  enabled   Indicates if alarm must be enabled or disabled
 */
void furi_hal_rtc_set_alarm(const DateTime* datetime, bool enabled);

/** Get alarm
 *
 * @param      datetime  Pointer to DateTime object
 *
 * @return     true if alarm was set, false otherwise
 */
bool furi_hal_rtc_get_alarm(DateTime* datetime);

/** Furi HAL RTC alarm callback signature */
typedef void (*FuriHalRtcAlarmCallback)(void* context);

/** Set alarm callback
 *
 * Use it to subscribe to alarm trigger event. Setting alarm callback is
 * independent from setting alarm.
 *
 * @warning    Normally this callback will be delivered from the ISR, however we may
 *             deliver it while this function is called. This happens when
 *             the alarm has already triggered, but there was no ISR set.
 *
 * @param[in]  callback  The callback
 * @param      context   The context
 */
void furi_hal_rtc_set_alarm_callback(FuriHalRtcAlarmCallback callback, void* context);

/** Set RTC Fault Data
 *
 * @param[in]  value  The value
 */
void furi_hal_rtc_set_fault_data(uint32_t value);

/** Get RTC Fault Data
 *
 * @return     RTC Fault Data value
 */
uint32_t furi_hal_rtc_get_fault_data(void);

/** Set PIN Fails count
 *
 * @param[in]  value  The PIN Fails count
 */
void furi_hal_rtc_set_pin_fails(uint32_t value);

/** Get PIN Fails count
 *
 * @return     PIN Fails Count
 */
uint32_t furi_hal_rtc_get_pin_fails(void);

/** Set encoded PIN value
 *
 * @param[in] value new PIN code value to be set
 */
void furi_hal_rtc_set_pin_value(uint32_t value);

/** Get the current PIN encoded value
 *
 */
uint32_t furi_hal_rtc_get_pin_value(void);

/** Get UNIX Timestamp
 *
 * @return     Unix Timestamp in seconds from UNIX epoch start
 */
uint32_t furi_hal_rtc_get_timestamp(void);

#ifdef __cplusplus
}
#endif
