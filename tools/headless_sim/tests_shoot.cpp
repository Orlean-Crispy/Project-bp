#include "tests_shoot.h"

#include <cmath>
#include <cstdio>
#include <string>

#include "options.h"
#include "sim/core/event.h"
#include <vector>

#include "sim/game/hitscan.h"
#include "sim/game/respawn.h"
#include "sim/game/shoot.h"
#include "sim/game/weapon.h"
#include "test_util.h"

namespace {

bool near(float a, float b) { return std::fabs(a - b) <= 1e-6f; }

}  // namespace

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
    auto run = [&](int ticks, const sim::MoveInput& input, sim::ShotReport* out,
                   sim::EventQueue* events = nullptr) {
        for (int i = 0; i < ticks; ++i, ++tick) {
            sim::update_weapon(world.players[0], 0, input, *rifle, world, history, 2, tick, 1.0f,
                               out, events);
            history[0].record(world.players[0], world.params, tick + 1);
            history[1].record(world.players[1], world.params, tick + 1);
        }
    };
    run(40, fire, &report);
    check("chest shots kill a bare player", world.players[1].vitals.health == 0);
    check("the rifle fired at its cadence", report.shots == 7);
    check("each round is spent", world.players[0].ammo == 23);
    // Four rounds kill; the rest pass through the corpse (M6.9), so only four hit.
    check("body shots stop at the kill", report.hits == 4 && report.damage == 4 * 25);
    std::printf("    chest: shots %d hits %d damage %d hp %d ammo %d\n", report.shots, report.hits,
                report.damage, world.players[1].vitals.health, world.players[0].ammo);

    // Tracers ride the event channel: one event per shot, muzzle to impact.
    sim::EventQueue shots;
    reset(shooter, victim);
    tick = 10;
    sim::ShotReport event_report;
    run(8, fire, &event_report, &shots);
    int shot_events = 0;
    bool tracer_ok = true;
    sim::SimEvent event;
    while (shots.pop(event)) {
        if (event.type != sim::EventType::Shot) continue;
        ++shot_events;
        if (!(event.a.z > 1.0f) || !(event.b.x > event.a.x)) tracer_ok = false;
    }
    check("each shot emits one tracer event",
          shot_events == event_report.shots && shot_events > 0);
    check("the tracer runs from the eye to the impact", tracer_ok);

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
    const int reloaded_ammo = world.players[0].ammo;
    check("reload refills the magazine from the reserve",
          reloaded_ammo == rifle->magazine &&
              world.players[0].reserve == rifle->reserve_ammo - rifle->magazine);


    // --- death: no trigger, no cover, and a respawn after the timer ------
    reset(shooter, victim);
    tick = 10;
    sim::ShotReport kill;
    run(40, fire, &kill);
    check("the victim dies", world.players[1].vitals.health == 0);

    world.players[0].vitals.health = 0;
    sim::ShotReport dead_fire;
    run(10, fire, &dead_fire);
    check("a dead player cannot fire", dead_fire.shots == 0);

    const sim::Vec3 spawn(-16.0f, 0.0f, 0.05f);
    uint16_t respawn_timer = 0;
    bool came_back = false;
    for (int i = 0; i < sim::kRespawnTicks + 2 && !came_back; ++i) {
        came_back = sim::tick_respawn(world, 0, respawn_timer, spawn);
    }
    check("the respawn timer brings the player back", came_back);
    check("a respawn is a fresh player, not a healed corpse",
          world.players[0].vitals.health == world.combat.max_health &&
              world.players[0].pos == spawn &&
              world.players[0].ammo == rifle->magazine);

    // A corpse is not cover: the ray must pass through it to the live body.
    sim::HitboxSample corpse;
    corpse.pos = sim::Vec3(0.0f, 5.0f, 0.0f);
    corpse.height = 1.42f;
    corpse.radius = 0.405f;
    corpse.alive = false;
    sim::HitboxSample live = corpse;
    live.pos = sim::Vec3(4.0f, 5.0f, 0.0f);
    live.alive = true;
    sim::HitboxSample line[2] = {corpse, live};
    const sim::ShotHit through = sim::hitscan(world.collision, line, 2, sim::Vec3(-6.0f, 5.0f, 1.0f),
                                              sim::Vec3(1.0f, 0.0f, 0.0f), 50.0f, -1);
    check("a corpse does not stop the ray", through.is_player && through.entity == 1);
    line[0].alive = true;
    const sim::ShotHit blocked = sim::hitscan(world.collision, line, 2, sim::Vec3(-6.0f, 5.0f, 1.0f),
                                              sim::Vec3(1.0f, 0.0f, 0.0f), 50.0f, -1);
    check("a live body in the way does block", blocked.is_player && blocked.entity == 0);


    // --- recoil: the pattern is data, the order is fixed ----------------
    const int cap = sim::recoil_cap(*rifle);
    check("the pattern has eight steps", cap == 8);
    check("the first shot is straight", sim::recoil_offset(*rifle, 0).pitch == 0.0f);
    float total_pitch = 0.0f;
    for (float step : rifle->recoil_pitch) total_pitch += step;
    check("the second shot carries the first step",
          near(sim::recoil_offset(*rifle, 1).pitch, rifle->recoil_pitch[0]));
    check("the spray stops climbing at the cap",
          near(sim::recoil_offset(*rifle, 100).pitch, total_pitch));

    // Fire the whole pattern at the far wall and watch the impacts climb.
    reset(shooter, victim);
    world.players[1].pos = sim::Vec3(3.0f, 9.0f, 0.0f);  // out of the line
    tick = 10;
    sim::EventQueue spray;
    sim::ShotReport spray_report;
    sim::MoveInput level;
    level.fire = true;
    run(70, level, &spray_report, &spray);
    std::vector<float> impact_z;
    sim::SimEvent shot_event;
    while (spray.pop(shot_event)) {
        if (shot_event.type == sim::EventType::Shot) impact_z.push_back(shot_event.b.z);
    }
    bool climbs = impact_z.size() >= 5 && impact_z[4] > impact_z[0] + 0.2f;
    check("the impacts climb down the pattern", climbs);
    std::printf("    recoil: shots %d first z %.3f fifth z %.3f\n", (int)impact_z.size(),
                impact_z.empty() ? 0.0f : impact_z[0],
                impact_z.size() > 4 ? impact_z[4] : 0.0f);

    // Silence resets the spray: 24 ticks of no fire (recovery_ticks).
    sim::MoveInput idle;
    run(rifle->recoil_recovery_ticks + 2, idle, &spray_report);
    check("silence resets the spray", world.players[0].recoil_index == 0);
    check("a fresh spray starts straight again",
          sim::recoil_offset(*rifle, world.players[0].recoil_index).pitch == 0.0f);

    std::printf("[damage] body 4 rounds armour %d head %d wall hits %d reload %d\n",
                armour_report.shots, head_report.damage, wall_report.hits, reloaded_ammo);

    if (failures != 0) {
        std::printf("FAIL: %d damage checks failed\n", failures);
        return 1;
    }
    std::printf("PASS: the trigger spends rounds, hits hulls and moves health\n");
    return 0;
}
