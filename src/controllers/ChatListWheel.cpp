#include "controllers/ChatListWheel.h"

#include <QCoreApplication>
#include <QWheelEvent>
#include <QtQml/qqml.h>

#include <cmath>

namespace
{

// Scales one axis of a delta, keeping a non-zero input non-zero.
int scaleAxis(int v)
{
    if (v == 0)
    {
        return 0;
    }
    const int scaled = static_cast<int>(std::lround(v * ChatListWheel::kScale));
    if (scaled == 0)
    {
        return v > 0 ? 1 : -1;
    }
    return scaled;
}

// Scales the vertical axis only; horizontal scrolling is left as Qt has it.
QPoint scaleVertical(QPoint p)
{
    return QPoint(p.x(), scaleAxis(p.y()));
}

} // namespace

void ChatListWheel::registerType()
{
    qmlRegisterType<ChatListWheel>("Shammy.Wheel", 1, 0, "ChatListWheel");
}

ChatListWheel::ChatListWheel(QObject *parent)
    : QObject(parent)
{
}

void ChatListWheel::classBegin()
{
}

void ChatListWheel::componentComplete()
{
    // The parent is the chat ListView. Other Flickables never see this filter.
    if (QObject *target = parent())
    {
        target->installEventFilter(this);
    }
}

bool ChatListWheel::eventFilter(QObject *watched, QEvent *event)
{
    if (m_sending || !event || event->type() != QEvent::Wheel)
    {
        return false;
    }

    auto *wheel = static_cast<QWheelEvent *>(event);
    // Nothing vertical to speed up.
    if (wheel->angleDelta().y() == 0 && wheel->pixelDelta().y() == 0)
    {
        return false;
    }

    // Scale both deltas. Which one Flickable uses depends on the device and
    // platform: on X11 a two-finger touchpad scroll arrives with no scroll
    // phase and both deltas set, and Qt 6.10 moves by the angle delta; a
    // phased touchpad (Wayland, macOS) moves by the pixel delta; a plain
    // wheel notch has only the angle delta. Scaling both covers all three.
    QWheelEvent copy(wheel->position(), wheel->globalPosition(),
                     scaleVertical(wheel->pixelDelta()), scaleVertical(wheel->angleDelta()),
                     wheel->buttons(), wheel->modifiers(), wheel->phase(), wheel->inverted(),
                     wheel->source(), wheel->pointingDevice());
    copy.setTimestamp(wheel->timestamp());
    m_sending = true;
    QCoreApplication::sendEvent(watched, &copy);
    m_sending = false;
    // The original must not also move the list. Keep the copy's accepted flag
    // so an event at the end still propagates the way Flickable decided.
    wheel->setAccepted(copy.isAccepted());
    return true;
}
