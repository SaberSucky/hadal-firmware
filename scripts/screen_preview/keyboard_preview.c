#include "preview.h"
#include "../../applications/services/gui/modules/text_input.c"

static TextInputModel model;
static char buffer[32];

static void draw(Canvas* canvas, void* context) {
    UNUSED(context);
    text_input_view_draw_callback(canvas, &model);
}

int main(int argc, char** argv) {
    if(argc > 1) preview_set_dir(argv[1]);
    memset(&model, 0, sizeof(model));
    model.header = "Name the signal";
    model.text_buffer = buffer;
    model.text_buffer_size = sizeof(buffer);
    strcpy(buffer, "Garage door");
    model.cursor_pos = strlen(buffer);
    model.validator_text = furi_string_alloc();

    model.selected_row = 2;
    model.selected_column = 9; // Save
    preview_draw("keyboard_letters_save", draw, NULL);

    model.selected_column = 8; // the symbols key
    preview_draw("keyboard_letters_symbols_key", draw, NULL);

    model.symbols = true;
    model.selected_column = 7;
    preview_draw("keyboard_symbols", draw, NULL);

    model.selected_column = 5; // space
    preview_draw("keyboard_symbols_space", draw, NULL);

    model.selected_row = 0;
    model.selected_column = 13;
    strcpy(buffer, "p@ss");
    model.cursor_pos = 4;
    preview_draw("keyboard_symbols_top", draw, NULL);

    // every key of both pages selected, and moves between rows, for the sanitizers
    Canvas* canvas = preview_canvas();
    for(int page = 0; page < 2; page++) {
        model.symbols = page;
        for(uint8_t row = 0; row < keyboard_row_count; row++) {
            for(uint8_t column = 0; column < get_row_size(&model, row); column++) {
                model.selected_row = row;
                model.selected_column = column;
                text_input_view_draw_callback(canvas, &model);
                text_input_handle_up(NULL, &model);
                model.cursor_select = false;
                text_input_handle_down(NULL, &model);
                text_input_handle_down(NULL, &model);
                if(model.selected_column >= get_row_size(&model, model.selected_row)) return 1;
            }
        }
    }
    return 0;
}
