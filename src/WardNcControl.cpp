#include "WardNcControl.h"

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
