#include "game_requests.h"

#include <cstring>
#include <map>
#include <utility>

#include "game_calls.h"

namespace zap {
namespace {

struct Kind {
    const char* name;
    // With data: the name, then a space or a line break and the data, matched by the first word.
    // Any other is matched by its whole text.
    bool data;
    bool needs_config;  // some say false: Game answers "refused not configured" itself
    bool sets_state;    // sets_client_state
    std::string (*simple)(Game&) = nullptr;                                     // no data
    void (*parse)(const std::string& request, PreparedRequest& out) = nullptr;  // with data
};

// Parsed by the serving thread: its error, or the work that applies what it parsed.
void parse_discs_request(const std::string& request, PreparedRequest& out) {
    // The OS Install Disks goal's count and the scanner's line (scan.h; the format: requests.h).
    int count = 0, required = 0;
    bool told = false;
    std::string line;
    out.error = parse_discs(request, count, required, line, told);
    out.run = [=](Game& game) mutable {
        const bool on = !line.empty();
        game.set_discs(count, required, std::move(line), told);
        return std::string(on ? "discs on" : "discs off");
    };
}

void parse_mission_request(const std::string& request, PreparedRequest& out) {
    // Offer RETURN HOME WITH THE DISKS? when the game is next idle in play, lapsing while the
    // player is in play outside home's cell (mission.h; the format: requests.h).
    int column = -1, row = -1;
    out.error = parse_mission(request, column, row);
    out.run = [=](Game& game) {
        return std::string(game.request_mission(column, row) ? "queued" : "refused not configured");
    };
}

void parse_labels_request(const std::string& request, PreparedRequest& out) {
    std::map<int, Label> labels;
    out.error = parse_labels(request, labels);
    out.run = [labels = std::move(labels)](Game& game) mutable {
        const size_t n = labels.size();
        game.set_labels(std::move(labels));
        return "labeled " + std::to_string(n);
    };
}

void parse_newgame_request(const std::string& request, PreparedRequest& out) {
    NewGamePlan plan;
    out.error = parse_newgame(request, plan);
    out.run = [plan = std::move(plan)](Game& game) mutable {
        return std::string(game.arm_newgame(std::move(plan)) ? "armed" : "refused not configured");
    };
}

void parse_apply_request(const std::string& request, PreparedRequest& out) {
    Batch batch;
    out.error = parse_batch(request, batch);
    out.run = [batch = std::move(batch)](Game& game) { return game.apply(batch); };
}

// The table. The `config` and `offline_put` rows have no handler: the host answers them, and the
// row says only that they set state.
const Kind kKinds[] = {
    {"config", true, false, true},
    {"offline_put", true, false, true},
    {"ap_newgame", true, true, true, nullptr, &parse_newgame_request},
    {"ap_labels", true, true, true, nullptr, &parse_labels_request},
    {"discs", true, true, true, nullptr, &parse_discs_request},
    {"mission_prompt", true, true, true, nullptr, &parse_mission_request},
    {"other_save", false, true, true, [](Game& g) { return g.other_save(); }},
    {"client_back", false, true, true,
     [](Game& g) { return std::string(g.client_back() ? "shown" : "none"); }},
    {"assists lock", false, true, true,
     [](Game& g) {
         g.set_assists_locked(true);
         return std::string("locked");
     }},
    {"assists free", false, true, true,
     [](Game& g) {
         g.set_assists_locked(false);
         return std::string("free");
     }},
    {"intel on", false, true, true,
     [](Game& g) {
         g.set_intel(true);
         return std::string("intel on");
     }},
    {"intel off", false, true, true,
     [](Game& g) {
         g.set_intel(false);
         return std::string("intel off");
     }},
    // Game::apply refuses "refused not configured" itself, and a batch no frame took never applies.
    {"apply", true, false, false, nullptr, &parse_apply_request},
    {"disarm", false, false, false,
     [](Game& g) {
         g.disarm_newgame();
         return std::string("disarmed");
     }},
    // A grant, a death, a shield: acts once, and no client's state to put back.
    {"shield", false, false, false, [](Game& g) { return g.spawn_shield(ticks_ms()); }},
    {"die", false, false, false, [](Game& g) { return g.kill(ticks_ms()); }},
    // Status, for live checks.
    {"mission_status", false, true, false, [](Game& g) { return g.mission_status(); }},
    {"mission_black", false, true, false, [](Game& g) { return g.mission_black(); }},
    {"discs_status", false, false, false, [](Game& g) { return g.equipment_status(); }},
    {"trap_status", false, false, false, [](Game& g) { return g.trap_status(); }},
    {"drop_status", false, false, false, [](Game& g) { return g.drop_status(); }},
    {"newgame_status", false, false, false, [](Game& g) { return g.newgame_status(); }},
    {"feats_status", false, false, false, [](Game& g) { return g.feats_state(); }},
    {"title_status", false, false, false, [](Game& g) { return g.title_state(); }},
};

const Kind* find_kind(std::string_view request) {
    for (const Kind& kind : kKinds) {
        const std::string_view name(kind.name);
        if (!kind.data ? request == name
                       : request == name || (request.starts_with(name) &&
                                             (request[name.size()] == ' ' ||
                                              request[name.size()] == '\n'))) {
            return &kind;
        }
    }
    return nullptr;
}

}  // namespace

std::optional<PreparedRequest> prepare_game_request(std::string_view request) {
    const Kind* kind = find_kind(request);
    if (kind == nullptr || (kind->simple == nullptr && kind->parse == nullptr)) return std::nullopt;
    PreparedRequest out;
    out.needs_config = kind->needs_config;
    if (kind->simple != nullptr) {
        out.run = kind->simple;
    } else {
        kind->parse(std::string(request), out);
    }
    return out;
}

bool sets_client_state(std::string_view request) {
    const Kind* kind = find_kind(request);
    return kind != nullptr && kind->sets_state;
}

}  // namespace zap
