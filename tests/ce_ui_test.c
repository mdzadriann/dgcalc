#include "platform/ce/ce_ui.h"
#include "test.h"

#include <graphx.h>
#include <keypadc.h>

#include <stdint.h>
#include <string.h>

uint16_t fake_gfx_palette[256];
uint8_t kb_Data[8];
uint8_t fake_kb_mode;

typedef struct {
    char text[16];
    int x;
    uint8_t color;
} FooterPrint;

typedef enum {
    TEST_COPY_SCREEN_TO_BUFFER,
    TEST_DRAW_DIALOG,
    TEST_COPY_BUFFER_TO_SCREEN,
    TEST_SWAP_BUFFERS
} TestDrawOperation;

static const kb_lkey_t *scan_keys;
static size_t scan_key_count;
static size_t scan_key_index;
static int screen_blits;
static int buffer_blits;
static int swaps;
static uint8_t draw_color;
static uint8_t text_color;
static uint8_t transparent_text_color;
static bool dialog_draw_pending;
static TestDrawOperation draw_operations[32];
static size_t draw_operation_count;
static FooterPrint footer_prints[8];
static size_t footer_print_count;
static int footer_highlights[5];
static uint8_t footer_highlight_colors[5];
static size_t footer_highlight_count;

static void record_operation(TestDrawOperation operation) {
    if (draw_operation_count <
        sizeof(draw_operations) / sizeof(draw_operations[0])) {
        draw_operations[draw_operation_count++] = operation;
    }
}

static void reset_fake_hardware(void) {
    memset(kb_Data, 0, sizeof(kb_Data));
    scan_keys = NULL;
    scan_key_count = 0;
    scan_key_index = 0;
    screen_blits = 0;
    buffer_blits = 0;
    swaps = 0;
    draw_color = 0;
    text_color = 0;
    transparent_text_color = 0;
    fake_kb_mode = MODE_3_CONTINUOUS;
    dialog_draw_pending = false;
    draw_operation_count = 0;
    footer_print_count = 0;
    footer_highlight_count = 0;
}

static void script_keys(const kb_lkey_t *keys, size_t count) {
    scan_keys = keys;
    scan_key_count = count;
    scan_key_index = 0;
}

void kb_Scan(void) {
    kb_lkey_t key = 0;

    memset(kb_Data, 0, sizeof(kb_Data));
    if (scan_key_index < scan_key_count) {
        key = scan_keys[scan_key_index++];
    }
    if (key != 0) {
        kb_Data[key >> 8] = (uint8_t)key;
    }
    fake_kb_mode = MODE_0_IDLE;
}

void fake_kb_set_mode(uint8_t mode) { fake_kb_mode = mode; }

uint16_t gfx_RGBTo1555(uint8_t red, uint8_t green, uint8_t blue) {
    return (uint16_t)(red + green + blue);
}

void gfx_Begin(void) {}
void gfx_End(void) {}
void fake_gfx_set_draw_buffer(void) {}
void gfx_SetTextScale(uint8_t width, uint8_t height) {
    (void)width;
    (void)height;
}
void gfx_SetTextFGColor(uint8_t color) { text_color = color; }
void gfx_SetTextBGColor(uint8_t color) { (void)color; }
void gfx_SetTextTransparentColor(uint8_t color) {
    transparent_text_color = color;
}
void gfx_SetColor(uint8_t color) { draw_color = color; }
void gfx_FillScreen(uint8_t color) { (void)color; }
void gfx_FillRectangle_NoClip(int x, int y, int width, int height) {
    if (dialog_draw_pending) {
        record_operation(TEST_DRAW_DIALOG);
        dialog_draw_pending = false;
    }
    if (y == 221 && width == 63 && height == 19 &&
        footer_highlight_count < 5) {
        footer_highlights[footer_highlight_count] = x;
        footer_highlight_colors[footer_highlight_count++] = draw_color;
    }
}
void gfx_Rectangle_NoClip(int x, int y, int width, int height) {
    (void)x;
    (void)y;
    (void)width;
    (void)height;
}
void gfx_HorizLine_NoClip(int x, int y, int length) {
    (void)x;
    (void)y;
    (void)length;
}
void gfx_VertLine_NoClip(int x, int y, int length) {
    (void)x;
    (void)y;
    (void)length;
}
void gfx_Line(int x0, int y0, int x1, int y1) {
    (void)x0;
    (void)y0;
    (void)x1;
    (void)y1;
}
void gfx_SetTextXY(int x, int y) {
    (void)x;
    (void)y;
}
void gfx_PrintUInt(unsigned int value, int digits) {
    (void)value;
    (void)digits;
}
int gfx_GetStringWidth(const char *text) {
    return (int)strlen(text) * 8;
}
void gfx_PrintStringXY(const char *text, int x, int y) {
    if (y >= 220 && footer_print_count < 8) {
        FooterPrint *printed = &footer_prints[footer_print_count++];
        size_t length = strlen(text);

        if (length >= sizeof(printed->text)) {
            length = sizeof(printed->text) - 1;
        }
        memcpy(printed->text, text, length);
        printed->text[length] = '\0';
        printed->x = x;
        printed->color = text_color;
    }
}
void gfx_Blit(gfx_location_t source) {
    if (source == gfx_screen) {
        screen_blits++;
        record_operation(TEST_COPY_SCREEN_TO_BUFFER);
        dialog_draw_pending = true;
    } else {
        buffer_blits++;
        record_operation(TEST_COPY_BUFFER_TO_SCREEN);
    }
}
void gfx_SwapDraw(void) {
    swaps++;
    record_operation(TEST_SWAP_BUFFERS);
}

static void test_every_menu_selection_preserves_normal_draw_buffer_roles(void) {
    static const AppAction expected_actions[] = {
        APP_ACTION_NEW_PROJECT,
        APP_ACTION_SAVE_PROJECT,
        APP_ACTION_LOAD_PROJECT,
        APP_ACTION_TOGGLE_GRAPH_MODE,
        APP_ACTION_QUIT
    };
    AppState state;
    Project project;
    size_t selection;

    for (selection = 0; selection < 5; selection++) {
        kb_lkey_t keys[15];
        size_t key_count = 0;
        size_t down;
        size_t frame;

        for (down = 0; down < selection; down++) {
            keys[key_count++] = 0;
            keys[key_count++] = kb_KeyDown;
            keys[key_count++] = 0;
        }
        keys[key_count++] = 0;
        keys[key_count++] = kb_KeyEnter;
        keys[key_count++] = 0;

        reset_fake_hardware();
        script_keys(keys, key_count);
        ASSERT_EQ_INT(expected_actions[selection], ce_ui_show_menu());
        ASSERT_EQ_INT(selection + 1, screen_blits);
        ASSERT_EQ_INT(selection + 1, buffer_blits);
        ASSERT_EQ_INT(0, swaps);
        ASSERT_EQ_INT(MODE_3_CONTINUOUS, kb_GetMode());
        ASSERT_EQ_INT((selection + 1) * 3, draw_operation_count);
        for (frame = 0; frame <= selection; frame++) {
            ASSERT_EQ_INT(TEST_COPY_SCREEN_TO_BUFFER,
                          draw_operations[frame * 3]);
            ASSERT_EQ_INT(TEST_DRAW_DIALOG,
                          draw_operations[frame * 3 + 1]);
            ASSERT_EQ_INT(TEST_COPY_BUFFER_TO_SCREEN,
                          draw_operations[frame * 3 + 2]);
        }

        app_state_init(&state);
        project_init(&project);
        ce_ui_draw(&state, &project, false);
        ASSERT_EQ_INT(TEST_SWAP_BUFFERS,
                      draw_operations[draw_operation_count - 1]);
        project_dispose(&project);
    }
}

static void test_trace_closes_the_project_menu_without_an_action(void) {
    static const kb_lkey_t keys[] = {
        0, kb_KeyTrace, 0,
        0, kb_KeyClear, 0
    };

    reset_fake_hardware();
    script_keys(keys, sizeof(keys) / sizeof(keys[0]));

    ASSERT_EQ_INT(APP_ACTION_NONE, ce_ui_show_menu());
    ASSERT_EQ_INT(3, scan_key_index);
    ASSERT_EQ_INT(MODE_3_CONTINUOUS, kb_GetMode());
}

static void test_footer_uses_five_non_overlapping_key_slots(void) {
    static const char *const expected[] = {
        "Expr", "Split", "2D/3D", "Menu", "Graph"
    };
    AppState state;
    Project project;
    size_t index;

    reset_fake_hardware();
    app_state_init(&state);
    project_init(&project);
    ce_ui_draw(&state, &project, false);

    ASSERT_EQ_INT(5, footer_print_count);
    for (index = 0; index < 5 && index < footer_print_count; index++) {
        int right = footer_prints[index].x +
                    gfx_GetStringWidth(footer_prints[index].text);

        ASSERT_STR_EQ(expected[index], footer_prints[index].text);
        ASSERT_TRUE(footer_prints[index].x >= (int)index * 64);
        ASSERT_TRUE(right <= (int)(index + 1) * 64);
    }
    project_dispose(&project);
}

static void test_footer_highlights_the_active_page(void) {
    static const AppView views[] = {
        APP_VIEW_EXPRESSIONS, APP_VIEW_SPLIT, APP_VIEW_GRAPH
    };
    static const int expected_left[] = {1, 65, 257};
    static const size_t expected_label[] = {0, 1, 4};
    AppState state;
    Project project;
    size_t view_index;
    size_t label;

    for (view_index = 0; view_index < 3; view_index++) {
        reset_fake_hardware();
        ce_ui_begin();
        app_state_init(&state);
        state.view = views[view_index];
        project_init(&project);
        ce_ui_draw(&state, &project, false);

        ASSERT_EQ_INT(1, footer_highlight_count);
        ASSERT_EQ_INT(expected_left[view_index], footer_highlights[0]);
        ASSERT_TRUE(footer_highlight_colors[0] != 0);
        ASSERT_EQ_INT(5, footer_print_count);
        for (label = 0; label < 5; label++) {
            if (label == expected_label[view_index]) {
                ASSERT_TRUE(footer_prints[label].color !=
                            footer_prints[(label + 1) % 5].color);
                ASSERT_TRUE(footer_prints[label].color !=
                            transparent_text_color);
            }
        }
        project_dispose(&project);
    }
}

void run_ce_ui_tests(void) {
    test_every_menu_selection_preserves_normal_draw_buffer_roles();
    test_trace_closes_the_project_menu_without_an_action();
    test_footer_uses_five_non_overlapping_key_slots();
    test_footer_highlights_the_active_page();
}
