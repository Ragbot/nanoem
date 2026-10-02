/*
   Copyright (c) 2015-2023 hkrn All rights reserved

   This file is part of emapp component and it's licensed under Mozilla Public License. see LICENSE.md for more details.
 */

#include "QtMorphCorrectionDialog.h"
#include "QtApplicationService.h"

#include "emapp/CommandRegistrator.h"
#include "emapp/Error.h"
#include "emapp/IEventPublisher.h"
#include "emapp/ITranslator.h"
#include "emapp/Motion.h"
#include "emapp/Project.h"

#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QTimer>
#include <QVBoxLayout>

namespace nanoem {
namespace qt {

/* the ImGui DragFloat rows were unbounded with a "%.2f" display; the spin box keeps the two decimal
   places and a prefix so it reads the same " * 1.00" / " + 0.00" as the ImGui format string */
static const nanoem_f32_t kFactorMinimum = -100000.0f;
static const nanoem_f32_t kFactorMaximum = 100000.0f;
static const nanoem_f32_t kFactorStep = 0.1f;

static QDoubleSpinBox *
createFactorSpinBox(const char *prefix, nanoem_f32_t value)
{
    auto *spinBox = new QDoubleSpinBox;
    spinBox->setDecimals(2);
    spinBox->setRange(kFactorMinimum, kFactorMaximum);
    spinBox->setSingleStep(kFactorStep);
    spinBox->setPrefix(QString::fromUtf8(prefix));
    spinBox->setValue(value);
    return spinBox;
}

QtMorphCorrectionDialog::QtMorphCorrectionDialog(QtApplicationService *service, Project *project, QWidget *parent)
    : QDialog(parent, Qt::Dialog | Qt::Window)
    , m_service(service)
    , m_project(project)
{
    setWindowTitle(translate("nanoem.gui.window.morph.correction.title"));
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(translate("nanoem.gui.window.morph.correction.weight"), this));
    auto *row = new QHBoxLayout;
    m_mul = createFactorSpinBox(" * ", 1.0f);
    m_add = createFactorSpinBox(" + ", 0.0f);
    row->addWidget(m_mul);
    row->addWidget(m_add);
    layout->addLayout(row);
    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QtMorphCorrectionDialog::handleAccepted);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QtMorphCorrectionDialog::reject);
    layout->addWidget(buttonBox);
    /* ImGuiWindow::draw closes this dialog as soon as the active model goes away */
    auto *timer = new QTimer(this);
    timer->setInterval(250);
    connect(timer, &QTimer::timeout, this, [this]() {
        if (!m_project->activeModel()) {
            close();
        }
    });
    timer->start();
}

void
QtMorphCorrectionDialog::handleAccepted()
{
    if (Model *activeModel = m_project->activeModel()) {
        Error error;
        CommandRegistrator registrator(m_project);
        const Motion::CorrectionScalarFactor weight(nanoem_f32_t(m_mul->value()), nanoem_f32_t(m_add->value()));
        registrator.registerCorrectAllSelectedMorphKeyframesCommand(activeModel, weight, error);
        error.notify(m_project->eventPublisher());
    }
    accept();
}

QString
QtMorphCorrectionDialog::translate(const char *key) const
{
    return QString::fromUtf8(m_service->translator()->translate(key));
}

} /* namespace qt */
} /* namespace nanoem */
