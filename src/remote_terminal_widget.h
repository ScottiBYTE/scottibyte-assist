#pragma once

#include <QByteArray>
#include <QPoint>
#include <QWidget>

#include <vterm.h>

class RemoteTerminalWidget final : public QWidget
{
    Q_OBJECT

public:
    explicit RemoteTerminalWidget(
        QWidget *parent = nullptr);

    ~RemoteTerminalWidget() override;

    void feedData(
        const QByteArray &data);

    void resetTerminal();

    QSize terminalSizeCells() const;

signals:
    void terminalInputReady(
        const QByteArray &data);

    void terminalResizeRequested(
        int columns,
        int rows);

protected:
    void paintEvent(
        QPaintEvent *event) override;

    void keyPressEvent(
        QKeyEvent *event) override;

    void resizeEvent(
        QResizeEvent *event) override;

    void focusInEvent(
        QFocusEvent *event) override;

    void focusOutEvent(
        QFocusEvent *event) override;

private:
    void updateTerminalSize();

    void adjustFontSize(
        int deltaPixels);

    void resetFontSize();

    static int damageCallback(
        VTermRect rect,
        void *user);

    static int moveRectCallback(
        VTermRect dest,
        VTermRect src,
        void *user);

    static int moveCursorCallback(
        VTermPos pos,
        VTermPos oldPos,
        int visible,
        void *user);

    static int setTermPropCallback(
        VTermProp prop,
        VTermValue *value,
        void *user);

    static int bellCallback(
        void *user);

    static int resizeCallback(
        int rows,
        int cols,
        void *user);

    static int sbPushlineCallback(
        int cols,
        const VTermScreenCell *cells,
        void *user);

    static int sbPoplineCallback(
        int cols,
        VTermScreenCell *cells,
        void *user);

    VTerm *vterm_ = nullptr;
    VTermScreen *screen_ = nullptr;

    int columns_ = 80;
    int rows_ = 24;

    int cellWidth_ = 10;
    int cellHeight_ = 20;

    int defaultFontPixelSize_ = 0;

    QPoint cursorCell_;
    bool cursorVisible_ = true;
    bool hasTerminalFocus_ = false;
};
