#include "libei_input.h"

#include <QDebug>
#include <QSocketNotifier>
#include <QStringList>

#include <libei.h>
#include <linux/input-event-codes.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <unistd.h>

LibeiInput::LibeiInput(QObject *parent)
    : QObject(parent)
{
}

LibeiInput::~LibeiInput()
{
    stop();
}

bool LibeiInput::pointerReady() const
{
    return pointerDevice_ != nullptr &&
           pointerResumed_;
}

bool LibeiInput::absolutePointerReady() const
{
    return absolutePointerDevice_ != nullptr &&
           absolutePointerResumed_;
}

bool LibeiInput::buttonReady() const
{
    return buttonDevice_ != nullptr &&
           buttonResumed_;
}

bool LibeiInput::scrollReady() const
{
    return scrollDevice_ != nullptr &&
           scrollResumed_;
}

bool LibeiInput::keyboardReady() const
{
    return keyboardDevice_ != nullptr &&
           keyboardResumed_;
}

void LibeiInput::start(
    int portalFileDescriptor)
{
    stop();

    if (portalFileDescriptor < 0) {
        emit errorOccurred(
            QStringLiteral(
                "The EIS portal file descriptor is invalid."));
        return;
    }

    const int duplicatedFd =
        ::dup(portalFileDescriptor);

    if (duplicatedFd < 0) {
        emit errorOccurred(
            QStringLiteral(
                "Could not duplicate the EIS descriptor: %1")
                .arg(
                    QString::fromLocal8Bit(
                        std::strerror(errno))));
        return;
    }

    qInfo() << "libei: creating sender context";

    ei_ = ei_new_sender(this);

    if (ei_ == nullptr) {
        ::close(duplicatedFd);

        emit errorOccurred(
            QStringLiteral(
                "Could not create the libei sender context."));
        return;
    }

    qInfo() << "libei: attaching EIS descriptor";

    const int setupResult =
        ei_setup_backend_fd(
            ei_,
            duplicatedFd);

    if (setupResult < 0) {
        ::close(duplicatedFd);
        ei_ = ei_unref(ei_);

        emit errorOccurred(
            QStringLiteral(
                "Could not attach libei to the EIS socket."));
        return;
    }

    qInfo() << "libei: backend attached";

    const int eventFd =
        ei_get_fd(ei_);

    if (eventFd < 0) {
        stop();

        emit errorOccurred(
            QStringLiteral(
                "libei did not provide an event descriptor."));
        return;
    }

    notifier_ =
        new QSocketNotifier(
            eventFd,
            QSocketNotifier::Read,
            this);

    connect(
        notifier_,
        &QSocketNotifier::activated,
        this,
        &LibeiInput::processEvents);

    emit statusChanged(
        QStringLiteral(
            "Connected to EIS; waiting for input devices…"));

    qInfo() << "libei: processing initial events";
    processEvents();
}

void LibeiInput::stop()
{
    const QSet<struct ei_device *> activeDevices =
        emulatingDevices_;

    for (struct ei_device *device :
         activeDevices) {
        stopDeviceEmulation(
            device);
    }

    if (notifier_ != nullptr) {
        notifier_->setEnabled(false);
        delete notifier_;
        notifier_ = nullptr;
    }

    clearPointerDevice();
    clearAbsolutePointerDevice();
    clearButtonDevice();
    clearScrollDevice();
    clearKeyboardDevice();

    if (ei_ != nullptr) {
        ei_ = ei_unref(ei_);
    }

    sequence_ = 0;
}

void LibeiInput::processEvents()
{
    if (ei_ == nullptr) {
        return;
    }

    ei_dispatch(ei_);

    while (ei_event *event =
               ei_get_event(ei_)) {
        const enum ei_event_type type =
            ei_event_get_type(event);

        qInfo() << "libei: event type"
                << static_cast<int>(type);

        switch (type) {
        case EI_EVENT_CONNECT:
            emit statusChanged(
                QStringLiteral(
                    "EIS connection established"));
            break;

        case EI_EVENT_DISCONNECT:
            emit errorOccurred(
                QStringLiteral(
                    "The compositor closed the EIS connection."));
            break;

        case EI_EVENT_SEAT_ADDED:
        {
            ei_seat *seat =
                ei_event_get_seat(event);

            if (seat != nullptr) {
                ei_seat_bind_capabilities(
                    seat,
                    EI_DEVICE_CAP_POINTER,
                    EI_DEVICE_CAP_POINTER_ABSOLUTE,
                    EI_DEVICE_CAP_BUTTON,
                    EI_DEVICE_CAP_SCROLL,
                    EI_DEVICE_CAP_KEYBOARD,
                    nullptr);

                emit statusChanged(
                    QStringLiteral(
                        "Input capabilities requested from EIS"));
            }
            break;
        }

        case EI_EVENT_DEVICE_ADDED:
        {
            ei_device *device =
                ei_event_get_device(event);

            if (device == nullptr) {
                break;
            }

            QStringList capabilities;

            if (ei_device_has_capability(
                    device,
                    EI_DEVICE_CAP_POINTER)) {
                capabilities.append(
                    QStringLiteral("pointer"));

                setPointerDevice(device);
            }

            if (ei_device_has_capability(
                    device,
                    EI_DEVICE_CAP_POINTER_ABSOLUTE)) {
                capabilities.append(
                    QStringLiteral(
                        "absolute pointer"));

                setAbsolutePointerDevice(
                    device);
            }

            if (ei_device_has_capability(
                    device,
                    EI_DEVICE_CAP_BUTTON)) {
                capabilities.append(
                    QStringLiteral("button"));

                setButtonDevice(device);
            }

            if (ei_device_has_capability(
                    device,
                    EI_DEVICE_CAP_SCROLL)) {
                capabilities.append(
                    QStringLiteral("scroll"));

                setScrollDevice(device);
            }

            if (ei_device_has_capability(
                    device,
                    EI_DEVICE_CAP_KEYBOARD)) {
                capabilities.append(
                    QStringLiteral("keyboard"));

                setKeyboardDevice(device);
            }

            const char *deviceName =
                ei_device_get_name(device);

            qInfo()
                << "libei: device added"
                << (
                    deviceName != nullptr
                        ? QString::fromUtf8(deviceName)
                        : QStringLiteral("(unnamed)"))
                << "capabilities"
                << capabilities.join(
                       QStringLiteral(", "));

            emit statusChanged(
                QStringLiteral(
                    "EIS device discovered: %1")
                    .arg(
                        capabilities.isEmpty()
                            ? QStringLiteral(
                                  "no supported capabilities")
                            : capabilities.join(
                                  QStringLiteral(", "))));
            break;
        }

        case EI_EVENT_DEVICE_RESUMED:
            updateDeviceResumeState(
                ei_event_get_device(event),
                true);
            break;

        case EI_EVENT_DEVICE_PAUSED:
            updateDeviceResumeState(
                ei_event_get_device(event),
                false);
            break;

        case EI_EVENT_DEVICE_REMOVED:
            removeDevice(
                ei_event_get_device(event));
            break;

        default:
            break;
        }

        ei_event_unref(event);
    }
}

void LibeiInput::movePointer()
{
    if (!pointerReady() ||
        ei_ == nullptr) {
        emit errorOccurred(
            QStringLiteral(
                "No active authorized pointer device."));
        return;
    }

    ei_device_pointer_motion(
        pointerDevice_,
        80.0,
        30.0);

    ei_device_frame(
        pointerDevice_,
        ei_now(ei_));

    ei_dispatch(ei_);

    emit statusChanged(
        QStringLiteral(
            "Pointer movement sent: +80, +30"));
}

void LibeiInput::movePointerRelative(
    int deltaX,
    int deltaY)
{
    if (!pointerReady() ||
        ei_ == nullptr) {
        emit errorOccurred(
            QStringLiteral(
                "No active authorized relative pointer device."));
        return;
    }

    if (deltaX == 0 &&
        deltaY == 0) {
        return;
    }

    ei_device_pointer_motion(
        pointerDevice_,
        static_cast<double>(deltaX),
        static_cast<double>(deltaY));

    ei_device_frame(
        pointerDevice_,
        ei_now(ei_));

    ei_dispatch(ei_);
}

void LibeiInput::movePointerAbsolute(
    int x,
    int y,
    int frameWidth,
    int frameHeight)
{
    if (!absolutePointerReady() ||
        ei_ == nullptr) {
        emit errorOccurred(
            QStringLiteral(
                "No active authorized absolute pointer device."));
        return;
    }

    if (frameWidth <= 0 ||
        frameHeight <= 0) {
        emit errorOccurred(
            QStringLiteral(
                "The remote desktop dimensions are invalid."));
        return;
    }

    ei_region *region =
        ei_device_get_region(
            absolutePointerDevice_,
            0);

    if (region == nullptr) {
        emit errorOccurred(
            QStringLiteral(
                "The authorized absolute pointer has no region."));
        return;
    }

    const uint32_t regionX =
        ei_region_get_x(region);

    const uint32_t regionY =
        ei_region_get_y(region);

    const uint32_t regionWidth =
        ei_region_get_width(region);

    const uint32_t regionHeight =
        ei_region_get_height(region);

    if (regionWidth == 0 ||
        regionHeight == 0) {
        emit errorOccurred(
            QStringLiteral(
                "The authorized pointer region is invalid."));
        return;
    }

    const int boundedX =
        std::clamp(
            x,
            0,
            frameWidth - 1);

    const int boundedY =
        std::clamp(
            y,
            0,
            frameHeight - 1);

    const double normalizedX =
        frameWidth > 1
            ? static_cast<double>(boundedX) /
                  static_cast<double>(frameWidth - 1)
            : 0.0;

    const double normalizedY =
        frameHeight > 1
            ? static_cast<double>(boundedY) /
                  static_cast<double>(frameHeight - 1)
            : 0.0;

    const double targetX =
        static_cast<double>(regionX) +
        normalizedX *
            static_cast<double>(regionWidth - 1);

    const double targetY =
        static_cast<double>(regionY) +
        normalizedY *
            static_cast<double>(regionHeight - 1);

    ei_device_pointer_motion_absolute(
        absolutePointerDevice_,
        targetX,
        targetY);

    ei_device_frame(
        absolutePointerDevice_,
        ei_now(ei_));

    ei_dispatch(ei_);
}

void LibeiInput::clickLeftButton()
{
    if (!buttonReady() ||
        ei_ == nullptr) {
        emit errorOccurred(
            QStringLiteral(
                "No active authorized button device."));
        return;
    }

    ei_device_button_button(
        buttonDevice_,
        BTN_LEFT,
        true);

    ei_device_frame(
        buttonDevice_,
        ei_now(ei_));

    ei_device_button_button(
        buttonDevice_,
        BTN_LEFT,
        false);

    ei_device_frame(
        buttonDevice_,
        ei_now(ei_));

    ei_dispatch(ei_);

    emit statusChanged(
        QStringLiteral(
            "Left-click press and release sent"));
}

void LibeiInput::pressLeftButton()
{
    if (!buttonReady() ||
        ei_ == nullptr) {
        emit errorOccurred(
            QStringLiteral(
                "No active authorized button device."));
        return;
    }

    ei_device_button_button(
        buttonDevice_,
        BTN_LEFT,
        true);

    ei_device_frame(
        buttonDevice_,
        ei_now(ei_));

    ei_dispatch(ei_);
}

void LibeiInput::releaseLeftButton()
{
    if (!buttonReady() ||
        ei_ == nullptr) {
        return;
    }

    ei_device_button_button(
        buttonDevice_,
        BTN_LEFT,
        false);

    ei_device_frame(
        buttonDevice_,
        ei_now(ei_));

    ei_dispatch(ei_);
}

void LibeiInput::clickRightButton()
{
    if (!buttonReady() ||
        ei_ == nullptr) {
        emit errorOccurred(
            QStringLiteral(
                "No active authorized button device."));
        return;
    }

    ei_device_button_button(
        buttonDevice_,
        BTN_RIGHT,
        true);

    ei_device_frame(
        buttonDevice_,
        ei_now(ei_));

    ei_device_button_button(
        buttonDevice_,
        BTN_RIGHT,
        false);

    ei_device_frame(
        buttonDevice_,
        ei_now(ei_));

    ei_dispatch(ei_);

    emit statusChanged(
        QStringLiteral(
            "Right-click press and release sent"));
}

void LibeiInput::scrollDown()
{
    if (!scrollReady() ||
        ei_ == nullptr) {
        emit errorOccurred(
            QStringLiteral(
                "No active authorized scroll device."));
        return;
    }

    ei_device_scroll_discrete(
        scrollDevice_,
        0,
        120);

    ei_device_frame(
        scrollDevice_,
        ei_now(ei_));

    ei_device_scroll_stop(
        scrollDevice_,
        false,
        true);

    ei_device_frame(
        scrollDevice_,
        ei_now(ei_));

    ei_dispatch(ei_);

    emit statusChanged(
        QStringLiteral(
            "One downward scroll step sent"));
}

void LibeiInput::pressKey(
    uint32_t linuxKeyCode)
{
    if (!keyboardReady() ||
        ei_ == nullptr) {
        emit errorOccurred(
            QStringLiteral(
                "No active authorized keyboard device."));
        return;
    }

    ei_device_keyboard_key(
        keyboardDevice_,
        linuxKeyCode,
        true);

    ei_device_frame(
        keyboardDevice_,
        ei_now(ei_));

    ei_dispatch(ei_);
}

void LibeiInput::releaseKey(
    uint32_t linuxKeyCode)
{
    if (!keyboardReady() ||
        ei_ == nullptr) {
        return;
    }

    ei_device_keyboard_key(
        keyboardDevice_,
        linuxKeyCode,
        false);

    ei_device_frame(
        keyboardDevice_,
        ei_now(ei_));

    ei_dispatch(ei_);
}

void LibeiInput::setPointerDevice(
    struct ei_device *device)
{
    if (device == pointerDevice_) {
        return;
    }

    clearPointerDevice();

    pointerDevice_ =
        ei_device_ref(device);

    pointerResumed_ = false;
    emit pointerReadyChanged(false);
}

void LibeiInput::setAbsolutePointerDevice(
    struct ei_device *device)
{
    if (device == absolutePointerDevice_) {
        return;
    }

    clearAbsolutePointerDevice();

    absolutePointerDevice_ =
        ei_device_ref(device);

    absolutePointerResumed_ = false;

    emit absolutePointerReadyChanged(
        false);
}

void LibeiInput::setButtonDevice(
    struct ei_device *device)
{
    if (device == buttonDevice_) {
        return;
    }

    clearButtonDevice();

    buttonDevice_ =
        ei_device_ref(device);

    buttonResumed_ = false;
    emit buttonReadyChanged(false);
}

void LibeiInput::setScrollDevice(
    struct ei_device *device)
{
    if (device == scrollDevice_) {
        return;
    }

    clearScrollDevice();

    scrollDevice_ =
        ei_device_ref(device);

    scrollResumed_ = false;
    emit scrollReadyChanged(false);
}

void LibeiInput::setKeyboardDevice(
    struct ei_device *device)
{
    if (device == keyboardDevice_) {
        return;
    }

    clearKeyboardDevice();

    keyboardDevice_ =
        ei_device_ref(device);

    keyboardResumed_ = false;
    emit keyboardReadyChanged(false);
}

void LibeiInput::clearPointerDevice()
{
    pointerResumed_ = false;
    emit pointerReadyChanged(false);

    if (pointerDevice_ != nullptr) {
        pointerDevice_ =
            ei_device_unref(pointerDevice_);
    }
}

void LibeiInput::clearAbsolutePointerDevice()
{
    absolutePointerResumed_ = false;

    emit absolutePointerReadyChanged(
        false);

    if (absolutePointerDevice_ != nullptr) {
        absolutePointerDevice_ =
            ei_device_unref(
                absolutePointerDevice_);
    }
}

void LibeiInput::clearButtonDevice()
{
    buttonResumed_ = false;
    emit buttonReadyChanged(false);

    if (buttonDevice_ != nullptr) {
        buttonDevice_ =
            ei_device_unref(buttonDevice_);
    }
}

void LibeiInput::clearScrollDevice()
{
    scrollResumed_ = false;
    emit scrollReadyChanged(false);

    if (scrollDevice_ != nullptr) {
        scrollDevice_ =
            ei_device_unref(scrollDevice_);
    }
}

void LibeiInput::clearKeyboardDevice()
{
    keyboardResumed_ = false;
    emit keyboardReadyChanged(false);

    if (keyboardDevice_ != nullptr) {
        keyboardDevice_ =
            ei_device_unref(keyboardDevice_);
    }
}

void LibeiInput::updateDeviceResumeState(
    struct ei_device *device,
    bool resumed)
{
    if (device == nullptr) {
        return;
    }

    if (resumed) {
        startDeviceEmulation(
            device);
    } else {
        stopDeviceEmulation(
            device);
    }

    if (device == pointerDevice_) {
        pointerResumed_ = resumed;
        emit pointerReadyChanged(
            pointerReady());
    }

    if (device == absolutePointerDevice_) {
        absolutePointerResumed_ = resumed;

        emit absolutePointerReadyChanged(
            absolutePointerReady());
    }

    if (device == buttonDevice_) {
        buttonResumed_ = resumed;
        emit buttonReadyChanged(
            buttonReady());
    }

    if (device == scrollDevice_) {
        scrollResumed_ = resumed;
        emit scrollReadyChanged(
            scrollReady());
    }

    if (device == keyboardDevice_) {
        keyboardResumed_ = resumed;
        emit keyboardReadyChanged(
            keyboardReady());
    }

    if (resumed) {
        emit statusChanged(
            QStringLiteral(
                "Authorized input device ready"));
    } else {
        emit statusChanged(
            QStringLiteral(
                "Authorized input device paused"));
    }
}

void LibeiInput::removeDevice(
    struct ei_device *device)
{
    if (device == nullptr) {
        return;
    }

    stopDeviceEmulation(
        device);

    if (device == pointerDevice_) {
        clearPointerDevice();
    }

    if (device == absolutePointerDevice_) {
        clearAbsolutePointerDevice();
    }

    if (device == buttonDevice_) {
        clearButtonDevice();
    }

    if (device == scrollDevice_) {
        clearScrollDevice();
    }

    if (device == keyboardDevice_) {
        clearKeyboardDevice();
    }
}

void LibeiInput::startDeviceEmulation(
    struct ei_device *device)
{
    if (device == nullptr ||
        emulatingDevices_.contains(device)) {
        return;
    }

    ei_device_start_emulating(
        device,
        nextSequence());

    emulatingDevices_.insert(device);

    emit statusChanged(
        QStringLiteral(
            "Authorized input device is emulating"));
}

void LibeiInput::stopDeviceEmulation(
    struct ei_device *device)
{
    if (device == nullptr ||
        !emulatingDevices_.contains(device)) {
        return;
    }

    ei_device_stop_emulating(
        device);

    emulatingDevices_.remove(
        device);
}

uint32_t LibeiInput::nextSequence()
{
    ++sequence_;

    if (sequence_ == 0) {
        ++sequence_;
    }

    return sequence_;
}
