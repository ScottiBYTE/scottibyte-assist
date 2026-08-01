#include "audio_devices.h"

#include <gst/gst.h>

#include <algorithm>
#include <mutex>

namespace
{

std::once_flag initializationFlag;
bool gstreamerInitialized = false;

bool initializeGStreamer()
{
    std::call_once(
        initializationFlag,
        []()
        {
            GError *error = nullptr;

            gstreamerInitialized =
                gst_init_check(
                    nullptr,
                    nullptr,
                    &error);

            if (error != nullptr) {
                g_error_free(error);
            }
        });

    return gstreamerInitialized;
}

void appendDevices(
    QList<AudioDevice> &destination,
    const char *deviceClass)
{
    GstDeviceMonitor *monitor =
        gst_device_monitor_new();

    if (monitor == nullptr) {
        return;
    }

    gst_device_monitor_add_filter(
        monitor,
        deviceClass,
        nullptr);

    if (!gst_device_monitor_start(
            monitor)) {
        gst_object_unref(monitor);
        return;
    }

    GList *devices =
        gst_device_monitor_get_devices(
            monitor);

    for (
        GList *entry = devices;
        entry != nullptr;
        entry = entry->next) {
        auto *device =
            GST_DEVICE(entry->data);

        gchar *displayName =
            gst_device_get_display_name(
                device);

        GstElement *element =
            gst_device_create_element(
                device,
                nullptr);

        gchar *deviceId = nullptr;

        if (element != nullptr) {
            GParamSpec *deviceProperty =
                g_object_class_find_property(
                    G_OBJECT_GET_CLASS(element),
                    "device");

            if (deviceProperty != nullptr) {
                g_object_get(
                    element,
                    "device",
                    &deviceId,
                    nullptr);
            }

            gst_object_unref(element);
        }

        const QString nodeName =
            deviceId != nullptr
                ? QString::fromUtf8(
                      deviceId)
                      .trimmed()
                : QString();

        const QString description =
            displayName != nullptr
                ? QString::fromUtf8(
                      displayName)
                      .trimmed()
                : QString();

        if (!nodeName.isEmpty()) {
            bool duplicate = false;

            for (
                const AudioDevice &existing :
                destination) {
                if (
                    existing.nodeName ==
                    nodeName) {
                    duplicate = true;
                    break;
                }
            }

            if (!duplicate) {
                destination.append(
                    {
                        nodeName,
                        description.isEmpty()
                            ? nodeName
                            : description
                    });
            }
        }

        g_free(deviceId);
        g_free(displayName);
    }

    g_list_free_full(
        devices,
        reinterpret_cast<GDestroyNotify>(
            gst_object_unref));

    gst_device_monitor_stop(monitor);
    gst_object_unref(monitor);
}

}

AudioDeviceInventory queryAudioDevices()
{
    AudioDeviceInventory inventory;

    if (!initializeGStreamer()) {
        inventory.error =
            QStringLiteral(
                "GStreamer could not be initialized.");
        return inventory;
    }

    appendDevices(
        inventory.inputs,
        "Audio/Source");

    appendDevices(
        inventory.outputs,
        "Audio/Sink");

    const auto byDescription =
        [](
            const AudioDevice &left,
            const AudioDevice &right)
        {
            return left.description
                       .localeAwareCompare(
                           right.description) < 0;
        };

    std::sort(
        inventory.inputs.begin(),
        inventory.inputs.end(),
        byDescription);

    std::sort(
        inventory.outputs.begin(),
        inventory.outputs.end(),
        byDescription);

    return inventory;
}
