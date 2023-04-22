#ifdef BAKING_APP
#include "bolos_target.h"

#include "ui.h"

const bagl_element_t empty_screen_elements[] = {
#if (BAGL_WIDTH == 128 && BAGL_HEIGHT == 64)
    {{BAGL_RECTANGLE, 0x00, 0, 0, 128, 64, 0, 0, BAGL_FILL, 0x000000, 0xFFFFFF, 0, 0},
     .text = NULL},
#elif (BAGL_WIDTH == 128 && BAGL_HEIGHT == 32)
    {{BAGL_RECTANGLE, 0x00, 0, 0, 128, 32, 0, 0, BAGL_FILL, 0x000000, 0xFFFFFF, 0, 0},
     .text = NULL},
#else
#error "BAGL_WIDTH/BAGL_HEIGHT not defined"
#endif
    {{BAGL_LABELINE,
      0x10,
      6,
      12,
      116,
      32,
      0,
      0,
      0,
      0xFFFFFF,
      0x000000,
      BAGL_FONT_OPEN_SANS_EXTRABOLD_11px | BAGL_FONT_ALIGNMENT_CENTER,
      0},
     .text = NULL}};

typedef struct ux_layout_empty_params_s {
    const char *line1;
} ux_layout_empty_params_t;

void ux_layout_empty_init(unsigned int stack_slot) {
    ux_stack_init(stack_slot);
    G_ux.stack[stack_slot].element_arrays[0].element_array = empty_screen_elements;
    G_ux.stack[stack_slot].element_arrays[0].element_array_count = ARRAYLEN(empty_screen_elements);
    G_ux.stack[stack_slot].element_arrays_count = 1;
    G_ux.stack[stack_slot].button_push_callback = ux_flow_button_callback;
    G_ux.stack[stack_slot].screen_before_element_display_callback = ux_layout_strings_prepro;
    ux_stack_display(stack_slot);
}

void ux_layout_empty_screen_init(unsigned int whatever) {
}

void return_to_idle() {
    ux_idle_screen(NULL, NULL);
}

UX_STEP_CB(empty_screen_step, empty, return_to_idle(), {" "});
UX_STEP_INIT(empty_screen_border, NULL, NULL, { return_to_idle(); });
UX_FLOW(ux_empty_flow, &empty_screen_step, &empty_screen_border, FLOW_LOOP);

void ux_empty_screen() {
    ux_flow_init(0, ux_empty_flow, NULL);
}

#endif