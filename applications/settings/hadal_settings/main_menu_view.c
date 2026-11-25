#include "main_menu_view.h"

#include <furi.h>
#include <gui/elements.h>
#include <assets_icons.h>

/* The control centre's picker's measures: 20x20 tiles 24 apart, two either side of the chosen */
#define CELL    20
#define STEP    24
#define TILES_Y 18 /* under the header */
#define LIFT    4 /* how far a carried app rises over its place */
#define SIDE    2
#define NAME_Y  50 /* the name's baseline, over the hint bar */

struct MainMenuView {
    View* view;
    MainMenuViewCallback callback;
    void* context;
};

static void main_menu_view_draw_glyph(
    Canvas* canvas,
    int32_t x,
    int32_t y,
    const MainMenuTile* tile) {
    const Icon* icon = tile->kind == MainMenuTileAdd   ? &I_cc_add_12x12 :
                       tile->kind == MainMenuTileReset ? &I_cc_rotate_12x12 :
                                                         tile->icon;
    if(icon) {
        canvas_draw_icon(
            canvas,
            x + (CELL - (int32_t)icon_get_width(icon)) / 2,
            y + (CELL - (int32_t)icon_get_height(icon)) / 2,
            icon);
    } else if(tile->label[0]) {
        const char initial[2] = {tile->label[0], '\0'};
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str_aligned(
            canvas, x + CELL / 2, y + CELL / 2 + 1, AlignCenter, AlignCenter, initial);
    }
}

static void main_menu_view_draw(Canvas* canvas, void* model_) {
    const MainMenuViewModel* model = model_;
    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);
    elements_header(canvas, model->title);
    const size_t count = model->count;
    if(!count) return;
    const MainMenuTile* chosen = &model->tiles[model->selected];

    // where in the menu the app is: at the header's end, on a clear patch over its line
    if(chosen->kind == MainMenuTileApp) {
        char position[12];
        snprintf(
            position,
            sizeof(position),
            "%u/%u",
            (unsigned)(model->selected + 1),
            (unsigned)model->apps);
        canvas_set_font(canvas, FontSecondary);
        const int32_t width = canvas_string_width(canvas, position);
        canvas_set_color(canvas, ColorWhite);
        canvas_draw_box(canvas, 125 - width - 3, 0, width + 6, 13);
        canvas_set_color(canvas, ColorBlack);
        canvas_draw_str_aligned(canvas, 125, 11, AlignRight, AlignBottom, position);
    }

    // the neighbours, as many either side as there are tiles to fill it without repeating one
    const int32_t before = MIN((int32_t)(count - 1) / 2, SIDE);
    const int32_t after = MIN((int32_t)count / 2, SIDE);
    for(int32_t offset = -before; offset <= after; offset++) {
        if(offset == 0) continue;
        const size_t index = (model->selected + count * SIDE + offset) % count;
        const int32_t x = 64 - CELL / 2 + offset * STEP;
        canvas_draw_rframe(canvas, x + 1, TILES_Y + 1, CELL - 2, CELL - 2, 3);
        main_menu_view_draw_glyph(canvas, x, TILES_Y, &model->tiles[index]);
    }

    const int32_t x = 64 - CELL / 2;
    if(model->lifted) {
        // its place left empty under it, the app held up, and arrows where it can go
        for(int32_t dot = x + 2; dot < x + CELL - 2; dot += 2) {
            canvas_draw_dot(canvas, dot, TILES_Y + CELL - 1);
        }
        const int32_t y = TILES_Y - LIFT;
        canvas_draw_rbox(canvas, x, y, CELL, CELL, 4);
        canvas_set_color(canvas, ColorWhite);
        main_menu_view_draw_glyph(canvas, x, y, chosen);
        canvas_set_color(canvas, ColorBlack);
        if(model->selected > 0) {
            canvas_draw_icon(canvas, x - 4, y + (CELL - 5) / 2, &I_ButtonLeftSmall_3x5);
        }
        if(model->selected + 1 < model->apps) {
            canvas_draw_icon(canvas, x + CELL + 1, y + (CELL - 5) / 2, &I_ButtonRightSmall_3x5);
        }
    } else {
        canvas_draw_rframe(canvas, x, TILES_Y, CELL, CELL, 4);
        canvas_draw_rbox(canvas, x + 1, TILES_Y + 1, CELL - 2, CELL - 2, 3);
        canvas_set_color(canvas, ColorWhite);
        main_menu_view_draw_glyph(canvas, x, TILES_Y, chosen);
        canvas_set_color(canvas, ColorBlack);
    }

    FuriString* name = furi_string_alloc_set_str(chosen->label);
    canvas_set_font(canvas, FontPrimary);
    elements_string_fit_width(canvas, name, 124);
    canvas_draw_str_aligned(
        canvas, 64, NAME_Y, AlignCenter, AlignBottom, furi_string_get_cstr(name));
    furi_string_free(name);

    const char* hints;
    if(model->adding) {
        hints = "lr=Browse;ok=Add;back=Cancel";
    } else if(model->lifted) {
        hints = "lr=Move;ok=Put down;back=Undo";
    } else if(chosen->kind == MainMenuTileAdd) {
        hints = "lr=Browse;ok=Add an app";
    } else if(chosen->kind == MainMenuTileReset) {
        hints = "lr=Browse;ok=Reset";
    } else if(chosen->fixed) {
        hints = "lr=Browse;ok=Move";
    } else {
        hints = "lr=Browse;ok=Move;down=Hide";
    }
    elements_hint_bar(canvas, hints);
}

static bool main_menu_view_input(InputEvent* event, void* context) {
    MainMenuView* instance = context;
    bool lifted = false, adding = false, fixed = false;
    size_t selected = 0, count = 0, apps = 0;
    MainMenuTileKind kind = MainMenuTileApp;
    with_view_model(
        instance->view,
        MainMenuViewModel * model,
        {
            lifted = model->lifted;
            adding = model->adding;
            selected = model->selected;
            count = model->count;
            apps = model->apps;
            if(selected < count) {
                kind = model->tiles[selected].kind;
                fixed = model->tiles[selected].fixed;
            }
        },
        false);

    // Back leaves the page (the scene's), unless an app is carried or one is being added
    if(event->key == InputKeyBack) {
        if(!lifted && !adding) return false;
        if(event->type == InputTypeShort && instance->callback) {
            instance->callback(
                instance->context,
                lifted ? MainMenuViewEventCancel : MainMenuViewEventCloseAdd,
                selected);
        }
        return true;
    }
    if(!count) return true;
    const bool step = event->type == InputTypeShort || event->type == InputTypeRepeat;
    const bool shortly = event->type == InputTypeShort;

    MainMenuViewEvent send = 0;
    size_t index = selected;
    if(lifted) {
        if(step && event->key == InputKeyLeft && selected > 0) {
            send = MainMenuViewEventMove;
            index = 0;
        } else if(step && event->key == InputKeyRight && selected + 1 < apps) {
            send = MainMenuViewEventMove;
            index = 1;
        } else if(shortly && event->key == InputKeyOk) {
            send = MainMenuViewEventDrop;
            with_view_model(
                instance->view, MainMenuViewModel * model, { model->lifted = false; }, true);
        }
    } else if(step && (event->key == InputKeyLeft || event->key == InputKeyRight)) {
        const size_t next = event->key == InputKeyLeft ? (selected + count - 1) % count :
                                                         (selected + 1) % count;
        with_view_model(
            instance->view, MainMenuViewModel * model, { model->selected = next; }, true);
    } else if(shortly && event->key == InputKeyOk) {
        if(adding) {
            send = MainMenuViewEventPick;
        } else if(kind == MainMenuTileApp) {
            send = MainMenuViewEventLift;
            with_view_model(
                instance->view, MainMenuViewModel * model, { model->lifted = true; }, true);
        } else if(kind == MainMenuTileAdd) {
            send = MainMenuViewEventOpenAdd;
        } else {
            send = MainMenuViewEventReset;
        }
    } else if(shortly && event->key == InputKeyDown && !adding && kind == MainMenuTileApp && !fixed) {
        send = MainMenuViewEventHide;
    }

    if(send && instance->callback) instance->callback(instance->context, send, index);
    return true;
}

MainMenuView* main_menu_view_alloc(void) {
    MainMenuView* instance = malloc(sizeof(MainMenuView));
    instance->callback = NULL;
    instance->context = NULL;
    instance->view = view_alloc();
    view_allocate_model(instance->view, ViewModelTypeLocking, sizeof(MainMenuViewModel));
    view_set_context(instance->view, instance);
    view_set_draw_callback(instance->view, main_menu_view_draw);
    view_set_input_callback(instance->view, main_menu_view_input);
    with_view_model(
        instance->view,
        MainMenuViewModel * model,
        { memset(model, 0, sizeof(MainMenuViewModel)); },
        false);
    return instance;
}

void main_menu_view_free(MainMenuView* instance) {
    furi_check(instance);
    view_free(instance->view);
    free(instance);
}

View* main_menu_view_get_view(MainMenuView* instance) {
    furi_check(instance);
    return instance->view;
}

void main_menu_view_set_callback(
    MainMenuView* instance,
    MainMenuViewCallback callback,
    void* context) {
    furi_check(instance);
    instance->callback = callback;
    instance->context = context;
}

MainMenuViewModel* main_menu_view_lock(MainMenuView* instance) {
    furi_check(instance);
    return view_get_model(instance->view);
}

void main_menu_view_unlock(MainMenuView* instance) {
    furi_check(instance);
    view_commit_model(instance->view, true);
}
