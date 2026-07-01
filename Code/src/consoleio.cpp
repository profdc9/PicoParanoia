// consoleio — see consoleio.h.
//
// Output: characters are fed to the TNTSCAnsi VT100 emulator, which parses ANSI
// escape sequences and updates its character buffer; its per-cell hook
// (tntscansi_hook.h) blits each changed cell into the pico_ntsc framebuffer, so
// there is no separate render/diff step. After each write we sync the hardware
// cursor to the emulator's cursor.
//
// Input: the PS/2 keyboard FIFO, falling back to USB serial (handy for testing
// without a keyboard).

#include "consoleio.h"

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "pico/stdlib.h"

#include "pico_ntsc.h"
#include "TNTSCAnsi.h"
#include "ps2_kbd.h"

#define CON_ROWS 25
#define CON_COLS 80

static screenchartype vbuf[CON_ROWS * CON_COLS];
static int last_cx = -1, last_cy = -1;

static void sync_cursor(void) {
    int cx = TNTSCAnsi.cur.xpos, cy = TNTSCAnsi.cur.ypos;
    if (cx != last_cx || cy != last_cy) {
        pico_ntsc_set_cursor(cy, cx, true);
        last_cx = cx;
        last_cy = cy;
    }
}

void console_init(void)
{
    pico_ntsc_init(PICO_NTSC_MODE_80);      // sets sysclk 126 MHz; call before stdio
    TNTSCAnsi.begin(vbuf, CON_ROWS, CON_COLS);  // clears screen via the blit hook
    ps2_kbd_init();
    last_cx = last_cy = -1;
    sync_cursor();
}

int console_inchar(void)
{
    int ch = ps2_kbd_getkey();
    if (ch >= 0) return ch;
    int u = getchar_timeout_us(0);          // USB serial; <0 when none
    return (u < 0) ? -1 : u;
}

int console_getch(void)
{
    int ch;
    while ((ch = console_inchar()) < 0) tight_loop_contents();
    return ch;
}

void console_putch(char c)
{
    TNTSCAnsi.output_character((unsigned char)c);
    sync_cursor();
}

void console_puts(const char *c)
{
    while (*c) TNTSCAnsi.output_character((unsigned char)*c++);
    sync_cursor();
}

void console_printcrlf(void) { console_puts("\r\n"); }
void console_clreol(void)    { console_puts("\033[K"); }
void console_clrscr(void)    { console_puts("\033[H\033[2J"); }
void console_highvideo(void) { console_puts("\033[1m"); }
void console_lowvideo(void)  { console_puts("\033[0m"); }

void console_printint(int n)
{
    char s[16];
    snprintf(s, sizeof s, "%d", n);
    console_puts(s);
}

void console_printuint(unsigned int n)
{
    char s[16];
    snprintf(s, sizeof s, "%u", n);
    console_puts(s);
}

void console_gotoxy(int x, int y)
{
    console_puts("\033[");
    console_printint(y);
    console_putch(';');
    console_printint(x);
    console_putch('H');
}

void console_delayframes(unsigned short fr)
{
    // ~60.05 Hz frames; no frame counter exposed yet, so approximate with sleep.
    sleep_us((unsigned)fr * 16653u);
}

int console_getstring(char *c, int len, int row, int col, int wrap)
{
    int n = 0;
    for (;;)
    {
        console_gotoxy((n % wrap) + col, (n / wrap) + row);
        int ch = console_getch();
        if (ch == '\r') { c[n] = '\000'; return n; }
        if ((ch >= ' ') && (ch < 127) && (n < len)) { console_putch(ch); c[n++] = ch; }
        if ((ch == '\010') && (n > 0))
        {
            n--;
            console_gotoxy((n % wrap) + col, (n / wrap) + row);
            console_puts(" \010");
        }
    }
}

char console_selectmenu(const char *menutext, const char *options)
{
    console_puts(menutext);
    for (;;)
    {
        int ch = toupper(console_getch());
        const char *scanoptions = options;
        while (*scanoptions)
        {
            if (*scanoptions == ch) { console_putch(ch); return ch; }
            scanoptions++;
        }
    }
}

void console_press_space(void)
{
    console_puts("\r\n\r\nPress SPACE to continue");
    while (console_getch() != ' ');
}

int console_yes(int row)
{
    char response[4];
    console_gotoxy(1, row);
    console_puts("Type YES to continue: ");
    console_getstring(response, 3, row, 23, 10);
    return strcmp(response, "YES");
}

void console_print_array(const char *c, unsigned char *a, int n)
{
    console_printcrlf();
    console_puts(c);
    for (int i = 0; i < n; i++)
    {
        if ((i % 8) == 0) console_printcrlf();
        console_printint(a[i]);
        console_puts(" ");
    }
    console_printcrlf();
}

char *myltoa(char *p, long n)
{
    snprintf(p, MY_ITOASIZE, "%ld", n);
    return p;
}

long mystrtol(const char *str, const char **end)
{
    unsigned long n = 0;
    int neg;
    while (*str == ' ') str++;
    if ((neg = (*str == '-'))) str++;
    if (isdigit((unsigned char)*str))
    {
        for (;;)
        {
            if ((*str >= '0') && (*str <= '9')) n = (n * 10) + (*str++ - '0');
            else break;
        }
    }
    if (end) *end = str;
    return neg ? -((long)n) : (long)n;
}

char *strcpy_n(char *dest, const char *src, size_t len)
{
    char *d = dest;
    while ((len > 0) && (*src)) { *d++ = *src++; len--; }
    *d = '\000';
    return dest;
}

char *strcat_n(char *dest, const char *src, size_t len)
{
    char *d = dest;
    while ((len > 0) && (*d)) { d++; len--; }
    while ((len > 0) && (*src)) { *d++ = *src++; len--; }
    *d = '\000';
    return dest;
}

size_t strlen_n(const char *d)
{
    const char *p = d;
    while (*p) p++;
    return (size_t)(p - d);
}
