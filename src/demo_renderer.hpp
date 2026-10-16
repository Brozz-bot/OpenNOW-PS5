/*
 * ps5-native-app-boilerplate - Small CPU-rendered demonstration API.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Keeps PS5 VideoOut setup and the bitmap font out of the starter main file.
 */

#pragma once

#include <cstdint>
#include <span>
#include <string_view>

namespace ps5::demo
{
enum class Color : std::uint32_t
{
    background = UINT32_C(0xff190d0a),
    panel = UINT32_C(0xff301f17),
    white = UINT32_C(0xffffffff),
    cyan = UINT32_C(0xffffff00),
    magenta = UINT32_C(0xffff00ff),
    yellow = UINT32_C(0xff00ffff),
};

class Canvas;
using DrawScene = bool (*)(Canvas &) noexcept;
void run(DrawScene draw, std::string_view ready_message) noexcept;

void requestStop() noexcept;
#ifdef OPENNOW_HOST_PREVIEW
using PreviewStep = void (*)(unsigned frame) noexcept;
void runFrames(DrawScene draw, unsigned count, PreviewStep step, const char *directory, const char *finalPath,
               bool report) noexcept;
#endif

struct Clip
{
    int left = 0, top = 0, right = 1920, bottom = 1080;
};

class Canvas final
{
  public:
    enum class Button : std::uint8_t { up, down, dpad, cross, circle, square, triangle, l1, r1, options };
    void setClip(float x, float y, float width, float height) noexcept;
    void setClip(const Clip &clip) noexcept { clip_ = clip; }
    Clip clip() const noexcept { return clip_; }
    void resetClip() noexcept;
    void setOpacity(unsigned alpha) noexcept { opacity_ = alpha > 255 ? 255 : alpha; }
    unsigned opacity() const noexcept { return opacity_; }
    void setOffset(float x, float y) noexcept { offsetX_ = x; offsetY_ = y; }
    void clear(Color color) noexcept;
    void image(unsigned x,unsigned y,unsigned width,unsigned height,const std::uint32_t*) noexcept;
    void rectangle(unsigned x, unsigned y, unsigned width, unsigned height, Color color) noexcept;
    void circle(unsigned center_x, unsigned center_y, unsigned radius, Color color) noexcept;
    void triangle(unsigned center_x, unsigned top, unsigned half_width, unsigned height,
                  Color color) noexcept;
    void button(unsigned x, unsigned y, Button button, unsigned size, Color color) noexcept;
    void text(unsigned x, unsigned y, std::string_view value, unsigned scale, Color color) noexcept;
    void coverage(int x, int y, unsigned width, unsigned height, const std::uint8_t *mask,
                  unsigned stride, Color color, unsigned alpha = 255) noexcept;
    void roundRect(float x, float y, float w, float h, float radius, Color color,
                   unsigned alpha = 255) noexcept;
    void roundRectStroke(float x, float y, float w, float h, float radius, float thickness,
                         Color color, unsigned alpha = 255) noexcept;
    void verticalGradient(float x, float y, float w, float h, float radius, Color top,
                          Color bottom) noexcept;
    void line(float x0, float y0, float x1, float y1, float thickness, Color color,
              unsigned alpha = 255) noexcept;
    void ring(float cx, float cy, float radius, float thickness, Color color,
              unsigned alpha = 255) noexcept;
    void arc(float cx, float cy, float radius, float thickness, float start, float sweep, Color color,
             unsigned alpha = 255) noexcept;
    void disc(float cx, float cy, float radius, Color color, unsigned alpha = 255) noexcept;
    void polygon(const float *points, unsigned count, Color color, unsigned alpha = 255) noexcept;
    void imageRounded(int x, int y, unsigned width, unsigned height, const std::uint32_t *data,
                      float radius, unsigned alpha = 255) noexcept;
    void imageScaled(float x, float y, float w, float h, const std::uint32_t *data, unsigned sourceWidth,
                     unsigned sourceHeight, float radius, unsigned alpha = 255) noexcept;
    void imageDoubled(const std::uint32_t *data, unsigned sourceWidth, unsigned sourceHeight,
                      unsigned alpha = 255, const std::uint32_t *under = nullptr) noexcept;
    void scrim(float x, float y, float w, float h, float radius, float from, Color color, unsigned alpha) noexcept;

  private:
    explicit Canvas(std::uint32_t *pixels) noexcept : pixels_{pixels}
    {
    }

    int shiftX() const noexcept;
    int shiftY() const noexcept;
    unsigned faded(unsigned alpha) const noexcept;

    std::uint32_t *pixels_;
    Clip clip_{};
    unsigned opacity_ = 255;
    float offsetX_ = 0, offsetY_ = 0;

    friend void run(DrawScene draw, std::string_view ready_message) noexcept;
#ifdef OPENNOW_HOST_PREVIEW
    friend void runFrames(DrawScene draw, unsigned count, PreviewStep step, const char *directory, const char *finalPath,
               bool report) noexcept;
#endif
};

void read_asset_text(const char *path, std::span<char> destination,
                     std::string_view fallback) noexcept;
} // namespace ps5::demo
