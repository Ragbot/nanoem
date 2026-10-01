/*
   Copyright (c) 2015-2023 hkrn All rights reserved

   This file is part of emapp component and it's licensed under Mozilla Public License. see LICENSE.md for more details.
 */

#include "QtPlayPanel.h"
#include "QtApplicationService.h"

#include "emapp/IAudioPlayer.h"
#include "emapp/IProjectHolder.h"
#include "emapp/ITranslator.h"
#include "emapp/Project.h"
#include "emapp/TimelineSegment.h"

#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

namespace nanoem {
namespace qt {
namespace {

static QString
translate(QtApplicationService *service, const char *key)
{
    return QString::fromUtf8(service->translator()->translate(key));
}

} /* namespace anonymous */

QtPlayPanel::QtPlayPanel(QtApplicationService *service, QWidget *parent)
    : QGroupBox(translate(service, "nanoem.gui.panel.play.title"), parent)
    , m_service(service)
    , m_timer(nullptr)
{
    auto *layout = new QVBoxLayout(this);
    m_playPauseButton = new QPushButton(translate(m_service, "nanoem.gui.panel.play.play"), this);
    m_loopCheckBox = new QCheckBox(translate(m_service, "nanoem.gui.panel.play.loop"), this);
    m_volumeLabel = new QLabel(this);
    m_volumeSlider = new QSlider(Qt::Horizontal, this);
    m_volumeSlider->setRange(0, 100);
    m_volumeSlider->setValue(100);
    m_enableFromCheckBox = new QCheckBox(this);
    m_fromSpinBox = new QSpinBox(this);
    m_fromSpinBox->setPrefix(QStringLiteral("From: "));
    m_fromSpinBox->setRange(0, 999999);
    m_enableToCheckBox = new QCheckBox(this);
    m_toSpinBox = new QSpinBox(this);
    m_toSpinBox->setPrefix(QStringLiteral("To: "));
    m_toSpinBox->setRange(0, 999999);
    updateVolumeLabel(m_volumeSlider->value());
    layout->addWidget(m_playPauseButton);
    layout->addWidget(m_loopCheckBox);
    layout->addWidget(m_volumeLabel);
    layout->addWidget(m_volumeSlider);
    auto *fromLayout = new QHBoxLayout;
    fromLayout->addWidget(m_enableFromCheckBox);
    fromLayout->addWidget(m_fromSpinBox, 1);
    layout->addLayout(fromLayout);
    auto *toLayout = new QHBoxLayout;
    toLayout->addWidget(m_enableToCheckBox);
    toLayout->addWidget(m_toSpinBox, 1);
    layout->addLayout(toLayout);
    connect(m_playPauseButton, &QPushButton::clicked, this, [this]() {
        Project *project = currentProject();
        if (project) {
            if (project->isPlaying()) {
                project->pause(true);
            }
            else if (project->currentLocalFrameIndex() > 0) {
                project->resume(true);
            }
            else {
                project->play();
            }
        }
    });
    connect(m_loopCheckBox, &QCheckBox::toggled, this, [this](bool checked) {
        Project *project = currentProject();
        if (project) {
            project->setLoopEnabled(checked);
        }
    });
    connect(m_volumeSlider, &QSlider::valueChanged, this, [this](int value) {
        updateVolumeLabel(value);
        Project *project = currentProject();
        if (project) {
            project->audioPlayer()->setVolumeGain(value * 0.01f);
        }
    });
    connect(m_enableFromCheckBox, &QCheckBox::toggled, this, [this](bool) { applyPlayingSegment(); });
    connect(m_fromSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) { applyPlayingSegment(); });
    connect(m_enableToCheckBox, &QCheckBox::toggled, this, [this](bool) { applyPlayingSegment(); });
    connect(m_toSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) { applyPlayingSegment(); });
    m_timer = new QTimer(this);
    m_timer->setInterval(100);
    connect(m_timer, &QTimer::timeout, this, [this]() { refresh(); });
    m_timer->start();
    refresh();
}

Project *
QtPlayPanel::currentProject()
{
    return m_service->projectHolder()->currentProject();
}

void
QtPlayPanel::updateVolumeLabel(int volume)
{
    const QByteArray format(translate(m_service, "nanoem.gui.panel.play.volume.format").toUtf8());
    m_volumeLabel->setText(QString::asprintf(format.constData(), volume));
}

void
QtPlayPanel::applyPlayingSegment()
{
    Project *project = currentProject();
    if (project && !project->isPlaying()) {
        TimelineSegment segment(project->playingSegment());
        segment.m_enableFrom = m_enableFromCheckBox->isChecked();
        segment.m_from = static_cast<nanoem_frame_index_t>(m_fromSpinBox->value());
        segment.m_enableTo = m_enableToCheckBox->isChecked();
        segment.m_to = static_cast<nanoem_frame_index_t>(m_toSpinBox->value());
        segment.m_from = std::min(segment.m_from, segment.m_to);
        project->setPlayingSegment(segment);
    }
}

void
QtPlayPanel::refresh()
{
    Project *project = currentProject();
    if (project) {
        const bool playing = project->isPlaying();
        const nanoem_frame_index_t frameIndex = project->currentLocalFrameIndex();
        const QString buttonText = playing
            ? translate(m_service, "nanoem.gui.panel.play.pause")
            : (frameIndex > 0 ? translate(m_service, "nanoem.gui.panel.play.resume")
                              : translate(m_service, "nanoem.gui.panel.play.play"));
        if (m_playPauseButton->text() != buttonText) {
            m_playPauseButton->setText(buttonText);
        }
        m_loopCheckBox->blockSignals(true);
        m_loopCheckBox->setChecked(project->isLoopEnabled());
        m_loopCheckBox->blockSignals(false);
        const int volume = static_cast<int>(project->audioPlayer()->volumeGain() * 100.0f);
        m_volumeSlider->blockSignals(true);
        m_volumeSlider->setValue(volume);
        m_volumeSlider->blockSignals(false);
        updateVolumeLabel(volume);
        const TimelineSegment segment(project->playingSegment());
        const nanoem_frame_index_t duration = project->duration();
        m_enableFromCheckBox->blockSignals(true);
        m_enableFromCheckBox->setChecked(segment.m_enableFrom);
        m_enableFromCheckBox->setEnabled(!playing);
        m_enableFromCheckBox->blockSignals(false);
        m_fromSpinBox->blockSignals(true);
        m_fromSpinBox->setRange(0, static_cast<int>(segment.m_to));
        m_fromSpinBox->setValue(static_cast<int>(segment.m_from));
        m_fromSpinBox->setEnabled(!playing);
        m_fromSpinBox->blockSignals(false);
        m_enableToCheckBox->blockSignals(true);
        m_enableToCheckBox->setChecked(segment.m_enableTo);
        m_enableToCheckBox->setEnabled(!playing);
        m_enableToCheckBox->blockSignals(false);
        m_toSpinBox->blockSignals(true);
        m_toSpinBox->setRange(static_cast<int>(segment.m_from), static_cast<int>(duration));
        m_toSpinBox->setValue(static_cast<int>(segment.m_to));
        m_toSpinBox->setEnabled(!playing);
        m_toSpinBox->blockSignals(false);
    }
}

} /* namespace qt */
} /* namespace nanoem */
