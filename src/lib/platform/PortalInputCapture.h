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
#include "platform/XdpInputCapture.h"
#include <memory>

namespace deskflow {

class PortalInputCapture : public XdpInputCapture
{
public:
  PortalInputCapture(EiScreen *screen, IEventQueue *events);
  virtual ~PortalInputCapture() = default;

protected:
  virtual void on_closed() override;
  virtual void on_created() override;
  virtual void on_error(std::string const &msg, bool fatal) override;
  virtual std::vector<Barrier> on_zones_changed() override;
  virtual void on_activated(unsigned barrier_id, std::optional<Point<double>> cursor) override;
  virtual void on_deactivated() override;
  virtual void on_barriers_changed() override;
  virtual void on_disabled() override;

private:
  int fake_eis_fd();
  void reenable_with_timer(std::chrono::milliseconds const &timeout);

  EiScreen *screen = nullptr;
  IEventQueue *events = nullptr;
  std::shared_ptr<void> timer;
  bool want_enable = false;
};

} // namespace deskflow
