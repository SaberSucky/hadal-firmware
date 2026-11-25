#pragma once

#include <gui/view.h>
#include <gui/icon.h>

/* The main menu as a row of app tiles, as the control centre picks its apps: the chosen one in the
 * middle with its name under it. OK lifts an app and Left/Right carry it along the row, OK puts it
 * down; Down takes it out of the menu. The row ends in a + tile (add an app) and, for a menu that
 * was changed, a reset tile. Adding shows the apps that can come in, in the same row. */

#define MAIN_MENU_VIEW_TILES_MAX 70u
#define MAIN_MENU_VIEW_LABEL     24u

typedef enum {
    MainMenuTileApp, /**< an app of the menu, or one that can be added */
    MainMenuTileAdd, /**< the + at the end of the row */
    MainMenuTileReset, /**< back to the firmware's own order */
} MainMenuTileKind;

typedef struct {
    char label[MAIN_MENU_VIEW_LABEL];
    const Icon* icon; /**< NULL: the label's initial stands in */
    MainMenuTileKind kind;
    bool fixed; /**< cannot be taken out (Settings) */
} MainMenuTile;

/** What the view shows; the scene fills it between main_menu_view_lock() and _unlock() */
typedef struct {
    char title[MAIN_MENU_VIEW_LABEL];
    MainMenuTile tiles[MAIN_MENU_VIEW_TILES_MAX];
    size_t count;
    size_t apps; /**< how many of the tiles are apps (they come first) */
    size_t selected;
    bool lifted; /**< the selected app is being carried */
    bool adding; /**< the tiles are the apps that can be added */
} MainMenuViewModel;

typedef enum {
    MainMenuViewEventLift = 1, /**< index: the app picked up */
    MainMenuViewEventMove, /**< index: 0 to the left, 1 to the right */
    MainMenuViewEventDrop,
    MainMenuViewEventCancel, /**< Back while carrying: it goes back where it was */
    MainMenuViewEventHide, /**< index: the app to take out */
    MainMenuViewEventOpenAdd,
    MainMenuViewEventReset,
    MainMenuViewEventPick, /**< adding, index: the app chosen */
    MainMenuViewEventCloseAdd,
} MainMenuViewEvent;

typedef void (*MainMenuViewCallback)(void* context, MainMenuViewEvent event, size_t index);

typedef struct MainMenuView MainMenuView;

MainMenuView* main_menu_view_alloc(void);
void main_menu_view_free(MainMenuView* view);
View* main_menu_view_get_view(MainMenuView* view);
void main_menu_view_set_callback(MainMenuView* view, MainMenuViewCallback callback, void* context);

/** The model, held: nothing draws it until main_menu_view_unlock() */
MainMenuViewModel* main_menu_view_lock(MainMenuView* view);
/** Lets it go and redraws */
void main_menu_view_unlock(MainMenuView* view);
