/*
   Copyright (c) 2015-2023 hkrn All rights reserved

   This file is part of emapp component and it's licensed under Mozilla Public License. see LICENSE.md for more details.
 */

#pragma once
#ifndef NANOEM_EMAPP_QT_QTINTERPOLATIONCURVEWIDGET_H_
#define NANOEM_EMAPP_QT_QTINTERPOLATIONCURVEWIDGET_H_

#include "emapp/Forward.h"

#include <QPointF>
#include <QWidget>

#include <functional>

class QMouseEvent;
class QPaintEvent;

namespace nanoem {
namespace qt {

/**
 * @brief MMD style bezier curve editor (the editable curve line instead of the four number fields).
 *
 *        Paints the 8x8 grid, the interpolation curve with its two control point handles (drag them
 *        with the mouse) and optionally the current frame position marker like
 *        BaseKeyframeInterpolationCurveGraphDialog. The four u8 control point components live in
 *        [0, 127] (the same domain as the ImGui dialog) and are handed to the owner callback which
 *        writes them through without undo, exactly like the ImGui panel and dialog do.
 */
class QtInterpolationCurveWidget final : public QWidget {
public:
    typedef std::function<void(const Vector4U8 &)> ControlPointCallback;

    explicit QtInterpolationCurveWidget(QWidget *parent);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

    const Vector4U8 &controlPoint() const;
    void setControlPoint(const Vector4U8 &value);
    void setPlayhead(nanoem_f32_t x, nanoem_f32_t y, bool visible);
    void setControlPointCallback(const ControlPointCallback &value);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    QRectF graphRect() const;
    QPointF toPosition(nanoem_u8_t x, nanoem_u8_t y) const;
    Vector2U8 toControlPoint(const QPointF &pos) const;
    int hitTest(const QPointF &pos) const;
    void moveDraggedPoint(const QPointF &pos);

    Vector4U8 m_controlPoint;
    nanoem_f32_t m_playheadX = 0;
    nanoem_f32_t m_playheadY = 0;
    bool m_playheadVisible = false;
    int m_dragging = -1;
    ControlPointCallback m_callback;
};

} /* namespace qt */
} /* namespace nanoem */

#endif /* NANOEM_EMAPP_QT_QTINTERPOLATIONCURVEWIDGET_H_ */
