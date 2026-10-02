/*
   Copyright (c) 2015-2023 hkrn All rights reserved

   This file is part of emapp component and it's licensed under Mozilla Public License. see LICENSE.md for more details.
 */

#include "QtBoneBiasDialog.h"
#include "QtApplicationService.h"

#include "emapp/ITranslator.h"
#include "emapp/Model.h"
#include "emapp/Project.h"

#include <QLabel>
#include <QTimer>
#include <QVBoxLayout>

namespace nanoem {
namespace qt {

QtBoneBiasDialog::QtBoneBiasDialog(QtApplicationService *service, Project *project, QWidget *parent)
    : QDialog(parent, Qt::Dialog | Qt::Window)
    , m_service(service)
    , m_project(project)
{
    setWindowTitle(translate("nanoem.gui.window.bone.correction.title"));
    setFixedSize(300, 120);
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(translate("nanoem.gui.window.bone.correction.title"), this), 0, Qt::AlignCenter);
    auto *timer = new QTimer(this);
    timer->setInterval(250);
    connect(timer, &QTimer::timeout, this, &QtBoneBiasDialog::checkActiveModel);
    timer->start();
}

void
QtBoneBiasDialog::closeEvent(QCloseEvent *event)
{
    m_closing = true;
    QDialog::closeEvent(event);
}

void
QtBoneBiasDialog::checkActiveModel()
{
    if (m_closing) {
        return;
    }
    if (!m_project->activeModel()) {
        close();
    }
}

QString
QtBoneBiasDialog::translate(const char *key) const
{
    return QString::fromUtf8(m_service->translator()->translate(key));
}

} /* namespace qt */
} /* namespace nanoem */