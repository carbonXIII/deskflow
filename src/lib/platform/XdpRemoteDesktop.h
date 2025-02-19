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

#pragma once

#include <memory>
#include <optional>
#include <string>

namespace deskflow {
struct RemoteDesktop;

enum class Devices : unsigned
{
  KEYBOARD = 1,
  POINTER = 2,
  TOUCHSCREEN = 4,
};

struct HandleBase;
template <typename Session> struct Handle;
template <typename T> struct SessionContract;

struct XdpClipboard;

struct XdpRemoteDesktop
{
  using Handle = deskflow::Handle<XdpRemoteDesktop>;

  XdpRemoteDesktop(const XdpRemoteDesktop &) = delete;
  XdpRemoteDesktop(XdpRemoteDesktop &&) = delete;
  XdpRemoteDesktop &operator=(const XdpRemoteDesktop &) = delete;
  XdpRemoteDesktop &operator=(XdpRemoteDesktop &&) = delete;

  XdpRemoteDesktop();
  virtual ~XdpRemoteDesktop();
  bool valid();

  virtual void on_created()
  {}
  virtual void on_started(Devices devices, bool clipboard_enabled, std::string restore_token)
  {}
  virtual void on_closed()
  {}

  void reconnect(bool fatal = true);
  int connect_to_eis();

  template <typename Clipboard> Clipboard get_clipboard()
  {
    return Clipboard(clipboard_enabled ? handle : nullptr);
  }

  // Session contract
  virtual void on_error(std::string const &message, bool fatal = false)
  {}

protected:
  void select_devices();

  // Session contract
  friend SessionContract<XdpRemoteDesktop>;
  friend Handle;
  bool do_create(const char *session, const char *request);
  void handle_created();
  std::shared_ptr<HandleBase> get_handle();

  // RemoteDesktop contract
  friend struct RemoteDesktops;
  void handle_devices_selected();
  void handle_started(Devices devices, std::optional<bool> clipboard_enabled, std::optional<std::string> restore_token);
  void handle_closed();

  // Clipboard contract
  friend XdpClipboard;
  bool has_clipboard()
  {
    return clipboard_enabled;
  }

private:
  std::shared_ptr<Handle> handle;
  std::string restore_token;

  bool clipboard_enabled = false;
};
} // namespace deskflow
