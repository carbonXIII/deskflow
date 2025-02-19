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

#include <QCoreApplication>
#include <QDBusObjectPath>
#include <QDBusPendingReply>
#include <QList>
#include <QVariant>
#include <base/String.h>
#include <qcontainerfwd.h>

#include "platform/XdpConversions.h"
#include "platform/sessioninterface.h"
#include <base/Log.h>
#include <base/String.h>

// HACK: Qt doesn't provide a convenient way to create a thread to manage
// all of its signals, at least not without racing other potential threads
// to create a QCoreApplication if one doesn't already exist.
// QDBus (and other Qt "library" code) uses this private interface,
// which marks the current QThread as not needing a QCoreApplication,
// so stealing this seems reasonable, though the interface might be
// unstable.
class QDaemonThread : public QThread
{
public:
  QDaemonThread(QObject *parent = nullptr);
  ~QDaemonThread();
};

namespace deskflow {
#define FORWARD(x) std::forward<decltype(x)>(x)

static constexpr char const *PORTAL_SERVICE = "org.freedesktop.portal.Desktop";
static constexpr char const *PORTAL_OBJECT_PATH = "/org/freedesktop/portal/desktop";

static struct XdpEventManager *_event_thread_instance();

static void deferred_deleter(QObject *object)
{
  object->deleteLater();
}

using DeferredDeleter = decltype(&deferred_deleter);

/// Thread where all of our Xdp-related QObjects live
/// Without this we can't receive signals from QDBus
struct XdpEventManager : QDaemonThread
{
  std::vector<std::shared_ptr<QObject>> singletons;

  XdpEventManager()
  {
    moveToThread(this);
    start();
    QObject::connect(this, &QThread::finished, this, [this]() { singletons.clear(); }, Qt::DirectConnection);
  }

  virtual void run() override
  {
    exec();
    moveToThread(nullptr);
  }

  ~XdpEventManager()
  {
    quit();
    wait();
  }

  /// Call func(args...) within the event loop thread
  template <typename... Args> auto lock(auto &&func, Args &&...args)
  {
    if (QThread::currentThread() == this) {
      return std::invoke(func, FORWARD(args)...);
    }

    using Res = std::invoke_result_t<decltype(func), Args...>;
    if constexpr (std::same_as<Res, void>) {
      QMetaObject::invokeMethod(instance(), FORWARD(func), Qt::BlockingQueuedConnection, FORWARD(args)...);
    } else {
      Res res;
      QMetaObject::invokeMethod(
          instance(), FORWARD(func), Qt::BlockingQueuedConnection, qReturnArg(res), FORWARD(args)...
      );
      return res;
    }
  }

  /// Call make_shared(...) within the event loop thread
  template <typename T, typename... Args> static std::shared_ptr<T> make_shared(Args &&...args)
  {
    T *ret = instance()->lock([&args...]() { return new T(FORWARD(args)...); });
    return std::shared_ptr<T>(ret, &deferred_deleter);
  }

  template <typename T> using unique_ptr = std::unique_ptr<T, DeferredDeleter>;

  /// Call make_unique(...) within the event loop thread
  template <typename T, typename... Args> static std::unique_ptr<T, DeferredDeleter> make_unique(Args &&...args)
  {
    T *ret = instance()->lock([&args...]() { return new T(FORWARD(args)...); });
    return {ret, &deferred_deleter};
  }

  template <typename T> struct Singleton
  {
    std::weak_ptr<T> self;

    Singleton()
    {
      auto ptr = instance()->make_shared<T>();
      instance()->singletons.push_back(ptr);
      self = std::weak_ptr{ptr};
    }

    T *operator->()
    {
      return self.lock().get();
    }
    T &operator*()
    {
      return *self.lock().get();
    }

    template <typename Func>
    using Optional = std::optional<std::conditional_t<
        std::same_as<std::invoke_result_t<Func, T &>, void>, std::monostate, std::invoke_result_t<Func, T &>>>;
    auto lock(auto &&func) -> Optional<decltype(func)>
    {
      if (auto ptr = self.lock()) {
        if constexpr (std::same_as<Optional<decltype(func)>, std::optional<std::monostate>>) {
          instance()->lock(FORWARD(func), *ptr);
          return std::monostate{};
        } else {
          return instance()->lock(FORWARD(func), *ptr);
        }
      }

      return std::nullopt;
    }
  };

  /// Creates a new "static" T that will be destroyed when the XdpEventManager passes out of scope
  template <typename T> static auto &singleton()
  {
    static Singleton<T> ret;
    return ret;
  }

  static XdpEventManager *instance()
  {
    return _event_thread_instance();
  }
};

Q_GLOBAL_STATIC(XdpEventManager, event_thread);
static XdpEventManager *_event_thread_instance()
{
  return event_thread();
}

/// Requirements for a class to be considered a valid Session
template <typename T> struct SessionContract
{
  static constexpr bool value = requires(T &t) {
    { t.do_create("", "") } -> std::convertible_to<bool>;
    { t.on_error(std::string{}, bool{}) };
    { &T::handle_created };
  } && std::is_member_function_pointer_v<decltype(&T::handle_created)>;
};
template <typename T>
concept IsSession = SessionContract<T>::value;

/// Convenience error message wrapper
struct Error
{
  std::string msg;

  Error() = default;
  Error(std::string const &msg) : msg(msg)
  {}
  Error(QDBusError const &e) : msg(e.message().toStdString())
  {}

  operator std::string() const
  {
    return msg;
  }

  std::string format(const char *fmt) const
  {
    return deskflow::string::sprintf(fmt, msg.c_str());
  }
};

/// Convenience Error-or-value wrapper
template <typename T> struct ErrorOr
{
  std::optional<T> v;
  Error e;

  ErrorOr()
  {}

  ErrorOr(T const &t) : v(t)
  {}

  ErrorOr(QDBusPendingReply<T> &&reply)
  {
    reply.waitForFinished();
    if (reply.isError()) {
      e = reply.error();
    } else {
      v = reply.value();
    }
  }

  ErrorOr(QDBusPendingReply<> &&reply)
  {
    reply.waitForFinished();
    if (reply.isError()) {
      e = reply.error();
    } else {
      v = std::monostate{};
    }
  }

  ErrorOr(std::optional<T> const &opt) : v(opt)
  {}
  ErrorOr(Error const &e) : e(e)
  {}

  explicit operator bool() const
  {
    return v.has_value();
  }

  T &operator*()
  {
    return v.value();
  }
  T *operator->()
  {
    return &v.value();
  }

  T value_or(T const &o) const
  {
    return v.value_or(o);
  }

  T or_else(auto &&f) const
  {
    if (!v.value())
      return std::invoke(FORWARD(f));
    return v.value();
  }

  Error const &error()
  {
    return e;
  }
};

template <typename T> ErrorOr(QDBusPendingReply<T> const &) -> ErrorOr<T>;
ErrorOr(QDBusPendingReply<> const &) -> ErrorOr<std::monostate>;
template <typename T> ErrorOr(std::optional<T> const &) -> ErrorOr<T>;

template <typename Session> struct Handle;

template <OptionFilter filter, typename... Args, typename Session>
static auto handle_request_ex(std::weak_ptr<Handle<Session>> weak, auto &&callback);

/// Weak reference to a Session, with a bound object path
struct HandleBase
{
public:
  using SessionIface = org::freedesktop::portal::Session;

  static std::string random_session_token()
  {
    static std::mt19937 gen;
    static std::uniform_int_distribution<int> distrib{0, 1000};
    return deskflow::string::sprintf("deskflow%d", distrib(gen));
  }

protected:
  /// You shouldn't construct your own Handle instance
  struct private_interface
  {};

public:
  HandleBase(private_interface)
  {}

  operator bool() const
  {
    return (bool)iface;
  }
  operator std::string() const
  {
    return path.path().toStdString();
  }
  operator QDBusObjectPath() const
  {
    return path;
  }

protected:
  QDBusObjectPath path;
  XdpEventManager::unique_ptr<SessionIface> iface{nullptr, &deferred_deleter};
};

template <typename Session> struct Handle : HandleBase, std::enable_shared_from_this<Handle<Session>>
{
  Handle(private_interface p, Session *session) : HandleBase(p), session(session)
  {}

  /// Creates a handle to a session that has not been completely created
  template <in_place_str... keys>
    requires IsSession<Session>
  static std::shared_ptr<Handle> create_pending(Session *session)
  {
    auto handle = std::make_shared<Handle>(private_interface{}, session);

    auto request = std::invoke(
        [weak = std::weak_ptr{handle}]<typename... Args>(void (Session::*callback)(Args...)) {
          return handle_request_ex<option_filter<"session_handle", keys...>, std::string const &, Args...>(
              weak,
              [](std::shared_ptr<Handle> &handle, std::string const &path, Args... args) {
                handle->handle_created(path);
                handle->get()->handle_created(FORWARD(args)...);
              }
          );
        },
        &Session::handle_created
    );

    auto req = request->handle().toStdString();
    if (!session->do_create(random_session_token().c_str(), req.c_str())) {
      request->cancel();
    }

    return handle;
  }

  static std::shared_ptr<Handle> from_existing(std::shared_ptr<HandleBase> const &other, Session *session)
  {
    auto handle = std::make_shared<Handle>(private_interface{}, session);
    handle->path = (QDBusObjectPath)*other;
    return handle;
  }

  Session *operator->()
  {
    return session;
  }
  Session &operator*()
  {
    return *session;
  }
  Session *get()
  {
    return session;
  }

protected:
  void handle_created(std::string const &path)
  {
    this->path = QDBusObjectPath(path.c_str());

    this->iface = XdpEventManager::make_unique<SessionIface>(
        QString(PORTAL_SERVICE), QString(path.c_str()), QDBusConnection::sessionBus()
    );

    QObject::connect(this->iface.get(), &SessionIface::Closed, [weak = std::weak_ptr{this->shared_from_this()}]() {
      if (auto handle = weak.lock()) {
        handle->path = {};
        handle->iface.reset();
        static_cast<Session *>(handle->session)->on_closed();
      }
    });
  }

  Session *session;
};

/// Provides basic utilities for connecting to session signals
/// These interfaces don't use the Session's object path,
/// instead they use a global PORTAL_OBJECT_PATH,
/// and the Session object path is passed as the first argument
template <typename Session, typename Base> struct SessionManager : Base
{
public:
  SessionManager() : Base(PORTAL_SERVICE, PORTAL_OBJECT_PATH, QDBusConnection::sessionBus())
  {}

  void add_session(std::shared_ptr<Handle<Session>> &session)
  {
    if (!session) {
      return;
    }
    sessions[*session] = session;
  }

  std::shared_ptr<Handle<Session>> get_session(std::string const &handle)
  {
    if (auto it = sessions.find(handle); it != sessions.end()) {
      if (auto session = it->second.lock()) {
        return session;
      } else {
        sessions.erase(it);
      }
    }
    return nullptr;
  }

  auto get_session(QDBusObjectPath const &handle)
  {
    return get_session(handle.path().toStdString());
  }

  template <typename... Args> void connect_direct(auto signal, auto (Session::*session_callback)(Args...))
  {
    auto connection =
        QObject::connect(this, signal, this, [this, session_callback](QDBusObjectPath const &handle, Args... args) {
          if (auto session = get_session(handle)) {
            std::invoke(session_callback, session->get(), FORWARD(args)...);
          } else {
            LOG_DEBUG("Ignoring orphaned portal signal");
          }
        });

    if (!connection) {
      LOG_WARN("Failed to connect to portal signal");
    }
  }

  template <in_place_str... args> void connect(auto signal, auto session_callback)
  {
    auto connection = QObject::connect(
        this, signal, this,
        [this, session_callback](QDBusObjectPath const &handle, QVariantMap const &options) {
          if (auto session = get_session(handle)) {
            auto filtered = option_filter<args...>(options);
            if (!qvariant_apply(session->get(), session_callback, filtered)) {
              session->get()->on_error("Failed to parse portal async response");
            }
          } else {
            LOG_DEBUG("Ignoring orphaned portal signal");
          }
        }
    );

    if (!connection) {
      LOG_WARN("Failed to connect to portal signal");
    }
  }

private:
  std::map<std::string, std::weak_ptr<Handle<Session>>> sessions;
};

#undef FORWARD
} // namespace deskflow
