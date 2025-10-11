#pragma once

#include <X11/Xlib.h>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

struct Point {
    std::size_t x;
    std::size_t y;
};

struct WindowPosition {
    Point start;
    Point end;

    void resize_to(Display *const display, const Window window) const {

        XMoveResizeWindow(display, window, start.x, start.y, end.x - start.x,
                          end.y - start.y);
    }
};

struct Resolution {
    Resolution() : width(1), height(1) {}
    Resolution(size_t width, size_t height) : width(width), height(height) {}

    Resolution(Display *display)
        : width(XDisplayWidth(display, 0)), height(XDisplayHeight(display, 0)) {
    }

    WindowPosition fullscreen() {
        return {.start = {0, 0}, .end = {width, height}};
    }

    std::size_t width;
    std::size_t height;
};

enum class TmuxBarPosition : bool {
    BOTTOM,
    TOP,
};

// Now now, assume padding like kitty
// i.e. even over x axis, all on bottom on y axis
struct WindowLayouts {

    WindowLayouts(Display *display, Resolution terminal_resolution)
        : m_screen_resolution(display), m_term_resolution(terminal_resolution),
          m_bar_position(TmuxBarPosition::BOTTOM),
          m_x_padding_distribution(PaddingDistribution::EVEN),
          m_y_padding_distribution(PaddingDistribution::EVEN),
          m_term_char_resolution(
              char_resolution(m_screen_resolution, terminal_resolution)),
          m_total_padding(padding(m_screen_resolution, terminal_resolution)),
          m_init_padding(init_padding(m_total_padding, m_x_padding_distribution,
                                      m_y_padding_distribution)) {}

    WindowPosition fullscreen_term_position() const {
        Point start_px, end_px;
        switch (m_bar_position) {
        case TmuxBarPosition::BOTTOM:
            start_px = {0, 0};
            end_px = {m_term_resolution.width, m_term_resolution.height - 1};
            break;
        case TmuxBarPosition::TOP:
            start_px = {0, 1};
            end_px = {m_term_resolution.width, m_term_resolution.height};
            break;
        }

        return {
            .start = term_to_screen_pos(start_px),
            .end = term_to_screen_pos(end_px),
        };
    }

    void set_term_resolution(const Resolution resolution) {
        m_term_resolution = resolution;
        update_char_resolution();
    }

    void set_screen_resolution(const Resolution resoltuion) {
        m_screen_resolution = resoltuion;
        update_char_resolution();
    }

    void set_bar_position(const TmuxBarPosition bar_position) {
        m_bar_position = bar_position;
    }

    constexpr WindowPosition add_bar(WindowPosition position) const {
        if (m_bar_position == TmuxBarPosition::TOP) {
            position.start.x++;
            position.end.x++;
        }
        return position;
    }

    // Ignores bar
    constexpr WindowPosition
    term_to_screen_pos(const WindowPosition position) const {
        return {.start = term_to_screen_pos(position.start),
                .end = term_to_screen_pos(position.end)};
    }


  private:
    enum class PaddingDistribution : uint8_t {
        START,
        EVEN,
        END,
    };

    constexpr Resolution
    char_resolution(const Resolution screen_resolution,
                    const Resolution term_resolution) const {
        return Resolution(screen_resolution.width / term_resolution.width,
                          screen_resolution.height / term_resolution.height);
    }

    constexpr Resolution padding(const Resolution screen_resolution,
                                 const Resolution term_resolution) const {
        return Resolution(screen_resolution.width % term_resolution.width,
                          screen_resolution.height % term_resolution.height);
    }

    constexpr size_t
    init_padding(const size_t total_padding,
                 const PaddingDistribution padding_distribution) const {
        switch (padding_distribution) {
        case PaddingDistribution::START:
            return total_padding;
        case PaddingDistribution::EVEN:
            return total_padding / 2;
        case PaddingDistribution::END:
            return 0;
        }
        std::unreachable();
    }

    constexpr Resolution
    init_padding(const Resolution total_padding,
                 const PaddingDistribution x_padding_distribution,
                 const PaddingDistribution y_padding_distribution) const {
        return {init_padding(total_padding.width, x_padding_distribution),
                init_padding(total_padding.height, y_padding_distribution)};
    }

    void update_char_resolution() {
        m_term_char_resolution =
            char_resolution(m_screen_resolution, m_term_resolution);
        m_total_padding = padding(m_screen_resolution, m_term_resolution);
        m_init_padding = init_padding(m_total_padding, m_x_padding_distribution,
                                      m_y_padding_distribution);
    }

    // Top left pixel of terminal character
    // Ignores bar
    constexpr Point term_to_screen_pos(const Point pixel_idx) const {
        return {.x = term_to_screen_pos(pixel_idx.x, m_screen_resolution.width,
                                        m_term_resolution.width,
                                        m_term_char_resolution.width,
                                        m_init_padding.width),
                .y = term_to_screen_pos(pixel_idx.y, m_screen_resolution.height,
                                        m_term_resolution.height,
                                        m_term_char_resolution.height,
                                        m_init_padding.height)};
    }

    // Helper: one axis
    constexpr size_t term_to_screen_pos(const size_t pixel_idx,
                                        const size_t screen_resolution,
                                        const size_t term_resolution,
                                        const size_t term_char_resolution,
                                        const size_t init_padding) const {
        if (pixel_idx == 0)
            return 0;
        else if (pixel_idx >= term_resolution)
            return screen_resolution;
        else
            return init_padding + pixel_idx * term_char_resolution;
    }

    Resolution m_screen_resolution;
    Resolution m_term_resolution;

    TmuxBarPosition m_bar_position;
    PaddingDistribution m_x_padding_distribution;
    PaddingDistribution m_y_padding_distribution;

    Resolution m_term_char_resolution;
    Resolution m_total_padding;

    Resolution m_init_padding;
};
