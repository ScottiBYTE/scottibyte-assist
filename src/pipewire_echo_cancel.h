#pragma once

#include <QString>

class PipeWireEchoCancel final
{
public:
    PipeWireEchoCancel() = default;
    ~PipeWireEchoCancel();

    PipeWireEchoCancel(
        const PipeWireEchoCancel &) = delete;

    PipeWireEchoCancel &operator=(
        const PipeWireEchoCancel &) = delete;

    bool start(
        const QString &inputNode,
        const QString &outputNode);

    void stop();

    bool isActive() const;

    QString sourceName() const;
    QString sinkName() const;
    QString errorString() const;

private:
    QString moduleId_;
    QString errorString_;

    const QString sourceName_ =
        QStringLiteral(
            "scottibyte_assist_aec_source");

    const QString sinkName_ =
        QStringLiteral(
            "scottibyte_assist_aec_sink");
};
