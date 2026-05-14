#include <stdint.h>
#include <limine.h>

#include <inc/kernel/graphics/graphics.hpp>
#include <inc/kernel/kfont/font.hpp>

// The framebuffer is a 2D array of pixels. Each pixel is 32 bits
// (BGRA or RGBA depending on firmware). The pitch is the number of
// bytes per row, which may be larger than width*4 due to alignment.
// We always use pitch/4 as the row stride, NOT max_width.

Graphics::Graphics(limine_framebuffer* fb)
{
    this->fb = fb;
    this->fb_ptr = static_cast<volatile uint32_t*>(fb->address);

    this->max_width = this->fb->width;
    this->max_height = this->fb->height;
}

Graphics::~Graphics()
{
}

uint64_t Graphics::get_max_width()
{
    return this->max_width;
}

uint64_t Graphics::get_max_height()
{
    return this->max_height;
}

// Draw a single pixel. Bounds-checked to avoid writing past the end
// of the framebuffer.
// The pitch (bytes per row) may be larger than width*4, so we divide
// by 4 to get the number of pixels per row.
void Graphics::draw_pixel(uint32_t color, uint64_t x, uint64_t y)
{
    if (x >= max_width || y >= max_height)
        return;
    this->fb_ptr[y * (this->fb->pitch / 4) + x] = color;
}

// Read back a pixel value. Uses the same stride calculation as draw_pixel.
uint32_t Graphics::get_pixel(uint64_t x, uint64_t y)
{
    if (x >= max_width || y >= max_height)
        return 0;
    return fb_ptr[y * (this->fb->pitch / 4) + x];
}

// Fill the entire screen with a single color.
void Graphics::clear_screen(uint32_t color)
{
    for (uint64_t y = 0; y < max_height; y++)
    {
        for (uint64_t x = 0; x < max_width; x++)
        {
            draw_pixel(color, x, y);
        }
    }
}

// Bresenham's line algorithm — integer-only, no floating point.
// Draws a line between any two points using only addition/subtraction.
// Works in all octants by tracking an error term.
void Graphics::draw_line(uint32_t color, uint64_t p1x, uint64_t p1y,
                         uint64_t p2x, uint64_t p2y)
{
    int64_t x1 = (int64_t)p1x;
    int64_t y1 = (int64_t)p1y;
    int64_t x2 = (int64_t)p2x;
    int64_t y2 = (int64_t)p2y;

    int64_t dx = x2 - x1;
    if (dx < 0)
        dx = -dx;

    int64_t dy = y2 - y1;
    if (dy < 0)
        dy = -dy;

    int64_t sx = (x1 < x2) ? 1 : -1;
    int64_t sy = (y1 < y2) ? 1 : -1;

    int64_t err = dx - dy;

    while (true)
    {
        draw_pixel(color, x1, y1);
        if (x1 == x2 && y1 == y2)
            break;
        int64_t e2 = err * 2;

        if (e2 > -dy)
        {
            err -= dy;
            x1 += sx;
        }
        if (e2 < dx)
        {
            err += dx;
            y1 += sy;
        }
    }
}
