#include "tests_weapons.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "options.h"
#include "sim/data/config.h"
#include "sim/game/weapon.h"

namespace {

std::string weapons_beside(const std::string& data_path) {
    return (std::filesystem::u8path(data_path).parent_path() / "weapons.toml").u8string();
}

bool write_text(const std::string& path, const std::string& text) {
    std::ofstream out(std::filesystem::u8path(path), std::ios::binary);
    if (!out) return false;
    out << text;
    return static_cast<bool>(out);
}

bool mentions(const std::vector<std::string>& list, const std::string& needle) {
    for (const std::string& line : list) {
        if (line.find(needle) != std::string::npos) return true;
    }
    return false;
}

// A complete one-weapon section; callers replace one line to test a bad value.
std::string one_weapon(const std::string& override_line) {
    return "[weapon.test]\n" + override_line;
}

}  // namespace

int run_weapon_test(const Options& opt) {
    int failures = 0;
    auto check = [&](const char* label, bool ok) {
        if (!ok) ++failures;
        std::printf("  %-56s %s\n", label, ok ? "ok" : "FAIL");
    };

    const std::string real = weapons_beside(opt.data_path);
    sim::WeaponParams params;
    std::vector<std::string> warnings;
    std::string error;
    if (!sim::load_weapon_params(real, params, warnings, error)) {
        std::printf("FAIL: cannot load %s: %s\n", real.c_str(), error.c_str());
        return 1;
    }

    const bool clean = warnings.empty();
    check("the shipped weapons.toml loads with no warnings", clean);
    for (const std::string& w : warnings) std::printf("    warning: %s\n", w.c_str());

    const sim::WeaponDef* rifle = params.find("rifle");
    const sim::WeaponDef* pistol = params.find("pistol");
    check("rifle and pistol are both defined", rifle != nullptr && pistol != nullptr);
    check("ids are exact: no case folding", params.find("Rifle") == nullptr &&
                                                params.find("") == nullptr);

    bool ordered = false;
    bool penetrates = false;
    bool stopped = false;
    bool usable = false;
    if (rifle != nullptr && pistol != nullptr) {
        ordered = rifle->damage_near > rifle->damage_far && rifle->damage_far > 0.0f &&
                  rifle->falloff_end > rifle->falloff_start;
        penetrates = rifle->penetration_count >= 1;
        // M6.5 needs a weapon the map can stop, or "penetration works" proves nothing.
        stopped = pistol->penetration_count == 0;
        usable = rifle->magazine > 0 && rifle->reserve_ammo >= 0 && rifle->reload_seconds > 0.0f;
    }
    check("damage falls off downhill inside an ordered window", ordered);
    check("the rifle can cross one layer", penetrates);
    check("the pistol cannot cross any", stopped);
    check("magazine, reserve and reload are usable", usable);

    // Two loads of the same bytes must agree, or nothing downstream is stable.
    sim::WeaponParams again;
    std::vector<std::string> again_warnings;
    std::string again_error;
    const bool stable = sim::load_weapon_params(real, again, again_warnings, again_error) &&
                        again.hash() == params.hash() && again.weapons.size() == params.weapons.size();
    check("loading twice yields the same hash", stable);

    // Independent hash: tuning a weapon must not look like a movement change,
    // and one changed number must move the weapon hash.
    sim::MovementParams movement;
    std::vector<std::string> movement_warnings;
    std::string movement_error;
    const bool movement_ok =
        sim::load_movement_params(opt.data_path, movement, movement_warnings, movement_error);
    const uint64_t movement_before = movement.hash();

    const std::string var_a = "build/weapons_var_a.toml";
    const std::string var_b = "build/weapons_var_b.toml";
    const bool wrote = write_text(var_a, one_weapon("damage_near = 20.0\nrpm = 500.0\n")) &&
                       write_text(var_b, one_weapon("damage_near = 21.0\nrpm = 500.0\n"));
    sim::WeaponParams a;
    sim::WeaponParams b;
    std::vector<std::string> scratch;
    std::string scratch_error;
    const bool both_loaded = wrote &&
                             sim::load_weapon_params(var_a, a, scratch, scratch_error) &&
                             sim::load_weapon_params(var_b, b, scratch, scratch_error);
    sim::MovementParams movement_after;
    std::vector<std::string> scratch2;
    const bool reloaded =
        sim::load_movement_params(opt.data_path, movement_after, scratch2, scratch_error);
    check("one changed number moves the weapon hash",
          both_loaded && a.hash() != b.hash());
    check("weapon data cannot move the movement hash",
          movement_ok && reloaded && movement_after.hash() == movement_before);

    // A weapon is a complete definition: missing keys warn and keep the default.
    const std::string partial = "build/weapons_partial.toml";
    // 30, not 10: the compiled default damage_far is 18, and a near damage below
    // the far damage is not a weapon (the validator says so, correctly).
    const bool partial_written = write_text(partial, one_weapon("damage_near = 30.0\n"));
    sim::WeaponParams soft;
    std::vector<std::string> soft_warnings;
    std::string soft_error;
    const sim::WeaponDef defaults;
    const bool partial_ok = partial_written &&
                            sim::load_weapon_params(partial, soft, soft_warnings, soft_error);
    check("a missing key keeps its default",
          partial_ok && soft.weapons.size() == 1 && soft.weapons[0].damage_far == defaults.damage_far &&
              soft.weapons[0].magazine == defaults.magazine);
    check("every missing key is reported", partial_ok && soft_warnings.size() == 10);

    const std::string typo = "build/weapons_typo.toml";
    const bool typo_written = write_text(typo, one_weapon("damgae_near = 10.0\n"));
    sim::WeaponParams typo_out;
    std::vector<std::string> typo_warnings;
    std::string typo_error;
    sim::load_weapon_params(typo, typo_out, typo_warnings, typo_error);
    check("a typo is reported as an unknown key",
          typo_written && mentions(typo_warnings, "damgae_near: unknown key"));

    // Windows editors save UTF-8 with a BOM; a data file that opens in the editor
    // must still load, or the first save silently breaks the game.
    const std::string bom = "build/weapons_bom.toml";
    const std::string bom_text = "\xEF\xBB\xBF" + one_weapon("damage_near = 30.0\n");
    sim::WeaponParams bom_out;
    std::vector<std::string> bom_warnings;
    std::string bom_error;
    check("a UTF-8 BOM does not break the file",
          write_text(bom, bom_text) &&
              sim::load_weapon_params(bom, bom_out, bom_warnings, bom_error) &&
              bom_out.weapons.size() == 1);

    // Values that cannot be simulated are hard errors, not warnings.
    auto rejects = [&](const std::string& name, const std::string& body, const std::string& needle) {
        const std::string path = "build/" + name + ".toml";
        sim::WeaponParams out;
        std::vector<std::string> out_warnings;
        std::string out_error;
        return write_text(path, body) &&
               !sim::load_weapon_params(path, out, out_warnings, out_error) &&
               out_error.find(needle) != std::string::npos;
    };
    const bool rpm_zero = rejects("weapons_rpm0", one_weapon("rpm = 0.0\n"), "rpm");
    const bool empty_mag = rejects("weapons_mag0", one_weapon("magazine = 0\n"), "magazine");
    const bool backwards =
        rejects("weapons_backwards", one_weapon("damage_near = 10.0\ndamage_far = 12.0\n"),
                "damage_far");
    const bool no_sections = rejects("weapons_empty", "# nothing here\n", "no [weapon.");
    const bool duplicate = rejects("weapons_dupe",
                                   "[weapon.test]\ndamage_near = 10.0\ndamage_near = 11.0\n",
                                   "duplicate key");
    check("rpm 0 is rejected", rpm_zero);
    check("an empty magazine is rejected", empty_mag);
    check("damage_far above damage_near is rejected", backwards);
    check("a file with no weapons is rejected", no_sections);
    check("a duplicate key is rejected", duplicate);

    std::printf("[weapons] weapons %zu clean %d stable %d independent %d defaults %d rejects %d\n",
                params.weapons.size(), clean ? 1 : 0, stable ? 1 : 0,
                (both_loaded && a.hash() != b.hash() && movement_after.hash() == movement_before) ? 1 : 0,
                partial_ok ? 1 : 0,
                (rpm_zero ? 1 : 0) + (empty_mag ? 1 : 0) + (backwards ? 1 : 0) +
                    (no_sections ? 1 : 0) + (duplicate ? 1 : 0));

    if (failures != 0) {
        std::printf("FAIL: %d weapon data checks failed\n", failures);
        return 1;
    }
    std::printf("PASS: weapon data loads, hashes independently, and rejects bad values\n");
    return 0;
}
