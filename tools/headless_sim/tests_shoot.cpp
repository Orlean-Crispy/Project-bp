#include "tests_shoot.h"

#include <cmath>
#include <cstdio>
#include <string>

#include "options.h"
#include "sim/game/shoot.h"
#include "sim/game/weapon.h"
#include "test_util.h"

int run_damage_test(const Options& opt) {
    sim::World world;
    std::string error;
    if (!load_world_from("sim/data/maps/test_box.map", opt.data_path, world, error)) {
        std::printf("FAIL: damage world: %s\n", error.c_str());
        return 1;
    }

    int failures = 0;
    auto check = [&](const char* label, bool ok) {
        if (!ok) ++failures;
        std::printf("  %-58s %s\n", label, ok ? "ok" : "FAIL");
    };

    const sim::WeaponDef* rifle = world.weapons.find_by_slot(0);
    check("the rifle is found by its slot", rifle != nullptr && rifle->id == "rifle");
    if (rifle == nullptr) return 1;

    // Shooter at -5 on the open lane, victim at +3, eight metres away.
    sim::PlayerState shooter;
    shooter.pos = sim::Vec3(-5.0f, 5.0f, 0.0f);
    shooter.yaw = 0.0f;
    shooter.impulses = world.params.max_impulses;
    sim::equip_weapon(shooter, *rifle);

    sim::PlayerState victim;
    victim.pos = sim::Vec3(3.0f, 5.0f, 0.0f);
    victim.yaw = 3.14159f;
    victim.impulses = world.params.max_impulses;
    sim::equip_weapon(victim, *rifle);

    sim::HitboxHistory history[2];

    auto reset = [&](const sim::PlayerState& a, const sim::PlayerState& b) {
        world.players = {a, b};
        history[0].reset();
        history[1].reset();
        for (uint32_t t = 0; t <= 10; ++t) {
            history[0].record(world.players[0], world.params, t);
            history[1].record(world.players[1], world.params, t);
        }
    };

    // A level shot from eye height (0.9 x 1.42 = 1.278 m) lands in the head band
    // (anything above 0.82 x 1.42 = 1.164 m), so aim down a little for the chest.
    sim::MoveInput fire;
    fire.fire = true;
    fire.pitch = -0.05f;

    // --- chest damage, cadence and ammo ---------------------------------
    reset(shooter, victim);
    sim::ShotReport report;
    uint32_t tick = 10;
    auto run = [&](int ticks, const sim::MoveInput& input, sim::ShotReport* out) {
        for (int i = 0; i < ticks; ++i, ++tick) {
            sim::update_weapon(world.players[0], 0, input, *rifle, world, history, 2, tick, 1.0f,
                               out);
            history[0].record(world.players[0], world.params, tick + 1);
            history[1].record(world.players[1], world.params, tick + 1);
        }
    };
    run(40, fire, &report);
    check("chest shots kill a bare player", world.players[1].vitals.health == 0);
    check("the rifle fired at its cadence", report.shots == 7 && report.hits == 7);
    check("each round is spent", world.players[0].ammo == 23);
    check("a 25 round body shot is 25 damage", report.damage == 7 * 25);
    std::printf("    chest: shots %d hits %d damage %d hp %d ammo %d\n", report.shots, report.hits,
                report.damage, world.players[1].vitals.health, world.players[0].ammo);

    // --- armour changes the number of rounds, not the rule ---------------
    sim::PlayerState armoured = victim;
    sim::equip_armor(armoured.vitals, sim::ArmorType::Heavy, world.combat);
    reset(shooter, armoured);
    tick = 10;
    sim::ShotReport armour_report;
    run(60, fire, &armour_report);
    check("50 armour takes two more rounds",
          world.players[1].vitals.health == 0 && world.players[1].vitals.armor == 0);
    std::printf("    armour: shots %d hits %d damage %d hp %d armour %d\n", armour_report.shots,
                armour_report.hits, armour_report.damage, world.players[1].vitals.health,
                world.players[1].vitals.armor);

    // --- a level shot is a headshot: true damage, one round --------------
    reset(shooter, armoured);
    tick = 10;
    sim::MoveInput head = fire;
    head.pitch = 0.0f;
    sim::ShotReport head_report;
    run(4, head, &head_report);
    check("a headshot ignores armour and kills in one round",
          world.players[1].vitals.health == 0 && world.players[1].vitals.armor == 50 &&
              head_report.shots == 1);
    check("the head multiplies by five", head_report.damage == 125);

    // --- a wall between them stops everything ----------------------------
    sim::PlayerState wall_shooter = shooter;
    wall_shooter.pos = sim::Vec3(-5.0f, 0.0f, 0.0f);
    sim::PlayerState wall_victim = victim;
    wall_victim.pos = sim::Vec3(5.0f, 0.0f, 0.0f);
    reset(wall_shooter, wall_victim);
    tick = 10;
    sim::ShotReport wall_report;
    sim::MoveInput wall_fire = fire;
    wall_fire.pitch = 0.0f;
    run(30, wall_fire, &wall_report);
    check("the thin wall absorbs the rounds",
          world.players[1].vitals.health == 100 && wall_report.hits == 0 &&
              wall_report.shots > 0);

    // --- reload ----------------------------------------------------------
    sim::PlayerState low = shooter;
    low.ammo = 1;
    reset(low, victim);
    tick = 10;
    report = sim::ShotReport{};
    run(2, fire, &report);
    check("the last round fires", report.shots == 1 && world.players[0].ammo == 0);
    run(2, fire, &report);
    check("an empty magazine cannot fire", report.shots == 1);
    sim::MoveInput press_reload;
    press_reload.reload = true;
    run(130, press_reload, &report);
    check("reload refills the magazine from the reserve",
          world.players[0].ammo == rifle->magazine &&
              world.players[0].reserve == rifle->reserve_ammo - rifle->magazine);

    std::printf("[damage] body 4 rounds armour %d head %d wall hits %d reload %d\n",
                armour_report.shots, head_report.damage, wall_report.hits,
                world.players[0].ammo);

    if (failures != 0) {
        std::printf("FAIL: %d damage checks failed\n", failures);
        return 1;
    }
    std::printf("PASS: the trigger spends rounds, hits hulls and moves health\n");
    return 0;
}
