#include "pipewire_echo_cancel.h"

#include <QProcess>

PipeWireEchoCancel::~PipeWireEchoCancel()
{
    stop();
}

bool PipeWireEchoCancel::start(
    const QString &inputNode,
    const QString &outputNode)
{
    stop();
    errorString_.clear();

    QStringList arguments = {
        QStringLiteral("load-module"),
        QStringLiteral("module-echo-cancel"),
        QStringLiteral("source_name=%1")
            .arg(sourceName_),
        QStringLiteral("sink_name=%1")
            .arg(sinkName_),
        QStringLiteral(
            "source_properties=node.description="
            "\"ScottiBYTE Assist Echo-Cancelled Microphone\""),
        QStringLiteral(
            "sink_properties=node.description="
            "\"ScottiBYTE Assist Echo-Cancelled Speakers\""),
        QStringLiteral("aec_method=webrtc")
    };

    if (!inputNode.trimmed().isEmpty()) {
        arguments.append(
            QStringLiteral("source_master=%1")
                .arg(inputNode.trimmed()));
    }

    if (!outputNode.trimmed().isEmpty()) {
        arguments.append(
            QStringLiteral("sink_master=%1")
                .arg(outputNode.trimmed()));
    }

    QProcess process;

    process.start(
        QStringLiteral("pactl"),
        arguments);

    if (!process.waitForStarted(3000)) {
        errorString_ =
            QStringLiteral(
                "Could not start pactl.");
        return false;
    }

    if (!process.waitForFinished(5000)) {
        process.kill();
        process.waitForFinished();

        errorString_ =
            QStringLiteral(
                "Timed out while loading "
                "PipeWire echo cancellation.");
        return false;
    }

    const QString standardError =
        QString::fromUtf8(
            process.readAllStandardError())
            .trimmed();

    if (
        process.exitStatus() !=
            QProcess::NormalExit ||
        process.exitCode() != 0) {
        errorString_ =
            standardError.isEmpty()
                ? QStringLiteral(
                      "PipeWire echo cancellation "
                      "could not be loaded.")
                : standardError;

        return false;
    }

    const QString result =
        QString::fromUtf8(
            process.readAllStandardOutput())
            .trimmed();

    bool validModuleId = false;

    result.toULongLong(
        &validModuleId);

    if (!validModuleId) {
        errorString_ =
            QStringLiteral(
                "pactl did not return a valid "
                "echo-cancel module ID.");
        return false;
    }

    moduleId_ = result;

    return true;
}

void PipeWireEchoCancel::stop()
{
    if (moduleId_.isEmpty()) {
        return;
    }

    QProcess process;

    process.start(
        QStringLiteral("pactl"),
        {
            QStringLiteral("unload-module"),
            moduleId_
        });

    process.waitForStarted(3000);
    process.waitForFinished(5000);

    moduleId_.clear();
}

bool PipeWireEchoCancel::isActive() const
{
    return !moduleId_.isEmpty();
}

QString PipeWireEchoCancel::sourceName() const
{
    return sourceName_;
}

QString PipeWireEchoCancel::sinkName() const
{
    return sinkName_;
}

QString PipeWireEchoCancel::errorString() const
{
    return errorString_;
}
