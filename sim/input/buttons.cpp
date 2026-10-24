#include "sim/input/buttons.h"

namespace sim {

uint16_t buttons_from_input(const MoveInput& input) {
    uint16_t bits = 0;
    if (input.jump) bits |= kButtonJump;
    if (input.crouch) bits |= kButtonCrouch;
    if (input.slide) bits |= kButtonSlide;
    if (input.dash) bits |= kButtonDash;
    if (input.wallrun) bits |= kButtonWallRun;
    if (input.wallclimb) bits |= kButtonWallClimb;
    if (input.vault) bits |= kButtonVault;
    if (input.fire) bits |= kButtonFire;
    if (input.reload) bits |= kButtonReload;
    if (input.ads) bits |= kButtonAds;
    return bits;
}

MoveInput input_from_buttons(uint16_t buttons) {
    MoveInput input;
    input.jump = (buttons & kButtonJump) != 0;
    input.crouch = (buttons & kButtonCrouch) != 0;
    input.slide = (buttons & kButtonSlide) != 0;
    input.dash = (buttons & kButtonDash) != 0;
    input.wallrun = (buttons & kButtonWallRun) != 0;
    input.wallclimb = (buttons & kButtonWallClimb) != 0;
    input.vault = (buttons & kButtonVault) != 0;
    input.fire = (buttons & kButtonFire) != 0;
    input.reload = (buttons & kButtonReload) != 0;
    input.ads = (buttons & kButtonAds) != 0;
    return input;
}

}  // namespace sim
