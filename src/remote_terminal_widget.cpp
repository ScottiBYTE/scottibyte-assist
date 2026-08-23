#include "remote_terminal_widget.h"

#include <QApplication>
#include <QClipboard>
#include <QFocusEvent>
#include <QFontDatabase>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QTimer>
#include <QResizeEvent>

namespace
{
QByteArray outputBuffer;

void vtermOutputCallback(
    const char *s,
    size_t len,
    void *user)
{
    auto *widget =
        static_cast<RemoteTerminalWidget *>(user);

    if (widget == nullptr ||
        s == nullptr ||
        len == 0) {
        return;
    }

    emit widget->terminalInputReady(
        QByteArray(
            s,
            static_cast<qsizetype>(len)));
}
}

RemoteTerminalWidget::RemoteTerminalWidget(
    QWidget *parent)
    : QWidget(parent)
{
    setFocusPolicy(
        Qt::StrongFocus);

    QFont font =
        QFontDatabase::systemFont(
            QFontDatabase::FixedFont);

    int initialPixelSize =
        font.pixelSize();

    if (initialPixelSize <= 0) {
        initialPixelSize =
            QFontMetrics(font).height();
    }

    initialPixelSize += 2;

    font.setPixelSize(
        initialPixelSize);

    setFont(font);

    defaultFontPixelSize_ =
        initialPixelSize;

    QFontMetrics metrics(font);

    cellWidth_ =
        qMax(
            1,
            metrics.horizontalAdvance(
                QLatin1Char('M')));

    cellHeight_ =
        qMax(
            1,
            metrics.height());

    vterm_ =
        vterm_new(
            rows_,
            columns_);

    vterm_set_utf8(
        vterm_,
        1);

    vterm_output_set_callback(
        vterm_,
        vtermOutputCallback,
        this);

    screen_ =
        vterm_obtain_screen(
            vterm_);

    static const VTermScreenCallbacks callbacks = {
        &RemoteTerminalWidget::damageCallback,
        &RemoteTerminalWidget::moveRectCallback,
        &RemoteTerminalWidget::moveCursorCallback,
        &RemoteTerminalWidget::setTermPropCallback,
        &RemoteTerminalWidget::bellCallback,
        &RemoteTerminalWidget::resizeCallback,
        &RemoteTerminalWidget::sbPushlineCallback,
        &RemoteTerminalWidget::sbPoplineCallback
    };

    vterm_screen_set_callbacks(
        screen_,
        &callbacks,
        this);

    vterm_screen_enable_altscreen(
        screen_,
        1);

    vterm_screen_reset(
        screen_,
        1);

    setMinimumSize(
        cellWidth_ * 40,
        cellHeight_ * 12);

    cursorBlinkTimer_ =
        new QTimer(this);

    cursorBlinkTimer_->setInterval(
        500);

    connect(
        cursorBlinkTimer_,
        &QTimer::timeout,
        this,
        [this]() {
            cursorBlinkOn_ =
                !cursorBlinkOn_;

            update();
        });

    cursorBlinkTimer_->start();
}

RemoteTerminalWidget::~RemoteTerminalWidget()
{
    if (vterm_ != nullptr) {
        vterm_free(
            vterm_);

        vterm_ = nullptr;
        screen_ = nullptr;
    }
}

void RemoteTerminalWidget::feedData(
    const QByteArray &data)
{
    if (vterm_ == nullptr ||
        data.isEmpty()) {
        return;
    }

    vterm_input_write(
        vterm_,
        data.constData(),
        static_cast<size_t>(
            data.size()));

    vterm_screen_flush_damage(
        screen_);

    VTermState *state =
        vterm_obtain_state(
            vterm_);

    if (state != nullptr) {
        VTermPos pos = {};

        vterm_state_get_cursorpos(
            state,
            &pos);

        cursorCell_ =
            QPoint(
                pos.col,
                pos.row);
    }

    cursorBlinkOn_ = true;

    if (cursorBlinkTimer_ != nullptr) {
        cursorBlinkTimer_->start();
    }

    update();
}

void RemoteTerminalWidget::resetTerminal()
{
    if (screen_ == nullptr) {
        return;
    }

    vterm_screen_reset(
        screen_,
        1);

    update();
}

QSize RemoteTerminalWidget::terminalSizeCells() const
{
    return QSize(
        columns_,
        rows_);
}

void RemoteTerminalWidget::paintEvent(
    QPaintEvent *)
{
    if (screen_ == nullptr) {
        return;
    }

    QPainter painter(this);

    painter.fillRect(
        rect(),
        Qt::black);

    painter.setFont(
        font());

    const QFontMetrics metrics(
        font());

    for (int row = 0;
         row < rows_;
         ++row) {
        for (int col = 0;
             col < columns_;
             ++col) {
            VTermPos pos = {
                row,
                col
            };

            VTermScreenCell cell = {};

            if (!vterm_screen_get_cell(
                    screen_,
                    pos,
                    &cell)) {
                continue;
            }

            if (cell.chars[0] == 0) {
                continue;
            }

            QString text;

            for (int i = 0;
                 i < VTERM_MAX_CHARS_PER_CELL &&
                 cell.chars[i] != 0;
                 ++i) {
                text.append(
                    QChar::fromUcs4(
                        cell.chars[i]));
            }

            const QRect cellRect(
                col * cellWidth_,
                row * cellHeight_,
                cellWidth_,
                cellHeight_);

            QColor foreground =
                Qt::white;

            QColor background =
                Qt::black;

            if (cell.attrs.reverse) {
                foreground =
                    Qt::black;

                background =
                    Qt::white;
            }

            if (cellIsSelected(
                    col,
                    row)) {
                foreground =
                    Qt::black;

                background =
                    Qt::white;
            }

            painter.fillRect(
                cellRect,
                background);

            painter.setPen(
                foreground);

            const uint codepoint =
                cell.chars[0];

            if (
                codepoint >= 0xE0B0 &&
                codepoint <= 0xE0B3
            ) {
                const QRect cellRect(
                    col * cellWidth_,
                    row * cellHeight_,
                    cellWidth_,
                    cellHeight_);

                QPolygon polygon;

                if (codepoint == 0xE0B0 ||
                    codepoint == 0xE0B1) {
                    polygon
                        << QPoint(
                            cellRect.left(),
                            cellRect.top())
                        << QPoint(
                            cellRect.right(),
                            cellRect.center().y())
                        << QPoint(
                            cellRect.left(),
                            cellRect.bottom());
                } else {
                    polygon
                        << QPoint(
                            cellRect.right(),
                            cellRect.top())
                        << QPoint(
                            cellRect.left(),
                            cellRect.center().y())
                        << QPoint(
                            cellRect.right(),
                            cellRect.bottom());
                }

                painter.drawPolygon(
                    polygon);

                continue;
            }
            painter.drawText(
                cellRect.left(),
                cellRect.top() +
                    metrics.ascent(),
                text);
        }
    }

    if (
        cursorVisible_ &&
        cursorBlinkOn_
    ) {
        const QRect cursorRect(
            cursorCell_.x() *
                cellWidth_,
            cursorCell_.y() *
                cellHeight_,
            cellWidth_,
            cellHeight_);

        if (hasTerminalFocus_) {
            painter.fillRect(
                cursorRect,
                Qt::white);
        } else {
            painter.setPen(
                Qt::white);

            painter.drawRect(
                cursorRect.adjusted(
                    0,
                    0,
                    -1,
                    -1));
        }
    }
}

void RemoteTerminalWidget::keyPressEvent(
    QKeyEvent *event)
{
    const Qt::KeyboardModifiers modifiers =
        event->modifiers();

    if (
        modifiers.testFlag(Qt::ControlModifier) &&
        modifiers.testFlag(Qt::ShiftModifier) &&
        event->key() == Qt::Key_C
    ) {
        if (hasSelection_) {
            QApplication::clipboard()->setText(
                selectedText());
        }

        return;
    }

    if (
        modifiers.testFlag(Qt::ControlModifier) &&
        modifiers.testFlag(Qt::ShiftModifier) &&
        event->key() == Qt::Key_V
    ) {
        const QString text =
            QApplication::clipboard()->text();

        if (!text.isEmpty()) {
            emit terminalInputReady(
                text.toUtf8());
        }

        return;
    }

    if (
        modifiers.testFlag(Qt::ControlModifier) &&
        modifiers.testFlag(Qt::ShiftModifier) &&
        (
            event->key() == Qt::Key_Plus ||
            event->key() == Qt::Key_Equal
        )
    ) {
        adjustFontSize(1);
        return;
    }

    if (
        modifiers.testFlag(Qt::ControlModifier) &&
        (
            event->key() == Qt::Key_Minus ||
            (
                modifiers.testFlag(Qt::ShiftModifier) &&
                event->key() == Qt::Key_Underscore
            )
        )
    ) {
        adjustFontSize(-1);
        return;
    }

    if (
        modifiers.testFlag(Qt::ControlModifier) &&
        event->key() == Qt::Key_0
    ) {
        resetFontSize();
        return;
    }

    if (vterm_ == nullptr) {
        return;
    }

#ifdef Q_OS_WIN
    if (event->key() == Qt::Key_Backspace) {
        emit terminalInputReady(
            QByteArray(
                1,
                static_cast<char>(0x7f)));

        return;
    }
#endif

    const QString text =
        event->text();

    if (!text.isEmpty() &&
        !event->modifiers().testFlag(
            Qt::ControlModifier)) {
        const QByteArray utf8 =
            text.toUtf8();

        vterm_keyboard_unichar(
            vterm_,
            utf8.isEmpty()
                ? 0
                : text.at(0).unicode(),
            VTERM_MOD_NONE);

        return;
    }

    if (event->modifiers().testFlag(
            Qt::ControlModifier) &&
        event->key() >= Qt::Key_A &&
        event->key() <= Qt::Key_Z) {
        const char control =
            static_cast<char>(
                event->key() -
                Qt::Key_A +
                1);

        emit terminalInputReady(
            QByteArray(
                1,
                control));

        return;
    }

    VTermKey key =
        VTERM_KEY_NONE;

    switch (event->key()) {
    case Qt::Key_Up:
        key = VTERM_KEY_UP;
        break;

    case Qt::Key_Down:
        key = VTERM_KEY_DOWN;
        break;

    case Qt::Key_Left:
        key = VTERM_KEY_LEFT;
        break;

    case Qt::Key_Right:
        key = VTERM_KEY_RIGHT;
        break;

    case Qt::Key_Home:
        key = VTERM_KEY_HOME;
        break;

    case Qt::Key_End:
        key = VTERM_KEY_END;
        break;

    case Qt::Key_PageUp:
        key = VTERM_KEY_PAGEUP;
        break;

    case Qt::Key_PageDown:
        key = VTERM_KEY_PAGEDOWN;
        break;

    case Qt::Key_Backspace:
        key = VTERM_KEY_BACKSPACE;
        break;

    case Qt::Key_Delete:
        key = VTERM_KEY_DEL;
        break;

    case Qt::Key_Insert:
        key = VTERM_KEY_INS;
        break;

    case Qt::Key_Enter:
    case Qt::Key_Return:
        key = VTERM_KEY_ENTER;
        break;

    case Qt::Key_Tab:
        key = VTERM_KEY_TAB;
        break;

    case Qt::Key_Escape:
        key = VTERM_KEY_ESCAPE;
        break;

    default:
        QWidget::keyPressEvent(
            event);

        return;
    }

    vterm_keyboard_key(
        vterm_,
        key,
        VTERM_MOD_NONE);
}

void RemoteTerminalWidget::resizeEvent(
    QResizeEvent *event)
{
    QWidget::resizeEvent(
        event);

    updateTerminalSize();
}

void RemoteTerminalWidget::focusInEvent(
    QFocusEvent *event)
{
    hasTerminalFocus_ = true;

    QWidget::focusInEvent(
        event);

    update();
}

void RemoteTerminalWidget::focusOutEvent(
    QFocusEvent *event)
{
    hasTerminalFocus_ = false;

    QWidget::focusOutEvent(
        event);

    update();
}

void RemoteTerminalWidget::adjustFontSize(
    int deltaPixels)
{
    QFont currentFont =
        font();

    int currentPixelSize =
        currentFont.pixelSize();

    if (currentPixelSize <= 0) {
        currentPixelSize =
            QFontMetrics(
                currentFont)
                .height();
    }

    const int newPixelSize =
        qBound(
            8,
            currentPixelSize +
                deltaPixels,
            36);

    currentFont.setPixelSize(
        newPixelSize);

    setFont(
        currentFont);

    QFontMetrics metrics(
        currentFont);

    cellWidth_ =
        qMax(
            1,
            metrics.horizontalAdvance(
                QLatin1Char('M')));

    cellHeight_ =
        qMax(
            1,
            metrics.height());

    updateTerminalSize();
    update();
}

void RemoteTerminalWidget::resetFontSize()
{
    if (defaultFontPixelSize_ <= 0) {
        return;
    }

    QFont currentFont =
        font();

    currentFont.setPixelSize(
        defaultFontPixelSize_);

    setFont(
        currentFont);

    QFontMetrics metrics(
        currentFont);

    cellWidth_ =
        qMax(
            1,
            metrics.horizontalAdvance(
                QLatin1Char('M')));

    cellHeight_ =
        qMax(
            1,
            metrics.height());

    updateTerminalSize();
    update();
}

void RemoteTerminalWidget::updateTerminalSize()
{
    if (vterm_ == nullptr) {
        return;
    }

    const int newColumns =
        qMax(
            1,
            width() /
                cellWidth_);

    const int newRows =
        qMax(
            1,
            height() /
                cellHeight_);

    if (newColumns == columns_ &&
        newRows == rows_) {
        return;
    }

    columns_ =
        newColumns;

    rows_ =
        newRows;

    vterm_set_size(
        vterm_,
        rows_,
        columns_);

    emit terminalResizeRequested(
        columns_,
        rows_);

    update();
}

void RemoteTerminalWidget::mousePressEvent(
    QMouseEvent *event)
{
    if (
        event->button() !=
            Qt::LeftButton
    ) {
        QWidget::mousePressEvent(
            event);
        return;
    }

    setFocus(
        Qt::MouseFocusReason);

    selectionStart_ =
        cellFromPosition(
            event->position().toPoint());

    selectionEnd_ =
        selectionStart_;

    selecting_ = true;
    hasSelection_ = true;

    update();
}

void RemoteTerminalWidget::mouseMoveEvent(
    QMouseEvent *event)
{
    if (!selecting_) {
        QWidget::mouseMoveEvent(
            event);
        return;
    }

    selectionEnd_ =
        cellFromPosition(
            event->position().toPoint());

    update();
}

void RemoteTerminalWidget::mouseReleaseEvent(
    QMouseEvent *event)
{
    if (
        event->button() ==
            Qt::LeftButton &&
        selecting_
    ) {
        selectionEnd_ =
            cellFromPosition(
                event->position().toPoint());

        selecting_ = false;

        update();
        return;
    }

    QWidget::mouseReleaseEvent(
        event);
}

QPoint RemoteTerminalWidget::cellFromPosition(
    const QPoint &position) const
{
    const int column =
        qBound(
            0,
            position.x() /
                qMax(1, cellWidth_),
            qMax(0, columns_ - 1));

    const int row =
        qBound(
            0,
            position.y() /
                qMax(1, cellHeight_),
            qMax(0, rows_ - 1));

    return QPoint(
        column,
        row);
}

bool RemoteTerminalWidget::cellIsSelected(
    int column,
    int row) const
{
    if (!hasSelection_) {
        return false;
    }

    QPoint start =
        selectionStart_;

    QPoint end =
        selectionEnd_;

    if (
        start.y() > end.y() ||
        (
            start.y() == end.y() &&
            start.x() > end.x()
        )
    ) {
        qSwap(
            start,
            end);
    }

    if (
        row < start.y() ||
        row > end.y()
    ) {
        return false;
    }

    if (
        start.y() == end.y()
    ) {
        return
            row == start.y() &&
            column >= start.x() &&
            column <= end.x();
    }

    if (row == start.y()) {
        return column >= start.x();
    }

    if (row == end.y()) {
        return column <= end.x();
    }

    return true;
}

QString RemoteTerminalWidget::selectedText() const
{
    if (
        !hasSelection_ ||
        screen_ == nullptr
    ) {
        return {};
    }

    QPoint start =
        selectionStart_;

    QPoint end =
        selectionEnd_;

    if (
        start.y() > end.y() ||
        (
            start.y() == end.y() &&
            start.x() > end.x()
        )
    ) {
        qSwap(
            start,
            end);
    }

    QString result;

    for (
        int row = start.y();
        row <= end.y();
        ++row
    ) {
        const int firstColumn =
            row == start.y()
                ? start.x()
                : 0;

        const int lastColumn =
            row == end.y()
                ? end.x()
                : columns_ - 1;

        QString line;

        for (
            int column = firstColumn;
            column <= lastColumn;
            ++column
        ) {
            VTermPos pos = {
                row,
                column
            };

            VTermScreenCell cell = {};

            if (!vterm_screen_get_cell(
                    screen_,
                    pos,
                    &cell)) {
                line.append(
                    QLatin1Char(' '));
                continue;
            }

            if (cell.chars[0] == 0) {
                line.append(
                    QLatin1Char(' '));
                continue;
            }

            for (
                int i = 0;
                i < VTERM_MAX_CHARS_PER_CELL &&
                cell.chars[i] != 0;
                ++i
            ) {
                line.append(
                    QChar::fromUcs4(
                        cell.chars[i]));
            }
        }

        while (
            line.endsWith(
                QLatin1Char(' '))
        ) {
            line.chop(1);
        }

        result.append(line);

        if (row != end.y()) {
            result.append(
                QLatin1Char('\n'));
        }
    }

    return result;
}

int RemoteTerminalWidget::damageCallback(
    VTermRect,
    void *user)
{
    auto *widget =
        static_cast<RemoteTerminalWidget *>(user);

    if (widget != nullptr) {
        widget->update();
    }

    return 1;
}

int RemoteTerminalWidget::moveRectCallback(
    VTermRect,
    VTermRect,
    void *user)
{
    auto *widget =
        static_cast<RemoteTerminalWidget *>(user);

    if (widget != nullptr) {
        widget->update();
    }

    return 1;
}

int RemoteTerminalWidget::moveCursorCallback(
    VTermPos pos,
    VTermPos,
    int visible,
    void *user)
{
    auto *widget =
        static_cast<RemoteTerminalWidget *>(user);

    if (widget != nullptr) {
        widget->cursorCell_ =
            QPoint(
                pos.col,
                pos.row);

        Q_UNUSED(visible);

        widget->cursorBlinkOn_ = true;

        if (widget->cursorBlinkTimer_ != nullptr) {
            widget->cursorBlinkTimer_->start();
        }

        widget->update();
    }

    return 1;
}

int RemoteTerminalWidget::setTermPropCallback(
    VTermProp prop,
    VTermValue *value,
    void *user)
{
    auto *widget =
        static_cast<RemoteTerminalWidget *>(
            user);

    if (
        widget == nullptr ||
        value == nullptr
    ) {
        return 1;
    }

    if (prop == VTERM_PROP_CURSORVISIBLE) {
        widget->cursorVisible_ =
            value->boolean != 0;

        widget->cursorBlinkOn_ = true;

        if (
            widget->cursorBlinkTimer_ !=
            nullptr
        ) {
            widget->cursorBlinkTimer_->
                start();
        }

        widget->update();
    }

    return 1;
}

int RemoteTerminalWidget::bellCallback(
    void *)
{
    return 1;
}

int RemoteTerminalWidget::resizeCallback(
    int,
    int,
    void *)
{
    return 1;
}

int RemoteTerminalWidget::sbPushlineCallback(
    int,
    const VTermScreenCell *,
    void *)
{
    return 1;
}

int RemoteTerminalWidget::sbPoplineCallback(
    int,
    VTermScreenCell *,
    void *)
{
    return 0;
}
