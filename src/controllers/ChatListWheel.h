#pragma once

#include <QObject>
#include <QQmlParserStatus>

// Speeds up wheel and two-finger touchpad scrolling on the chat list only.
// It scales each wheel event's deltas and lets the list handle the scaled
// copy, so Flickable's own motion (and the chat's bottom pin) is unchanged.
class ChatListWheel : public QObject, public QQmlParserStatus
{
    Q_OBJECT
    Q_INTERFACES(QQmlParserStatus)

public:
    // How much farther one wheel event moves the chat than Qt's default.
    static constexpr double kScale = 1.75;

    explicit ChatListWheel(QObject *parent = nullptr);

    // Registers ChatListWheel as Shammy.Wheel 1.0 for ChatView.qml.
    static void registerType();

    void classBegin() override;
    void componentComplete() override;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    // True while the scaled copy is being delivered, so it is not scaled again.
    bool m_sending = false;
};
