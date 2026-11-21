// The requests that act on the game, one table for both helpers (native/README.md, "The pipe" and
// "The Linux helper"): each one's name, whether it needs the helper configured, whether it sets
// something a client's going must put back, and what it runs on the game's thread. Parsed on the
// serving thread (requests.h), so a bad one is answered with no frame. What a host adds to it, as
// it must: `config` and `offline_put` (their own addresses and files), and the replies of the
// pipe's or the socket's own (`hello`, `ping`, `unload`, `lang`, `read`, ...).
#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace zap {

class Game;

// A request parsed and ready to run in a frame.
struct PreparedRequest {
    std::string error;           // what is wrong with it: answered "error <error>", no frame taken
    bool needs_config = false;   // answered "refused not configured" while nothing configured Game
    std::function<std::string(Game&)> run;  // the reply, on the game's thread
};

// `request` parsed, or nothing if it is not one Game runs (`config` and `offline_put` are the
// hosts', and so is anything else the table does not name).
std::optional<PreparedRequest> prepare_game_request(std::string_view request);

// Whether a connection that sent `request` has something a client's going must put back (a plan,
// labels, the ASSIST lock, Intel Chips, the disks' count, a prompt asked for, a seed put, a
// config that made it the active client): then Game::client_gone runs once it goes, and the
// seeds' copies are written. The others only read the game, answer it, or act once (a grant, a
// death, a shield), so `helper.py status`, a ping or a bare hello leaves everything as it was.
// Anything the table does not name sets nothing.
bool sets_client_state(std::string_view request);

}  // namespace zap
