#include "widget_element_i.h"

#include <gui/elements.h>

/* The header every Hadal page has (elements_header), as a widget's element: for the pages made of
 * widgets, a file's info say, to look like every other */

typedef struct {
    FuriString* text;
} GuiHeaderModel;

static void gui_header_draw(Canvas* canvas, WidgetElement* element) {
    furi_assert(canvas);
    furi_assert(element);
    GuiHeaderModel* model = element->model;
    elements_header(canvas, furi_string_get_cstr(model->text));
}

static void gui_header_free(WidgetElement* gui_header) {
    furi_assert(gui_header);
    GuiHeaderModel* model = gui_header->model;
    furi_string_free(model->text);
    free(gui_header->model);
    free(gui_header);
}

WidgetElement* widget_element_header_create(const char* text) {
    GuiHeaderModel* model = malloc(sizeof(GuiHeaderModel));
    model->text = furi_string_alloc_set_str(text);

    WidgetElement* gui_header = malloc(sizeof(WidgetElement));
    gui_header->parent = NULL;
    gui_header->input = NULL;
    gui_header->draw = gui_header_draw;
    gui_header->free = gui_header_free;
    gui_header->model = model;

    return gui_header;
}
