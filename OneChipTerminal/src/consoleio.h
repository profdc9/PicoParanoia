// consoleio — terminal console over pico_ntsc + TNTSCAnsi + PS/2 keyboard.
//
// Ported from the OneChipTerminal STM32 consoleio: the API is unchanged so
// OneChipTerminal.ino ports with minimal edits. Output goes through the
// TNTSCAnsi VT100 emulator into a character buffer, which is blitted into
// the pico_ntsc framebuffer via its per-cell hook. Input comes from the PS/2
// keyboard.
#ifndef _CONSOLEIO_H
#define _CONSOLEIO_H

#include <stddef.h>

#define MY_ITOASIZE 40

#ifdef __cplusplus
extern "C" {
#endif

int  console_inchar(void);      // next key, or -1 if none
int  console_getch(void);       // block for a key
void console_putch(char c);
void console_puts(const char *c);
void console_init(void);
void console_printcrlf(void);
void console_clreol(void);
void console_clrscr(void);
void console_gotoxy(int x, int y);
void console_highvideo(void);
void console_lowvideo(void);
void console_printint(int n);
void console_printuint(unsigned int n);
void console_delayframes(unsigned short fr);
char console_selectmenu(const char *menutext, const char *options);
int  console_getstring(char *c, int len, int row, int col, int wrap);
void console_press_space(void);
int  console_yes(int row);
void console_print_array(const char *c, unsigned char *a, int n);
char *myltoa(char *p, long n);
long  mystrtol(const char *str, const char **end);
char *strcpy_n(char *dest, const char *src, size_t len);
char *strcat_n(char *dest, const char *src, size_t len);
size_t strlen_n(const char *d);

#ifdef __cplusplus
}
#endif

#endif  /* _CONSOLEIO_H */
