#pragma once

#include <map>
#include <string>
#include <vector>

#include "sim/collision/brush.h"

namespace sim {

struct MapEntity {
    std::map<std::string, std::string> keys;
    std::vector<Brush> brushes;
};

struct MapData {
    std::vector<MapEntity> entities;
    std::vector<Brush> brushes;  // flattened, in file order (deterministic)
    Bounds bounds;
    int rejected_brushes = 0;
    std::string source;
};

// Parses Quake style .map text. This function is the ONLY place where Quake
// units are converted: every coordinate is multiplied by 0.0254 on the way in,
// and nothing downstream ever sees an inch (iron law A).
bool parse_map_text(const std::string& text, MapData& out, std::string& error);
bool parse_map_file(const std::string& path, MapData& out, std::string& error);

}  // namespace sim
