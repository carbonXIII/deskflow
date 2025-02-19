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

#include "platform/PortalClipboard.h"

#include <algorithm>
#include <ranges>
#include <utility>

namespace deskflow {
using EFormat = IClipboard::EFormat;
using Time = IClipboard::Time;

namespace mime {
static constexpr std::array<std::string_view, 2> types = {
    "text/plain",
    "image/bmp",
};

static constexpr std::array<EFormat, 2> formats = {
    EFormat::kText,
    EFormat::kBitmap,
};

static_assert(types.size() == formats.size());

static std::string_view to(EFormat fmt)
{
  if (auto it = std::ranges::find(formats, fmt); it != formats.end()) {
    auto idx = it - formats.begin();
    return types[idx];
  }

  return "";
}

static std::string to(EFormat fmt, std::string_view data)
{
  if (auto it = std::ranges::find(formats, fmt); it != formats.end()) {
    // TODO: no conversions?
    return std::string(data);
  }

  return "";
}

static int from(std::string_view mime_type)
{
  if (auto it = std::ranges::find(types, mime_type); it != types.end()) {
    auto idx = it - types.begin();
    return formats[idx] < EFormat::kNumFormats ? formats[idx] : -1;
  }

  return -1;
}

static std::pair<int, std::string> from(std::string_view mime_type, std::string_view mime_data)
{
  auto fmt = from(mime_type);
  // TODO: no conversions?
  return {fmt, std::string(mime_data)};
}
} // namespace mime

PortalClipboard::PortalClipboard(std::shared_ptr<HandleBase> handle, IEventQueue *events, EiScreen *screen)
    : XdpClipboard(handle),
      events(events),
      screen(screen)
{}

void PortalClipboard::on_error(std::string const &message, bool fatal)
{
  LOG_ERR("%s", message.c_str());
  if (fatal) {
    // TODO
  }
}

std::span<const std::string_view> PortalClipboard::supported_mime_types()
{
  return mime::types;
}

std::string PortalClipboard::on_transfer(std::string_view mime_type)
{
  if (auto fmt = mime::from(mime_type); fmt >= 0) {
    auto data = selection.get(EFormat(fmt), PortalClipboardSelection::Owner::EXTERNAL);
    return mime::to(EFormat(fmt), data);
  }

  return "";
}

void PortalClipboard::on_owner_changed(std::string_view mime_type, std::string_view mime_data)
{
  auto [fmt, data] = mime::from(mime_type, mime_data);
  if (fmt >= 0)
    selection.on_local_set(EFormat(fmt), data);

  EiScreen::ClipboardInfo *info;
  {
    info = static_cast<EiScreen::ClipboardInfo *>(malloc(sizeof(*info)));
    info->m_id = 0;
    info->m_sequenceNumber = screen->get_sequence_num();
  }

  events->addEvent(Event(events->forClipboard().clipboardGrabbed(), screen->getEventTarget(), info));
}

bool PortalClipboard::open(Time time) const
{
  return true;
}

Time PortalClipboard::getTime() const
{
  return {};
}

void PortalClipboard::close() const
{
  if (auto types = selection.pop()) {
    std::vector<std::string> mime_types;
    std::ranges::copy(
        *types | std::views::transform([](auto type) { return std::string(mime::to(type)); }),
        std::back_inserter(mime_types)
    );

    // HACK: Weird const_cast, but we need to send the signal at some point
    // Why is this function const?
    const_cast<PortalClipboard *>(this)->set_selection(mime_types);
  }
}

bool PortalClipboard::empty()
{
  // clear data buffer (implied pending write)
  // queue setselection() to advertise we own the mime types
  selection.reset();
  return true;
}

void PortalClipboard::add(EFormat format, std::string const &data)
{
  selection.set(format, data);
}

bool PortalClipboard::has(EFormat format) const
{
  return selection.get(format).size() > 0;
}

std::string PortalClipboard::get(EFormat format) const
{
  return selection.get(format);
}

void PortalClipboardSelection::reset()
{
  have_pending = true;
  pending.fill(std::nullopt);
}

void PortalClipboardSelection::set(EFormat format, std::string const &value)
{
  if (value.empty())
    return;
  pending[format] = value;
}

std::optional<std::vector<EFormat>> PortalClipboardSelection::pop() const
{
  if (!std::exchange(have_pending, false))
    return std::nullopt;

  std::vector<EFormat> ret;

  std::lock_guard g{mtx};
  for (unsigned fmt = 0; fmt < N; ++fmt) {
    if (pending[fmt]) {
      data[fmt] = {*pending[fmt], Owner::EXTERNAL};
      ret.push_back(EFormat(fmt));
    } else if (data[fmt].owner == Owner::EXTERNAL) {
      data[fmt] = {"", Owner::NONE};
    }
  }

  return ret;
}

std::string PortalClipboardSelection::get(EFormat format, Owner owner) const
{
  std::lock_guard g{mtx};
  if (data[format].owner != owner)
    return "";
  return data[format].val;
}

void PortalClipboardSelection::on_local_set(EFormat format, std::string const &s)
{
  std::lock_guard g{mtx};
  data[format] = {s, Owner::LOCAL};
}
} // namespace deskflow
