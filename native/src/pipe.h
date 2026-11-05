// The helper's command channel: a local named pipe in message mode, one request and one reply
// per message, UTF-8 text. The client connects with client/native.py.
#pragma once

#include <functional>
#include <string>

namespace zap {

// \\.\pipe\zexion-ap-<pid>, for the process the helper runs in.
std::wstring pipe_name(unsigned long pid);

// Creates the only instance of the pipe (FIRST_PIPE_INSTANCE: a squatter makes it fail),
// current user only, local only, and serves one client at a time, sending back what `handle`
// returns. Returns true once `handle` sets `stop` (after the reply), false if the pipe cannot
// be created or breaks. `on_disconnect` runs on this thread after each client leaves.
using Handler = std::function<std::string(const std::string& request, bool& stop)>;
bool serve_pipe(const std::wstring& name, const Handler& handle,
                const std::function<void()>& on_disconnect = {});

}  // namespace zap
