/*
   Copyright (c) 2015-2023 hkrn All rights reserved

   This file is part of emapp component and it's licensed under Mozilla Public License. see LICENSE.md for more details.
 */

#include "QtModelTransformOrderDialog.h"
#include "QtApplicationService.h"

#include "emapp/ITranslator.h"
#include "emapp/Model.h"
#include "emapp/Project.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

namespace nanoem {
namespace qt {

QtModelTransformOrderDialog::QtModelTransformOrderDialog(
    QtApplicationService *service, Project *project, QWidget *parent)
    : QDialog(parent, Qt::Dialog | Qt::Window)
    , m_service(service)
    , m_project(project)
    , m_lastTransformOrderList(*project->transformOrderList())
    , m_orderState(m_lastTransformOrderList)
{
    setWindowTitle(translate("nanoem.gui.window.project.order.transform.title"));
    auto *layout = new QVBoxLayout(this);
    auto *orderRow = new QHBoxLayout;
    m_upButton = new QPushButton(QStringLiteral("Up"), this);
    m_downButton = new QPushButton(QStringLiteral("Down"), this);
    connect(m_upButton, &QPushButton::clicked, this, [this]() { handleMove(-1); });
    connect(m_downButton, &QPushButton::clicked, this, [this]() { handleMove(1); });
    orderRow->addWidget(m_upButton);
    orderRow->addWidget(m_downButton);
    layout->addLayout(orderRow);
    m_listWidget = new QListWidget(this);
    m_listWidget->setFixedHeight(200);
    connect(m_listWidget, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row >= 0) {
            QListWidgetItem *item = m_listWidget->item(row);
            m_orderState.m_checkedTransformModel =
                reinterpret_cast<Model *>(item->data(Qt::UserRole).toULongLong());
        }
    });
    layout->addWidget(m_listWidget);
    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QtModelTransformOrderDialog::handleAccepted);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QtModelTransformOrderDialog::reject);
    layout->addWidget(buttonBox);
    refresh();
}

/* Cancel, Escape and the window close button all end up here: restore the snapshot the ImGui
   dialog restored in its `else if (!visible)` branch and its kResponseTypeCancel branch */
void
QtModelTransformOrderDialog::reject()
{
    if (m_restoreOnReject) {
        m_project->setTransformOrderList(m_lastTransformOrderList);
    }
    QDialog::reject();
}

void
QtModelTransformOrderDialog::forceClose()
{
    m_restoreOnReject = false;
    close();
}

void
QtModelTransformOrderDialog::handleAccepted()
{
    /* the ImGui dialog re-applied the working list on OK; every move already applied it live, so
       this only matters when nothing was moved (the lists are identical then) */
    m_project->setTransformOrderList(m_orderState.m_currentTransformModelList);
    accept();
}

void
QtModelTransformOrderDialog::handleMove(int offset)
{
    m_orderState.setOrderAt(offset);
    m_project->setTransformOrderList(m_orderState.m_currentTransformModelList);
    refresh();
}

void
QtModelTransformOrderDialog::refresh()
{
    const Project::ModelList *models = m_project->transformOrderList();
    int checkedRow = -1;
    m_listWidget->clear();
    for (Project::ModelList::const_iterator it = models->begin(), end = models->end(); it != end; ++it) {
        Model *model = *it;
        auto *item = new QListWidgetItem(QString::fromUtf8(model->nameConstString()), m_listWidget);
        item->setData(Qt::UserRole, reinterpret_cast<qulonglong>(model));
        if (model == m_orderState.m_checkedTransformModel) {
            checkedRow = m_listWidget->count() - 1;
        }
    }
    if (checkedRow >= 0) {
        m_listWidget->setCurrentRow(checkedRow);
    }
    m_upButton->setEnabled(!m_orderState.isOrderBegin());
    m_downButton->setEnabled(!m_orderState.isOrderEnd());
}

QString
QtModelTransformOrderDialog::translate(const char *key) const
{
    return QString::fromUtf8(m_service->translator()->translate(key));
}

} /* namespace qt */
} /* namespace nanoem */
