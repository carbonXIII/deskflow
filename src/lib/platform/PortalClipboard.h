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

#include "deskflow/IClipboard.h"
#include "platform/EiScreen.h"
#include "platform/XdpClipboard.h"

#include "base/IEventQueue.h"
#include "base/Log.h"

#include <array>
#include <mutex>
#include <optional>
#include <vector>

namespace deskflow {
struct PortalClipboardSelection
{
  using EFormat = IClipboard::EFormat;
  static constexpr unsigned N = EFormat::kNumFormats;

  enum class Owner : unsigned
  {
    LOCAL,
    EXTERNAL,
    NONE
  };

  struct Data
  {
    std::string val;
    Owner owner;
  };

  std::array<std::optional<std::string>, N> pending;
  mutable bool have_pending = false;
  mutable std::mutex mtx;
  mutable std::array<Data, N> data;

  /// Resets for a pending external write operation
  void reset();

  /// Pending external write for a given format
  void set(EFormat format, std::string const &value);

  /// Finish external clipboard write, updating data to reflect it.
  /// Returns the list of types we have pending data for.
  /// std::nullopt - no pending modifications
  /// empty set - advertise no data types
  std::optional<std::vector<EFormat>> pop() const;

  /// Returns data matching the given format and owner
  /// Where owner = false: The data was set by local event
  std::string get(EFormat format, Owner owner = Owner::LOCAL) const;

  // Finished reading data after SelectionOwnerChanged() event.
  // This races with pop(), so intertwined local (this event)
  // and external (self modification) write operations might have inconsistent results.
  // There are situations where a pending event might come in,
  // but we dont' finish reading until after an ongoing pop()...
  void on_local_set(EFormat format, std::string const &s);
};

class PortalClipboard : public IClipboard, public XdpClipboard
{
public:
  PortalClipboard(std::shared_ptr<HandleBase> handle, IEventQueue *events, EiScreen *screen);

  // XdpClipboard
  virtual void on_error(std::string const &message, bool fatal = false) override;
  virtual std::span<const std::string_view> supported_mime_types() override;

  virtual std::string on_transfer(std::string_view mime_type) override;
  virtual void on_owner_changed(std::string_view mime_type, std::string_view mime_data) override;

  // IClipboard:
  virtual bool open(Time time) const override;
  virtual void close() const override;
  virtual bool empty() override;
  virtual void add(EFormat format, std::string const &data) override;
  virtual bool has(EFormat format) const override;
  virtual std::string get(EFormat format) const override;
  virtual Time getTime() const override;

  PortalClipboardSelection selection;
  IEventQueue *events;
  EiScreen *screen;
};
} // namespace deskflow
