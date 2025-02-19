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

#pragma once

#include "platform/EiScreen.h"
#include "platform/XdpRemoteDesktop.h"

#include <memory>

namespace deskflow {

class PortalRemoteDesktop : public XdpRemoteDesktop
{
public:
  PortalRemoteDesktop(EiScreen *screen, IEventQueue *events);
  virtual ~PortalRemoteDesktop() = default;

protected:
  virtual void on_closed() override;
  virtual void on_started(Devices, bool clipboard_enabled, std::string restore_token) override;

  virtual void on_error(std::string const &msg, bool fatal) override;

  void reconnect_with_timeout(std::chrono::milliseconds const &timeout);

private:
  EiScreen *screen;
  IEventQueue *events;
  std::shared_ptr<void> timer;
};

} // namespace deskflow
