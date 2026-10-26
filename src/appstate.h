#ifndef APPSTATE_H
#define APPSTATE_H

#include <QObject>
#include <QGuiApplication>
#include <QWindow>

// Tracks whether the app is in the foreground. The QML layer uses it to
// pause expensive work (tile downloads) while the app is in the background,
// which keeps battery use minimal. Defaults to foreground: if the platform
// never reports a state change, nothing is ever paused (no regression).
class AppState : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool foreground READ foreground NOTIFY foregroundChanged)
public:
    explicit AppState(QWindow *view, QObject *parent = nullptr)
        : QObject(parent), m_view(view)
    {
        connect(m_view, &QWindow::visibilityChanged, this, &AppState::update);
    }

    bool foreground() const { return m_foreground; }

signals:
    void foregroundChanged();

private:
    void update()
    {
        // Pause only when the window is hidden (app covered by another app).
        // Deliberately no focus/application-state test: those stay unset when
        // the screen is off, and the system stops the app anyway in that case.
        const bool fg = m_view->visibility() != QWindow::Hidden;
        if (fg == m_foreground)
            return;
        m_foreground = fg;
        emit foregroundChanged();
    }

    QWindow *m_view;
    bool m_foreground = true;
};

#endif // APPSTATE_H
