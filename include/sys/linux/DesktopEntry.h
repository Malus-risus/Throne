#pragma once

// Ensures a per-user application desktop entry
// (~/.local/share/applications/Throne.desktop) exists. Its basename matches the
// app_id set via QGuiApplication::setDesktopFileName("Throne"), giving the XDG
// desktop portal a desktop entry to resolve the running app's identity from.
//
// This is what lets the GlobalShortcuts portal (and other portals) persist
// their state across restarts on portable/zip installs that have no system-wide
// desktop entry. No-op when a system entry already exists (e.g. via the .deb).
void DesktopEntry_Ensure();
