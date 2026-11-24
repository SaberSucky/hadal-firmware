#include "../hadal_settings_i.h"

#include <flipper_format/flipper_format.h>
#include <gui/elements.h>
#include <toolbox/compress.h>

/* Asset Pack: each pack as the desktop would show it, its first animation playing full screen.
 * Left and Right go through them (Built-in first), OK uses the one on show, Back leaves. A pack
 * with no animation says what it holds instead. */

#define DOLPHIN_PATH      "/ext/dolphin" // the built-in look's animations
#define PREVIEW_ORDER_MAX 32U // frames of the animation played, at most (it loops)
#define FRAME_BYTES_MAX   (128 * 64 / 8)
#define FRAME_FILE_MAX    (FRAME_BYTES_MAX + 8)
#define BAR_TOP           49 // the bar along the bottom: a dotted rule, two clear rows, the name
#define PAGE_DOTS_MAX     12
#define FRAMES_BIN_MAX    64U // frames a frames.bin may hold for the preview to read it
#define FRAMES_BIN_MAGIC  0x31464448U // "HDF1", as animation_storage.c

enum {
    EventPrev = 100,
    EventNext,
    EventUse,
    EventTick,
};

typedef struct {
    uint8_t frame[FRAME_BYTES_MAX];
    uint8_t width;
    uint8_t height;
    bool has_frame;
    char name[HADAL_PACK_NAME_SIZE];
    uint8_t index; // 0: Built-in
    uint8_t count;
    bool in_use;
    HadalPackInfo info;
} PackPreviewModel;

struct HadalPackPreview {
    View* view;
    FuriTimer* timer;
    CompressIcon* compress;
    uint8_t* file;
    FuriString* path;
    FuriString* anim_dir;
    uint32_t order[PREVIEW_ORDER_MAX];
    uint8_t order_len;
    uint8_t position;
    uint8_t width;
    uint8_t height;
    // the frames in one frames.bin (the firmware's own animations): where each starts
    bool packed;
    uint16_t packed_count;
    uint32_t packed_offset[FRAMES_BIN_MAX];
    uint16_t packed_size[FRAMES_BIN_MAX];
};

static void pack_preview_draw(Canvas* canvas, void* model_ptr) {
    PackPreviewModel* model = model_ptr;
    canvas_clear(canvas);

    if(model->has_frame) {
        // where the desktop draws it: on the screen's bottom, under the bar here
        canvas_draw_xbm(canvas, 0, 64 - model->height, model->width, model->height, model->frame);
    } else {
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str_aligned(canvas, 64, 20, AlignCenter, AlignCenter, model->name);
        canvas_set_font(canvas, FontSecondary);
        char line[40];
        if(model->index == 0) {
            snprintf(line, sizeof(line), "The firmware's own look");
        } else if(!model->info.icons && !model->info.fonts) {
            snprintf(line, sizeof(line), "Nothing in it to use");
        } else {
            snprintf(
                line,
                sizeof(line),
                "%u icon%s, %u font%s",
                model->info.icons,
                model->info.icons == 1 ? "" : "s",
                model->info.fonts,
                model->info.fonts == 1 ? "" : "s");
        }
        canvas_draw_str_aligned(canvas, 64, 34, AlignCenter, AlignCenter, line);
    }

    // which of how many: dots on a clear pill at the top right, this one filled
    if(model->count > 1 && model->count <= PAGE_DOTS_MAX) {
        const int32_t width = model->count * 4 + 3;
        const int32_t left = 126 - width;
        canvas_set_color(canvas, ColorWhite);
        canvas_draw_rbox(canvas, left, 0, width, 7, 2);
        canvas_set_color(canvas, ColorBlack);
        for(uint8_t i = 0; i < model->count; i++) {
            const int32_t x = left + 3 + i * 4;
            if(i == model->index) {
                canvas_draw_box(canvas, x - 1, 2, 3, 3);
            } else {
                canvas_draw_dot(canvas, x, 3);
            }
        }
    }

    // the bar, as every screen's hint bar: Left and Right with the pack's name, then OK
    canvas_set_color(canvas, ColorWhite);
    canvas_draw_box(canvas, 0, BAR_TOP, 128, 64 - BAR_TOP);
    canvas_set_color(canvas, ColorBlack);
    for(int32_t x = 2; x < 126; x += 2) {
        canvas_draw_dot(canvas, x, BAR_TOP);
    }
    canvas_set_font(canvas, FontSecondary);
    const uint16_t right_width =
        elements_hints(canvas, 126, 61, AlignRight, model->in_use ? "In use" : "ok=Use");
    const uint16_t arrows = elements_hints(canvas, 2, 61, AlignLeft, "lr=");
    canvas_set_font(canvas, FontPrimary);
    FuriString* name = furi_string_alloc_set_str(model->name);
    elements_string_fit_width(canvas, name, 126 - right_width - 6 - (2 + arrows + 3));
    canvas_draw_str(canvas, 2 + arrows + 3, 61, furi_string_get_cstr(name));
    furi_string_free(name);
}

static bool pack_preview_input(InputEvent* event, void* context) {
    HadalSettings* app = context;
    if(event->type != InputTypeShort && event->type != InputTypeRepeat) return false;
    uint32_t custom;
    if(event->key == InputKeyLeft) {
        custom = EventPrev;
    } else if(event->key == InputKeyRight) {
        custom = EventNext;
    } else if(event->key == InputKeyOk && event->type == InputTypeShort) {
        custom = EventUse;
    } else {
        return false;
    }
    view_dispatcher_send_custom_event(app->view_dispatcher, custom);
    return true;
}

static void pack_preview_timer(void* context) {
    HadalSettings* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, EventTick);
}

/** The frame `number` of the animation being played, into the model; false if it cannot be read */
static bool pack_preview_load_frame(HadalSettings* app, uint32_t number) {
    HadalPackPreview* preview = app->pack_preview;
    furi_string_printf(
        preview->path, "%s/frame_%lu.bm", furi_string_get_cstr(preview->anim_dir), number);
    File* file = storage_file_alloc(app->storage);
    size_t size = 0;
    if(preview->packed) {
        // one file: the frame's blob at its offset
        furi_string_printf(
            preview->path, "%s/frames.bin", furi_string_get_cstr(preview->anim_dir));
        const uint16_t frame_size = number < preview->packed_count ? preview->packed_size[number] :
                                                                     0;
        if(frame_size && frame_size <= FRAME_FILE_MAX &&
           storage_file_open(
               file, furi_string_get_cstr(preview->path), FSAM_READ, FSOM_OPEN_EXISTING) &&
           storage_file_seek(file, preview->packed_offset[number], true)) {
            size = storage_file_read(file, preview->file, frame_size);
            if(size != frame_size) size = 0;
        }
    } else if(storage_file_open(
                  file, furi_string_get_cstr(preview->path), FSAM_READ, FSOM_OPEN_EXISTING)) {
        const uint64_t file_size = storage_file_size(file);
        if(file_size > 0 && file_size <= FRAME_FILE_MAX) {
            size = storage_file_read(file, preview->file, (size_t)file_size);
            if(size != file_size) size = 0;
        }
    }
    storage_file_free(file);
    const size_t bytes = ((preview->width + 7) / 8) * preview->height;
    if(!size || !bytes || bytes > FRAME_BYTES_MAX) return false;
    uint8_t* bitmap = NULL;
    compress_icon_decode(preview->compress, preview->file, &bitmap);
    if(!bitmap) return false;
    with_view_model(
        preview->view,
        PackPreviewModel * model,
        {
            memcpy(model->frame, bitmap, bytes);
            model->width = preview->width;
            model->height = preview->height;
            model->has_frame = true;
        },
        true);
    return true;
}

/** Where each frame starts in the animation's frames.bin, if it has one: header (magic, count,
 * reserved), the frames' sizes, then the frames one after another */
static void pack_preview_read_frames_bin(HadalSettings* app) {
    HadalPackPreview* preview = app->pack_preview;
    preview->packed = false;
    furi_string_printf(preview->path, "%s/frames.bin", furi_string_get_cstr(preview->anim_dir));
    File* file = storage_file_alloc(app->storage);
    struct {
        uint32_t magic;
        uint16_t count;
        uint16_t reserved;
    } header;
    if(storage_file_open(
           file, furi_string_get_cstr(preview->path), FSAM_READ, FSOM_OPEN_EXISTING) &&
       storage_file_read(file, &header, sizeof(header)) == sizeof(header) &&
       header.magic == FRAMES_BIN_MAGIC && header.count && header.count <= FRAMES_BIN_MAX &&
       storage_file_read(file, preview->packed_size, header.count * sizeof(uint16_t)) ==
           header.count * sizeof(uint16_t)) {
        uint32_t offset = sizeof(header) + header.count * sizeof(uint16_t);
        for(uint16_t i = 0; i < header.count; i++) {
            preview->packed_offset[i] = offset;
            offset += preview->packed_size[i];
        }
        preview->packed_count = header.count;
        preview->packed = true;
    }
    storage_file_free(file);
}

/** The first animation of the pack on show (its manifest's first), how its frames go, and how
 * fast; false if it has none the desktop could play */
static bool pack_preview_find_animation(HadalSettings* app, const char* root, uint32_t* period) {
    HadalPackPreview* preview = app->pack_preview;
    bool found = false;
    FlipperFormat* ff = flipper_format_file_alloc(app->storage);
    FuriString* text = furi_string_alloc();
    uint32_t version = 0;
    do {
        furi_string_printf(preview->path, "%s/manifest.txt", root);
        if(!flipper_format_file_open_existing(ff, furi_string_get_cstr(preview->path))) break;
        if(!flipper_format_read_header(ff, text, &version)) break;
        if(!flipper_format_read_string(ff, "Name", text)) break;
        furi_string_printf(preview->anim_dir, "%s/%s", root, furi_string_get_cstr(text));
        flipper_format_file_close(ff);

        furi_string_printf(preview->path, "%s/meta.txt", furi_string_get_cstr(preview->anim_dir));
        if(!flipper_format_file_open_existing(ff, furi_string_get_cstr(preview->path))) break;
        if(!flipper_format_read_header(ff, text, &version)) break;
        uint32_t width = 0, height = 0, passive = 0, rate = 0, count = 0;
        if(!flipper_format_read_uint32(ff, "Width", &width, 1)) break;
        if(!flipper_format_read_uint32(ff, "Height", &height, 1)) break;
        if(!flipper_format_read_uint32(ff, "Passive frames", &passive, 1)) break;
        if(!flipper_format_get_value_count(ff, "Frames order", &count) || !count) break;
        if(width > 128 || height > 64 || !width || !height) break;
        // the idle part only: the passive frames, as the desktop plays them unbothered
        const uint32_t played = MIN(MIN(count, passive ? passive : count), PREVIEW_ORDER_MAX);
        uint32_t order[PREVIEW_ORDER_MAX + 64];
        if(count > COUNT_OF(order)) count = COUNT_OF(order);
        if(!flipper_format_read_uint32(ff, "Frames order", order, count)) break;
        flipper_format_rewind(ff);
        if(!flipper_format_read_uint32(ff, "Frame rate", &rate, 1) || !rate) rate = 2;
        memcpy(preview->order, order, played * sizeof(uint32_t));
        preview->order_len = played;
        preview->width = width;
        preview->height = height;
        *period = CLAMP(1000 / rate, 1000U, 100U);
        found = true;
        pack_preview_read_frames_bin(app);
    } while(false);
    flipper_format_free(ff);
    furi_string_free(text);
    return found;
}

/** The pack app->pack_chosen on show: its name, whether it is in use, its animation */
static void pack_preview_show(HadalSettings* app) {
    HadalPackPreview* preview = app->pack_preview;
    furi_timer_stop(preview->timer);
    const bool builtin = app->pack_chosen == 0;
    const char* name = builtin ? "Built-in" : app->pack_names[app->pack_chosen - 1];

    DesktopSettings* settings = hadal_settings_desktop_get(app);
    const bool in_use = builtin ? settings->active_asset_pack[0] == '\0' :
                                  strcmp(name, settings->active_asset_pack) == 0;
    free(settings);

    HadalPackInfo info = {0};
    if(!builtin) hadal_pack_info_scan(app->storage, name, &info);
    with_view_model(
        preview->view,
        PackPreviewModel * model,
        {
            strlcpy(model->name, name, sizeof(model->name));
            model->index = app->pack_chosen;
            model->count = app->pack_count + 1;
            model->in_use = in_use;
            model->info = info;
            model->has_frame = false;
        },
        true);

    FuriString* root = furi_string_alloc_set_str(DOLPHIN_PATH);
    if(!builtin) furi_string_printf(root, "%s/%s/Anims", HADAL_PACKS_PATH, name);
    uint32_t period = 500;
    preview->order_len = 0;
    preview->position = 0;
    if(pack_preview_find_animation(app, furi_string_get_cstr(root), &period) &&
       pack_preview_load_frame(app, preview->order[0]) && preview->order_len > 1) {
        furi_timer_start(preview->timer, furi_ms_to_ticks(period));
    }
    furi_string_free(root);
}

/* Made the first time the page opens, and kept until Hadal closes (hadal_settings_pack_preview_free):
 * a view freed on the way out, while it still handles the Back that leaves, closed all of Hadal */
static HadalPackPreview* pack_preview_alloc(HadalSettings* app) {
    HadalPackPreview* preview = malloc(sizeof(HadalPackPreview));
    preview->compress = compress_icon_alloc(FRAME_BYTES_MAX);
    preview->file = malloc(FRAME_FILE_MAX);
    preview->path = furi_string_alloc();
    preview->anim_dir = furi_string_alloc();
    preview->timer = furi_timer_alloc(pack_preview_timer, FuriTimerTypePeriodic, app);
    preview->view = view_alloc();
    view_allocate_model(preview->view, ViewModelTypeLocking, sizeof(PackPreviewModel));
    view_set_context(preview->view, app);
    view_set_draw_callback(preview->view, pack_preview_draw);
    view_set_input_callback(preview->view, pack_preview_input);
    view_dispatcher_add_view(app->view_dispatcher, HadalViewPackPreview, preview->view);
    return preview;
}

void hadal_settings_pack_preview_free(HadalSettings* app) {
    HadalPackPreview* preview = app->pack_preview;
    if(!preview) return;
    furi_timer_stop(preview->timer);
    furi_timer_free(preview->timer);
    view_dispatcher_remove_view(app->view_dispatcher, HadalViewPackPreview);
    view_free(preview->view);
    compress_icon_free(preview->compress);
    free(preview->file);
    furi_string_free(preview->path);
    furi_string_free(preview->anim_dir);
    free(preview);
    app->pack_preview = NULL;
}

void hadal_settings_scene_asset_pack_on_enter(void* context) {
    HadalSettings* app = context;
    if(!app->pack_preview) app->pack_preview = pack_preview_alloc(app);

    // it opens on the pack in use
    app->pack_count = hadal_pack_list(app->storage, app->pack_names, HADAL_MAX_PACKS);
    DesktopSettings* settings = hadal_settings_desktop_get(app);
    app->pack_chosen = 0;
    for(size_t i = 0; i < app->pack_count; i++) {
        if(strcmp(app->pack_names[i], settings->active_asset_pack) == 0) app->pack_chosen = i + 1;
    }
    free(settings);
    pack_preview_show(app);
    view_dispatcher_switch_to_view(app->view_dispatcher, HadalViewPackPreview);
}

bool hadal_settings_scene_asset_pack_on_event(void* context, SceneManagerEvent event) {
    HadalSettings* app = context;
    HadalPackPreview* preview = app->pack_preview;
    if(event.type != SceneManagerEventTypeCustom) return false;
    const size_t count = app->pack_count + 1;
    if(event.event == EventPrev || event.event == EventNext) {
        app->pack_chosen = event.event == EventNext ? (app->pack_chosen + 1) % count :
                                                      (app->pack_chosen + count - 1) % count;
        pack_preview_show(app);
    } else if(event.event == EventTick) {
        if(preview->order_len) {
            preview->position = (preview->position + 1) % preview->order_len;
            pack_preview_load_frame(app, preview->order[preview->position]);
        }
    } else if(event.event == EventUse) {
        DesktopSettings* settings = hadal_settings_desktop_get(app);
        strlcpy(
            settings->active_asset_pack,
            app->pack_chosen == 0 ? "" : app->pack_names[app->pack_chosen - 1],
            sizeof(settings->active_asset_pack));
        hadal_settings_desktop_set(app, settings);
        with_view_model(preview->view, PackPreviewModel * model, { model->in_use = true; }, true);
    } else {
        return false;
    }
    return true;
}

void hadal_settings_scene_asset_pack_on_exit(void* context) {
    HadalSettings* app = context;
    // the animation stops; the page's view stays until Hadal closes
    furi_timer_stop(app->pack_preview->timer);
}
