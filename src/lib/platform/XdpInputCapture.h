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

#include <array>
#include <memory>
#include <optional>
#include <tuple>
#include <vector>

namespace deskflow {
enum Capabilities : unsigned
{
  KEYBOARD = 1,
  POINTER = 2,
  TOUCHSCREEN = 4,
  CLIPBOARD = 8,
};

template <typename T> using Point = std::array<T, 2>;

struct Zone
{
  using Like = std::tuple<unsigned, unsigned, int, int>;
  unsigned w, h;
  int x, y;
};

struct Barrier
{
  using Like = std::tuple<unsigned, std::array<int, 4>>;
  unsigned id;
  std::array<int, 4> pos;
  constexpr auto operator<=>(Barrier const &) const = default;
  operator Like() const
  {
    return Like{id, pos};
  }
};

struct HandleBase;
template <typename Session> struct Handle;
template <typename T> struct SessionContract;

struct XdpClipboard;

struct XdpInputCapture
{
  using Handle = deskflow::Handle<XdpInputCapture>;

  XdpInputCapture(const XdpInputCapture &) = delete;
  XdpInputCapture(XdpInputCapture &&) = delete;
  XdpInputCapture &operator=(const XdpInputCapture &) = delete;
  XdpInputCapture &operator=(XdpInputCapture &&) = delete;

  XdpInputCapture(unsigned caps);
  virtual ~XdpInputCapture();
  bool valid();

  virtual void on_created()
  {}
  virtual void on_activated(unsigned barrier_id, std::optional<Point<double>> cursor)
  {}
  virtual void on_deactivated()
  {}
  virtual void on_disabled()
  {}
  virtual std::vector<Barrier> on_zones_changed()
  {
    return {};
  }
  virtual void on_barriers_changed()
  {
    enable();
  }
  virtual void on_closed()
  {}

  std::vector<Zone> const &get_zones()
  {
    return zones;
  }
  bool is_active()
  {
    return activation.has_value();
  }

  int connect_to_eis();
  void enable();
  void disable();
  void release(std::optional<Point<double>> cursor_pos = std::nullopt);
  void release(double cursor_x, double cursor_y)
  {
    release(Point{cursor_x, cursor_y});
  }

  // Session contract
  virtual void on_error(std::string const &message, bool fatal = false)
  {}

protected:
  friend SessionContract<XdpInputCapture>;
  friend Handle;
  bool do_create(const char *session, const char *request);
  void handle_created(unsigned capabilities);
  std::shared_ptr<HandleBase> get_handle();

  // InputCapture contract
  friend struct InputCaptures;
  void handle_activated(unsigned activation_id, unsigned barrier_id, std::optional<Point<double>> cursor);
  void handle_deactivated(unsigned activation_id);
  void handle_disabled();
  void handle_zones_changed(std::optional<unsigned> invalid_set = {});
  void handle_zones(unsigned new_set, std::vector<Zone> const &new_zones);
  void handle_barriers(std::vector<unsigned> failed);
  void handle_closed();

  // Clipboard contract
  friend XdpClipboard;
  bool has_clipboard()
  {
    return clipboard_enabled;
  }

private:
  unsigned capabilities;

  std::vector<Barrier> pending_barriers;
  std::vector<Barrier> barriers;
  std::vector<Zone> zones;
  int zone_set = -1;

  std::optional<unsigned> activation;
  std::shared_ptr<Handle> handle;
  bool ready = false;
  bool clipboard_enabled = false;
};
} // namespace deskflow
