/*
 * libctru's text console (source/console.c, zlib licence, devkitPro), with
 * three changes for Android:
 *  - the scroll copy uses uintptr_t instead of casting pointers to int;
 *  - every write also flushes the framebuffer columns it touched with
 *    GSPGPU_FlushDataCache, so text appears even without the newline
 *    (gfxFlushBuffers) libctru relies on;
 *  - the text is copied to logcat.
 * Like libctru it is not thread safe: callers serialise (3ds_log.c does).
 */
#include <3ds/console.h>
#include <3ds/gfx.h>
#include <3ds/services/gspgpu.h>
#include <3ds/svc.h>
#include <stdio.h>
#include <string.h>
#include <sys/iosupport.h>

#include "default_font.h"
#include "stdio_internal.h"

#define CON_RGB565(r, g, b) ((((b) >> 3) & 0x1f) | ((((g) >> 2) & 0x3f) << 5) | ((((r) >> 3) & 0x1f) << 11))

static u16 colorTable[] = {
    CON_RGB565(0, 0, 0),       // black
    CON_RGB565(128, 0, 0),     // red
    CON_RGB565(0, 128, 0),     // green
    CON_RGB565(128, 128, 0),   // yellow
    CON_RGB565(0, 0, 128),     // blue
    CON_RGB565(128, 0, 128),   // magenta
    CON_RGB565(0, 128, 128),   // cyan
    CON_RGB565(192, 192, 192), // white

    CON_RGB565(128, 128, 128), // bright black
    CON_RGB565(255, 0, 0),     // bright red
    CON_RGB565(0, 255, 0),     // bright green
    CON_RGB565(255, 255, 0),   // bright yellow
    CON_RGB565(0, 0, 255),     // bright blue
    CON_RGB565(255, 0, 255),   // bright magenta
    CON_RGB565(0, 255, 255),   // bright cyan
    CON_RGB565(255, 255, 255), // bright white

    CON_RGB565(0, 0, 0),    // faint black
    CON_RGB565(64, 0, 0),   // faint red
    CON_RGB565(0, 64, 0),   // faint green
    CON_RGB565(64, 64, 0),  // faint yellow
    CON_RGB565(0, 0, 64),   // faint blue
    CON_RGB565(64, 0, 64),  // faint magenta
    CON_RGB565(0, 64, 64),  // faint cyan
    CON_RGB565(96, 96, 96), // faint white
};

static const u8 colorCube[] = {
    0x00, 0x5f, 0x87, 0xaf, 0xd7, 0xff,
};

static const u8 grayScale[] = {
    0x08, 0x12, 0x1c, 0x26, 0x30, 0x3a, 0x44, 0x4e, 0x58, 0x62, 0x6c, 0x76,
    0x80, 0x8a, 0x94, 0x9e, 0xa8, 0xb2, 0xbc, 0xc6, 0xd0, 0xda, 0xe4, 0xee,
};

static PrintConsole defaultConsole = {
    {(u8 *)default_font_bin, 0, 256},
    (u16 *)NULL,
    0, 0, // cursorX cursorY
    0, 0, // prevcursorX prevcursorY
    40,   // console width
    30,   // console height
    1,    // window x
    1,    // window y
    40,   // window width
    30,   // window height
    3,    // tab size
    7,    // foreground color
    0,    // background color
    0,    // flags
    0,    // print callback
    false // console initialized
};

static PrintConsole currentCopy;
static PrintConsole *currentConsole = &currentCopy;

/* Framebuffer columns drawn since the last flush, [dirtyMin, dirtyMax). */
static u16 *dirtyBuffer;
static int dirtyMin = 1 << 30, dirtyMax = -1;

PrintConsole *consoleGetDefault(void)
{
    return &defaultConsole;
}

static void consolePrintChar(int c);
static void consoleDrawChar(int c);

static void markDirty(int x0, int x1)
{
    if (dirtyBuffer != currentConsole->frameBuffer)
    {
        dirtyBuffer = currentConsole->frameBuffer;
        dirtyMin = 1 << 30;
        dirtyMax = -1;
    }
    if (x0 < dirtyMin)
        dirtyMin = x0;
    if (x1 > dirtyMax)
        dirtyMax = x1;
}

static void flushDirty(void)
{
    if (dirtyBuffer != NULL && dirtyMax > dirtyMin)
        GSPGPU_FlushDataCache(dirtyBuffer + dirtyMin * 240, (u32)(dirtyMax - dirtyMin) * 240 * sizeof(u16));
    dirtyMin = 1 << 30;
    dirtyMax = -1;
}

static void consoleCls(int mode)
{
    int i = 0;
    int colTemp, rowTemp;

    switch (mode)
    {
    case 0:
        colTemp = currentConsole->cursorX;
        rowTemp = currentConsole->cursorY;
        while (i++ < ((currentConsole->windowHeight * currentConsole->windowWidth)
                      - (rowTemp * currentConsole->windowWidth + colTemp)))
            consolePrintChar(' ');
        currentConsole->cursorX = colTemp;
        currentConsole->cursorY = rowTemp;
        break;
    case 1:
        colTemp = currentConsole->cursorX;
        rowTemp = currentConsole->cursorY;
        currentConsole->cursorY = 1;
        currentConsole->cursorX = 1;
        while (i++ < (rowTemp * currentConsole->windowWidth + colTemp))
            consolePrintChar(' ');
        currentConsole->cursorX = colTemp;
        currentConsole->cursorY = rowTemp;
        break;
    case 2:
        currentConsole->cursorY = 1;
        currentConsole->cursorX = 1;
        while (i++ < currentConsole->windowHeight * currentConsole->windowWidth)
            consolePrintChar(' ');
        currentConsole->cursorY = 1;
        currentConsole->cursorX = 1;
        break;
    }
    gfxFlushBuffers();
}

static void consoleClearLine(int mode)
{
    int i, colTemp;

    switch (mode)
    {
    case 0:
        colTemp = currentConsole->cursorX;
        for (i = 0; i < currentConsole->windowWidth - colTemp + 1; i++)
            consolePrintChar(' ');
        currentConsole->cursorX = colTemp;
        break;
    case 1:
        colTemp = currentConsole->cursorX;
        currentConsole->cursorX = 1;
        for (i = 0; i < colTemp - 1; i++)
            consolePrintChar(' ');
        currentConsole->cursorX = colTemp;
        break;
    case 2:
        colTemp = currentConsole->cursorX;
        currentConsole->cursorX = 1;
        for (i = 0; i < currentConsole->windowWidth; i++)
            consolePrintChar(' ');
        currentConsole->cursorX = colTemp;
        break;
    }
    gfxFlushBuffers();
}

static inline void consolePosition(int x, int y)
{
    if (x < 0 || y < 0)
        return;
    if (x < 1)
        x = 1;
    if (y < 1)
        y = 1;
    if (x > currentConsole->windowWidth)
        x = currentConsole->windowWidth;
    if (y > currentConsole->windowHeight)
        y = currentConsole->windowHeight;
    currentConsole->cursorX = x;
    currentConsole->cursorY = y;
}

#define _ANSI_MAXARGS 16

static struct
{
    struct
    {
        int flags;
        u32 fg;
        u32 bg;
    } color;
    int argIdx;
    int args[_ANSI_MAXARGS];
    int colorArgCount;
    unsigned int colorArgs[3];
    bool hasArg;
    enum
    {
        ESC_NONE,
        ESC_START,
        ESC_BUILDING_UNKNOWN,
        ESC_BUILDING_FORMAT_FG,
        ESC_BUILDING_FORMAT_BG,
        ESC_BUILDING_FORMAT_FG_NONRGB,
        ESC_BUILDING_FORMAT_BG_NONRGB,
        ESC_BUILDING_FORMAT_FG_RGB,
        ESC_BUILDING_FORMAT_BG_RGB,
    } state;
} escapeSeq;

static void consoleSetColorState(int code)
{
    switch (code)
    {
    case 0: // reset
        escapeSeq.color.flags = 0;
        escapeSeq.color.bg = 0;
        escapeSeq.color.fg = 7;
        break;
    case 1: // bold
        escapeSeq.color.flags &= ~CONSOLE_COLOR_FAINT;
        escapeSeq.color.flags |= CONSOLE_COLOR_BOLD;
        break;
    case 2: // faint
        escapeSeq.color.flags &= ~CONSOLE_COLOR_BOLD;
        escapeSeq.color.flags |= CONSOLE_COLOR_FAINT;
        break;
    case 3: // italic
        escapeSeq.color.flags |= CONSOLE_ITALIC;
        break;
    case 4: // underline
        escapeSeq.color.flags |= CONSOLE_UNDERLINE;
        break;
    case 5: // blink slow
        escapeSeq.color.flags &= ~CONSOLE_BLINK_FAST;
        escapeSeq.color.flags |= CONSOLE_BLINK_SLOW;
        break;
    case 6: // blink fast
        escapeSeq.color.flags &= ~CONSOLE_BLINK_SLOW;
        escapeSeq.color.flags |= CONSOLE_BLINK_FAST;
        break;
    case 7: // reverse video
        escapeSeq.color.flags |= CONSOLE_COLOR_REVERSE;
        break;
    case 8: // conceal
        escapeSeq.color.flags |= CONSOLE_CONCEAL;
        break;
    case 9: // crossed-out
        escapeSeq.color.flags |= CONSOLE_CROSSED_OUT;
        break;
    case 21: // bold off
        escapeSeq.color.flags &= ~CONSOLE_COLOR_BOLD;
        break;
    case 22: // normal color
        escapeSeq.color.flags &= ~CONSOLE_COLOR_BOLD;
        escapeSeq.color.flags &= ~CONSOLE_COLOR_FAINT;
        break;
    case 23: // italic off
        escapeSeq.color.flags &= ~CONSOLE_ITALIC;
        break;
    case 24: // underline off
        escapeSeq.color.flags &= ~CONSOLE_UNDERLINE;
        break;
    case 25: // blink off
        escapeSeq.color.flags &= ~CONSOLE_BLINK_SLOW;
        escapeSeq.color.flags &= ~CONSOLE_BLINK_FAST;
        break;
    case 27: // reverse off
        escapeSeq.color.flags &= ~CONSOLE_COLOR_REVERSE;
        break;
    case 29: // crossed-out off
        escapeSeq.color.flags &= ~CONSOLE_CROSSED_OUT;
        break;
    case 30 ... 37: // writing color
        escapeSeq.color.flags &= ~CONSOLE_FG_CUSTOM;
        escapeSeq.color.fg = code - 30;
        break;
    case 38: // custom foreground color
        escapeSeq.state = ESC_BUILDING_FORMAT_FG;
        escapeSeq.colorArgCount = 0;
        break;
    case 39: // reset foreground color
        escapeSeq.color.flags &= ~CONSOLE_FG_CUSTOM;
        escapeSeq.color.fg = 7;
        break;
    case 40 ... 47: // screen color
        escapeSeq.color.flags &= ~CONSOLE_BG_CUSTOM;
        escapeSeq.color.bg = code - 40;
        break;
    case 48: // custom background color
        escapeSeq.state = ESC_BUILDING_FORMAT_BG;
        escapeSeq.colorArgCount = 0;
        break;
    case 49: // reset background color
        escapeSeq.color.flags &= ~CONSOLE_BG_CUSTOM;
        escapeSeq.color.bg = 0;
        break;
    case 90 ... 97: // bright foreground
        escapeSeq.color.flags &= ~CONSOLE_COLOR_FAINT;
        escapeSeq.color.flags |= CONSOLE_COLOR_FG_BRIGHT;
        escapeSeq.color.flags &= ~CONSOLE_BG_CUSTOM;
        escapeSeq.color.fg = code - 90;
        break;
    case 100 ... 107: // bright background
        escapeSeq.color.flags &= ~CONSOLE_COLOR_FAINT;
        escapeSeq.color.flags |= CONSOLE_COLOR_BG_BRIGHT;
        escapeSeq.color.flags &= ~CONSOLE_BG_CUSTOM;
        escapeSeq.color.bg = code - 100;
        break;
    }
}

static u16 cubeColor(int code)
{
    unsigned int r, g, b;

    code -= 16;
    r = code / 36;
    g = (code - r * 36) / 6;
    b = code - r * 36 - g * 6;
    return CON_RGB565(colorCube[r], colorCube[g], colorCube[b]);
}

static void consoleHandleColorEsc(int argCount)
{
    escapeSeq.color.bg = currentConsole->bg;
    escapeSeq.color.fg = currentConsole->fg;
    escapeSeq.color.flags = currentConsole->flags;

    for (int arg = 0; arg < argCount; arg++)
    {
        int code = escapeSeq.args[arg];

        switch (escapeSeq.state)
        {
        case ESC_BUILDING_UNKNOWN:
            consoleSetColorState(code);
            break;
        case ESC_BUILDING_FORMAT_FG:
            if (code == 5)
                escapeSeq.state = ESC_BUILDING_FORMAT_FG_NONRGB;
            else if (code == 2)
                escapeSeq.state = ESC_BUILDING_FORMAT_FG_RGB;
            else
                escapeSeq.state = ESC_BUILDING_UNKNOWN;
            break;
        case ESC_BUILDING_FORMAT_BG:
            if (code == 5)
                escapeSeq.state = ESC_BUILDING_FORMAT_BG_NONRGB;
            else if (code == 2)
                escapeSeq.state = ESC_BUILDING_FORMAT_BG_RGB;
            else
                escapeSeq.state = ESC_BUILDING_UNKNOWN;
            break;
        case ESC_BUILDING_FORMAT_FG_NONRGB:
            if (code <= 15)
            {
                escapeSeq.color.fg = code;
                escapeSeq.color.flags &= ~CONSOLE_FG_CUSTOM;
            }
            else if (code <= 231)
            {
                escapeSeq.color.fg = cubeColor(code);
                escapeSeq.color.flags |= CONSOLE_FG_CUSTOM;
            }
            else if (code <= 255)
            {
                code -= 232;
                escapeSeq.color.fg = CON_RGB565(grayScale[code], grayScale[code], grayScale[code]);
                escapeSeq.color.flags |= CONSOLE_FG_CUSTOM;
            }
            escapeSeq.state = ESC_BUILDING_UNKNOWN;
            break;
        case ESC_BUILDING_FORMAT_BG_NONRGB:
            if (code <= 15)
            {
                escapeSeq.color.bg = code;
                escapeSeq.color.flags &= ~CONSOLE_BG_CUSTOM;
            }
            else if (code <= 231)
            {
                escapeSeq.color.bg = cubeColor(code);
                escapeSeq.color.flags |= CONSOLE_BG_CUSTOM;
            }
            else if (code <= 255)
            {
                code -= 232;
                escapeSeq.color.bg = CON_RGB565(grayScale[code], grayScale[code], grayScale[code]);
                escapeSeq.color.flags |= CONSOLE_BG_CUSTOM;
            }
            escapeSeq.state = ESC_BUILDING_UNKNOWN;
            break;
        case ESC_BUILDING_FORMAT_FG_RGB:
            escapeSeq.colorArgs[escapeSeq.colorArgCount++] = code;
            if (escapeSeq.colorArgCount == 3)
            {
                escapeSeq.color.fg = CON_RGB565(escapeSeq.colorArgs[0], escapeSeq.colorArgs[1],
                                                escapeSeq.colorArgs[2]);
                escapeSeq.color.flags |= CONSOLE_FG_CUSTOM;
                escapeSeq.state = ESC_BUILDING_UNKNOWN;
            }
            break;
        case ESC_BUILDING_FORMAT_BG_RGB:
            escapeSeq.colorArgs[escapeSeq.colorArgCount++] = code;
            if (escapeSeq.colorArgCount == 3)
            {
                escapeSeq.color.bg = CON_RGB565(escapeSeq.colorArgs[0], escapeSeq.colorArgs[1],
                                                escapeSeq.colorArgs[2]);
                escapeSeq.color.flags |= CONSOLE_BG_CUSTOM;
                escapeSeq.state = ESC_BUILDING_UNKNOWN;
            }
            break;
        default:
            break;
        }
    }
    escapeSeq.argIdx = 0;

    currentConsole->bg = escapeSeq.color.bg;
    currentConsole->fg = escapeSeq.color.fg;
    currentConsole->flags = escapeSeq.color.flags;
}

static ssize_t con_write(struct _reent *r, void *fd, const char *ptr, size_t len)
{
    size_t i = 0;
    int count = 0;

    (void)r;
    (void)fd;
    if (!ptr)
        return -1;
    ShimLogText(ptr, len);

    while (i < len)
    {
        char chr = ptr[i++];

        count++;
        switch (escapeSeq.state)
        {
        case ESC_NONE:
            if (chr == 0x1b)
                escapeSeq.state = ESC_START;
            else
                consolePrintChar(chr);
            break;
        case ESC_START:
            if (chr == '[')
            {
                escapeSeq.state = ESC_BUILDING_UNKNOWN;
                escapeSeq.hasArg = false;
                memset(escapeSeq.args, 0, sizeof(escapeSeq.args));
                escapeSeq.color.bg = currentConsole->bg;
                escapeSeq.color.fg = currentConsole->fg;
                escapeSeq.color.flags = currentConsole->flags;
                escapeSeq.argIdx = 0;
            }
            else
            {
                consolePrintChar(0x1b);
                consolePrintChar(chr);
                escapeSeq.state = ESC_NONE;
            }
            break;
        case ESC_BUILDING_UNKNOWN:
            switch (chr)
            {
            case '0' ... '9':
                escapeSeq.hasArg = true;
                if (escapeSeq.argIdx < _ANSI_MAXARGS)
                    escapeSeq.args[escapeSeq.argIdx] = escapeSeq.args[escapeSeq.argIdx] * 10 + (chr - '0');
                break;
            case ';':
                if (escapeSeq.hasArg && escapeSeq.argIdx < _ANSI_MAXARGS)
                    escapeSeq.argIdx++;
                escapeSeq.hasArg = false;
                break;
            // Cursor directional movement
            case 'A':
                if (!escapeSeq.hasArg && !escapeSeq.argIdx)
                    escapeSeq.args[0] = 1;
                currentConsole->cursorY = currentConsole->cursorY - escapeSeq.args[0];
                if (currentConsole->cursorY < 1)
                    currentConsole->cursorY = 1;
                escapeSeq.state = ESC_NONE;
                break;
            case 'B':
                if (!escapeSeq.hasArg && !escapeSeq.argIdx)
                    escapeSeq.args[0] = 1;
                currentConsole->cursorY = currentConsole->cursorY + escapeSeq.args[0];
                if (currentConsole->cursorY > currentConsole->windowHeight)
                    currentConsole->cursorY = currentConsole->windowHeight;
                escapeSeq.state = ESC_NONE;
                break;
            case 'C':
                if (!escapeSeq.hasArg && !escapeSeq.argIdx)
                    escapeSeq.args[0] = 1;
                currentConsole->cursorX = currentConsole->cursorX + escapeSeq.args[0];
                if (currentConsole->cursorX > currentConsole->windowWidth)
                    currentConsole->cursorX = currentConsole->windowWidth;
                escapeSeq.state = ESC_NONE;
                break;
            case 'D':
                if (!escapeSeq.hasArg && !escapeSeq.argIdx)
                    escapeSeq.args[0] = 1;
                currentConsole->cursorX = currentConsole->cursorX - escapeSeq.args[0];
                if (currentConsole->cursorX < 1)
                    currentConsole->cursorX = 1;
                escapeSeq.state = ESC_NONE;
                break;
            // Cursor position movement
            case 'H':
            case 'f':
                consolePosition(escapeSeq.args[1], escapeSeq.args[0]);
                escapeSeq.state = ESC_NONE;
                break;
            // Screen clear
            case 'J':
                if (escapeSeq.argIdx == 0 && !escapeSeq.hasArg)
                    escapeSeq.args[0] = 0;
                consoleCls(escapeSeq.args[0]);
                escapeSeq.state = ESC_NONE;
                break;
            // Line clear
            case 'K':
                if (escapeSeq.argIdx == 0 && !escapeSeq.hasArg)
                    escapeSeq.args[0] = 0;
                consoleClearLine(escapeSeq.args[0]);
                escapeSeq.state = ESC_NONE;
                break;
            // Save cursor position
            case 's':
                currentConsole->prevCursorX = currentConsole->cursorX;
                currentConsole->prevCursorY = currentConsole->cursorY;
                escapeSeq.state = ESC_NONE;
                break;
            // Load cursor position
            case 'u':
                currentConsole->cursorX = currentConsole->prevCursorX;
                currentConsole->cursorY = currentConsole->prevCursorY;
                escapeSeq.state = ESC_NONE;
                break;
            // Color scan codes
            case 'm':
                if (escapeSeq.argIdx == 0 && !escapeSeq.hasArg)
                    escapeSeq.args[escapeSeq.argIdx++] = 0;
                if (escapeSeq.hasArg && escapeSeq.argIdx < _ANSI_MAXARGS)
                    escapeSeq.argIdx++;
                consoleHandleColorEsc(escapeSeq.argIdx);
                escapeSeq.state = ESC_NONE;
                break;
            default:
                // some sort of unsupported escape; just gloss over it
                escapeSeq.state = ESC_NONE;
                break;
            }
            break;
        default:
            break;
        }
    }
    flushDirty();
    return count;
}

static const devoptab_t dotab_stdout = {.name = "con", .write_r = con_write};

static ssize_t debug_write(struct _reent *r, void *fd, const char *ptr, size_t len)
{
    (void)r;
    (void)fd;
    svcOutputDebugString(ptr, (s32)len);
    return (ssize_t)len;
}

static const devoptab_t dotab_svc = {.name = "svc", .write_r = debug_write};
static const devoptab_t dotab_null = {.name = "null"};

PrintConsole *consoleInit(gfxScreen_t screen, PrintConsole *console)
{
    static bool firstConsoleInit = true;

    if (firstConsoleInit)
    {
        devoptab_list[STD_OUT] = &dotab_stdout;
        devoptab_list[STD_ERR] = &dotab_stdout;
        setvbuf(stdout, NULL, _IONBF, 0);
        setvbuf(stderr, NULL, _IONBF, 0);
        memset(&escapeSeq, 0, sizeof(escapeSeq));
        firstConsoleInit = false;
    }

    if (console)
        currentConsole = console;
    else
        console = currentConsole;

    *currentConsole = defaultConsole;
    console->consoleInitialised = 1;

    gfxSetScreenFormat(screen, GSP_RGB565_OES);
    gfxSetDoubleBuffering(screen, false);
    gfxSwapBuffersGpu();
    gspWaitForVBlank();

    console->frameBuffer = (u16 *)gfxGetFramebuffer(screen, GFX_LEFT, NULL, NULL);

    if (screen == GFX_TOP)
    {
        bool isWide = gfxIsWide();

        console->consoleWidth = isWide ? 100 : 50;
        console->windowWidth = isWide ? 100 : 50;
    }

    consoleCls(2);
    flushDirty();
    return currentConsole;
}

void consoleDebugInit(debugDevice device)
{
    int buffertype = _IONBF;

    switch (device)
    {
    case debugDevice_SVC:
        devoptab_list[STD_ERR] = &dotab_svc;
        buffertype = _IOLBF;
        break;
    case debugDevice_CONSOLE:
        devoptab_list[STD_ERR] = &dotab_stdout;
        break;
    case debugDevice_NULL:
        devoptab_list[STD_ERR] = &dotab_null;
        break;
    }
    setvbuf(stderr, NULL, buffertype, 0);
}

PrintConsole *consoleSelect(PrintConsole *console)
{
    PrintConsole *tmp = currentConsole;

    currentConsole = console;
    return tmp;
}

void consoleSetFont(PrintConsole *console, ConsoleFont *font)
{
    if (!console)
        console = currentConsole;
    console->font = *font;
}

static void newRow(void)
{
    currentConsole->cursorY++;

    if (currentConsole->cursorY > currentConsole->windowHeight)
    {
        u16 *dst, *src;
        int i, j;

        currentConsole->cursorY = currentConsole->windowHeight;
        dst = &currentConsole->frameBuffer[((currentConsole->windowX - 1) * 8 * 240)
                                           + (239 - ((currentConsole->windowY - 1) * 8))];
        src = dst - 8;

        for (i = 0; i < (currentConsole->windowWidth) * 8; i++)
        {
            u32 *from = (u32 *)((uintptr_t)src & ~(uintptr_t)3);
            u32 *to = (u32 *)((uintptr_t)dst & ~(uintptr_t)3);

            for (j = 0; j < (((currentConsole->windowHeight - 1) * 8) / 2); j++)
                *(to--) = *(from--);
            dst += 240;
            src += 240;
        }
        markDirty((currentConsole->windowX - 1) * 8,
                  (currentConsole->windowX - 1 + currentConsole->windowWidth) * 8);

        consoleClearLine(2);
    }
}

static void consoleDrawChar(int c)
{
    u8 *fontdata;
    u16 fg, bg;
    u8 b1, b2, b3, b4, b5, b6, b7, b8, mask;
    int i, x, y;
    u16 *screen;

    c -= currentConsole->font.asciiOffset;
    if (c < 0 || c > currentConsole->font.numChars)
        return;

    fontdata = currentConsole->font.gfx + (8 * c);
    fg = currentConsole->fg;
    bg = currentConsole->bg;

    if (!(currentConsole->flags & CONSOLE_FG_CUSTOM))
    {
        if (currentConsole->flags & (CONSOLE_COLOR_BOLD | CONSOLE_COLOR_FG_BRIGHT))
            fg += 8;
        else if (currentConsole->flags & CONSOLE_COLOR_FAINT)
            fg += 16;
        fg = colorTable[fg];
    }

    if (!(currentConsole->flags & CONSOLE_BG_CUSTOM))
    {
        if (currentConsole->flags & CONSOLE_COLOR_BG_BRIGHT)
            bg += 8;
        bg = colorTable[bg];
    }

    if (currentConsole->flags & CONSOLE_COLOR_REVERSE)
    {
        u16 tmp = fg;

        fg = bg;
        bg = tmp;
    }

    b1 = *(fontdata++);
    b2 = *(fontdata++);
    b3 = *(fontdata++);
    b4 = *(fontdata++);
    b5 = *(fontdata++);
    b6 = *(fontdata++);
    b7 = *(fontdata++);
    b8 = *(fontdata++);

    if (currentConsole->flags & CONSOLE_UNDERLINE)
        b8 = 0xff;
    if (currentConsole->flags & CONSOLE_CROSSED_OUT)
        b4 = 0xff;

    mask = 0x80;
    x = (currentConsole->cursorX - 1 + currentConsole->windowX - 1) * 8;
    y = ((currentConsole->cursorY - 1 + currentConsole->windowY - 1) * 8);

    screen = &currentConsole->frameBuffer[(x * 240) + (239 - (y + 7))];
    markDirty(x, x + 8);

    for (i = 0; i < 8; i++)
    {
        *(screen++) = (b8 & mask) ? fg : bg;
        *(screen++) = (b7 & mask) ? fg : bg;
        *(screen++) = (b6 & mask) ? fg : bg;
        *(screen++) = (b5 & mask) ? fg : bg;
        *(screen++) = (b4 & mask) ? fg : bg;
        *(screen++) = (b3 & mask) ? fg : bg;
        *(screen++) = (b2 & mask) ? fg : bg;
        *(screen++) = (b1 & mask) ? fg : bg;
        mask >>= 1;
        screen += 240 - 8;
    }
}

static void consolePrintChar(int c)
{
    int tabspaces;

    if (c == 0)
        return;
    if (currentConsole->frameBuffer == NULL)
        return;

    if (currentConsole->PrintChar)
        if (currentConsole->PrintChar(currentConsole, c))
            return;

    switch (c)
    {
    /*
     * The only special characters handled are tab (\t), carriage return
     * (\r), line feed (\n) and backspace (\b). Carriage return and line feed
     * both go to the start of the next line. Everything else uses VT
     * sequences.
     */
    case 8:
        currentConsole->cursorX--;
        if (currentConsole->cursorX < 1)
        {
            if (currentConsole->cursorY > 1)
            {
                currentConsole->cursorX = currentConsole->windowWidth;
                currentConsole->cursorY--;
            }
            else
                currentConsole->cursorX = 1;
        }
        consoleDrawChar(' ');
        break;
    case 9:
        tabspaces = currentConsole->tabSize - ((currentConsole->cursorX - 1) % currentConsole->tabSize);
        for (int i = 0; i < tabspaces; i++)
            consolePrintChar(' ');
        break;
    case 10:
        newRow();
        /* fall through */
    case 13:
        currentConsole->cursorX = 1;
        gfxFlushBuffers();
        break;
    default:
        if (currentConsole->cursorX > currentConsole->windowWidth)
        {
            currentConsole->cursorX = 1;
            newRow();
        }
        consoleDrawChar(c);
        ++currentConsole->cursorX;
        break;
    }
}

void consoleClear(void)
{
    consoleCls(2);
    flushDirty();
}

void consoleSetWindow(PrintConsole *console, int x, int y, int width, int height)
{
    if (!console)
        console = currentConsole;
    if (x < 1)
        x = 1;
    if (y < 1)
        y = 1;
    console->windowWidth = width;
    console->windowHeight = height;
    console->windowX = x;
    console->windowY = y;
    console->cursorX = 1;
    console->cursorY = 1;
}
