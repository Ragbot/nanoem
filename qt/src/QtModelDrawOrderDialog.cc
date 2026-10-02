/*
   Copyright (c) 2015-2023 hkrn All rights reserved

   This file is part of emapp component and it's licensed under Mozilla Public License. see LICENSE.md for more details.
 */

#include "QtModelDrawOrderDialog.h"
#include "QtApplicationService.h"

#include "emapp/IDrawable.h"
#include "emapp/ITranslator.h"
#include "emapp/Project.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

namespace nanoem {
namespace qt {

QtModelDrawOrderDialog::QtModelDrawOrderDialog(
    QtApplicationService *service, Project *project, QWidget *parent)
    : QDialog(parent)
    , m_service(service)
    , m_project(project)
    , m_lastDrawableOrderList(*project->drawableOrderList())
    , m_orderState(m_lastDrawableOrderList)
{
    setWindowTitle(translate("nanoem.gui.window.project.order.drawable.title"));
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
            m_orderState.m_checkedDrawable =
                reinterpret_cast<IDrawable *>(item->data(Qt::UserRole).toULongLong());
        }
    });
    layout->addWidget(m_listWidget);
    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QtModelDrawOrderDialog::handleAccepted);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QtModelDrawOrderDialog::reject);
    layout->addWidget(buttonBox);
    refresh();
}

/* Cancel, Escape and the window close button all end up here: restore the snapshot the ImGui
   dialog restored in its `else if (!visible)` branch and its kResponseTypeCancel branch */
void
QtModelDrawOrderDialog::reject()
{
    if (m_restoreOnReject) {
        m_project->setDrawableOrderList(m_lastDrawableOrderList);
    }
    QDialog::reject();
}

void
QtModelDrawOrderDialog::forceClose()
{
    m_restoreOnReject = false;
    close();
}

void
QtModelDrawOrderDialog::handleAccepted()
{
    /* the ImGui dialog re-applied the working list on OK; every move already applied it live, so
       this only matters when nothing was moved (the lists are identical then) */
    m_project->setDrawableOrderList(m_orderState.m_currentDrawableList);
    accept();
}

void
QtModelDrawOrderDialog::handleMove(int offset)
{
    m_orderState.setOrderAt(offset);
    m_project->setDrawableOrderList(m_orderState.m_currentDrawableList);
    refresh();
}

void
QtModelDrawOrderDialog::refresh()
{
    const Project::DrawableList *drawables = m_project->drawableOrderList();
    int checkedRow = -1;
    m_listWidget->clear();
    for (Project::DrawableList::const_iterator it = drawables->begin(), end = drawables->end(); it != end; ++it) {
        IDrawable *drawable = *it;
        auto *item = new QListWidgetItem(QString::fromUtf8(drawable->nameConstString()), m_listWidget);
        item->setData(Qt::UserRole, reinterpret_cast<qulonglong>(drawable));
        if (drawable == m_orderState.m_checkedDrawable) {
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
QtModelDrawOrderDialog::translate(const char *key) const
{
    return QString::fromUtf8(m_service->translator()->translate(key));
}

} /* namespace qt */
} /* namespace nanoem */
