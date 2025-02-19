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

#include "platform/XdpInputCapture.h"

#include "platform/XdpClipboard.h"
#include "platform/XdpConversions.h"
#include "platform/XdpRequest.h"
#include "platform/XdpUtils.h"
#include "platform/inputcaptureinterface.h"
#include "platform/sessioninterface.h"

#include "base/Log.h"

#include <QDBusPendingReply>
#include <QVariant>

namespace deskflow {
template <> constexpr auto filter_for_v<Barrier> = option_filter<"barrier_id", "position">;

static auto &iface();

using Base = org::freedesktop::portal::InputCapture;
struct InputCaptures : SessionManager<XdpInputCapture, Base>
{
  using Sessions = org::freedesktop::portal::Session;

  friend auto &iface()
  {
    return XdpEventManager::singleton<InputCaptures>();
  }

  InputCaptures()
  {
    // These types are used in QDBus generated code,
    // but we still need to register them manually.
    qDBusRegisterMetaType<QList<QVariantMap>>();
    qDBusRegisterMetaType<QVariantMap>();
    qDBusRegisterMetaType<QVariantList>();

    SessionManager::connect<"activation_id", "barrier_id", "cursor_position">(
        &Base::Activated, &XdpInputCapture::handle_activated
    );
    SessionManager::connect<"activation_id">(&Base::Deactivated, &XdpInputCapture::handle_deactivated);
    SessionManager::connect<>(&Base::Disabled, &XdpInputCapture::handle_disabled);
    SessionManager::connect<"zone_set">(&Base::ZonesChanged, &XdpInputCapture::handle_zones_changed);
  }
};

XdpInputCapture::XdpInputCapture(unsigned caps) : capabilities(caps)
{
  handle = Handle::create_pending<"capabilities">(this);
}

XdpInputCapture::~XdpInputCapture() = default;

bool XdpInputCapture::valid()
{
  return (bool)handle && (bool)*handle;
}

int XdpInputCapture::connect_to_eis()
{
  return iface()
      .lock([this](auto &iface) {
        if (ErrorOr fd = iface.ConnectToEIS(*handle, {})) {
          return fd->takeFileDescriptor();
        } else {
          on_error(fd.error().format("Failed to connect to EIS server: %s"));
          return -1;
        }
      })
      .value_or(-1);
}

void XdpInputCapture::enable()
{
  if (ErrorOr res = XdpClipboard::try_request_clipboard(handle); !res) {
    on_error(res.error().format("Failed to request clipboard: %s"));
  } else {
    clipboard_enabled = true;
  }

  iface().lock([this](auto &iface) { iface.Enable(*handle, {}); });
}

void XdpInputCapture::disable()
{
  iface().lock([this](auto &iface) { iface.Disable(*handle, {}); });
}

void XdpInputCapture::release(std::optional<Point<double>> cursor_pos)
{
  iface().lock([&](auto &iface) {
    QVariantMap options;
    options["activation_id"] = activation.value_or(-1);
    if (cursor_pos) {
      options["cursor_position"] = to_qdbus(*cursor_pos);
    }

    iface.Release(*handle, options);
  });

  activation.reset();
}

bool XdpInputCapture::do_create(const char *session, const char *request)
{
  auto args = QVariantMap{
      std::pair{"capabilities", capabilities},
      std::pair{"session_handle_token", session},
      std::pair{"handle_token", request},
  };

  (void)iface()->CreateSession("", args);

  return true;
}

void XdpInputCapture::handle_created(unsigned capabilities)
{
  std::string path = *handle;
  LOG_DEBUG("Input capture session object path: %s", path.c_str());

  this->capabilities = capabilities;
  iface()->add_session(this->handle);
  handle_zones_changed();
}

std::shared_ptr<HandleBase> XdpInputCapture::get_handle()
{
  return std::static_pointer_cast<HandleBase>(handle);
}

void XdpInputCapture::handle_activated(unsigned activation_id, unsigned barrier_id, std::optional<Point<double>> cursor)
{
  activation = activation_id;
  on_activated(barrier_id, cursor);
}

void XdpInputCapture::handle_deactivated(unsigned activation_id)
{
  activation.reset();
  on_deactivated();
}

void XdpInputCapture::handle_disabled()
{
  activation.reset();
  on_disabled();
}

void XdpInputCapture::handle_zones_changed(std::optional<unsigned> invalidated_set)
{
  if (invalidated_set && *invalidated_set < zone_set)
    return;

  auto request = handle_request<"zone_set", "zones">(handle, &XdpInputCapture::handle_zones);
  if (ErrorOr pending = iface()->GetZones(*handle, request->args()); !pending) {
    on_error(pending.error().format("Failed to request updated zones: %s"));
  }
}

void XdpInputCapture::handle_zones(unsigned new_set, std::vector<Zone> const &new_zones)
{
  zone_set = new_set;
  zones = new_zones;

  barriers.clear();
  pending_barriers.clear();

  // Defer on_created() until after GetZones() response to match libportal
  if (!ready) {
    ready = true;
    on_created();
  }

  pending_barriers = on_zones_changed();
  std::sort(pending_barriers.begin(), pending_barriers.end());

  auto request = handle_request<"failed_barriers">(handle, &XdpInputCapture::handle_barriers);

  QList<QVariantMap> qbarriers;
  for (auto &barrier : pending_barriers) {
    qbarriers.push_back(QVariantMap{std::pair{"barrier_id", barrier.id}, std::pair{"position", to_qdbus(barrier.pos)}});
  }

  if (ErrorOr pending = iface()->SetPointerBarriers(*handle, request->args(), qbarriers, zone_set); !pending) {
    on_error(pending.error().format("Failed to set updated pointer barriers: %s"));
  }
}

void XdpInputCapture::handle_barriers(std::vector<unsigned> failed)
{
  if (pending_barriers.empty())
    return;

  barriers.clear();
  barriers.reserve(pending_barriers.size() - failed.size());

  std::sort(failed.begin(), failed.end());
  for (int j = 0; auto &b : pending_barriers) {
    while (j < failed.size() && b.id > failed[j])
      ++j;
    if (j < failed.size() && b.id == failed[j]) {
      LOG_WARN("Failed barrier: %d: {%d, %d, %d, %d}", b.id, b.pos[0], b.pos[1], b.pos[2], b.pos[3]);
    } else {
      barriers.push_back(b);
    }
  }

  on_barriers_changed();
}

void XdpInputCapture::handle_closed()
{
  on_closed();
}
} // namespace deskflow
