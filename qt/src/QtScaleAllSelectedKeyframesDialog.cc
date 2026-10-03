/*
   Copyright (c) 2015-2023 hkrn All rights reserved

   This file is part of emapp component and it's licensed under Mozilla Public License. see LICENSE.md for more details.
 */

#include "QtScaleAllSelectedKeyframesDialog.h"
#include "QtApplicationService.h"

#include "emapp/CommandRegistrator.h"
#include "emapp/Error.h"
#include "emapp/IEventPublisher.h"
#include "emapp/ITranslator.h"
#include "emapp/Project.h"

#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

namespace nanoem {
namespace qt {

/* ImGuiWindow::drawKeyframeActionPanel edits ImGuiWindow::kFrameIndexDataType inputs; the Qt
   timeline uses the same 6 digit bound, so the read only from/to fields follow it */
static const int kFrameIndexMaximum = 999999;

QtScaleAllSelectedKeyframesDialog::QtScaleAllSelectedKeyframesDialog(
    QtApplicationService *service, Project *project, QWidget *parent)
    : QDialog(parent, Qt::Dialog | Qt::Window)
    , m_service(service)
    , m_project(project)
    , m_scaleFactor(1.0f)
{
    setWindowTitle(translate("nanoem.gui.keyframe.scale"));
    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    m_fromSpinBox = new QSpinBox(this);
    m_fromSpinBox->setRange(0, kFrameIndexMaximum);
    m_fromSpinBox->setReadOnly(true);
    m_toSpinBox = new QSpinBox(this);
    m_toSpinBox->setRange(0, kFrameIndexMaximum);
    m_toSpinBox->setReadOnly(true);
    auto *rangeRow = new QHBoxLayout;
    rangeRow->addWidget(m_fromSpinBox, 1);
    rangeRow->addWidget(m_toSpinBox, 1);
    auto *rangeWidget = new QWidget(this);
    rangeWidget->setLayout(rangeRow);
    form->addRow(QString::fromUtf8("Target Range"), rangeWidget);
    auto *scaleSpinBox = new QDoubleSpinBox(this);
    scaleSpinBox->setRange(0.1, 10.0);
    scaleSpinBox->setDecimals(1);
    scaleSpinBox->setSingleStep(0.1);
    scaleSpinBox->setValue(m_scaleFactor);
    form->addRow(QString::fromUtf8("Scale Factor"), scaleSpinBox);
    layout->addLayout(form);
    auto *note = new QLabel(
        QString::fromUtf8("* Camera keyframe cannot be scaled due to scene change by adjacent keyframe"), this);
    note->setWordWrap(true);
    layout->addWidget(note);
    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QtScaleAllSelectedKeyframesDialog::handleAccepted);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QtScaleAllSelectedKeyframesDialog::reject);
    layout->addWidget(buttonBox);
    connect(scaleSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
        [this](double value) { m_scaleFactor = nanoem_f32_t(value); });
    /* the ImGui dialog re-read project->selectionSegment() every frame; keep the read only range
       in sync the same way the other native dialogs poll their close condition */
    auto *timer = new QTimer(this);
    timer->setInterval(250);
    connect(timer, &QTimer::timeout, this, &QtScaleAllSelectedKeyframesDialog::refresh);
    timer->start();
    refresh();
}

void
QtScaleAllSelectedKeyframesDialog::refresh()
{
    const TimelineSegment segment(m_project->selectionSegment());
    {
        const QSignalBlocker blocker(m_fromSpinBox);
        m_fromSpinBox->setValue(int(qMin(segment.m_from, nanoem_frame_index_t(kFrameIndexMaximum))));
    }
    {
        const QSignalBlocker blocker(m_toSpinBox);
        m_toSpinBox->setValue(int(qMin(segment.m_to, nanoem_frame_index_t(kFrameIndexMaximum))));
    }
}

void
QtScaleAllSelectedKeyframesDialog::handleAccepted()
{
    Error error;
    CommandRegistrator registrator(m_project);
    registrator.registerScaleAllMotionKeyframesInCommand(
        m_project->selectionSegment(), m_scaleFactor, NANOEM_MUTABLE_MOTION_KEYFRAME_TYPE_ALL, error);
    error.notify(m_project->eventPublisher());
    accept();
}

QString
QtScaleAllSelectedKeyframesDialog::translate(const char *key) const
{
    return QString::fromUtf8(m_service->translator()->translate(key));
}

} /* namespace qt */
} /* namespace nanoem */
