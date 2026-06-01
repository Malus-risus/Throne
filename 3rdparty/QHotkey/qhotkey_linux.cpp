#include "qhotkey.h"
#include "qhotkey_p.h"

#include <QGuiApplication>
#include <memory>

// Runtime backend selection for Unix.
//
// Unlike Windows/macOS, a single Linux binary can run under X11, native Wayland
// or XWayland depending on the user's session. The legacy X11 grab model
// (XGrabKey on the root window + sniffing the raw key event stream) is
// impossible on Wayland by design, so we ship two backends and pick one here at
// runtime:
//   * X11  (qhotkey_x11.cpp)    - real X11 sessions.
//   * Portal (qhotkey_portal.cpp) - Wayland sessions, via the
//     org.freedesktop.portal.GlobalShortcuts D-Bus interface.

namespace {

bool isWaylandSession()
{
	// Native Wayland Qt plugin ("wayland", "wayland-egl", ...).
	if (QGuiApplication::platformName().startsWith(QLatin1String("wayland"), Qt::CaseInsensitive))
		return true;
	// Qt running on xcb but inside a Wayland session (XWayland): the X11 grab
	// cannot see globally-routed key events, so prefer the portal here too.
	if (!qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY"))
		return true;
	return qgetenv("XDG_SESSION_TYPE").toLower() == "wayland";
}

QHotkeyPrivate *createBackend()
{
	if (isWaylandSession())
		return createPortalHotkeyPrivate();
	return createX11HotkeyPrivate();
}

} // namespace

QHotkeyPrivate *QHotkeyPrivate::instance()
{
	// Thread-safe lazy initialisation (C++11 "magic statics"). The singleton
	// lives for the rest of the program; its destructor (run at exit, after
	// qApp is gone) already guards against a null qApp.
	static std::unique_ptr<QHotkeyPrivate> hotkeyPrivate{createBackend()};
	return hotkeyPrivate.get();
}

bool QHotkeyPrivate::isPlatformSupported()
{
	// Cached: the session type and portal availability are stable for the life
	// of the process, and the Wayland probe is a (one-off) blocking D-Bus
	// round-trip we don't want to repeat on every query.
	static const bool supported = isWaylandSession() ? portalHotkeyPlatformSupported()
													 : x11HotkeyPlatformSupported();
	return supported;
}
