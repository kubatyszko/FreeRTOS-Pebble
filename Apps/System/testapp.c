/* testapp.c
 * routines for Testing each feature of RebbleOS
 * Please add your test in here with your code submission
 * tests list in the SystemApp/tests folder. Put them in config.mk
 * RebbleOS
 *
 * Author: Barry Carter <barry.carter@gmail.com>
 */

#include "rebbleos.h"
#include "menu.h"
#include "status_bar_layer.h"
#include "test_defs.h"

static Window *s_main_window;
static Menu *s_menu;
StatusBarLayer *status_bar;


typedef bool (*init_test)(Window *);
typedef bool (*run_test)();

typedef struct app_test_t {
    const char *test_name;
    const char *test_desc;
    init_test test_init;
    run_test test_execute;
    run_test test_deinit;
} app_test;

/*TODO
 * something about hooking the buttons before we switch to the test
 * i'm sure tests will want input sometimes. Leave it to them?
 * either way stop the menu doing it's thing in the background
 *  - menu destroy/rebuild?
 * 
 * move tests to test file
 */


app_test _tests[] = {
    {
        .test_name = "Test the Test",
        .test_desc = "Verify this app",
        .test_init = &test_test_init,
        .test_execute = &test_test_exec,
        .test_deinit = &test_test_deinit,
    },
    {
        .test_name = "Colour Test",
        .test_desc = "Palette",
        .test_init = &color_test_init,
        .test_execute = &color_test_exec,
        .test_deinit = &color_test_deinit,
    }
};

#define TEST_COUNT sizeof(_tests) / sizeof(app_test)

app_test *_test_running = NULL;

void test_complete(bool success)
{
    SYS_LOG("test", APP_LOG_LEVEL_ERROR, "Test Complete: %s", _test_running->test_name);
    _test_running = NULL;
}

static MenuItems* test_test_item_selected(const MenuItem *item)
{
    app_test *test = (app_test *)item->context;
    SYS_LOG("test", APP_LOG_LEVEL_ERROR, "Running Test: %s", test->test_name);
    test->test_init(s_main_window);
    _test_running = test;
    test->test_execute();
    // HMM!
    
    return NULL;
        
}

static void exit_to_watchface(struct Menu *menu, void *context)
{
    if (_test_running != NULL)
    {
        _test_running->test_deinit();
        _test_running = NULL;
        Layer *window_layer = window_get_root_layer(s_main_window);
        layer_mark_dirty(window_layer);

        return;
    }

    // Exit to watchface
    appmanager_app_start("Simple");
}

static void testapp_window_load(Window *window)
{
    Layer *window_layer = window_get_root_layer(s_main_window);
#ifdef PBL_RECT
    s_menu = menu_create(GRect(0, 16, DISPLAY_COLS, DISPLAY_ROWS - 16));
#else
    // Let the menu draw behind the statusbar so it is perfectly centered
    s_menu = menu_create(GRect(0, 0, DISPLAY_COLS, DISPLAY_ROWS));
#endif
    menu_set_callbacks(s_menu, s_menu, (MenuCallbacks) {
        .on_menu_exit = exit_to_watchface
    });
    layer_add_child(window_layer, menu_get_layer(s_menu));

    menu_set_click_config_onto_window(s_menu, window);

    MenuItems *items = menu_items_create(TEST_COUNT);
    for(int i = 0; i < TEST_COUNT; i++)
    {
        MenuItem mi = MenuItem(_tests[i].test_name, _tests[i].test_desc, 25, test_test_item_selected);
        mi.context = &_tests[i];
        menu_items_add(items, mi);
    }
    
    menu_set_items(s_menu, items);
    
    // Status Bar
    status_bar = status_bar_layer_create();
    layer_add_child(menu_get_layer(s_menu), status_bar_layer_get_layer(status_bar));

    //tick_timer_service_subscribe(MINUTE_UNIT, prv_tick_handler);
}

static void testapp_window_unload(Window *window)
{
    menu_destroy(s_menu);
}

void testapp_init(void)
{
    s_main_window = window_create();

    window_set_window_handlers(s_main_window, (WindowHandlers) {
        .load = testapp_window_load,
        .unload = testapp_window_unload,
    });
    
    window_stack_push(s_main_window, true);
}

void testapp_deinit(void)
{
    window_destroy(s_main_window);
}

void testapp_main(void)
{
    testapp_init();
    app_event_loop();
    testapp_deinit();
}
