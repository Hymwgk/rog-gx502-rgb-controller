#ifndef KEYBOARD_SETTINGS_H
#define KEYBOARD_SETTINGS_H
#include "keyboard_controls.h"
/* Missing/invalid storage leaves the caller's defaults unchanged. */
int keyboard_settings_load(keyboard_controls_t *controls);
/* Returns 0 on success, including an unchanged setting; -1 on write failure.
 * Caller must stop GCLK and disable all banks while saving. */
int keyboard_settings_save(const keyboard_controls_t *controls);
#endif
