#pragma once

// Native 1280x720 layout units, relative to the row's horizontal origin (screen x=640).
// Column x is its left edge. Future headers should use these same bounds.
struct BindingRowColumn {
    float x, width;
    constexpr float center() const { return x + width * .5f; }
};
struct BindingRowLayout {
    float label_right, label_width;
    BindingRowColumn key, pad;
    float text_inset, glow_extra;
};
constexpr BindingRowLayout binding_row_layout() {
    return {-110.f, 300.f, {-85.f, 240.f}, {175.f, 240.f}, 20.f, 56.f};
}
