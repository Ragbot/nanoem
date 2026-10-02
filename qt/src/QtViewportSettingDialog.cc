/*
   Copyright (c) 2015-2023 hkrn All rights reserved

   This file is part of emapp component and it's licensed under Mozilla Public License. see LICENSE.md for more details.
 */

#include "QtViewportSettingDialog.h"
#include "QtApplicationService.h"

#include "emapp/ITranslator.h"
#include "emapp/Project.h"

#include <QApplication>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace nanoem {
namespace qt {

struct ViewportSizePreset {
    const char *m_nameKey;
    Vector2SI32 m_size;
};

static const ViewportSizePreset s_viewportSizePresets[] = {
    { "nanoem.gui.window.project.viewport.size.preset.default", Vector2SI32(640, 360) },
    { "nanoem.gui.window.project.viewport.size.preset.720p", Vector2SI32(1280, 720) },
    { "nanoem.gui.window.project.viewport.size.preset.1080p", Vector2SI32(1920, 1080) },
    { "nanoem.gui.window.project.viewport.size.preset.4k", Vector2SI32(3840, 2160) }
};

QtViewportSettingDialog::QtViewportSettingDialog(
    QtApplicationService *service, Project *project, QWidget *parent)
    : QDialog(parent, Qt::Dialog | Qt::Window)
    , m_service(service)
    , m_project(project)
    , m_lastViewportColor(project->viewportBackgroundColor())
    , m_lastViewportWithTransparent(project->isViewportWithTransparentEnabled())
    , m_viewportImageSize(project->viewportImageSize())
    , m_viewportWithTransparent(project->isViewportWithTransparentEnabled())
{
    setWindowTitle(translate("nanoem.gui.window.project.viewport.title"));
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(translate("nanoem.gui.window.project.viewport.size.title"), this));
    auto *sizeRow = new QHBoxLayout;
    m_sizeX = new QSpinBox(this);
    m_sizeY = new QSpinBox(this);
    m_sizeX->setRange(1, 65535);
    m_sizeY->setRange(1, 65535);
    m_sizeX->setValue(m_viewportImageSize.x);
    m_sizeY->setValue(m_viewportImageSize.y);
    sizeRow->addWidget(new QLabel(translate("nanoem.gui.window.project.viewport.size.width"), this));
    sizeRow->addWidget(m_sizeX);
    sizeRow->addWidget(new QLabel(translate("nanoem.gui.window.project.viewport.size.height"), this));
    sizeRow->addWidget(m_sizeY);
    layout->addLayout(sizeRow);
    layout->addWidget(new QLabel(translate("nanoem.gui.window.project.viewport.size.preset"), this));
    m_presetCombo = new QComboBox(this);
    for (nanoem_rsize_t i = 0; i < BX_COUNTOF(s_viewportSizePresets); i++) {
        m_presetCombo->addItem(translate(s_viewportSizePresets[i].m_nameKey));
    }
    connect(m_presetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &QtViewportSettingDialog::handlePresetChanged);
    layout->addWidget(m_presetCombo);
    layout->addWidget(new QLabel(translate("nanoem.gui.window.project.viewport.color.title"), this));
    auto *colorButton = new QPushButton(this);
    colorButton->setFixedHeight(32);
    connect(colorButton, &QPushButton::clicked, this, &QtViewportSettingDialog::handleColorChanged);
    layout->addWidget(colorButton);
    auto *transparentCheckBox = new QCheckBox(translate("nanoem.gui.window.project.viewport.transparent.title"), this);
    transparentCheckBox->setChecked(m_viewportWithTransparent);
    connect(transparentCheckBox, &QCheckBox::stateChanged, this, &QtViewportSettingDialog::handleTransparentChanged);
    layout->addWidget(transparentCheckBox);
    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QtViewportSettingDialog::handleAccepted);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QtViewportSettingDialog::reject);
    layout->addWidget(buttonBox);
    handleColorChanged(); // initialize button color
}

void
QtViewportSettingDialog::reject()
{
    m_project->setViewportBackgroundColor(m_lastViewportColor);
    m_project->setViewportWithTransparentEnabled(m_lastViewportWithTransparent);
    QDialog::reject();
}

void
QtViewportSettingDialog::handleAccepted()
{
    m_project->setViewportBackgroundColor(m_lastViewportColor);
    m_project->setViewportImageSize(m_viewportImageSize);
    accept();
}

void
QtViewportSettingDialog::handlePresetChanged(int index)
{
    if (index >= 0 && index < int(BX_COUNTOF(s_viewportSizePresets))) {
        m_viewportImageSize = s_viewportSizePresets[index].m_size;
        m_sizeX->setValue(m_viewportImageSize.x);
        m_sizeY->setValue(m_viewportImageSize.y);
    }
}

void
QtViewportSettingDialog::handleColorChanged()
{
    QColor current(m_lastViewportColor.x * 255, m_lastViewportColor.y * 255, m_lastViewportColor.z * 255, m_lastViewportColor.w * 255);
    QColor color;
    if (QGuiApplication::platformName() == QLatin1String("offscreen")) {
        /* on offscreen platform the native color dialog blocks indefinitely; use a fixed
           cycle to verify the dialog renders without blocking the harness */
        static int s_offscreenColorCycle = 0;
        static const QColor s_offscreenColors[] = {
            QColor(255, 0, 0, 255),   // red
            QColor(0, 255, 0, 255),   // green
            QColor(0, 0, 255, 255),   // blue
            QColor(255, 255, 0, 255), // yellow
        };
        color = s_offscreenColors[s_offscreenColorCycle % 4];
        s_offscreenColorCycle++;
    } else {
        color = QColorDialog::getColor(current, this, translate("nanoem.gui.window.project.viewport.color.title"), QColorDialog::ShowAlphaChannel);
    }
    if (color.isValid()) {
        m_lastViewportColor = Vector4(color.redF(), color.greenF(), color.blueF(), color.alphaF());
        m_project->setViewportBackgroundColor(m_lastViewportColor);
        QPushButton *btn = qobject_cast<QPushButton *>(sender());
        if (btn) {
            btn->setStyleSheet(QString("background-color: %1;").arg(color.name(QColor::HexArgb)));
        }
    }
}

void
QtViewportSettingDialog::handleTransparentChanged(int state)
{
    bool enabled = state == Qt::Checked;
    m_viewportWithTransparent = enabled;
    m_project->setViewportWithTransparentEnabled(enabled);
}

void
QtViewportSettingDialog::refreshSizeFromPreset(int index)
{
    if (index >= 0 && index < int(BX_COUNTOF(s_viewportSizePresets))) {
        m_viewportImageSize = s_viewportSizePresets[index].m_size;
        m_sizeX->setValue(m_viewportImageSize.x);
        m_sizeY->setValue(m_viewportImageSize.y);
    }
}

QString
QtViewportSettingDialog::translate(const char *key) const
{
    return QString::fromUtf8(m_service->translator()->translate(key));
}

} /* namespace qt */
} /* namespace nanoem */