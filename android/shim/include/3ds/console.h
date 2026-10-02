/**
 * @file console.h
 * @brief libctru's text console (zlib licence, devkitPro).
 *
 * The same engine as libctru (8x8 font, 1-based window, ANSI escapes,
 * RGB565 LCD framebuffer written column by column), drawn into
 * gfxGetFramebuffer and flushed with gfxFlushBuffers/GSPGPU_FlushDataCache.
 * The first consoleInit points stdout and stderr at the console, as libctru
 * does; console text is also copied to logcat.
 */
#pragma once

#include <3ds/types.h>
#include <3ds/gfx.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CONSOLE_ESC(x) "\x1b[" #x
#define CONSOLE_RESET CONSOLE_ESC(0m)
#define CONSOLE_BLACK CONSOLE_ESC(30m)
#define CONSOLE_RED CONSOLE_ESC(31;1m)
#define CONSOLE_GREEN CONSOLE_ESC(32;1m)
#define CONSOLE_YELLOW CONSOLE_ESC(33;1m)
#define CONSOLE_BLUE CONSOLE_ESC(34;1m)
#define CONSOLE_MAGENTA CONSOLE_ESC(35;1m)
#define CONSOLE_CYAN CONSOLE_ESC(36;1m)
#define CONSOLE_WHITE CONSOLE_ESC(37;1m)

typedef bool (*ConsolePrint)(void *con, int c);

typedef struct ConsoleFont
{
    u8 *gfx;
    u16 asciiOffset;
    u16 numChars;
} ConsoleFont;

typedef struct PrintConsole
{
    ConsoleFont font;
    u16 *frameBuffer;
    int cursorX;
    int cursorY;
    int prevCursorX;
    int prevCursorY;
    int consoleWidth;
    int consoleHeight;
    int windowX;
    int windowY;
    int windowWidth;
    int windowHeight;
    int tabSize;
    u16 fg;
    u16 bg;
    int flags;
    ConsolePrint PrintChar;
    bool consoleInitialised;
} PrintConsole;

#define CONSOLE_COLOR_BOLD (1 << 0)
#define CONSOLE_COLOR_FAINT (1 << 1)
#define CONSOLE_ITALIC (1 << 2)
#define CONSOLE_UNDERLINE (1 << 3)
#define CONSOLE_BLINK_SLOW (1 << 4)
#define CONSOLE_BLINK_FAST (1 << 5)
#define CONSOLE_COLOR_REVERSE (1 << 6)
#define CONSOLE_CONCEAL (1 << 7)
#define CONSOLE_CROSSED_OUT (1 << 8)
#define CONSOLE_FG_CUSTOM (1 << 9)
#define CONSOLE_BG_CUSTOM (1 << 10)
#define CONSOLE_COLOR_FG_BRIGHT (1 << 11)
#define CONSOLE_COLOR_BG_BRIGHT (1 << 12)

typedef enum
{
    debugDevice_NULL,
    debugDevice_SVC,
    debugDevice_CONSOLE,
    debugDevice_3DMOO = debugDevice_SVC,
} debugDevice;

void consoleSetFont(PrintConsole *console, ConsoleFont *font);
void consoleSetWindow(PrintConsole *console, int x, int y, int width, int height);
PrintConsole *consoleGetDefault(void);
PrintConsole *consoleSelect(PrintConsole *console);
PrintConsole *consoleInit(gfxScreen_t screen, PrintConsole *console);
void consoleDebugInit(debugDevice device);
void consoleClear(void);

#ifdef __cplusplus
}
#endif
