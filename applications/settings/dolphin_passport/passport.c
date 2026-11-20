#include <furi.h>
#include <furi_hal_version.h>

#include <gui/gui.h>
#include <dolphin/dolphin.h>
#include <dolphin/helpers/dolphin_state.h>

#include <assets_icons.h>

#define MOODS_TOTAL  3
#define BUTTHURT_MAX 3

static const Icon* const portrait_happy[BUTTHURT_MAX] = {
    &I_passport_happy1_46x49,
    &I_passport_happy2_46x49,
    &I_passport_happy3_46x49};
static const Icon* const portrait_ok[BUTTHURT_MAX] = {
    &I_passport_okay1_46x49,
    &I_passport_okay2_46x49,
    &I_passport_okay3_46x49};
static const Icon* const portrait_bad[BUTTHURT_MAX] = {
    &I_passport_bad1_46x49,
    &I_passport_bad2_46x49,
    &I_passport_bad3_46x49};

static const Icon* const* portraits[MOODS_TOTAL] = {portrait_happy, portrait_ok, portrait_bad};

static void input_callback(InputEvent* input, void* ctx) {
    FuriSemaphore* semaphore = ctx;

    if((input->type == InputTypeShort) && (input->key == InputKeyBack)) {
        furi_semaphore_release(semaphore);
    }
}

static void render_callback(Canvas* canvas, void* ctx) {
    DolphinStats* stats = ctx;

    char level_str[20];
    char mood_str[32];
    uint8_t mood = 0;

    if(stats->butthurt <= 4) {
        mood = 0;
        snprintf(mood_str, 20, "Mood: Happy");
    } else if(stats->butthurt <= 9) {
        mood = 1;
        snprintf(mood_str, 20, "Mood: Ok");
    } else {
        mood = 2;
        snprintf(mood_str, 20, "Mood: Angry");
    }

    uint32_t xp_progress = 0;
    uint32_t xp_to_levelup = dolphin_state_xp_to_levelup(stats->icounter);
    uint32_t xp_for_current_level =
        xp_to_levelup + dolphin_state_xp_above_last_levelup(stats->icounter);
    if(stats->level >= DOLPHIN_LEVEL_MAX) {
        xp_progress = 0;
    } else {
        xp_progress = xp_to_levelup * 64 / xp_for_current_level;
    }

    // The card, then the portrait and the text over it. The layout is the one asset packs'
    // passport backgrounds (Passport/passport_128x64) are drawn for, so theirs fit as well.
    canvas_draw_icon(canvas, 0, 0, &I_passport_128x64);

    // three portraits: the third from level 3 on
    furi_assert((stats->level > 0) && (stats->level <= DOLPHIN_LEVEL_MAX));
    canvas_draw_icon(canvas, 11, 2, portraits[mood][MIN(stats->level, 3) - 1]);

    const char* my_name = furi_hal_version_get_name_ptr();
    snprintf(level_str, 20, "Level: %hu", stats->level);
    canvas_set_font(canvas, FontSecondary);
    // each line a clear row away from the rules between them (assets/hadal_ui/passport.py)
    canvas_draw_str(canvas, 60, 9, my_name ? my_name : "Unknown");
    canvas_draw_str(canvas, 60, 21, mood_str);
    canvas_draw_str(canvas, 60, 41, level_str);

    // experience: the bar's well cleared, then filled as far as this level has come
    const uint32_t filled = 61 - MIN(xp_progress * 61 / 64, 61U);
    canvas_set_color(canvas, ColorWhite);
    canvas_draw_box(canvas, 61, 45, 61, 5);
    canvas_set_color(canvas, ColorBlack);
    if(filled) canvas_draw_box(canvas, 61, 45, filled, 5);
}

int32_t passport_app(void* p) {
    UNUSED(p);
    FuriSemaphore* semaphore = furi_semaphore_alloc(1, 0);
    ViewPort* view_port = view_port_alloc();

    Dolphin* dolphin = furi_record_open(RECORD_DOLPHIN);
    DolphinStats stats = dolphin_stats(dolphin);
    furi_record_close(RECORD_DOLPHIN);
    view_port_draw_callback_set(view_port, render_callback, &stats);
    view_port_input_callback_set(view_port, input_callback, semaphore);
    Gui* gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(gui, view_port, GuiLayerFullscreen);
    view_port_update(view_port);

    furi_check(furi_semaphore_acquire(semaphore, FuriWaitForever) == FuriStatusOk);

    gui_remove_view_port(gui, view_port);
    view_port_free(view_port);
    furi_record_close(RECORD_GUI);
    furi_semaphore_free(semaphore);

    return 0;
}
