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

#include "platform/XdpClipboard.h"
#include "platform/XdpUtils.h"
#include "platform/clipboardinterface.h"

#include <stdio.h>

namespace deskflow {
using Base = org::freedesktop::portal::Clipboard;
static auto &iface();
struct XdpClipboards : SessionManager<XdpClipboard, Base>
{
  friend auto &iface()
  {
    return XdpEventManager::singleton<XdpClipboards>();
  }

  XdpClipboards()
  {
    SessionManager::connect<"mime_types", "session_is_owner">(
        &Base::SelectionOwnerChanged, &XdpClipboard::handle_owner_changed
    );
    SessionManager::connect_direct(&Base::SelectionTransfer, &XdpClipboard::handle_transfer);
  }
};

ErrorOr<std::monostate> XdpClipboard::try_request_clipboard(std::shared_ptr<HandleBase> const &handle)
{
  return iface().lock([&](XdpClipboards &iface) { return ErrorOr{iface.RequestClipboard(*handle, {})}; }
  ).value_or(Error{"lock failed"});
}

XdpClipboard::XdpClipboard(std::shared_ptr<HandleBase> const &session) : handle(Handle::from_existing(session, this))
{
  iface()->add_session(handle);
}

XdpClipboard::~XdpClipboard() = default;

bool XdpClipboard::valid()
{
  return (bool)handle;
}

void XdpClipboard::set_selection(std::vector<std::string> mime_types)
{
  iface().lock([&](XdpClipboards &iface) {
    if (ErrorOr res = iface.SetSelection(
            *handle,
            QVariantMap{
                std::pair{"mime_types", to_qdbus(mime_types)},
            }
        );
        !res) {
      on_error(res.error().format("Failed to set clipboard selection: %s"));
    }
  });
}

static bool good(FILE *f)
{
  if (std::ferror(f)) {
    if (errno == EAGAIN) {
      std::clearerr(f);
    } else {
      return false;
    }
  }

  return !std::feof(f);
}

static ErrorOr<std::string> read_all(int fd)
{
  FILE *f = fdopen(fd, "rb");
  if (!f)
    return Error{"Failed to open file descriptor"};

  std::string ret;
  while (good(f)) {
    std::array<char, 1024> buf;
    if (auto read = fread(buf.data(), sizeof(buf[0]), buf.size(), f); read > 0) {
      ret.insert(ret.end(), buf.begin(), buf.begin() + read);
    }
  }

  if (std::ferror(f))
    return Error{deskflow::string::sprintf("Error while reading file: %d", (int)errno)};

  return ret;
}

static ErrorOr<std::monostate> write_all(int fd, std::string const &s)
{
  FILE *f = fdopen(fd, "wb");
  if (!f)
    return Error{"Failed to open file descriptor"};

  if (!s.size())
    return std::monostate{};

  auto wrote = fwrite(s.data(), sizeof(s[0]), s.size(), f);
  if (wrote < s.size()) {
    auto rc = std::ferror(f);
    return Error{deskflow::string::sprintf("Error after writing %d bytes to file: %d", wrote, rc)};
  }

  return std::monostate{};
}

bool XdpClipboard::is_supported(std::string_view type)
{
  return std::ranges::count(supported_mime_types(), type) > 0;
}

void XdpClipboard::handle_owner_changed(MimeTypesStruct const &mime_types, bool is_owner)
{
  // TODO: filter out mime types we can't handle
  for (auto const &type : std::get<0>(mime_types)) {
    LOG_DEBUG("Owner changed: %s", type.c_str());
    if (!is_supported(type))
      continue;

    ErrorOr<std::monostate> res = std::monostate{};
    if (ErrorOr in_fd = iface()->SelectionRead(*handle, type.c_str())) {
      if (ErrorOr data = read_all(in_fd->fileDescriptor())) {
        LOG_DEBUG("Read data: %s", data->c_str());
        on_owner_changed(type, *data);
      } else {
        res = data.error();
      }
    } else {
      res = in_fd.error();
    }

    if (!res)
      on_error(res.error().format("Failed to read clipboard selection: %s"));
  }
}

void XdpClipboard::handle_transfer(QString const &_mime_type, unsigned serial)
{
  auto mime_type = _mime_type.toStdString();
  LOG_DEBUG("Handle transfer: %s, %d", mime_type.c_str(), serial);
  if (!is_supported(mime_type))
    return;

  auto data = on_transfer(mime_type);
  if (data.empty()) {
    LOG_DEBUG("No clipboard selection of type: %s", mime_type.c_str());
    (void)iface()->SelectionWriteDone(*handle, serial, false);
    return;
  }

  ErrorOr<std::monostate> res = std::monostate{};
  if (ErrorOr out_fd = iface()->SelectionWrite(*handle, serial)) {
    if (ErrorOr write_res = write_all(out_fd->fileDescriptor(), data); !res) {
      res = write_res.error();
    }
  } else {
    res = out_fd.error();
  }

  if (!res) {
    on_error(res.error().format("Failed to write clipboard selection: %s"));
    (void)iface()->SelectionWriteDone(*handle, serial, false);
  } else {
    iface()->SelectionWriteDone(*handle, serial, true);
  }
}
} // namespace deskflow
