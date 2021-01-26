#ifndef __DEV__VGA_TEXTMODE_H__
#define __DEV__VGA_TEXTMODE_H__

#include <stdbool.h>
#include <stdint.h>

bool vga_textmode_init(void);
void text_set_cursor_palette(uint8_t c);
uint8_t text_get_cursor_palette(void);
void text_set_text_palette(uint8_t c);
uint8_t text_get_text_palette(void);
int text_get_cursor_pos_x(void);
int text_get_cursor_pos_y(void);
void text_set_cursor_pos(int x, int y);
void text_putchar(char c);
void text_disable_cursor(void);
void text_enable_cursor(void);
void text_clear(void);

#endif
