/*
 * Deskflow -- mouse and keyboard sharing utility
 * Copyright (C) 2022 Red Hat, Inc.
 * Copyright (C) 2024 Symless Ltd.
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

#include "platform/PortalInputCapture.h"
#include "base/Event.h"
#include "base/Log.h"

#include <sys/socket.h> // for EIS fd hack, remove
#include <sys/un.h>     // for EIS fd hack, remove

#include <optional>

#include "platform/TimerWrapper.h"

namespace deskflow {

static constexpr auto DESIRED_CAPS =
    Capabilities::KEYBOARD | Capabilities::POINTER | Capabilities::TOUCHSCREEN | Capabilities::CLIPBOARD;

PortalInputCapture::PortalInputCapture(EiScreen *screen, IEventQueue *events)
    : XdpInputCapture(DESIRED_CAPS),
      screen(screen),
      events(events)
{}

void PortalInputCapture::on_created()
{
  int fd = connect_to_eis();

  if (fd < 0) {
    fake_eis_fd();
  }

  if (fd < 0) {
    on_error("Failed to connect to EIS", true);
    return;
  }

  // Socket ownership is transferred to the EiScreen
  events->addEvent(Event(events->forEi().connected(), screen->getEventTarget(), EiScreen::EiConnectInfo::alloc(fd)));
}

int PortalInputCapture::fake_eis_fd()
{
  auto path = std::getenv("LIBEI_SOCKET");

  if (!path) {
    LOG_DEBUG("cannot fake eis socket, env var not set: LIBEI_SOCKET");
    return -1;
  }

  auto sock = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0);

  // Dealing with the socket directly because nothing in lib/... supports
  // AF_UNIX and I'm too lazy to fix all this for a temporary hack
  int fd = sock;
  struct sockaddr_un addr = {
      .sun_family = AF_UNIX,
      .sun_path = {0},
  };
  std::snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path);

  auto result = connect(fd, (struct sockaddr *)&addr, sizeof(addr));
  if (result != 0) {
    LOG_DEBUG("faked eis fd failed: %s", strerror(errno));
  }

  return sock;
}

void PortalInputCapture::on_closed()
{
  on_error("portal input capture session was closed, exiting", true);
}

void PortalInputCapture::reenable_with_timer(std::chrono::milliseconds const &timeout)
{
  if (timer) {
    timer.reset();
  } else {
    timer = make_timer_wrapper(events, timeout, [this](const Event &, void *) mutable { enable(); });
  }
}

void PortalInputCapture::on_disabled()
{
  LOG_DEBUG("portal cb disabled");

  if (!want_enable)
    return; // Nothing to do

  want_enable = false;

  // FIXME: need some better heuristics here of when we want to enable again
  // But we don't know *why* we got disabled (and it's doubtfull we ever
  // will), so we just assume that the zones will change or something and we
  // can re-enable again
  // ... very soon
  reenable_with_timer(std::chrono::milliseconds{1000});
}

void PortalInputCapture::on_activated(unsigned barrier_id, std::optional<Point<double>> cursor)
{
  LOG_DEBUG("portal cb activated");

  if (cursor) {
    auto [x, y] = *cursor;
    LOG_DEBUG("warping cursor: %d, %d", x, y);
    screen->warpCursor((int)x, (int)y);
  } else {
    LOG_WARN("failed to get cursor position");
  }
}

void PortalInputCapture::on_deactivated()
{
  LOG_DEBUG("cb deactivated");
}

std::vector<Barrier> PortalInputCapture::on_zones_changed()
{
  auto const &zones = get_zones();
  LOG_DEBUG("zones changed: count=%d", (int)zones.size());

  std::vector<Barrier> barriers;

  auto add_barrier = [&barriers](const char *name, int x1, int y1, int x2, int y2) {
    unsigned id = barriers.size() + 1;
    LOG_DEBUG("barrier (%s) %zd at %d,%d-%d,%d", name, id, x1, y1, x2, y2);
    barriers.push_back({id, std::array{x1, y1, x2, y2}});
  };

  for (auto const &[w, h, x, y] : zones) {
    LOG_DEBUG("input capture zone, %dx%d@%d,%d", w, h, x, y);

    // Hardcoded behaviour: our pointer barriers are always at the edges of
    // all zones. Since the implementation is supposed to reject the ones in
    // the wrong place, we can just install barriers everywhere and let EIS
    // figure it out. Also a lot easier to implement for now though it doesn't
    // cover differently-sized screens...
    add_barrier("top", x, y, x + w - 1, y);
    add_barrier("right", x + w, y, x + w, y + h - 1);
    add_barrier("left", x, y, x, y + h - 1);
    add_barrier("bottom", x, y + h, x + w - 1, y + h);
  }

  return barriers;
}

void PortalInputCapture::on_barriers_changed()
{
  want_enable = true;
  enable();
}

void PortalInputCapture::on_error(std::string const &msg, bool fatal)
{
  LOG_ERR("%s", msg.c_str());
  if (fatal) {
    events->addEvent(Event::kQuit);
  }
}

} // namespace deskflow
