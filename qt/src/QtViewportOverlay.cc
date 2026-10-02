/*
   Copyright (c) 2015-2023 hkrn All rights reserved

   This file is part of emapp component and it's licensed under Mozilla Public License. see LICENSE.md for more details.
 */

#include "QtViewportOverlay.h"
#include "QtApplicationService.h"

#include "emapp/IProjectHolder.h"
#include "emapp/Project.h"

#include "bx/string.h"

#include <QFile>
#include <QFontMetrics>
#include <QPaintEvent>
#include <QPainter>
#include <QTimer>

#include <chrono>
#include <ctime>

namespace nanoem {
namespace qt {
namespace {

/* matches ImGuiWindow::kFontSize */
const int kFontSize = 16;
/* matches ImGuiWindow::drawPerformanceMonitor */
const nanoem_f32_t kSpacingSize = 10, kMarginSize = 5;
const qreal kMonitorTextWidth = 115;
/* matches ImGuiWindow::internalFillRect */
const QColor kFillColor(0, 0, 0, 166); /* Vector4(0, 0, 0, 0.65) */
const QColor kStrokeColor(128, 128, 128); /* Vector4(0.5, 0.5, 0.5, 1) */

static double
steadySeconds()
{
    using namespace std::chrono;
    return duration_cast<duration<double>>(steady_clock::now().time_since_epoch()).count();
}

static double
processCPUTimeSeconds()
{
#if defined(Q_OS_LINUX)
    timespec ts;
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts);
    return static_cast<double>(ts.tv_sec) + static_cast<double>(ts.tv_nsec) * 1e-9;
#else
    return static_cast<double>(std::clock()) / CLOCKS_PER_SEC;
#endif
}

static nanoem_u64_t
residentMemoryBytes()
{
#if defined(Q_OS_LINUX)
    QFile file(QStringLiteral("/proc/self/status"));
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QByteArray content(file.readAll());
        const int index = content.indexOf("VmRSS:");
        if (index >= 0) {
            const QByteArray tail(content.mid(index + 6).trimmed());
            bool ok = false;
            const qint64 kB = QByteArray(tail.split(' ').value(0)).toLongLong(&ok);
            if (ok) {
                return static_cast<nanoem_u64_t>(kB) * 1024;
            }
        }
    }
#endif
    return 0;
}

static void
fillBox(QPainter &painter, const QRectF &rect)
{
    painter.setPen(QPen(kStrokeColor, 1.0));
    painter.setBrush(kFillColor);
    painter.drawRoundedRect(rect, 4.0, 4.0);
}

} /* namespace anonymous */

QtViewportOverlay::QtViewportOverlay(QtApplicationService *service, QWidget *parent)
    : QWidget(parent)
    , m_service(service)
{
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setGeometry(parent->rect());
    m_timer = new QTimer(this);
    m_timer->setInterval(200);
    connect(m_timer, &QTimer::timeout, this, [this]() { refresh(); });
    m_timer->start();
    refresh();
}

void
QtViewportOverlay::refresh()
{
    samplePerformanceCounters();
    update();
}

void
QtViewportOverlay::samplePerformanceCounters()
{
    const double wall = steadySeconds(), cpu = processCPUTimeSeconds();
    if (m_lastWallSeconds > 0 && wall > m_lastWallSeconds) {
        const double ratio = (cpu - m_lastCPUTimeSeconds) / (wall - m_lastWallSeconds);
        m_cpuPercentage = static_cast<nanoem_f32_t>(ratio * 100.0);
    }
    m_lastWallSeconds = wall;
    m_lastCPUTimeSeconds = cpu;
    m_memoryBytes = residentMemoryBytes();
}

void
QtViewportOverlay::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    Project *project = m_service->projectHolder()->currentProject();
    if (!project) {
        return;
    }
    const Vector4UI16 layout(project->logicalScaleUniformedViewportLayoutRect());
    if (layout.z == 0 || layout.w == 0) {
        return;
    }
    const QRectF rect(layout.x, layout.y, layout.z, layout.w);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    QFont font(painter.font());
    font.setPixelSize(kFontSize);
    painter.setFont(font);
    const QFontMetricsF metrics(font);
    const qreal lineHeight = metrics.height();
    if (project->isPerformanceMonitorEnabled()) {
        char memoryBuffer[32];
        bx::prettify(memoryBuffer, sizeof(memoryBuffer), m_memoryBytes, bx::Units::Kilo);
        const QRectF box(rect.left() + kSpacingSize, rect.top() + kSpacingSize,
            kMarginSize + kMonitorTextWidth + kMarginSize, lineHeight * 2 + kMarginSize * 2);
        fillBox(painter, box);
        painter.setPen(Qt::white);
        /* unclipped, like the ImGui draw list: long MEM values spill past the box edge */
        const qreal firstLine = box.top() + kMarginSize + metrics.ascent(),
                   secondLine = firstLine + metrics.lineSpacing();
        const QPointF origin(box.left() + kSpacingSize, 0);
        painter.drawText(QPointF(origin.x(), firstLine),
            QString::asprintf("CPU: %.2f%%", static_cast<double>(m_cpuPercentage)));
        painter.drawText(QPointF(origin.x(), secondLine),
            QStringLiteral("MEM: ") + QString::fromLatin1(memoryBuffer));
    }
    if (project->isFPSCounterEnabled()) {
        const nanoem_u32_t actualFPS = project->actualFPS();
        const QString text =
            (actualFPS > 0 && !project->isPowerSavingEnabled())
            ? QString::asprintf("%u FPS", actualFPS)
            : QStringLiteral("-- FPS");
        /* mirrors Project::internalQueryRectangle(kRectangleActualFPS): relative to the layout
           origin the badge sits at the bottom-left corner of the viewport */
        const QRectF badge(rect.left() + kSpacingSize, rect.bottom() - 35, 60, 25);
        fillBox(painter, badge);
        painter.setPen(Qt::white);
        painter.drawText(badge, Qt::AlignCenter, text);
    }
}

} /* namespace qt */
} /* namespace nanoem */
