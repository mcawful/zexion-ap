// The helper's command channel: a local named pipe in message mode, one request and one reply
// per message, UTF-8 text. The client connects with multiprocessing.connection (client/native.py).
#pragma once

#include <functional>
#include <string>

namespace zap {

// \\.\pipe\zexion-ap-<pid>, for the process the helper runs in.
std::wstring pipe_name(unsigned long pid);

// Serves one client at a time, calling `handle` for each request and sending back what it
// returns. A client that disconnects is replaced by the next one. Returns once `handle` sets
// `stop` (the reply is sent first), or if the pipe cannot be created.
using Handler = std::function<std::string(const std::string& request, bool& stop)>;
void serve_pipe(const std::wstring& name, const Handler& handle);

}  // namespace zap
