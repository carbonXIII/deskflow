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
#include <span>
#include <string>
#include <variant>
#include <vector>

struct QDBusObjectPath;
struct QString;

namespace deskflow {
template <typename Res> struct ErrorOr;

template <typename Session> struct Handle;
struct HandleBase;
struct XdpClipboard
{
  using Handle = deskflow::Handle<XdpClipboard>;

  static ErrorOr<std::monostate> try_request_clipboard(std::shared_ptr<HandleBase> const &session);

  template <typename Clipboard, typename Session>
  static std::shared_ptr<Clipboard> get_clipboard(Session *session, auto &&...args)
  {
    if (!session->has_clipboard())
      return nullptr;
    return std::make_shared<Clipboard>(session->get_handle(), std::forward<decltype(args)>(args)...);
  }

  XdpClipboard(std::shared_ptr<HandleBase> const &session);
  virtual ~XdpClipboard();
  bool valid();

  virtual std::string on_transfer(std::string_view mime_type)
  {
    return "";
  }
  virtual void on_owner_changed(std::string_view mime_type, std::string_view mime_data)
  {}
  virtual void on_error(std::string const &message, bool fatal = false)
  {}

  virtual std::span<const std::string_view> supported_mime_types()
  {
    return {};
  }
  bool is_supported(std::string_view type);

  void set_selection(std::vector<std::string> mime_types);

protected:
  friend struct XdpClipboards;

  // For some reason this is a structure containing an array, not just an array
  using MimeTypesStruct = std::tuple<std::vector<std::string>>;
  void handle_owner_changed(MimeTypesStruct const &mime_types, bool is_owner);
  void handle_transfer(QString const &mime_type, unsigned serial);

  std::shared_ptr<Handle> handle;
};
} // namespace deskflow
