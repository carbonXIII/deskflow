/*
 * Deskflow -- mouse and keyboard sharing utility
 * Copyright (C) 2025 Jordan Richards
 *
 * This package is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * found in the file LICENSE that should have accompanied this file.
 *
 * This package is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "platform/XdpRemoteDesktop.h"

#include "base/Log.h"
#include "platform/XdpClipboard.h"
#include "platform/XdpRequest.h"
#include "platform/XdpUtils.h"
#include "platform/remotedesktopinterface.h"
#include "platform/sessioninterface.h"

namespace deskflow {
static auto &iface();
struct RemoteDesktops : SessionManager<XdpRemoteDesktop, org::freedesktop::portal::RemoteDesktop>
{
  using Base = org::freedesktop::portal::RemoteDesktop;
  using Sessions = org::freedesktop::portal::Session;

  friend auto &iface()
  {
    return XdpEventManager::singleton<RemoteDesktops>();
  }

  RemoteDesktops()
  {}
};

XdpRemoteDesktop::XdpRemoteDesktop()
{
  reconnect();
}

XdpRemoteDesktop::~XdpRemoteDesktop() = default;

void XdpRemoteDesktop::reconnect(bool fatal)
{
  if (valid())
    return;
  handle = Handle::create_pending<>(this);
}

bool XdpRemoteDesktop::valid()
{
  return !!handle;
}

int XdpRemoteDesktop::connect_to_eis()
{
  return iface()
      .lock([this](auto &iface) {
        if (ErrorOr fd = iface.ConnectToEIS(*handle, {})) {
          return fd->takeFileDescriptor();
        } else {
          on_error(fd.error().format("Failed to connect to EIS server: %s"));
          return -1;
        }
      })
      .value_or(-1);
}

enum PersistMode : unsigned
{
  NONE,
  APPLICATION,
  EXPLICIT,
};

void XdpRemoteDesktop::select_devices()
{
  ErrorOr pending = iface().lock([this](auto &iface) {
    auto request = handle_request<>(handle, &XdpRemoteDesktop::handle_devices_selected);

    auto args = request->args();
    // leaving out "types" = all devices
    args["persist_mode"] = PersistMode::APPLICATION;
    if (restore_token.size())
      args["restore_token"] = QString(restore_token.c_str());
    return iface.SelectDevices(*handle, args);
  });

  if (!pending) {
    on_error(pending.error().format("Failed to call select devices: %s"));
  }
}

bool XdpRemoteDesktop::do_create(const char *session, const char *request)
{
  auto args = QVariantMap{
      std::pair{"session_handle_token", session},
      std::pair{"handle_token", request},
  };

  if (ErrorOr pending = iface()->CreateSession(args); !pending) {
    on_error(pending.error().format("Unable to create session: %s"), true);
    return false;
  }

  return true;
}

void XdpRemoteDesktop::handle_created()
{
  LOG_DEBUG("Remote desktop session object path: %s", std::string{*handle}.c_str());
  iface()->add_session(this->handle);
  select_devices();
  on_created();
}

std::shared_ptr<HandleBase> XdpRemoteDesktop::get_handle()
{
  return std::static_pointer_cast<HandleBase>(handle);
}

void XdpRemoteDesktop::handle_devices_selected()
{
  LOG_DEBUG("Devices selected, starting remote desktop session");
  auto request =
      handle_request<"devices", "clipboard_enabled", "restore_token">(handle, &XdpRemoteDesktop::handle_started);

  if (ErrorOr res = XdpClipboard::try_request_clipboard(handle); !res)
    on_error(res.error().format("Failed to request clipboard: %s"));

  if (ErrorOr pending = iface()->Start(*handle, {}, request->args()); !pending)
    on_error(pending.error().format("Failed to call start: %s"));
}

void XdpRemoteDesktop::handle_started(
    Devices devices, std::optional<bool> clipboard_enabled, std::optional<std::string> restore_token
)
{
  LOG_DEBUG("Remote desktop session started.");
  this->clipboard_enabled = clipboard_enabled.value_or(false);
  this->restore_token = restore_token.value_or("");
  on_started(devices, this->clipboard_enabled, this->restore_token);
}

void XdpRemoteDesktop::handle_closed()
{
  on_closed();
}
} // namespace deskflow
