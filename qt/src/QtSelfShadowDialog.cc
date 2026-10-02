/*
   Copyright (c) 2015-2023 hkrn All rights reserved

   This file is part of emapp component and it's licensed under Mozilla Public License. see LICENSE.md for more details.
 */

#include "QtSelfShadowDialog.h"
#include "QtApplicationService.h"

#include "emapp/CommandRegistrator.h"
#include "emapp/ITranslator.h"
#include "emapp/Project.h"
#include "emapp/ShadowCamera.h"

#include <QFrame>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QSlider>
#include <QVBoxLayout>

namespace nanoem {
namespace qt {

QtSelfShadowDialog::QtSelfShadowDialog(QtApplicationService *service, Project *project, QWidget *parent)
    : QDialog(parent, Qt::Dialog | Qt::Window)
    , m_service(service)
    , m_project(project)
{
    ShadowCamera *shadow = project->shadowCamera();
    setWindowTitle(translate("nanoem.gui.window.op.self-shadow"));
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(translate("nanoem.gui.window.op.self-shadow.mode.title"), this));
    const ShadowCamera::CoverageModeType mode = shadow->coverageMode();
    const struct {
        const char *m_key;
        ShadowCamera::CoverageModeType m_mode;
    } modes[] = { { "nanoem.gui.window.op.self-shadow.mode.none", ShadowCamera::kCoverageModeTypeNone },
        { "nanoem.gui.window.op.self-shadow.mode.type1", ShadowCamera::kCoverageModeType1 },
        { "nanoem.gui.window.op.self-shadow.mode.type2", ShadowCamera::kCoverageModeType2 } };
    for (nanoem_rsize_t i = 0; i < BX_COUNTOF(modes); i++) {
        auto *radioButton = new QRadioButton(translate(modes[i].m_key), this);
        radioButton->setChecked(mode == modes[i].m_mode);
        const ShadowCamera::CoverageModeType value = modes[i].m_mode;
        connect(radioButton, &QRadioButton::toggled, this, [shadow, value](bool checked) {
            if (checked) {
                shadow->setCoverageMode(value);
            }
        });
        layout->addWidget(radioButton);
    }
    layout->addWidget(new QLabel(translate("nanoem.gui.window.op.self-shadow.distance.title"), this));
    auto *distanceSlider = new QSlider(Qt::Horizontal, this);
    distanceSlider->setRange(int(ShadowCamera::kMinimumDistance), int(ShadowCamera::kMaximumDistance));
    distanceSlider->setValue(int(shadow->distance()));
    connect(distanceSlider, &QSlider::valueChanged, this, [shadow](int value) {
        shadow->setDistance(nanoem_f32_t(value));
    });
    layout->addWidget(distanceSlider);
    auto *initializeButton = new QPushButton(translate("nanoem.gui.window.op.self-shadow.initialize"), this);
    connect(initializeButton, &QPushButton::clicked, this, [shadow]() { shadow->reset(); });
    layout->addWidget(initializeButton);
    auto *separator = new QFrame(this);
    separator->setFrameShape(QFrame::HLine);
    separator->setFrameShadow(QFrame::Sunken);
    layout->addWidget(separator);
    auto *registerButton = new QPushButton(translate("nanoem.gui.window.op.self-shadow.register"), this);
    connect(registerButton, &QPushButton::clicked, this, [this]() {
        CommandRegistrator registrator(m_project);
        registrator.registerAddSelfShadowKeyframesCommandByCurrentLocalFrameIndex();
    });
    layout->addWidget(registerButton);
}

QString
QtSelfShadowDialog::translate(const char *key) const
{
    return QString::fromUtf8(m_service->translator()->translate(key));
}

} /* namespace qt */
} /* namespace nanoem */
