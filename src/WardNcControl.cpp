#include "WardNcControl.h"

/*
 * WardNcControl is the private control D-Bus adapter for an existing daemon.
 * Methods are intentionally thin: each exported call emits a Qt signal and lets
 * the long-lived application objects decide how to handle it.
 *
 * The capitalized method names are part of the exported D-Bus surface and match
 * the calls made by main.cpp for --reload, --open, --close, and --toggle.
 */
WardNcControl::WardNcControl(QObject *parent)
    : QObject(parent)
{
}

void WardNcControl::Reload()
{
    emit reloadRequested();
}

void WardNcControl::Open()
{
    emit openRequested();
}

void WardNcControl::Close()
{
    emit closeRequested();
}

void WardNcControl::Toggle()
{
    emit toggleRequested();
}
