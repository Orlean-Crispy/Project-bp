#pragma once

#include <cstdint>
#include <string>

namespace sim {

// Physical keys the client can bind. This is a policy layer: it turns device
// input into MoveInput, and nothing here is ever simulated or hashed.
enum class Key : uint16_t {
    None = 0,
    A, B, C, D, E, F, G, H, I, J, K, L, M,
    N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    Num0, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,
    Space, Enter, Escape, Tab, Backspace,
    LeftShift, RightShift, LeftCtrl, RightCtrl, LeftAlt, RightAlt,
    Up, Down, Left, Right,
    MouseLeft, MouseRight, MouseMiddle,
    Count
};

constexpr size_t kKeyCount = static_cast<size_t>(Key::Count);

// Canonical name, e.g. "SPACE", "LEFT_SHIFT", "Q".
const char* key_name(Key key);

// Accepts canonical names plus a few aliases ("SHIFT", "CTRL", "ESC"), and is
// case insensitive so a hand edited file does not need exact casing.
bool key_from_name(const std::string& name, Key& out);

}  // namespace sim
