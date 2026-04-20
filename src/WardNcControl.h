#pragma once

#include <QObject>

class WardNcControl : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.dxle.wardnc.Control")

public:
    explicit WardNcControl(QObject *parent = nullptr);

signals:
    void reloadRequested();
    void openRequested();
    void closeRequested();
    void toggleRequested();

public slots:
    void Reload();
    void Open();
    void Close();
    void Toggle();
};
