// OneChipTerminal — RP2040 port.
//
// PS/2 keyboard + NTSC TV VT100 terminal, bridged to a MAX232-level serial
// link on uart0 (GPIO0=TX, GPIO1=RX, GPIO2=CTS, GPIO3=RTS -- hardware flow
// control, PORTING notes in this repo's plan). Ported from the STM32/Bluepill
// OneChipTerminal.ino; the menu/config logic is largely unchanged, but two
// things differ from a literal port:
//
//  1. The original pokes the NTSC library's raw screendata() array directly
//     in write_message() and do_altkey()'s save/restore, since on the STM32
//     that buffer is scanned out live by hardware -- any write is immediately
//     visible. pico_ntsc has no such exposed live buffer: every visible
//     change must go through TNTSCAnsi so its per-cell hook can blit into the
//     framebuffer. TNTSCAnsi gained two small additions for this
//     (poke_string/poke_region, see TNTSCAnsi.cpp) so those two call sites
//     still go through a write+notify path instead of a raw memcpy/memset.
//  2. RTS/CTS is real RP2040 UART hardware flow control (uart_set_hw_flow)
//     instead of the original's manually bit-banged GPIOs + software FIFO
//     high-watermark logic -- this board's GPIO2/3 are already the uart0
//     hardware CTS/RTS pins. One behavioral consequence: toggling
//     ser_state.rtscts must now actively reprogram the UART peripheral
//     (do_flow_control() below), where the original could get away with just
//     flipping a flag consulted by the software bit-bang in loop().

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "hardware/gpio.h"
#include "hardware/flash.h"

#include "consoleio.h"
#include "TNTSCAnsi.h"
#include "pico_ps2kbd.h"
#include "flashstruct.h"

#define MAGIC_DEFAULT 0xA901BEEFu

#define SERFIFOSIZE 64

struct serfifo
{
  unsigned char buf[SERFIFOSIZE];
  int fifohead;
  int fifotail;
};

static struct serfifo inputfifo;
static struct serfifo outputfifo;

static void initserfifo(struct serfifo *fifo)
{
  fifo->fifohead = fifo->fifotail = 0;
}

static int fifoused(struct serfifo *fifo)
{
  if (fifo->fifotail > fifo->fifohead)
      return (SERFIFOSIZE + fifo->fifohead - fifo->fifotail);
  else
      return (fifo->fifohead - fifo->fifotail);
}

static int getserfifo(struct serfifo *fifo)
{
  int ch;
  int newpos;
  if (fifo->fifotail == fifo->fifohead)
    return -1;
  ch = fifo->buf[fifo->fifotail];
  newpos = fifo->fifotail+1;
  if (newpos >= SERFIFOSIZE) newpos = 0;
  fifo->fifotail = newpos;
  return ch;
}

static void putserfifo(struct serfifo *fifo, int ch)
{
  int newpos = fifo->fifohead + 1;
  if (newpos >= SERFIFOSIZE) newpos = 0;
  if (newpos == fifo->fifotail)
    return;
  fifo->buf[fifo->fifohead] = (unsigned char)ch;
  fifo->fifohead = newpos;
}

static const char intro[] =
  "One Chip Terminal by D.L. Marks (RP2040 port)\r\n"
  "zLib license for software, CC-BY-SA 4.0 for hardware\r\n"
  "\r\n\r\nPress ALT-H for help\r\n\r\n";

typedef struct _serial_state
{
  uint32_t magic;
  uint32_t baud;
  uint8_t stopbits;
  uint8_t parity;
  uint8_t localecho;
  uint8_t rtscts;
  uint8_t crlf;
} serial_state;

static serial_state ser_state;

// This image has no key store or other flash user, so any free sector works;
// 1 MB in is comfortably clear of the (small) program image.
#define CONFIGURATION_STORAGE_OFFSET 0x100000u

static void configuration_read_storage(void)
{
  void *vp[1];
  int b[1];
  vp[0] = (void *)&ser_state;
  b[0] = sizeof(ser_state);
  readflashstruct((void *)(XIP_BASE + CONFIGURATION_STORAGE_OFFSET), 1, vp, b);
}

static void configuration_write_storage(void)
{
  void *vp[1];
  int b[1];
  vp[0] = (void *)&ser_state;
  b[0] = sizeof(ser_state);
  writeflashstruct((void *)(XIP_BASE + CONFIGURATION_STORAGE_OFFSET), 1, vp, b);
}

static void setup_serial_port(struct _serial_state *st)
{
  uart_parity_t parity = (st->parity == 'E') ? UART_PARITY_EVEN
                        : (st->parity == 'O') ? UART_PARITY_ODD
                        : UART_PARITY_NONE;
  uart_set_baudrate(uart0, st->baud);
  uart_set_format(uart0, 8, st->stopbits, parity);
  uart_set_hw_flow(uart0, st->rtscts, st->rtscts);
  initserfifo(&inputfifo);
  initserfifo(&outputfifo);
}

static void write_message(int row, int col, const char *c)
{
  TNTSCAnsi.poke_string(row, col, c);
}

static void do_set_baud_rate(void)
{
  int ch, baud = -1;
  write_message(0,0,"Baud Rate: 0=300,1=1200,2=2400,3=4800,4=9600,5=19200");
  write_message(1,11,"6=38400,7=57600,8=115200,9=230400");
  while ((ch=console_inchar()) < 0);
  switch (ch)
  {
    case '0': baud = 300; break;
    case '1': baud = 1200; break;
    case '2': baud = 2400; break;
    case '3': baud = 4800; break;
    case '4': baud = 9600; break;
    case '5': baud = 19200; break;
    case '6': baud = 38400; break;
    case '7': baud = 57600; break;
    case '8': baud = 115200; break;
    case '9': baud = 230400; break;
  }
  if (baud >= 0)
  {
    char s[20];
    write_message(2,0,"Baud rate set to ");
    myltoa(s, baud);
    write_message(2,18, s);
    ser_state.baud = (uint32_t)baud;
    setup_serial_port(&ser_state);
  } else
    write_message(2,0,"Baud rate not selected");
  sleep_ms(1000);
}

#define CLEAR_ROWS 6

static void write_pause_message(const char *c)
{
  write_message(0,0,c);
  sleep_ms(1000);
}

static void do_set_no_parity(void)
{
  ser_state.parity = 'N';
  setup_serial_port(&ser_state);
  write_pause_message("No parity set");
}

static void do_set_even_parity(void)
{
  ser_state.parity = 'E';
  setup_serial_port(&ser_state);
  write_pause_message("Even parity set");
}

static void do_set_odd_parity(void)
{
  ser_state.parity = 'O';
  setup_serial_port(&ser_state);
  write_pause_message("Odd parity set");
}

static void do_set_one_stop_bit(void)
{
  ser_state.stopbits = 1;
  setup_serial_port(&ser_state);
  write_pause_message("One stop bit set");
}

static void do_set_two_stop_bits(void)
{
  ser_state.stopbits = 2;
  setup_serial_port(&ser_state);
  write_pause_message("Two stop bits set");
}

static void do_local_echo(void)
{
  ser_state.localecho = !ser_state.localecho;
  write_pause_message(ser_state.localecho ? "Local echo on" : "Local echo off");
}

static void do_flow_control(void)
{
  ser_state.rtscts = !ser_state.rtscts;
  // Unlike the STM32 original (a software flag consulted by hand-rolled
  // RTS/CTS bit-banging in loop()), flow control here lives in the UART
  // peripheral itself, so the toggle has to be applied, not just recorded.
  uart_set_hw_flow(uart0, ser_state.rtscts, ser_state.rtscts);
  write_pause_message(ser_state.rtscts ? "Hardware flow control on" : "Hardware flow control off");
}

static void do_crlf(uint8_t ch, const char *c)
{
  ser_state.crlf = ch;
  write_message(0,16,c);
  write_pause_message("CRLF sequence: ");
}

static void do_write_storage(void)
{
  configuration_write_storage();
  write_pause_message("Configuration written to flash");
}

static void do_show_help(void)
{
  write_message(0,0,"Alt-H Help, Alt-B Baud Rate, Alt-N No Parity, Alt-E Even Parity");
  write_message(1,0,"Alt-O Odd Parity, Alt-1 One Stop Bit, Alt-2 Two Stop Bits,");
  write_message(2,0,"Alt-L Toggle Local Echo, Alt-F Toggle Hardware Flow Control,");
  write_message(3,0,"Alt-J LF is CRLF, Alt-M CR is CRLF, Alt-R none is CRLF,");
  write_message(4,0,"Alt-W Write Configuration To Flash");
  while (console_inchar() < 0);
}

static void do_altkey(void)
{
  int rows = TNTSCAnsi.cur.rows;
  int cols = TNTSCAnsi.cur.columns;
  int tchars = rows*cols;
  unsigned char *save_screen;
  int save_xpos, save_ypos;

  if ((save_screen = (unsigned char *)malloc((size_t)tchars)) == NULL) return;
  memcpy(save_screen, TNTSCAnsi.cur.data, (size_t)tchars);
  save_xpos = TNTSCAnsi.cur.xpos;
  save_ypos = TNTSCAnsi.cur.ypos;

  while (pico_ps2kbd_altkey())
  {
    int ch = console_inchar();
    if (ch >= 0)
    {
      TNTSCAnsi.clear_region(0, 0, CLEAR_ROWS-2, cols-1, 0);
      TNTSCAnsi.clear_region(CLEAR_ROWS-1, 0, CLEAR_ROWS-1, cols-1, 0x08);
      ch = toupper(ch);
      switch (ch)
      {
         case 'H':  do_show_help();
                    break;
         case 'B' : do_set_baud_rate();
                    break;
         case 'N':  do_set_no_parity();
                    break;
         case 'E':  do_set_even_parity();
                    break;
         case 'O':  do_set_odd_parity();
                    break;
         case '1':  do_set_one_stop_bit();
                    break;
         case '2':  do_set_two_stop_bits();
                    break;
         case 'L':  do_local_echo();
                    break;
         case 'F':  do_flow_control();
                    break;
         case 'J':  do_crlf('\n', "LF");
                    break;
         case 'M':  do_crlf('\r', "CR");
                    break;
         case 'R':  do_crlf('\000', "none");
                    break;
         case 'W':  do_write_storage();
                    break;
      }
    }
  }

  // Restore via the console (ANSI CUP is 1-based; cur.xpos/ypos are 0-based)
  // and poke_region, not a raw memcpy -- both go through TNTSCAnsi's hook so
  // the framebuffer actually gets the restored contents.
  console_gotoxy(save_xpos+1, save_ypos+1);
  TNTSCAnsi.poke_region(0, 0, save_screen, tchars);
  free(save_screen);
}

int main(void)
{
  configuration_read_storage();
  if (ser_state.magic != MAGIC_DEFAULT)
  {
    ser_state.magic = MAGIC_DEFAULT;
    ser_state.baud = 19200;
    ser_state.stopbits = 1;
    ser_state.parity = 'N';
    ser_state.localecho = 0;
    ser_state.rtscts = 0;
    ser_state.crlf = 0;
  }

  gpio_set_function(0, GPIO_FUNC_UART);   // TX
  gpio_set_function(1, GPIO_FUNC_UART);   // RX
  gpio_set_function(2, GPIO_FUNC_UART);   // CTS
  gpio_set_function(3, GPIO_FUNC_UART);   // RTS
  uart_init(uart0, 19200);
  setup_serial_port(&ser_state);

  console_init();
  console_puts(intro);

  for (;;)
  {
    int ch;
    if (pico_ps2kbd_altkey())
      do_altkey();
    while ((ch = console_inchar()) >= 0)
      putserfifo(&inputfifo, ch);
    while ((ch = getserfifo(&inputfifo)) >= 0)
    {
      if (!uart_is_writable(uart0)) break;
      uart_putc_raw(uart0, (char)ch);
      if (ser_state.localecho)
      {
        if ((ch == ser_state.crlf) && (ser_state.crlf))
        {
          console_putch('\r');
          console_putch('\n');
        } else console_putch((char)ch);
      }
    }
    while (uart_is_readable(uart0))
      putserfifo(&outputfifo, uart_getc(uart0));
    while ((ch = getserfifo(&outputfifo)) >= 0)
    {
      if ((ch == ser_state.crlf) && (ser_state.crlf))
      {
        console_putch('\r');
        console_putch('\n');
      } else console_putch((char)ch);
    }
  }
}
