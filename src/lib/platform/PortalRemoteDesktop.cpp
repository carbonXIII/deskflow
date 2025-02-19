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

#include "platform/PortalRemoteDesktop.h"
#include "base/Log.h"

#include "platform/TimerWrapper.h"

namespace deskflow {

PortalRemoteDesktop::PortalRemoteDesktop(EiScreen *screen, IEventQueue *events)
    : XdpRemoteDesktop(),
      screen(screen),
      events(events)
{}

void PortalRemoteDesktop::reconnect_with_timeout(std::chrono::milliseconds const &timeout)
{
  if (timer) {
    timer.reset();
  } else {
    timer = make_timer_wrapper(events, timeout, [this](const Event &, void *) mutable { reconnect(false); });
  }
}

void PortalRemoteDesktop::on_closed()
{
  LOG_ERR("portal remote desktop session was closed, reconnecting");
  events->addEvent(Event(events->forEi().sessionClosed(), screen->getEventTarget()));
  reconnect_with_timeout(std::chrono::milliseconds{1000});
}

void PortalRemoteDesktop::on_started(Devices, bool clipboard_enabled, std::string restore_token)
{
  // ConnectToEIS requires version 2 of the xdg-desktop-portal (and the same
  // version in the impl.portal), i.e. you'll need an updated compositor on
  // top of everything...
  auto fd = connect_to_eis();
  if (fd < 0) {
    on_error("Failed to connect to EIS", true);
    return;
  }

  // Socket ownership is transferred to the EiScreen
  events->addEvent(Event(events->forEi().connected(), screen->getEventTarget(), EiScreen::EiConnectInfo::alloc(fd)));
}

void PortalRemoteDesktop::on_error(std::string const &msg, bool fatal)
{
  LOG_ERR("%s", msg.c_str());
  if (fatal) {
    events->addEvent(Event::kQuit);
  } else if (!valid()) {
    reconnect_with_timeout(std::chrono::milliseconds{1000});
  }
}

} // namespace deskflow
