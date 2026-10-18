#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "sim/input/keycodes.h"
#include "sim/move/pmove.h"

namespace sim {

// Client-side actions. These never enter the simulation: the simulation only
// ever sees a MoveInput, which is what makes the keymap safe to change at runtime.
enum class Action : uint8_t {
    MoveForward = 0,
    MoveBack,
    MoveLeft,
    MoveRight,
    Jump,
    Crouch,
    Slide,
    Dash,
    WallRun,
    WallClimb,
    Vault,
    Reload,
    Use,
    Scoreboard,
    Count
};

constexpr size_t kActionCount = static_cast<size_t>(Action::Count);

const char* action_name(Action action);
bool action_from_name(const std::string& name, Action& out);

// What is physically held right now. The client fills this from the OS; tests
// fill it directly.
class KeyState {
public:
    void set(Key key, bool down);
    void clear();
    bool down(Key key) const;

private:
    std::array<bool, kKeyCount> m_down{};
};

// A data-driven keymap. Defaults match the Valorant ability layout
// (C / Q / E / X), but every action can be rebound at runtime and persisted,
// which is the actual requirement.
class Bindings {
public:
    Bindings();

    bool load(const std::string& path, std::string& error, std::vector<std::string>& warnings);
    bool save(const std::string& path, std::string& error) const;

    Key key(Action action) const;
    void set_key(Action action, Key key);
    bool down(Action action, const KeyState& keys) const;

    // Two actions sharing one key, or an action with no key at all.
    std::vector<std::string> conflicts() const;
    std::vector<std::string> unbound() const;

private:
    std::array<Key, kActionCount> m_keys{};
};

MoveInput build_move_input(const Bindings& bindings, const KeyState& keys, float yaw);

}  // namespace sim
