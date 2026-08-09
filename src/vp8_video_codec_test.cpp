#include "vp8_video_codec.h"

#include <QGuiApplication>
#include <QDebug>
#include <QImage>
#include <QPainter>

int main(
    int argc,
    char *argv[])
{
    QGuiApplication application(
        argc,
        argv);

    Vp8VideoCodec codec;

    quint64 totalEncodedBytes = 0;

    for (int index = 0; index < 30; ++index) {
        QImage source(
            1280,
            720,
            QImage::Format_RGBA8888);

        source.fill(
            QColor(
                24,
                28,
                36));

        QPainter painter(&source);

        painter.setPen(
            Qt::white);

        painter.setBrush(
            QColor(
                70,
                150,
                240));

        painter.drawRect(
            40 + index * 25,
            260,
            180,
            180);

        painter.drawText(
            40,
            80,
            QStringLiteral(
                "ScottiBYTE Assist VP8 frame %1")
                .arg(index));

        painter.end();

        QByteArray encoded;

        if (!codec.encodeFrame(
                source,
                encoded)) {
            qCritical().noquote()
                << "Encode failed:"
                << codec.lastError();

            return 1;
        }

        QImage decoded;

        if (!codec.decodeFrame(
                encoded,
                decoded)) {
            qCritical().noquote()
                << "Decode failed:"
                << codec.lastError();

            return 1;
        }

        if (
            decoded.size() !=
            source.size()
        ) {
            qCritical()
                << "Decoded size mismatch:"
                << decoded.size()
                << source.size();

            return 1;
        }

        totalEncodedBytes +=
            static_cast<quint64>(
                encoded.size());

        qInfo()
            << "Frame"
            << index
            << "encoded bytes"
            << encoded.size();
    }

    qInfo()
        << "VP8 round-trip PASS"
        << "frames"
        << 30
        << "total bytes"
        << totalEncodedBytes
        << "average bytes"
        << totalEncodedBytes / 30;

    return 0;
}
