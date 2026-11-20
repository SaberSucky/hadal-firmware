#include <gui/gui_i.h>
#include <gui/view.h>
#include <gui/elements.h>
#include <gui/canvas.h>
#include <furi.h>
#include <input/input.h>
#include <dolphin/dolphin.h>

#include "desktop_view_main.h"
#include "../helpers/desktop_keybinds.h"

struct DesktopMainView {
    View* view;
    DesktopMainViewCallback callback;
    void* context;
    FuriTimer* poweroff_timer;
    bool dummy_mode;
};

#define DESKTOP_MAIN_VIEW_POWEROFF_TIMEOUT 1300

static void desktop_main_poweroff_timer_callback(void* context) {
    DesktopMainView* main_view = context;
    main_view->callback(DesktopMainEventOpenPowerOff, main_view->context);
}

void desktop_main_set_callback(
    DesktopMainView* main_view,
    DesktopMainViewCallback callback,
    void* context) {
    furi_assert(main_view);
    furi_assert(callback);
    main_view->callback = callback;
    main_view->context = context;
}

View* desktop_main_get_view(DesktopMainView* main_view) {
    furi_assert(main_view);
    return main_view->view;
}

void desktop_main_set_dummy_mode_state(DesktopMainView* main_view, bool dummy_mode) {
    furi_assert(main_view);
    main_view->dummy_mode = dummy_mode;
}

bool desktop_main_input_callback(InputEvent* event, void* context) {
    furi_assert(event);
    furi_assert(context);

    DesktopMainView* main_view = context;

    // every press and hold of the five buttons goes to the scene, which looks up what it is bound
    // to (helpers/desktop_keybinds.h); a short Right press is the animation's, which hands it on
    if(event->type == InputTypeShort || event->type == InputTypeLong) {
        static const int8_t keybinds[InputKeyMAX] = {
            [InputKeyUp] = DesktopKeybindUpPress,
            [InputKeyDown] = DesktopKeybindDownPress,
            [InputKeyRight] = DesktopKeybindRightPress,
            [InputKeyLeft] = DesktopKeybindLeftPress,
            [InputKeyOk] = DesktopKeybindOkPress,
            [InputKeyBack] = -1,
        };
        const int8_t press = keybinds[event->key];
        if(press >= 0) {
            const int bind = press + (event->type == InputTypeLong ? 1 : 0);
            main_view->callback(DesktopMainEventKeybind + bind, main_view->context);
        }
    }

    if(event->key == InputKeyBack) {
        if(event->type == InputTypePress) {
            furi_timer_start(main_view->poweroff_timer, DESKTOP_MAIN_VIEW_POWEROFF_TIMEOUT);
        } else if(event->type == InputTypeRelease) {
            furi_timer_stop(main_view->poweroff_timer);
        }
    }

    return true;
}

DesktopMainView* desktop_main_alloc(void) {
    DesktopMainView* main_view = malloc(sizeof(DesktopMainView));

    main_view->view = view_alloc();
    view_set_context(main_view->view, main_view);
    view_set_input_callback(main_view->view, desktop_main_input_callback);

    main_view->poweroff_timer =
        furi_timer_alloc(desktop_main_poweroff_timer_callback, FuriTimerTypeOnce, main_view);

    return main_view;
}

void desktop_main_free(DesktopMainView* main_view) {
    furi_assert(main_view);
    view_free(main_view->view);
    furi_timer_free(main_view->poweroff_timer);
    free(main_view);
}
