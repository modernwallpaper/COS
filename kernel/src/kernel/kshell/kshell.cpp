#include <cstdint>
#include <cstdarg>
#include <inc/kernel/kshell/kshell.hpp>
#include <inc/kernel/graphics/graphics.hpp>
#include <inc/kernel/kfont/font.hpp>
#include <inc/kernel/mem/memory.hpp>

// KShell: A minimal framebuffer text console.
// Characters are rendered from a fixed 8x16 bitmap font (see kfont/).
// The screen scrolls up by one line when the cursor reaches the bottom.
// Newlines, tabs, and backspace are all handled.

KShell::KShell(Graphics* graphics)
{
    this->graphics = graphics;

    this->max_width = this->graphics->get_max_width();
    this->max_height = this->graphics->get_max_height();
    this->cursor_pos_x = 0;
    this->cursor_pos_y = 0;
    this->char_width = 8; // Adjust according to font
    this->char_height = 16;
    this->safeguard_width = 20;
    this->safeguard_height = 40;

    this->default_foreground_color = 0xFFFFFF;
    this->default_background_color = 0x000000;
    this->foreground_color = this->default_foreground_color;
    this->background_color = this->default_background_color;

    this->shell_logo = "root@kernel:~#";

    this->graphics->clear_screen(this->background_color);
}

KShell::~KShell()
{
}

void KShell::set_foreground_color(uint32_t color)
{
    this->foreground_color = color;
}

void KShell::set_background_color(uint32_t color)
{
    this->background_color = color;
}

// Scroll the screen up by one character row.
// Each pixel row is shifted up by char_height, and the new
// bottom row is filled with the background color.
void KShell::scroll()
{
    for (uint64_t y = 0; y < max_height - char_height; y++)
    {
        for (uint64_t x = 0; x < max_width; x++)
        {
            uint32_t pixel = graphics->get_pixel(x, y + char_height);
            graphics->draw_pixel(pixel, x, y);
        }
    }

    for (uint64_t y = max_height - char_height; y < max_height; y++)
    {
        for (uint64_t x = 0; x < max_width; x++)
        {
            graphics->draw_pixel(background_color, x, y);
        }
    }

    cursor_pos_y -= char_height;
}

// Render a single character at the cursor position using the 8x16
// bitmap font. Each character in the font array has 16 bytes, one
// per pixel row. Each bit in a byte represents one pixel column.
void KShell::draw_char(char c)
{
    if (cursor_pos_x >= max_width - safeguard_width)
    {
        cursor_pos_x = 0;
        cursor_pos_y += char_height;
    }

    if (cursor_pos_y >= max_height - safeguard_height)
    {
        scroll();
    }

    for (uint64_t row = 0; row < char_height; row++)
    {
        unsigned char bits = font[(unsigned char)c][row];
        for (uint64_t col = 0; col < char_width; col++)
        {
            if (bits & (1 << (7 - col)))
            {
                graphics->draw_pixel(foreground_color, cursor_pos_x + col,
                                     cursor_pos_y + row);
            }
        }
    }

    cursor_pos_x += char_width;
}

// Erase the character before the cursor and move back.
void KShell::backspace()
{
    if (cursor_pos_x == 0)
    {
        if (cursor_pos_y < char_height)
            return;
        cursor_pos_y -= char_height;
        cursor_pos_x = max_width - char_width;
    }
    else
    {
        cursor_pos_x -= char_width;
    }

    for (uint64_t y = 0; y < char_height; y++)
    {
        for (uint64_t x = 0; x < char_width; x++)
        {
            graphics->draw_pixel(background_color, cursor_pos_x + x,
                                 cursor_pos_y + y);
        }
    }
}

// --- Colored kernel log helpers ---
// Each prints a colored prefix, then the formatted message, then a newline.
// [i]     = info (cyan)
// [OK]    = success (green)
// [WARN]  = warning (yellow)
// [ERROR] = error (red)

void KShell::vprint_kernel_info(const char* fmt, va_list args)
{
    this->print("[ ");
    this->foreground_color = 0x00FFFF;
    this->print("i");
    this->foreground_color = this->default_foreground_color;
    this->print(" ] ");

    this->vprint(fmt, args);

    this->print("\n");
}

void KShell::print_kernel_info(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    this->vprint_kernel_info(fmt, args);
    va_end(args);
}

void KShell::vprint_kernel_success(const char* fmt, va_list args)
{
    this->print("[ ");
    this->foreground_color = 0x00FF00;
    this->print("OK");
    this->foreground_color = this->default_foreground_color;
    this->print(" ] ");

    this->vprint(fmt, args);

    this->print("\n");
}

void KShell::print_kernel_success(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    this->vprint_kernel_success(fmt, args);
    va_end(args);
}

void KShell::vprint_kernel_error(const char* fmt, va_list args)
{
    this->print("[ ");
    this->foreground_color = 0xFF0000;
    this->print("ERROR");
    this->foreground_color = this->default_foreground_color;
    this->print(" ] ");

    this->vprint(fmt, args);

    this->print("\n");
}

void KShell::print_kernel_warning(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    this->vprint_kernel_warning(fmt, args);
    va_end(args);
}

void KShell::vprint_kernel_warning(const char* fmt, va_list args)
{
    this->print("[ ");
    this->foreground_color = 0xFFFF00;
    this->print("WARN");
    this->foreground_color = this->default_foreground_color;
    this->print(" ] ");

    this->vprint(fmt, args);

    this->print("\n");
}

void KShell::print_kernel_error(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    this->vprint_kernel_error(fmt, args);
    va_end(args);
}

// Main formatted print. Wraps vprint with va_list setup.
void KShell::print(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vprint(fmt, args);
    va_end(args);
}

// Core format-string parser. Walks the format string character by
// character. Plain characters are drawn directly; % sequences
// consume va_args and format the value accordingly.
//
// Supported: %s %d %u %x %llu %b %c %p %%
// NOTE: %x, %d, and %u read 32-bit values. For 64-bit, use %llu.
void KShell::vprint(const char* fmt, va_list args)
{
    for (int i = 0; fmt[i] != '\0'; i++)
    {
        if (fmt[i] == '%')
        {
            i++; // skip '%'
            switch (fmt[i])
            {
            case 's': {
                const char* s = va_arg(args, const char*);
                while (*s)
                    draw_char(*s++);
                break;
            }
            case 'd': {
                int val = va_arg(args, int);
                if (val < 0)
                {
                    draw_char('-');
                    val = -val;
                }
                char buf[20];
                int idx = 0;
                if (val == 0)
                    buf[idx++] = '0';
                while (val > 0)
                {
                    buf[idx++] = '0' + (val % 10);
                    val /= 10;
                }
                for (int j = idx - 1; j >= 0; j--)
                    draw_char(buf[j]);
                break;
            }
            case 'u': {
                unsigned int val = va_arg(args, unsigned int);
                char buf[20];
                int idx = 0;
                if (val == 0)
                    buf[idx++] = '0';
                while (val > 0)
                {
                    buf[idx++] = '0' + (val % 10);
                    val /= 10;
                }
                for (int j = idx - 1; j >= 0; j--)
                    draw_char(buf[j]);
                break;
            }
            case 'x': {
                unsigned int val = va_arg(args, unsigned int);
                char buf[16];
                int idx = 0;
                if (val == 0)
                    buf[idx++] = '0';
                while (val > 0)
                {
                    int digit = val & 0xF;
                    buf[idx++] = digit < 10 ? '0' + digit : 'A' + digit - 10;
                    val >>= 4;
                }
                for (int j = idx - 1; j >= 0; j--)
                    draw_char(buf[j]);
                break;
            }
            case 'l': {
                if (fmt[i + 1] == 'l' && fmt[i + 2] == 'u')
                {
                    i += 2; // skip both 'l's
                    uint64_t val = va_arg(args, uint64_t);
                    char buf[32];
                    int idx = 0;
                    if (val == 0)
                        buf[idx++] = '0';
                    while (val > 0)
                    {
                        buf[idx++] = '0' + (val % 10);
                        val /= 10;
                    }
                    for (int j = idx - 1; j >= 0; j--)
                        draw_char(buf[j]);
                }
                break;
            }
            case 'b': {
                unsigned int val = va_arg(args, unsigned int);
                for (int j = 31; j >= 0; j--)
                    draw_char((val & (1 << j)) ? '1' : '0');
                break;
            }
            case 'c': {
                char c = (char)va_arg(args, int);
                draw_char(c);
                break;
            }
            case 'p': {
                void* ptr = va_arg(args, void*);
                this->print("0x%x", (uintptr_t)ptr);
                break;
            }
            case '%':
                draw_char('%');
                break;
            default:
                draw_char(fmt[i]);
                break;
            }
        }
        else if (fmt[i] == '\n')
        {
            cursor_pos_y += char_height;
            cursor_pos_x = 0;
            if (cursor_pos_y >= max_height - safeguard_height)
            {
                scroll();
            }
        }
        else if (fmt[i] == '\t')
        {
            cursor_pos_x += char_width * 4;
            if (cursor_pos_x >= max_width - safeguard_width)
            {
                cursor_pos_x = 0;
                cursor_pos_y += char_height;
                if (cursor_pos_y >= max_height - safeguard_height)
                    scroll();
            }
        }
        else
        {
            draw_char(fmt[i]);
        }
    }
}
