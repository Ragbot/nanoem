/*
   Copyright (c) 2015-2023 hkrn All rights reserved

   This file is part of emapp component and it's licensed under Mozilla Public License. see LICENSE.md for more details.
 */

#include "QtModelEdgeDialog.h"
#include "QtApplicationService.h"

#include "emapp/CommandRegistrator.h"
#include "emapp/ITranslator.h"
#include "emapp/Model.h"
#include "emapp/Project.h"

#include <QApplication>
#include <QColorDialog>
#include <QCloseEvent>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

namespace nanoem {
namespace qt {

QtModelEdgeDialog::QtModelEdgeDialog(QtApplicationService *service, Project *project, QWidget *parent)
    : QDialog(parent, Qt::Dialog | Qt::Window)
    , m_service(service)
    , m_project(project)
    , m_activeModel(project->activeModel())
    , m_lastColor(m_activeModel ? m_activeModel->edgeColor() : Vector4(1, 1, 1, 1))
    , m_lastScaleFactor(m_activeModel ? m_activeModel->edgeSizeScaleFactor() : 1.0f)
    , m_lastEditingMode(project->editingMode())
{
    setWindowTitle(translate("nanoem.gui.window.model.edge.title"));
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(translate("nanoem.gui.window.model.edge.color"), this));
    auto *colorButton = new QPushButton(this);
    colorButton->setFixedHeight(32);
    connect(colorButton, &QPushButton::clicked, this, &QtModelEdgeDialog::handleColorChanged);
    layout->addWidget(colorButton);
    layout->addWidget(new QLabel(translate("nanoem.gui.window.model.edge.size"), this));
    auto *scaleSpinBox = new QDoubleSpinBox(this);
    scaleSpinBox->setRange(0.0, 2.0);
    scaleSpinBox->setDecimals(2);
    scaleSpinBox->setSingleStep(0.01);
    scaleSpinBox->setValue(m_lastScaleFactor);
    connect(scaleSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
        [this, scaleSpinBox](double value) {
            if (m_activeModel) {
                m_activeModel->setEdgeSizeScaleFactor(nanoem_f32_t(value));
                m_activeModel->markStagingVertexBufferDirty();
            }
        });
    layout->addWidget(scaleSpinBox);
    auto *registerButton = new QPushButton(translate("nanoem.gui.window.model.edge.register"), this);
    connect(registerButton, &QPushButton::clicked, this, &QtModelEdgeDialog::handleRegister);
    layout->addWidget(registerButton);
    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QtModelEdgeDialog::handleAccepted);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QtModelEdgeDialog::reject);
    layout->addWidget(buttonBox);
    handleColorChanged(); // initialize button color
    if (!m_activeModel) {
        project->setEditingMode(Project::kEditingModeNone);
    }
    auto *timer = new QTimer(this);
    timer->setInterval(250);
    connect(timer, &QTimer::timeout, this, [this]() {
        if (!m_project->activeModel() || m_project->activeModel() != m_activeModel) {
            close();
        }
    });
    timer->start();
}

void
QtModelEdgeDialog::reject()
{
    if (m_activeModel) {
        m_activeModel->setEdgeColor(m_lastColor);
        m_activeModel->setEdgeSizeScaleFactor(m_lastScaleFactor);
    }
    m_project->setEditingMode(m_lastEditingMode);
    QDialog::reject();
}

void
QtModelEdgeDialog::handleAccepted()
{
    m_project->setEditingMode(m_lastEditingMode);
    accept();
}

void
QtModelEdgeDialog::handleColorChanged()
{
    if (!m_activeModel) {
        return;
    }
    QColor current(m_lastColor.x * 255, m_lastColor.y * 255, m_lastColor.z * 255, m_lastColor.w * 255);
    QColor color;
    if (QGuiApplication::platformName() == QLatin1String("offscreen")) {
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
        color = QColorDialog::getColor(current, this, translate("nanoem.gui.window.model.edge.color"), QColorDialog::ShowAlphaChannel);
    }
    if (color.isValid()) {
        m_lastColor = Vector4(color.redF(), color.greenF(), color.blueF(), color.alphaF());
        m_activeModel->setEdgeColor(m_lastColor);
        m_activeModel->markStagingVertexBufferDirty();
        QPushButton *btn = qobject_cast<QPushButton *>(sender());
        if (btn) {
            btn->setStyleSheet(QString("background-color: %1;").arg(color.name(QColor::HexArgb)));
        }
    }
}

void
QtModelEdgeDialog::handleRegister()
{
    if (m_activeModel) {
        CommandRegistrator registrator(m_project);
        registrator.registerAddModelKeyframesCommandByCurrentLocalFrameIndex(m_activeModel);
    }
}

void
QtModelEdgeDialog::closeEvent(QCloseEvent *event)
{
    if (m_activeModel) {
        m_activeModel->setEdgeColor(m_lastColor);
        m_activeModel->setEdgeSizeScaleFactor(m_lastScaleFactor);
    }
    m_project->setEditingMode(m_lastEditingMode);
    QDialog::closeEvent(event);
}

QString
QtModelEdgeDialog::translate(const char *key) const
{
    return QString::fromUtf8(m_service->translator()->translate(key));
}

} /* namespace qt */
} /* namespace nanoem */