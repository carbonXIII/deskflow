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

#include "platform/XdpUtils.h"

#include <platform/requestinterface.h>

namespace deskflow {
#define FORWARD(x) std::forward<decltype(x)>(x)

namespace detail {
/// Helper object for receiving Request::Response signals
template <typename Session>
struct Request : org::freedesktop::portal::Request, std::enable_shared_from_this<Request<Session>>
{
  using Base = org::freedesktop::portal::Request;

  Request(std::weak_ptr<Handle<Session>> weak)
      : Base(PORTAL_SERVICE, random_request_path().c_str(), QDBusConnection::sessionBus()),
        weak(weak)
  {}

  QString handle() const
  {
    return path().split('/').back();
  }
  QVariantMap args() const
  {
    return QVariantMap{std::pair{"handle_token", handle()}};
  }
  void cancel()
  {
    QObject::disconnect(connection);
  }

  template <OptionFilter filter, typename... Args> void connect(auto &&callback)
  {
    QObject::disconnect(connection);
    connection = QObject::connect(this, &Base::Response, create_callback<filter, Args...>(FORWARD(callback)));
    if (!connection) {
      LOG_WARN("Connection to request::response signal failed");
    }
  }

private:
  static QString unique_service_name()
  {
    return QDBusConnection::sessionBus().baseService().replace('.', '_').replace(':', "");
  }

  static std::string random_request_path()
  {
    return deskflow::string::sprintf(
        "%s/request/%s/%s", PORTAL_OBJECT_PATH, unique_service_name().toStdString().c_str(),
        Handle<Session>::random_session_token().c_str()
    );
  }

  template <typename... Args>
  static constexpr auto wrap_callback(std::shared_ptr<Handle<Session>> &handle, auto &&callback)
  {
    return [&handle, callback = FORWARD(callback)](Args... args) { callback(handle, FORWARD(args)...); };
  }

  template <OptionFilter filter, typename... Args> auto create_callback(auto &&callback)
  {
    return [self = this->shared_from_this(),
            callback = FORWARD(callback)](uint response, QVariantMap const &results) mutable {
      if (auto handle = self->weak.lock()) {
        auto filtered = filter(results);
        if (!qvariant_apply_ex<Args...>(wrap_callback<Args...>(handle, callback), filtered)) {
          handle->get()->on_error("Failed to parse portal async response");
        }
      } else {
        LOG_DEBUG("Ignoring orphaned response");
      }

      self->cancel();
      self.reset();
    };
  }

  std::weak_ptr<Handle<Session>> weak;
  QMetaObject::Connection connection;
};
} // namespace detail

template <OptionFilter filter, typename... Args, typename Session>
static auto handle_request_ex(std::weak_ptr<Handle<Session>> weak, auto &&callback)
{
  auto request = XdpEventManager::make_shared<detail::Request<Session>>(weak);
  request->template connect<filter, Args...>(callback);
  return request;
}

template <in_place_str... keys, typename Session, typename... Args>
static auto handle_request(std::weak_ptr<Handle<Session>> weak, void (Session::*callback)(Args...))
{
  return handle_request_ex<option_filter<keys...>, Args...>(
      weak,
      [callback](std::shared_ptr<Handle<Session>> &handle, Args... args) {
        std::invoke(callback, handle->get(), FORWARD(args)...);
      }
  );
}

template <in_place_str... keys, typename Session, typename... Args>
static auto handle_request(std::shared_ptr<Handle<Session>> &handle, void (Session::*callback)(Args...))
{
  return handle_request<keys...>(std::weak_ptr{handle}, callback);
}

#undef FORWARD
} // namespace deskflow
