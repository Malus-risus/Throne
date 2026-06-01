#pragma once

#include "qhotkey.h"
#include <QAbstractNativeEventFilter>
#include <QMultiHash>
#include <QMutex>
#include <QGlobalStatic>

#define _NATIVE_EVENT_RESULT qintptr

class QHOTKEY_EXPORT QHotkeyPrivate : public QObject, public QAbstractNativeEventFilter
{
	Q_OBJECT

public:
	QHotkeyPrivate();//singleton!!!
	~QHotkeyPrivate();

	static QHotkeyPrivate *instance();
	static bool isPlatformSupported();

	QHotkey::NativeShortcut nativeShortcut(Qt::Key keycode, Qt::KeyboardModifiers modifiers);

	bool addShortcut(QHotkey *hotkey);
	bool removeShortcut(QHotkey *hotkey);

protected:
	void activateShortcut(QHotkey::NativeShortcut shortcut);
	void releaseShortcut(QHotkey::NativeShortcut shortcut);

	virtual quint32 nativeKeycode(Qt::Key keycode, bool &ok) = 0;//platform implement
	virtual quint32 nativeModifiers(Qt::KeyboardModifiers modifiers, bool &ok) = 0;//platform implement

	virtual bool registerShortcut(QHotkey::NativeShortcut shortcut) = 0;//platform implement
	virtual bool unregisterShortcut(QHotkey::NativeShortcut shortcut) = 0;//platform implement

	QString error;

private:
	QHash<QPair<Qt::Key, Qt::KeyboardModifiers>, QHotkey::NativeShortcut> mapping;
	QMultiHash<QHotkey::NativeShortcut, QHotkey*> shortcuts;

	Q_INVOKABLE void addMappingInvoked(Qt::Key keycode, Qt::KeyboardModifiers modifiers, QHotkey::NativeShortcut nativeShortcut);
	Q_INVOKABLE bool addShortcutInvoked(QHotkey *hotkey);
	Q_INVOKABLE bool removeShortcutInvoked(QHotkey *hotkey);
	Q_INVOKABLE QHotkey::NativeShortcut nativeShortcutInvoked(Qt::Key keycode, Qt::KeyboardModifiers modifiers);
};

// Single-backend platforms (Windows, macOS) use this macro to define the
// QHotkeyPrivate singleton accessor. On Unix the backend is chosen at runtime
// (X11 vs. the XDG desktop portal), so qhotkey_linux.cpp defines instance() and
// isPlatformSupported() itself and does NOT use this macro.
#define NATIVE_INSTANCE(ClassName) \
	Q_GLOBAL_STATIC(ClassName, hotkeyPrivate) \
	\
	QHotkeyPrivate *QHotkeyPrivate::instance()\
	{\
		return hotkeyPrivate;\
	}

#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
// Backend factories and availability probes used by the runtime dispatcher in
// qhotkey_linux.cpp. Each is implemented in its respective backend translation
// unit (qhotkey_x11.cpp / qhotkey_portal.cpp) and both are compiled into the
// library, since a single Linux binary may run under X11, Wayland or XWayland.
QHotkeyPrivate *createX11HotkeyPrivate();
bool x11HotkeyPlatformSupported();
QHotkeyPrivate *createPortalHotkeyPrivate();
bool portalHotkeyPlatformSupported();
#endif
