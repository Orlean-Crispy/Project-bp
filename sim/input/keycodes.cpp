#include "sim/input/keycodes.h"

#include <algorithm>
#include <cctype>

namespace sim {
namespace {

struct Entry {
    Key key;
    const char* name;
};

const Entry kTable[] = {
    {Key::A, "A"}, {Key::B, "B"}, {Key::C, "C"}, {Key::D, "D"}, {Key::E, "E"},
    {Key::F, "F"}, {Key::G, "G"}, {Key::H, "H"}, {Key::I, "I"}, {Key::J, "J"},
    {Key::K, "K"}, {Key::L, "L"}, {Key::M, "M"}, {Key::N, "N"}, {Key::O, "O"},
    {Key::P, "P"}, {Key::Q, "Q"}, {Key::R, "R"}, {Key::S, "S"}, {Key::T, "T"},
    {Key::U, "U"}, {Key::V, "V"}, {Key::W, "W"}, {Key::X, "X"}, {Key::Y, "Y"},
    {Key::Z, "Z"},
    {Key::Num0, "NUM0"}, {Key::Num1, "NUM1"}, {Key::Num2, "NUM2"}, {Key::Num3, "NUM3"},
    {Key::Num4, "NUM4"}, {Key::Num5, "NUM5"}, {Key::Num6, "NUM6"}, {Key::Num7, "NUM7"},
    {Key::Num8, "NUM8"}, {Key::Num9, "NUM9"},
    {Key::Space, "SPACE"}, {Key::Enter, "ENTER"}, {Key::Escape, "ESCAPE"},
    {Key::Tab, "TAB"}, {Key::Backspace, "BACKSPACE"},
    {Key::LeftShift, "LEFT_SHIFT"}, {Key::RightShift, "RIGHT_SHIFT"},
    {Key::LeftCtrl, "LEFT_CTRL"}, {Key::RightCtrl, "RIGHT_CTRL"},
    {Key::LeftAlt, "LEFT_ALT"}, {Key::RightAlt, "RIGHT_ALT"},
    {Key::Up, "UP"}, {Key::Down, "DOWN"}, {Key::Left, "LEFT"}, {Key::Right, "RIGHT"},
    {Key::MouseLeft, "MOUSE_LEFT"}, {Key::MouseRight, "MOUSE_RIGHT"},
    {Key::MouseMiddle, "MOUSE_MIDDLE"},
};

std::string upper(const std::string& s) {
    std::string out = s;
    for (char& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}

}  // namespace

const char* key_name(Key key) {
    for (const Entry& e : kTable) {
        if (e.key == key) return e.name;
    }
    return "NONE";
}

bool key_from_name(const std::string& name, Key& out) {
    std::string n = upper(name);
    // Spaces are tolerated: "left shift" reads better in a hand edited file.
    n.erase(std::remove(n.begin(), n.end(), ' '), n.end());
    if (n.empty() || n == "NONE") {
        // Bindings::save writes the literal "NONE" for an unbound action, so the
        // loader has to accept it or a saved file reloads with the old default.
        out = Key::None;
        return true;
    }

    for (const Entry& e : kTable) {
        if (n == e.name) {
            out = e.key;
            return true;
        }
    }

    if (n == "SHIFT") { out = Key::LeftShift; return true; }
    if (n == "CTRL" || n == "CONTROL") { out = Key::LeftCtrl; return true; }
    if (n == "ALT") { out = Key::LeftAlt; return true; }
    if (n == "ESC") { out = Key::Escape; return true; }
    return false;
}

}  // namespace sim
