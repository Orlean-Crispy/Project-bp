#include "tests_hitscan.h"

#include <cmath>
#include <cstdio>
#include <string>

#include "options.h"
#include "sim/game/hitscan.h"
#include "sim/game/rewind.h"
#include "test_util.h"

namespace {

bool near(float a, float b, float tol) { return std::fabs(a - b) <= tol; }

sim::HitboxSample standing(float x, float y) {
    sim::HitboxSample box;
    box.pos = sim::Vec3(x, y, 0.0f);
    box.height = 1.42f;
    box.radius = 0.405f;
    box.crouched = false;
    return box;
}

}  // namespace

int run_hitscan_test(const Options& opt) {
    sim::World world;
    std::string error;
    if (!load_world_from("sim/data/maps/test_box.map", opt.data_path, world, error)) {
        std::printf("FAIL: hitscan world: %s\n", error.c_str());
        return 1;
    }

    int failures = 0;
    auto check = [&](const char* label, bool ok) {
        if (!ok) ++failures;
        std::printf("  %-58s %s\n", label, ok ? "ok" : "FAIL");
    };

    // Open lane at y = 5 (the thin wall only spans y in [-2, 2]).
    const sim::Vec3 eye(-3.0f, 5.0f, 1.28f);
    const sim::Vec3 right(1.0f, 0.0f, 0.0f);
    const float range = 150.0f;

    sim::HitboxSample boxes[4];
    boxes[0] = standing(-3.0f, 5.0f);  // the shooter, ignored by its own shot
    boxes[1] = standing(3.0f, 5.0f);   // the target

    const sim::ShotHit chest =
        sim::hitscan(world.collision, boxes, 2, eye, right, range, 0);
    check("aimed at the chest: hit the player",
          chest.hit && chest.is_player && chest.entity == 1);
    // The ray stops at the near face of the hull, not at its centre.
    check("impact lands on the front face",
          near(chest.point.x, 3.0f - 0.405f, 1e-3f) && near(chest.distance, 5.595f, 1e-3f));
    check("the face points back at the shooter", near(chest.normal.x, -1.0f, 1e-6f));

    // E1's other half: one box width (2 x radius) to the side must miss.
    boxes[1] = standing(3.0f, 5.0f + 2.0f * 0.405f);
    const sim::ShotHit wide = sim::hitscan(world.collision, boxes, 2, eye, right, range, 0);
    check("one box width off: no player hit", !wide.hit || !wide.is_player);

    // The edge is the hull, not a generous margin.
    boxes[1] = standing(3.0f, 5.0f + 0.40f);
    const sim::ShotHit inside = sim::hitscan(world.collision, boxes, 2, eye, right, range, 0);
    check("0.40 m off centre still hits (radius 0.405)", inside.is_player);
    boxes[1] = standing(3.0f, 5.0f + 0.41f);
    const sim::ShotHit outside = sim::hitscan(world.collision, boxes, 2, eye, right, range, 0);
    check("0.41 m off centre misses", !outside.is_player);

    // Crouching drops the hull under the same eye-level ray.
    boxes[1] = standing(3.0f, 5.0f);
    boxes[1].height = 0.91f;
    boxes[1].crouched = true;
    const sim::ShotHit ducked = sim::hitscan(world.collision, boxes, 2, eye, right, range, 0);
    check("a crouched target ducks under an eye-level shot", !ducked.is_player);

    // The world blocks: the thin wall sits between the two sides.
    boxes[1] = standing(3.0f, 0.0f);
    const sim::Vec3 wall_eye(-3.0f, 0.0f, 1.28f);
    const sim::ShotHit blocked =
        sim::hitscan(world.collision, boxes, 2, wall_eye, right, range, 0);
    check("a wall between them stops the shot",
          blocked.hit && !blocked.is_player && near(blocked.point.x, -0.05f, 1e-3f));

    // Range: a shot that ends before the target is a miss, not a hit at 0.
    boxes[1] = standing(3.0f, 5.0f);
    const sim::ShotHit short_shot =
        sim::hitscan(world.collision, boxes, 2, eye, right, 3.0f, 0);
    check("max range ends the ray", !short_shot.hit);

    // Self: the eye is inside the shooter's own hull (1.28 < 1.42).
    const sim::ShotHit self_hit =
        sim::hitscan(world.collision, boxes, 2, eye, right, range, -1);
    check("without an ignore index the shooter hits itself", self_hit.is_player &&
                                                                self_hit.entity == 0);
    check("the ignore index removes the shooter", !chest.is_player || chest.entity != 0);

    // Nearest wins when two players line up.
    sim::HitboxSample line[4];
    line[1] = standing(1.0f, 5.0f);
    line[2] = standing(5.0f, 5.0f);
    const sim::ShotHit nearest = sim::hitscan(world.collision, line, 3, eye, right, range, 0);
    check("the nearest player in the line is hit", nearest.is_player && nearest.entity == 1);

    // A direction of any length is normalised, so distance stays in metres.
    const sim::Vec3 long_dir(10.0f, 0.0f, 0.0f);
    const sim::ShotHit scaled = sim::hitscan(world.collision, boxes, 2, eye, long_dir, range, 0);
    check("a non-unit direction gives the same distance",
          scaled.is_player && near(scaled.distance, chest.distance, 1e-3f));

    // Zero direction is a miss, not a NaN.
    const sim::ShotHit degenerate =
        sim::hitscan(world.collision, boxes, 2, eye, sim::Vec3(0.0f, 0.0f, 0.0f), range, 0);
    check("a zero direction is a miss", !degenerate.hit);


    // ---- M6.4: rewind -------------------------------------------------
    // A target crossing the lane at 8 m/s (0.125 m per tick), moving across
    // the line of fire. At tick 100 it is at y = 5; six ticks earlier it was
    // at y = 4.25, which is 0.75 m away - more than a hull radius, so
    // "shoot where it was" and "shoot where it is" give opposite answers.
    sim::HitboxHistory history;
    const float per_tick = 8.0f / 64.0f;
    // 61 frames: the ring keeps 64 ticks, so this is a full second of history.
    for (uint32_t t = 40; t <= 100; ++t) {
        sim::PlayerState mover;
        mover.pos = sim::Vec3(3.0f, 5.0f - static_cast<float>(100 - t) * per_tick, 0.0f);
        history.record(mover, world.params, t);
    }

    check("6 ticks is the rewind window", std::fabs(sim::rewind_ticks(90.0f) - 6.0f) < 1e-4f);
    check("a bad latency clamps to the floor",
          std::fabs(sim::rewind_ticks(-5.0f) - sim::kInterpTicks) < 1e-4f);

    sim::HitboxSample past[1];
    const int past_count = sim::rewind_frame(&history, 1, 100, 6.0f, past, 1);
    check("the rewound frame is the one from six ticks ago",
          past_count == 1 && near(past[0].pos.y, 4.25f, 1e-4f));

    sim::HitboxSample now_frame[1];
    sim::rewind_frame(&history, 1, 100, 0.0f, now_frame, 1);
    check("rewind 0 is the current frame", near(now_frame[0].pos.y, 5.0f, 1e-4f));

    // Aim at where the target was. Against the rewound frame that is a hit;
    // against the current frame it is 0.75 m of air.
    const sim::Vec3 aim(6.0f, 4.25f - 5.0f, 0.0f);
    const sim::ShotHit old_shot = sim::hitscan(world.collision, past, 1, eye, aim, range, -1);
    const sim::ShotHit new_shot =
        sim::hitscan(world.collision, now_frame, 1, eye, aim, range, -1);
    check("a 6-tick-late shot still hits the old position", old_shot.is_player);
    check("the same aim against the current frame misses", !new_shot.is_player);

    // Between two frames the sample is interpolated, not snapped.
    sim::HitboxSample half[1];
    sim::rewind_frame(&history, 1, 100, 5.5f, half, 1);
    check("a fractional rewind interpolates between frames",
          near(half[0].pos.y, 4.3125f, 1e-4f));

    // Older than the retained history: clamp to the oldest frame, never
    // extrapolate (a rewind must not invent a position nobody recorded).
    sim::HitboxSample clamped[1];
    sim::rewind_frame(&history, 1, 45, 6.0f, clamped, 1);
    check("rewinding past the oldest frame clamps to it",
          near(clamped[0].pos.y, -2.5f, 1e-4f));
    std::printf("[hitscan] chest %d face %d wide %d edge %d crouch %d wall %d range %d\n",
                chest.is_player ? 1 : 0, near(chest.point.x, 2.595f, 1e-3f) ? 1 : 0,
                (!wide.is_player) ? 1 : 0, (inside.is_player && !outside.is_player) ? 1 : 0,
                (!ducked.is_player) ? 1 : 0, (!blocked.is_player && blocked.hit) ? 1 : 0,
                (!short_shot.hit) ? 1 : 0);

    std::printf("[rewind] window %d at-6 %d current-miss %d interp %d clamp %d\n",
                sim::kMaxRewindTicks, old_shot.is_player ? 1 : 0,
                (!new_shot.is_player) ? 1 : 0, near(half[0].pos.y, 4.3125f, 1e-4f) ? 1 : 0,
                near(clamped[0].pos.y, -2.5f, 1e-4f) ? 1 : 0);

    if (failures != 0) {
        std::printf("FAIL: %d hitscan checks failed\n", failures);
        return 1;
    }
    std::printf("PASS: the ray hits what is in front of it and nothing else\n");
    return 0;
}
