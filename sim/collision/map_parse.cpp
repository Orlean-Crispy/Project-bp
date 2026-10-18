#include "sim/collision/map_parse.h"

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace sim {
namespace {

// The one and only Quake-unit -> metre conversion in the project (iron law A).
constexpr float kUnitToMetre = 0.0254f;

struct Token {
    enum class Kind { End, Open, Close, ParenOpen, ParenClose, String, Word } kind = Kind::End;
    std::string text;
    int line = 0;
};

class Lexer {
public:
    explicit Lexer(const std::string& text) : m_text(text) {}

    Token next() {
        if (m_has_pushback) {
            m_has_pushback = false;
            return m_pushback;
        }
        skip_space_and_comments();
        Token t;
        t.line = m_line;
        if (m_pos >= m_text.size()) return t;

        const char c = m_text[m_pos];
        if (c == '{') { ++m_pos; t.kind = Token::Kind::Open; return t; }
        if (c == '}') { ++m_pos; t.kind = Token::Kind::Close; return t; }
        if (c == '(') { ++m_pos; t.kind = Token::Kind::ParenOpen; return t; }
        if (c == ')') { ++m_pos; t.kind = Token::Kind::ParenClose; return t; }
        if (c == '"') {
            ++m_pos;
            const size_t start = m_pos;
            while (m_pos < m_text.size() && m_text[m_pos] != '"') {
                if (m_text[m_pos] == '\n') ++m_line;
                ++m_pos;
            }
            t.kind = Token::Kind::String;
            t.text = m_text.substr(start, m_pos - start);
            if (m_pos < m_text.size()) ++m_pos;
            return t;
        }

        const size_t start = m_pos;
        while (m_pos < m_text.size()) {
            const char ch = m_text[m_pos];
            if (std::isspace(static_cast<unsigned char>(ch)) || ch == '{' || ch == '}' ||
                ch == '(' || ch == ')' || ch == '"') {
                break;
            }
            if (ch == '/' && m_pos + 1 < m_text.size() && m_text[m_pos + 1] == '/') break;
            ++m_pos;
        }
        t.kind = Token::Kind::Word;
        t.text = m_text.substr(start, m_pos - start);
        return t;
    }

    void push_back(const Token& t) {
        m_pushback = t;
        m_has_pushback = true;
    }

private:
    void skip_space_and_comments() {
        while (m_pos < m_text.size()) {
            const char c = m_text[m_pos];
            if (c == '\n') { ++m_line; ++m_pos; continue; }
            if (std::isspace(static_cast<unsigned char>(c))) { ++m_pos; continue; }
            if (c == '/' && m_pos + 1 < m_text.size() && m_text[m_pos + 1] == '/') {
                while (m_pos < m_text.size() && m_text[m_pos] != '\n') ++m_pos;
                continue;
            }
            break;
        }
    }

    const std::string& m_text;
    size_t m_pos = 0;
    int m_line = 1;
    Token m_pushback;
    bool m_has_pushback = false;
};

bool parse_float(const std::string& s, float& out) {
    if (s.empty()) return false;
    char* end = nullptr;
    const double d = std::strtod(s.c_str(), &end);
    if (end == s.c_str() || *end != '\0') return false;
    out = static_cast<float>(d);
    return true;
}

// Reads "x y z )" after the opening parenthesis has already been consumed.
bool read_point_body(Lexer& lexer, Vec3& out, std::string& error) {
    float v[3] = {0.0f, 0.0f, 0.0f};
    for (int i = 0; i < 3; ++i) {
        Token t = lexer.next();
        if (t.kind != Token::Kind::Word || !parse_float(t.text, v[i])) {
            error = "line " + std::to_string(t.line) + ": expected a coordinate";
            return false;
        }
    }
    Token close = lexer.next();
    if (close.kind != Token::Kind::ParenClose) {
        error = "line " + std::to_string(close.line) + ": expected ')'";
        return false;
    }
    // Iron law A: the single conversion point.
    out = Vec3(v[0] * kUnitToMetre, v[1] * kUnitToMetre, v[2] * kUnitToMetre);
    return true;
}

bool parse_brush(Lexer& lexer, Brush& out, std::string& error) {
    std::vector<Plane> planes;
    std::string material;

    for (;;) {
        Token t = lexer.next();
        if (t.kind == Token::Kind::Close) break;
        if (t.kind == Token::Kind::End) {
            error = "unexpected end of file inside a brush";
            return false;
        }
        if (t.kind != Token::Kind::ParenOpen) {
            error = "line " + std::to_string(t.line) + ": expected a plane";
            return false;
        }

        std::vector<Vec3> points;
        Vec3 first;
        if (!read_point_body(lexer, first, error)) return false;
        points.push_back(first);

        for (;;) {
            Token n = lexer.next();
            if (n.kind != Token::Kind::ParenOpen) {
                lexer.push_back(n);
                break;
            }
            Vec3 p;
            if (!read_point_body(lexer, p, error)) return false;
            points.push_back(p);
        }

        if (points.size() < 3) {
            error = "line " + std::to_string(t.line) + ": plane needs at least three points";
            return false;
        }
        // Quake 2 lists four points; only the first three define the plane.
        planes.push_back(make_plane(points[0], points[1], points[2]));

        Token tex = lexer.next();
        if (tex.kind != Token::Kind::Word && tex.kind != Token::Kind::String) {
            error = "line " + std::to_string(tex.line) + ": expected a texture name";
            return false;
        }
        if (material.empty()) material = tex.text;

        for (int i = 0; i < 5; ++i) {
            Token n = lexer.next();
            float dummy = 0.0f;
            if (n.kind != Token::Kind::Word || !parse_float(n.text, dummy)) {
                error = "line " + std::to_string(n.line) + ": expected texture placement numbers";
                return false;
            }
        }
        // Optional trailing numbers (Valve 220 / Quake 2 surface flags), then
        // either the next plane or the end of the brush.
        for (;;) {
            Token n = lexer.next();
            if (n.kind == Token::Kind::Close) {
                out = Brush();
                out.material = material;
                std::string build_error;
                if (!build_brush(planes, out, &build_error)) {
                    error = "line " + std::to_string(n.line) + ": " + build_error;
                    return false;
                }
                return true;
            }
            if (n.kind == Token::Kind::ParenOpen) {
                lexer.push_back(n);
                break;
            }
            float dummy = 0.0f;
            if (n.kind == Token::Kind::Word && parse_float(n.text, dummy)) continue;
            error = "line " + std::to_string(n.line) + ": unexpected token in brush";
            return false;
        }
    }
    error = "brush ended without a texture";
    return false;
}

}  // namespace

bool parse_map_text(const std::string& text, MapData& out, std::string& error) {
    MapData map;
    Lexer lexer(text);

    for (;;) {
        Token t = lexer.next();
        if (t.kind == Token::Kind::End) break;
        if (t.kind != Token::Kind::Open) {
            error = "line " + std::to_string(t.line) + ": expected '{'";
            return false;
        }

        MapEntity entity;
        for (;;) {
            Token inner = lexer.next();
            if (inner.kind == Token::Kind::Close) break;
            if (inner.kind == Token::Kind::End) {
                error = "unexpected end of file inside an entity";
                return false;
            }
            if (inner.kind == Token::Kind::String) {
                Token value = lexer.next();
                if (value.kind != Token::Kind::String) {
                    error = "line " + std::to_string(value.line) + ": key without a value";
                    return false;
                }
                entity.keys[inner.text] = value.text;
                continue;
            }
            if (inner.kind == Token::Kind::Open) {
                Brush brush;
                if (!parse_brush(lexer, brush, error)) return false;
                entity.brushes.push_back(brush);
                continue;
            }
            error = "line " + std::to_string(inner.line) + ": unexpected token in entity";
            return false;
        }

        for (Brush& brush : entity.brushes) {
            const auto surface = entity.keys.find("_surface");
            if (surface != entity.keys.end() && !surface->second.empty()) brush.material = surface->second;
            brush.climbable = entity.keys.count("_climbable") && entity.keys.at("_climbable") == "1";
            brush.wallrunnable = entity.keys.count("_wallrunnable") && entity.keys.at("_wallrunnable") == "1";
        }

        map.entities.push_back(entity);
    }

    bool have_bounds = false;
    for (size_t e = 0; e < map.entities.size(); ++e) {
        for (Brush& brush : map.entities[e].brushes) {
            brush.entity_index = static_cast<int>(e);
            if (!have_bounds) {
                map.bounds = brush.bounds;
                have_bounds = true;
            } else {
                map.bounds.expand(brush.bounds.mins);
                map.bounds.expand(brush.bounds.maxs);
            }
            map.brushes.push_back(brush);
        }
    }

    if (map.entities.empty()) {
        error = "map contains no entities";
        return false;
    }
    out = map;
    return true;
}

bool parse_map_file(const std::string& path, MapData& out, std::string& error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        error = "cannot open " + path;
        return false;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    std::string text = buffer.str();
    if (!parse_map_text(text, out, error)) {
        error = path + ": " + error;
        return false;
    }
    out.source = path;
    return true;
}

}  // namespace sim
