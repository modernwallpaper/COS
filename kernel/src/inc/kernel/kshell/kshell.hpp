#pragma once
#include "inc/kernel/graphics/graphics.hpp"
#include <cstdarg>
#include <limine.h>
#include <stdint.h>

class KShell 
{
private:
    Graphics* graphics;

    uint64_t cursor_pos_x;
    uint64_t cursor_pos_y;
    
    typedef struct {
        uint64_t max_width;
        uint64_t max_height;
    } CursorPos;

    uint64_t max_width;
    uint64_t max_height;
    uint64_t safeguard_width;
    uint64_t safeguard_height;

    uint64_t char_width;
    uint64_t char_height;

    const char* shell_logo;

    uint32_t foreground_color;
    uint32_t background_color;

    uint32_t default_foreground_color;
    uint32_t default_background_color;
public:
    KShell(Graphics* graphics);
    ~KShell();

    void draw_char(char c);
    void print(const char* fmt, ...);
    void vprint(const char* fmt, va_list args);

    void print_kernel_info(const char* fmt, ...);
    void vprint_kernel_info(const char* fmt, va_list args);
    
    void print_kernel_success(const char* fmt, ...);
    void vprint_kernel_success(const char* fmt, va_list args);
    
    void print_kernel_warning(const char* fmt, ...);
    void vprint_kernel_warning(const char* fmt, va_list args);

    void print_kernel_error(const char* fmt, ...);
    void vprint_kernel_error(const char* fmt, va_list args);

    void backspace();
    void scroll();

    void set_foreground_color(uint32_t color);
    void set_background_color(uint32_t color);

    CursorPos get_cursor_pos();
};
