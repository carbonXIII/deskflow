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

#include "base/IEventQueue.h"
#include "base/TMethodEventJob.h"
#include <chrono>

namespace deskflow {
#define FORWARD(x) std::forward<decltype(x)>(x)

template <typename Callback> struct TimerWrapper : Callback
{
  using Callback::operator();

  IEventQueue *events;
  EventQueueTimer *timer;

  TimerWrapper(IEventQueue *events, std::chrono::milliseconds const &timeout, Callback &&cb)
      : Callback(FORWARD(cb)),
        events(events),
        timer(events->newOneShotTimer(timeout.count() / 1000., nullptr))
  {
    events->adoptHandler(Event::kTimer, timer, new TMethodEventJob<Callback>(this, &Callback::operator()));
  }

  ~TimerWrapper()
  {
    events->deleteTimer(timer);
  }
};

template <typename Callback>
std::shared_ptr<void> make_timer_wrapper(IEventQueue *events, std::chrono::milliseconds const &timeout, Callback &&cb)
{
  return std::make_shared<TimerWrapper<std::decay_t<Callback>>>(events, timeout, FORWARD(cb));
}

#undef FORWARD
} // namespace deskflow
