#include "desktop_keybinds.h"

#include <string.h>
#include <strings.h>

static const char* const keybind_names[DesktopKeybindCount] = {
    [DesktopKeybindUpPress] = "up press",
    [DesktopKeybindUpHold] = "up hold",
    [DesktopKeybindDownPress] = "down press",
    [DesktopKeybindDownHold] = "down hold",
    [DesktopKeybindLeftPress] = "left press",
    [DesktopKeybindLeftHold] = "left hold",
    [DesktopKeybindRightPress] = "right press",
    [DesktopKeybindRightHold] = "right hold",
    [DesktopKeybindOkPress] = "ok press",
    [DesktopKeybindOkHold] = "ok hold",
    [DesktopKeybindControl1] = "control 1",
    [DesktopKeybindControl2] = "control 2",
};

static const struct {
    const char* word;
    DesktopKeybindAction action;
} keybind_actions[] = {
    {"main menu", DesktopKeybindActionMainMenu},
    {"lock menu", DesktopKeybindActionLockMenu},
    {"lock", DesktopKeybindActionLock},
    {"lock keypad", DesktopKeybindActionLockKeypad},
    {"archive", DesktopKeybindActionArchive},
    {"power off", DesktopKeybindActionPowerOff},
    {"nothing", DesktopKeybindActionNothing},
};

static const struct {
    const char* folder; /**< NULL: anywhere */
    const char* extension;
    const char* app;
} file_apps[] = {
    {NULL, ".sub", "Sub-GHz"},
    {NULL, ".nfc", "NFC"},
    {NULL, ".rfid", "125 kHz RFID"},
    {NULL, ".ibtn", "iButton"},
    {NULL, ".ir", "Infrared"},
    {NULL, ".js", "/ext/apps/assets/js_app.fap"},
    {"/ext/badusb/", ".txt", "Bad USB"},
    {"/ext/subghz_remote/", ".txt", "Sub-GHz Remote"},
};

const char* desktop_keybind_name(DesktopKeybind bind) {
    return bind < DesktopKeybindCount ? keybind_names[bind] : "";
}

static bool is_space(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

bool desktop_keybinds_parse_line(
    const char* line,
    size_t length,
    DesktopKeybind* bind,
    const char** action,
    size_t* action_length) {
    while(length && is_space(*line)) {
        line++;
        length--;
    }
    const char* colon = memchr(line, ':', length);
    if(!colon) return false;
    // the binding: before the colon, with the spaces at its end gone
    size_t name_length = (size_t)(colon - line);
    while(name_length && is_space(line[name_length - 1])) {
        name_length--;
    }
    DesktopKeybind found = DesktopKeybindCount;
    for(size_t i = 0; i < DesktopKeybindCount; i++) {
        if(strlen(keybind_names[i]) == name_length &&
           strncasecmp(line, keybind_names[i], name_length) == 0) {
            found = (DesktopKeybind)i;
        }
    }
    if(found == DesktopKeybindCount) return false;
    // the action: after the colon, trimmed
    const char* start = colon + 1;
    size_t rest = length - (size_t)(start - line);
    while(rest && is_space(*start)) {
        start++;
        rest--;
    }
    while(rest && is_space(start[rest - 1])) {
        rest--;
    }
    if(!rest) return false;
    *bind = found;
    *action = start;
    *action_length = rest;
    return true;
}

DesktopKeybindAction desktop_keybind_action(const char* action) {
    for(size_t i = 0; i < sizeof(keybind_actions) / sizeof(keybind_actions[0]); i++) {
        if(strcasecmp(action, keybind_actions[i].word) == 0) return keybind_actions[i].action;
    }
    return DesktopKeybindActionNone;
}

const char* desktop_keybind_app_for_file(const char* path) {
    const size_t length = strlen(path);
    for(size_t i = 0; i < sizeof(file_apps) / sizeof(file_apps[0]); i++) {
        const size_t ext = strlen(file_apps[i].extension);
        if(length <= ext || strcasecmp(path + length - ext, file_apps[i].extension) != 0) continue;
        if(file_apps[i].folder &&
           strncmp(path, file_apps[i].folder, strlen(file_apps[i].folder)) != 0) {
            continue;
        }
        return file_apps[i].app;
    }
    return NULL;
}
