#pragma once

#include <cstdint>
#include <limine.h>
#include <stdint.h>

class Graphics 
{
private:
    // VERY IMPORTANT. NEVER WRITE TO THE FRAMEBUFFER DIRECTLY. INSTEAD USE THE POINTER (this->fb_ptr)
    limine_framebuffer *fb;
    volatile uint32_t *fb_ptr;
    
    uint64_t max_width;
    uint64_t max_height;
public:
    Graphics(limine_framebuffer *fb);
    ~Graphics();

    uint64_t get_max_width();
    uint64_t get_max_height();

    void draw_pixel(uint32_t color, uint64_t x, uint64_t y);
    uint32_t get_pixel(uint64_t x, uint64_t y); 
    void draw_line(uint32_t color, uint64_t p1_x, uint64_t p1_y, uint64_t p2_x, uint64_t p2_y);
    void clear_screen(uint32_t color);
};
