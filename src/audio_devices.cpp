#include "audio_devices.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>

#include <algorithm>

namespace
{

QString propertyString(
    const QJsonObject &properties,
    const QString &name)
{
    return properties
        .value(name)
        .toString()
        .trimmed();
}

void appendDevice(
    QList<AudioDevice> &devices,
    const QJsonObject &properties)
{
    const QString nodeName =
        propertyString(
            properties,
            QStringLiteral("node.name"));

    if (nodeName.isEmpty()) {
        return;
    }

    QString description =
        propertyString(
            properties,
            QStringLiteral(
                "node.description"));

    if (description.isEmpty()) {
        description =
            propertyString(
                properties,
                QStringLiteral(
                    "node.nick"));
    }

    if (description.isEmpty()) {
        description = nodeName;
    }

    for (const AudioDevice &existing :
         devices) {
        if (existing.nodeName == nodeName) {
            return;
        }
    }

    devices.append(
        {
            nodeName,
            description
        });
}

}

AudioDeviceInventory queryAudioDevices()
{
    AudioDeviceInventory inventory;

    QProcess process;

    process.start(
        QStringLiteral("pw-dump"),
        {});

    if (!process.waitForStarted(3000)) {
        inventory.error =
            QStringLiteral(
                "Could not start pw-dump.");
        return inventory;
    }

    if (!process.waitForFinished(5000)) {
        process.kill();
        process.waitForFinished();

        inventory.error =
            QStringLiteral(
                "Timed out while reading "
                "PipeWire audio devices.");
        return inventory;
    }

    if (
        process.exitStatus() !=
            QProcess::NormalExit ||
        process.exitCode() != 0) {
        inventory.error =
            QString::fromUtf8(
                process.readAllStandardError())
                .trimmed();

        if (inventory.error.isEmpty()) {
            inventory.error =
                QStringLiteral(
                    "PipeWire device discovery failed.");
        }

        return inventory;
    }

    QJsonParseError parseError;

    const QJsonDocument document =
        QJsonDocument::fromJson(
            process.readAllStandardOutput(),
            &parseError);

    if (
        parseError.error !=
            QJsonParseError::NoError ||
        !document.isArray()) {
        inventory.error =
            QStringLiteral(
                "PipeWire returned invalid device data.");
        return inventory;
    }

    const QJsonArray objects =
        document.array();

    for (const QJsonValue &value : objects) {
        const QJsonObject object =
            value.toObject();

        if (
            object.value(
                QStringLiteral("type"))
                .toString() !=
            QStringLiteral(
                "PipeWire:Interface:Node")) {
            continue;
        }

        const QJsonObject information =
            object.value(
                QStringLiteral("info"))
                .toObject();

        const QJsonObject properties =
            information.value(
                QStringLiteral("props"))
                .toObject();

        const QString mediaClass =
            propertyString(
                properties,
                QStringLiteral(
                    "media.class"));

        if (
            mediaClass ==
            QStringLiteral("Audio/Source")) {
            appendDevice(
                inventory.inputs,
                properties);
        } else if (
            mediaClass ==
            QStringLiteral("Audio/Sink")) {
            appendDevice(
                inventory.outputs,
                properties);
        }
    }

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
