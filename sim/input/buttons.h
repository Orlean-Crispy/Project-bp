#pragma once

#include <cstdint>

#include "sim/move/pmove.h"

namespace sim {

// One bit per button the simulation can see. The same mask is what the command
// packet carries, so adding a button costs no wire change (the field is already
// u16) and the bridge never grows another positional bool argument.
enum Button : uint16_t {
    kButtonJump = 1u << 0,
    kButtonCrouch = 1u << 1,
    kButtonSlide = 1u << 2,
    kButtonDash = 1u << 3,
    kButtonWallRun = 1u << 4,
    kButtonWallClimb = 1u << 5,
    kButtonVault = 1u << 6,
    kButtonFire = 1u << 7,
    kButtonReload = 1u << 8,
    kButtonAds = 1u << 9,
};

constexpr unsigned kButtonCount = 10;
constexpr uint16_t kAllButtons = static_cast<uint16_t>((1u << kButtonCount) - 1u);

// Mask <-> struct. Both directions live in one file so "add a button" touches
// this header, the bindings table and nothing else.
uint16_t buttons_from_input(const MoveInput& input);
MoveInput input_from_buttons(uint16_t buttons);

}  // namespace sim
