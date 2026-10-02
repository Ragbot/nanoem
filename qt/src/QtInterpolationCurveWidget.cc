/*
   Copyright (c) 2015-2023 hkrn All rights reserved

   This file is part of emapp component and it's licensed under Mozilla Public License. see LICENSE.md for more details.
 */

#include "QtInterpolationCurveWidget.h"

#include <QEvent>
#include <QLineF>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>

namespace nanoem {
namespace qt {
namespace {

/* matches BaseKeyframeInterpolationCurveGraphDialog::kInterpolationMaxFloatValue */
const qreal kInterpolationMaxValue = 127.0;
const int kGridCount = 8;
const qreal kHandleRadius = 7.5;
const qreal kHitRadius = 11.0;

/* matches ImGuiWindow::kColorFrameBg/kColorBorder/kColorInterpolationCurve* colors */
const QColor kFrameBg(60, 60, 60);
const QColor kBorderColor(63, 63, 70);
const QColor kCurveLineColor(255, 127, 0);
const QColor kControlLineColor(0, 255, 0, 127);
const QColor kControlPointColor(255, 0, 0);
const QColor kDisabledOverlayColor(128, 128, 128, 140);

} /* namespace anonymous */

QtInterpolationCurveWidget::QtInterpolationCurveWidget(QWidget *parent)
    : QWidget(parent)
{
    m_controlPoint = Vector4U8(20, 20, 107, 107);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

QSize
QtInterpolationCurveWidget::sizeHint() const
{
    /* keeps the interpolation panel within the baseline strip size so the dock height (viewport)
       and the downstream panel widths stay unchanged; the dialog overrides this with 256x256 */
    return QSize(176, 116);
}

QSize
QtInterpolationCurveWidget::minimumSizeHint() const
{
    return QSize(0, 0);
}

const Vector4U8 &
QtInterpolationCurveWidget::controlPoint() const
{
    return m_controlPoint;
}

void
QtInterpolationCurveWidget::setControlPoint(const Vector4U8 &value)
{
    /* do not fight an active drag; while disabled the drag is stale so refresh anyway */
    if (m_dragging < 0 || !isEnabled()) {
        m_controlPoint = value;
        update();
    }
}

void
QtInterpolationCurveWidget::setPlayhead(nanoem_f32_t x, nanoem_f32_t y, bool visible)
{
    m_playheadX = x;
    m_playheadY = y;
    m_playheadVisible = visible;
    update();
}

void
QtInterpolationCurveWidget::setControlPointCallback(const ControlPointCallback &value)
{
    m_callback = value;
}

QRectF
QtInterpolationCurveWidget::graphRect() const
{
    return QRectF(rect()).adjusted(1, 1, -1, -1);
}

QPointF
QtInterpolationCurveWidget::toPosition(nanoem_u8_t x, nanoem_u8_t y) const
{
    const QRectF r(graphRect());
    return QPointF(r.left() + r.width() * (x / kInterpolationMaxValue),
        r.bottom() - r.height() * (y / kInterpolationMaxValue));
}

Vector2U8
QtInterpolationCurveWidget::toControlPoint(const QPointF &pos) const
{
    const QRectF r(graphRect());
    const qreal x = qBound(0.0, (pos.x() - r.left()) / qMax<qreal>(1.0, r.width()), 1.0);
    const qreal y = qBound(0.0, (pos.y() - r.top()) / qMax<qreal>(1.0, r.height()), 1.0);
    return Vector2U8(static_cast<nanoem_u8_t>(qRound(x * kInterpolationMaxValue)),
        static_cast<nanoem_u8_t>(qRound((1.0 - y) * kInterpolationMaxValue)));
}

int
QtInterpolationCurveWidget::hitTest(const QPointF &pos) const
{
    const QRectF r(graphRect());
    if (r.width() < 8 || r.height() < 8) {
        return -1;
    }
    const qreal d1 = QLineF(pos, toPosition(m_controlPoint.x, m_controlPoint.y)).length();
    const qreal d2 = QLineF(pos, toPosition(m_controlPoint.z, m_controlPoint.w)).length();
    int index = -1;
    qreal distance = kHitRadius;
    if (d1 <= distance) {
        index = 0;
        distance = d1;
    }
    if (d2 <= distance) {
        index = 1;
    }
    return index;
}

void
QtInterpolationCurveWidget::moveDraggedPoint(const QPointF &pos)
{
    const Vector2U8 point(toControlPoint(pos));
    Vector4U8 value(m_controlPoint);
    if (m_dragging == 0) {
        value.x = point.x;
        value.y = point.y;
    }
    else {
        value.z = point.x;
        value.w = point.y;
    }
    m_controlPoint = value;
    update();
    if (m_callback) {
        m_callback(m_controlPoint);
    }
}

void
QtInterpolationCurveWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QRectF r(graphRect());
    painter.fillRect(rect(), kFrameBg);
    if (r.width() < 8 || r.height() < 8) {
        return;
    }
    QPen gridPen(kBorderColor);
    gridPen.setWidthF(1.0);
    painter.setPen(gridPen);
    for (int i = 1; i < kGridCount; i++) {
        const qreal ratio = static_cast<qreal>(i) / kGridCount;
        painter.drawLine(QPointF(r.left() + r.width() * ratio, r.top()),
            QPointF(r.left() + r.width() * ratio, r.bottom()));
        painter.drawLine(QPointF(r.left(), r.top() + r.height() * ratio),
            QPointF(r.right(), r.top() + r.height() * ratio));
    }
    const QPointF curveFrom(r.left(), r.bottom()), curveTo(r.right(), r.top());
    const QPointF controlPointX(toPosition(m_controlPoint.x, m_controlPoint.y)),
        controlPointY(toPosition(m_controlPoint.z, m_controlPoint.w));
    QPen controlLinePen(kControlLineColor);
    controlLinePen.setWidthF(1.0);
    painter.setPen(controlLinePen);
    painter.drawLine(curveFrom, controlPointX);
    painter.drawLine(curveTo, controlPointY);
    QPainterPath path;
    path.moveTo(curveFrom);
    path.cubicTo(controlPointX, controlPointY, curveTo);
    QPen curvePen(kCurveLineColor);
    curvePen.setWidthF(2.0);
    curvePen.setCapStyle(Qt::RoundCap);
    painter.setPen(curvePen);
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(path);
    painter.setPen(Qt::NoPen);
    painter.setBrush(kControlPointColor);
    if (m_playheadVisible) {
        const QPointF marker(r.left() + r.width() * qBound(0.0, static_cast<qreal>(m_playheadX), 1.0),
            r.bottom() - r.height() * qBound(0.0, static_cast<qreal>(m_playheadY), 1.0));
        painter.drawEllipse(marker, kHandleRadius, kHandleRadius);
    }
    painter.drawEllipse(controlPointX, kHandleRadius, kHandleRadius);
    painter.drawEllipse(controlPointY, kHandleRadius, kHandleRadius);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(gridPen);
    painter.drawRect(r);
    if (!isEnabled()) {
        painter.fillRect(rect(), kDisabledOverlayColor);
    }
}

void
QtInterpolationCurveWidget::mousePressEvent(QMouseEvent *event)
{
    if (isEnabled() && event->button() == Qt::LeftButton) {
        m_dragging = hitTest(event->position());
        if (m_dragging >= 0) {
            event->accept();
            return;
        }
    }
    QWidget::mousePressEvent(event);
}

void
QtInterpolationCurveWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (m_dragging >= 0) {
        moveDraggedPoint(event->position());
        event->accept();
        return;
    }
    QWidget::mouseMoveEvent(event);
}

void
QtInterpolationCurveWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_dragging >= 0) {
        m_dragging = -1;
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void
QtInterpolationCurveWidget::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::EnabledChange && !isEnabled()) {
        m_dragging = -1;
    }
    QWidget::changeEvent(event);
}

} /* namespace qt */
} /* namespace nanoem */
