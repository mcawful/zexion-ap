// The helper's command channel: a local named pipe in message mode, one request and one reply
// per message, UTF-8 text. The client connects with client/native.py.
#pragma once

#include <functional>
#include <string>

namespace zap {

// \\.\pipe\zexion-ap-<pid>, for the process the helper runs in.
std::wstring pipe_name(unsigned long pid);

// Creates the pipe (failing if any process already has one by that name, so a squatter cannot
// sit in front of it), open to the current user only, and serves one client at a time on that
// one instance, calling `handle` for each request and sending back what it returns. A client
// that disconnects is replaced by the next one. Returns true once `handle` sets `stop` (after
// the reply is sent), false if the pipe could not be created.
using Handler = std::function<std::string(const std::string& request, bool& stop)>;
bool serve_pipe(const std::wstring& name, const Handler& handle);

}  // namespace zap
