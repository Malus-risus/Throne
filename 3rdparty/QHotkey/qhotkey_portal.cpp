#include "qhotkey.h"
#include "qhotkey_p.h"

#include <QTimer>
#include <QRandomGenerator>
#include <QStringList>
#include <QKeySequence>

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusError>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>

#include <functional>

// Wayland backend, implemented on top of the freedesktop XDG desktop portal:
//   org.freedesktop.portal.GlobalShortcuts
//
// Wayland deliberately forbids the X11 approach (no global key grab, no global
// key event stream), so global hotkeys must go through the compositor via this
// D-Bus portal. The flow is:
//   CreateSession  -> obtain a session handle
//   BindShortcuts  -> hand the compositor a *set* of shortcuts; the user/
//                     compositor decides (and may override) the actual trigger
//   Activated/Deactivated signals -> delivered when a bound shortcut fires
//
// Differences from the X11 backend that shape this code:
//   * Everything is asynchronous and user-mediated; registerShortcut() can only
//     report success optimistically and reconcile later.
//   * The portal has no per-shortcut "unbind"; to change the set we rebuild the
//     whole session. Register/unregister calls are therefore debounced and
//     applied as one session rebuild.
//   * Activation arrives by shortcut *id* (a string we choose), not a keycode,
//     so we map ids back to the QHotkey NativeShortcut they were derived from.

namespace {

const QString PORTAL_SERVICE = QStringLiteral("org.freedesktop.portal.Desktop");
const QString PORTAL_PATH    = QStringLiteral("/org/freedesktop/portal/desktop");
const QString GS_IFACE       = QStringLiteral("org.freedesktop.portal.GlobalShortcuts");
const QString REQUEST_IFACE  = QStringLiteral("org.freedesktop.portal.Request");
const QString SESSION_IFACE  = QStringLiteral("org.freedesktop.portal.Session");
const QString PROPS_IFACE    = QStringLiteral("org.freedesktop.DBus.Properties");

// A QHotkey NativeShortcut on this backend simply carries the Qt key/modifier
// values (see nativeKeycode/nativeModifiers), so a stable id can be derived
// from them and parsed/looked up again when an Activated signal arrives.
QString shortcutId(QHotkey::NativeShortcut s)
{
	return QStringLiteral("qhotkey_%1_%2").arg(s.key).arg(s.modifier);
}

QString shortcutDescription(QHotkey::NativeShortcut s)
{
	const QKeySequence seq(static_cast<int>(s.key) | static_cast<int>(s.modifier));
	const QString text = seq.toString(QKeySequence::NativeText);
	return text.isEmpty() ? QStringLiteral("Global hotkey")
						  : QStringLiteral("Global hotkey: %1").arg(text);
}

// Best-effort conversion to the portal's "preferred_trigger" string. The
// compositor has final say over the binding, so an empty/ignored value just
// means the user assigns the shortcut manually in their compositor settings.
QString portalKeyToken(Qt::Key key)
{
	if (key >= Qt::Key_A && key <= Qt::Key_Z)
		return QString(QChar(static_cast<char16_t>(key))).toLower();
	if (key >= Qt::Key_0 && key <= Qt::Key_9)
		return QString(QChar(static_cast<char16_t>(key)));
	if (key >= Qt::Key_F1 && key <= Qt::Key_F35)
		return QStringLiteral("F%1").arg(key - Qt::Key_F1 + 1);

	switch (key) {
		case Qt::Key_Space:  return QStringLiteral("space");
		case Qt::Key_Return:
		case Qt::Key_Enter:  return QStringLiteral("Return");
		case Qt::Key_Tab:    return QStringLiteral("Tab");
		case Qt::Key_Escape: return QStringLiteral("Escape");
		case Qt::Key_Up:     return QStringLiteral("Up");
		case Qt::Key_Down:   return QStringLiteral("Down");
		case Qt::Key_Left:   return QStringLiteral("Left");
		case Qt::Key_Right:  return QStringLiteral("Right");
		default:             return QString();
	}
}

QString preferredTrigger(QHotkey::NativeShortcut s)
{
	const QString keyToken = portalKeyToken(static_cast<Qt::Key>(s.key));
	if (keyToken.isEmpty())
		return QString(); // let the compositor/user decide

	const auto mods = static_cast<Qt::KeyboardModifiers>(s.modifier);
	QStringList parts;
	if (mods & Qt::ControlModifier) parts << QStringLiteral("CTRL");
	if (mods & Qt::AltModifier)     parts << QStringLiteral("ALT");
	if (mods & Qt::ShiftModifier)   parts << QStringLiteral("SHIFT");
	if (mods & Qt::MetaModifier)    parts << QStringLiteral("LOGO");
	parts << keyToken;
	return parts.join(QLatin1Char('+'));
}

} // namespace

// org.freedesktop.portal: element of the a(sa{sv}) "shortcuts" argument.
struct PortalShortcut
{
	QString id;
	QVariantMap props;
};
Q_DECLARE_METATYPE(PortalShortcut)
Q_DECLARE_METATYPE(QList<PortalShortcut>)

QDBusArgument &operator<<(QDBusArgument &arg, const PortalShortcut &s)
{
	arg.beginStructure();
	arg << s.id << s.props;
	arg.endStructure();
	return arg;
}

const QDBusArgument &operator>>(const QDBusArgument &arg, PortalShortcut &s)
{
	arg.beginStructure();
	arg >> s.id >> s.props;
	arg.endStructure();
	return arg;
}

// Small self-deleting helper: subscribes to a single portal Request's Response.
class PortalRequest : public QObject
{
	Q_OBJECT
public:
	PortalRequest(std::function<void(uint, const QVariantMap &)> cb, QObject *parent)
		: QObject(parent), m_cb(std::move(cb)) {}

public slots:
	void responded(uint response, const QVariantMap &results)
	{
		if (m_cb)
			m_cb(response, results);
		deleteLater();
	}

private:
	std::function<void(uint, const QVariantMap &)> m_cb;
};

class QHotkeyPrivatePortal : public QHotkeyPrivate
{
	Q_OBJECT
public:
	QHotkeyPrivatePortal();
	~QHotkeyPrivatePortal() override;

	// QAbstractNativeEventFilter: unused on Wayland (events arrive via D-Bus).
	bool nativeEventFilter(const QByteArray &, void *, _NATIVE_EVENT_RESULT *) override { return false; }

protected:
	// QHotkeyPrivate interface
	quint32 nativeKeycode(Qt::Key keycode, bool &ok) override;
	quint32 nativeModifiers(Qt::KeyboardModifiers modifiers, bool &ok) override;
	bool registerShortcut(QHotkey::NativeShortcut shortcut) override;
	bool unregisterShortcut(QHotkey::NativeShortcut shortcut) override;

private slots:
	void onActivated(const QDBusObjectPath &session, const QString &id, qulonglong timestamp, const QVariantMap &options);
	void onDeactivated(const QDBusObjectPath &session, const QString &id, qulonglong timestamp, const QVariantMap &options);
	void syncShortcuts();

private:
	void scheduleSync();
	void createSession();
	void bindShortcuts();
	void closeSession();
	void finishSync(bool ok);
	void makeRequest(const QString &token, std::function<void(uint, const QVariantMap &)> cb);
	void asyncCallChecked(const QDBusMessage &msg);
	static QString newToken();

	QString m_sessionHandle;                          // current session path, empty if none
	QHash<QString, QHotkey::NativeShortcut> m_shortcuts; // desired set: id -> shortcut
	bool m_sessionInProgress = false;                 // a create/bind round-trip is underway
	bool m_syncScheduled = false;                     // a debounced sync is queued
	bool m_resyncNeeded = false;                      // the set changed; sync again when free
};

QHotkeyPrivatePortal::QHotkeyPrivatePortal()
{
	qDBusRegisterMetaType<PortalShortcut>();
	qDBusRegisterMetaType<QList<PortalShortcut>>();

	QDBusConnection bus = QDBusConnection::sessionBus();
	bus.connect(PORTAL_SERVICE, PORTAL_PATH, GS_IFACE, QStringLiteral("Activated"),
				this, SLOT(onActivated(QDBusObjectPath,QString,qulonglong,QVariantMap)));
	bus.connect(PORTAL_SERVICE, PORTAL_PATH, GS_IFACE, QStringLiteral("Deactivated"),
				this, SLOT(onDeactivated(QDBusObjectPath,QString,qulonglong,QVariantMap)));
}

QHotkeyPrivatePortal::~QHotkeyPrivatePortal()
{
	if (!m_sessionHandle.isEmpty())
		closeSession();
}

quint32 QHotkeyPrivatePortal::nativeKeycode(Qt::Key keycode, bool &ok)
{
	if (keycode == Qt::Key_unknown) {
		ok = false;
		return 0;
	}
	ok = true;
	return static_cast<quint32>(keycode);
}

quint32 QHotkeyPrivatePortal::nativeModifiers(Qt::KeyboardModifiers modifiers, bool &ok)
{
	ok = true;
	return static_cast<quint32>(modifiers & (Qt::ShiftModifier | Qt::ControlModifier |
											  Qt::AltModifier | Qt::MetaModifier));
}

bool QHotkeyPrivatePortal::registerShortcut(QHotkey::NativeShortcut shortcut)
{
	// Optimistic: the actual bind is asynchronous and user-mediated. If the
	// compositor declines, the shortcut simply never fires.
	m_shortcuts.insert(shortcutId(shortcut), shortcut);
	scheduleSync();
	return true;
}

bool QHotkeyPrivatePortal::unregisterShortcut(QHotkey::NativeShortcut shortcut)
{
	m_shortcuts.remove(shortcutId(shortcut));
	scheduleSync();
	return true;
}

void QHotkeyPrivatePortal::scheduleSync()
{
	m_resyncNeeded = true;
	if (m_syncScheduled || m_sessionInProgress)
		return;
	m_syncScheduled = true;
	// Debounce: coalesce the many register/unregister calls QHotkey makes in a
	// single turn (e.g. re-registering all hotkeys) into one session rebuild.
	QTimer::singleShot(0, this, &QHotkeyPrivatePortal::syncShortcuts);
}

void QHotkeyPrivatePortal::syncShortcuts()
{
	m_syncScheduled = false;
	if (m_sessionInProgress)
		return; // finishSync() will start the next pass

	m_resyncNeeded = false;
	m_sessionInProgress = true;

	// No per-shortcut unbind exists, so rebuild the session from scratch.
	if (!m_sessionHandle.isEmpty())
		closeSession();

	if (m_shortcuts.isEmpty()) {
		finishSync(true);
		return;
	}
	createSession();
}

void QHotkeyPrivatePortal::createSession()
{
	const QString reqToken = newToken();
	const QString sessionToken = newToken();

	makeRequest(reqToken, [this](uint response, const QVariantMap &results) {
		if (response != 0) {
			qCWarning(logQHotkey) << "GlobalShortcuts CreateSession cancelled/failed, response =" << response;
			finishSync(false);
			return;
		}
		m_sessionHandle = results.value(QStringLiteral("session_handle")).toString();
		if (m_sessionHandle.isEmpty()) {
			qCWarning(logQHotkey) << "GlobalShortcuts CreateSession returned an empty session handle";
			finishSync(false);
			return;
		}
		bindShortcuts();
	});

	QVariantMap options;
	options.insert(QStringLiteral("handle_token"), reqToken);
	options.insert(QStringLiteral("session_handle_token"), sessionToken);

	QDBusMessage msg = QDBusMessage::createMethodCall(PORTAL_SERVICE, PORTAL_PATH, GS_IFACE,
													  QStringLiteral("CreateSession"));
	msg << options;
	asyncCallChecked(msg);
}

void QHotkeyPrivatePortal::bindShortcuts()
{
	QList<PortalShortcut> list;
	list.reserve(m_shortcuts.size());
	for (auto it = m_shortcuts.cbegin(); it != m_shortcuts.cend(); ++it) {
		PortalShortcut ps;
		ps.id = it.key();
		ps.props.insert(QStringLiteral("description"), shortcutDescription(it.value()));
		const QString trigger = preferredTrigger(it.value());
		if (!trigger.isEmpty())
			ps.props.insert(QStringLiteral("preferred_trigger"), trigger);
		list.append(ps);
	}

	const QString reqToken = newToken();
	makeRequest(reqToken, [this](uint response, const QVariantMap &) {
		if (response != 0)
			qCWarning(logQHotkey) << "GlobalShortcuts BindShortcuts cancelled/failed, response =" << response;
		finishSync(response == 0);
	});

	QVariantMap options;
	options.insert(QStringLiteral("handle_token"), reqToken);

	QDBusMessage msg = QDBusMessage::createMethodCall(PORTAL_SERVICE, PORTAL_PATH, GS_IFACE,
													  QStringLiteral("BindShortcuts"));
	msg << QVariant::fromValue(QDBusObjectPath(m_sessionHandle))
		<< QVariant::fromValue(list)
		<< QString()        // parent_window (empty: the portal dialog is unparented)
		<< options;
	asyncCallChecked(msg);
}

void QHotkeyPrivatePortal::closeSession()
{
	if (m_sessionHandle.isEmpty())
		return;
	QDBusMessage msg = QDBusMessage::createMethodCall(PORTAL_SERVICE, m_sessionHandle, SESSION_IFACE,
													  QStringLiteral("Close"));
	QDBusConnection::sessionBus().asyncCall(msg);
	m_sessionHandle.clear();
}

void QHotkeyPrivatePortal::finishSync(bool ok)
{
	Q_UNUSED(ok)
	m_sessionInProgress = false;
	if (m_resyncNeeded)
		scheduleSync();
}

void QHotkeyPrivatePortal::makeRequest(const QString &token, std::function<void(uint, const QVariantMap &)> cb)
{
	// Subscribe to the Request's Response signal *before* issuing the call. The
	// object path is derived from our unique bus name + handle_token, as the
	// portal spec prescribes, which avoids racing the reply.
	QString sender = QDBusConnection::sessionBus().baseService();
	if (sender.startsWith(QLatin1Char(':')))
		sender = sender.mid(1);
	sender.replace(QLatin1Char('.'), QLatin1Char('_'));
	const QString path = QStringLiteral("/org/freedesktop/portal/desktop/request/%1/%2").arg(sender, token);

	auto *req = new PortalRequest(std::move(cb), this);
	const bool ok = QDBusConnection::sessionBus().connect(PORTAL_SERVICE, path, REQUEST_IFACE,
														  QStringLiteral("Response"),
														  req, SLOT(responded(uint,QVariantMap)));
	if (!ok) {
		qCWarning(logQHotkey) << "Failed to subscribe to portal Request.Response at" << path;
		delete req;
	}
}

void QHotkeyPrivatePortal::asyncCallChecked(const QDBusMessage &msg)
{
	QDBusPendingCall call = QDBusConnection::sessionBus().asyncCall(msg);
	auto *watcher = new QDBusPendingCallWatcher(call, this);
	connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
		QDBusPendingReply<QDBusObjectPath> reply = *w;
		w->deleteLater();
		// On success we wait for the Request's Response; only handle the case
		// where the method call itself errors (e.g. portal unavailable), so the
		// sync state machine doesn't get stuck waiting for a reply that never comes.
		if (reply.isError()) {
			qCWarning(logQHotkey) << "GlobalShortcuts portal call failed:" << reply.error().message();
			finishSync(false);
		}
	});
}

void QHotkeyPrivatePortal::onActivated(const QDBusObjectPath &session, const QString &id,
									   qulonglong timestamp, const QVariantMap &options)
{
	Q_UNUSED(timestamp)
	Q_UNUSED(options)
	if (session.path() != m_sessionHandle)
		return;
	const auto it = m_shortcuts.constFind(id);
	if (it != m_shortcuts.cend())
		activateShortcut(it.value());
}

void QHotkeyPrivatePortal::onDeactivated(const QDBusObjectPath &session, const QString &id,
										 qulonglong timestamp, const QVariantMap &options)
{
	Q_UNUSED(timestamp)
	Q_UNUSED(options)
	if (session.path() != m_sessionHandle)
		return;
	const auto it = m_shortcuts.constFind(id);
	if (it != m_shortcuts.cend())
		releaseShortcut(it.value());
}

QString QHotkeyPrivatePortal::newToken()
{
	return QStringLiteral("qhotkey_%1").arg(QRandomGenerator::global()->generate());
}

// Backend factory + availability probe for the runtime dispatcher in
// qhotkey_linux.cpp. (instance() / isPlatformSupported() are defined there.)
QHotkeyPrivate *createPortalHotkeyPrivate()
{
	return new QHotkeyPrivatePortal();
}

bool portalHotkeyPlatformSupported()
{
	QDBusConnection bus = QDBusConnection::sessionBus();
	if (!bus.isConnected())
		return false;
	// The GlobalShortcuts interface only exists on portal backends that
	// implement it; probing its "version" property is a cheap availability test.
	QDBusMessage msg = QDBusMessage::createMethodCall(PORTAL_SERVICE, PORTAL_PATH, PROPS_IFACE,
													  QStringLiteral("Get"));
	msg << GS_IFACE << QStringLiteral("version");
	const QDBusMessage reply = bus.call(msg, QDBus::Block, 1000);
	return reply.type() == QDBusMessage::ReplyMessage;
}

#include "qhotkey_portal.moc"
