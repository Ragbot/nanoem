/*
   Copyright (c) 2015-2023 hkrn All rights reserved

   This file is part of emapp component and it's licensed under Mozilla Public License. see LICENSE.md for more details.
 */

#include "QtModalDialogPresenter.h"

#include "emapp/BaseApplicationService.h"
#include "emapp/IEventPublisher.h"
#include "emapp/IModalDialog.h"
#include "emapp/IProjectHolder.h"
#include "emapp/Project.h"
#include "emapp/private/CommonInclude.h"

#include <QtCore/QTimer>
#include <QtWidgets/QDialog>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QProgressBar>
#include <QtWidgets/QTextEdit>
#include <QtWidgets/QVBoxLayout>

namespace nanoem {
namespace qt {
namespace {

static const int kProgressMaximum = 1000;
static const int kProgressUpdateIntervalMs = 80;

} /* namespace */

QtModalDialogPresenter::QtModalDialogPresenter(BaseApplicationService *service)
    : m_service(service)
    , m_parentWidget(nullptr)
    , m_shownDialog(nullptr)
    , m_activeDialog(nullptr)
    , m_showScheduled(false)
    , m_clearRequested(false)
{
}

QtModalDialogPresenter::~QtModalDialogPresenter()
{
    if (m_activeDialog) {
        m_activeDialog->done(-1);
    }
    for (QList<IModalDialog *>::const_iterator it = m_dialogs.constBegin(), end = m_dialogs.constEnd(); it != end;
         ++it) {
        nanoem_delete(*it);
    }
    m_dialogs.clear();
}

void
QtModalDialogPresenter::add(IModalDialog *dialog)
{
    if (dialog) {
        m_dialogs.prepend(dialog);
        m_service->eventPublisher()->publishAddModalDialogEvent();
        scheduleShow();
    }
}

void
QtModalDialogPresenter::clearAll()
{
    if (m_activeDialog) {
        m_clearRequested = true;
        m_activeDialog->done(-1);
    }
    else if (m_shownDialog) {
        m_clearRequested = true;
    }
    else if (!m_dialogs.isEmpty()) {
        removeAllDialogs();
    }
}

IModalDialog *
QtModalDialogPresenter::current() const NANOEM_DECL_NOEXCEPT
{
    return m_dialogs.isEmpty() ? nullptr : m_dialogs.front();
}

void
QtModalDialogPresenter::scheduleShow()
{
    if (!m_showScheduled && !m_shownDialog && !m_dialogs.isEmpty()) {
        m_showScheduled = true;
        QTimer::singleShot(0, this, [this]() { showNext(); });
    }
}

void
QtModalDialogPresenter::removeAllDialogs()
{
    if (!m_dialogs.isEmpty()) {
        for (QList<IModalDialog *>::const_iterator it = m_dialogs.constBegin(), end = m_dialogs.constEnd(); it != end;
             ++it) {
            nanoem_delete(*it);
        }
        m_dialogs.clear();
        m_service->eventPublisher()->publishClearModalDialogEvent();
    }
}

void
QtModalDialogPresenter::showNext()
{
    m_showScheduled = false;
    if (m_shownDialog || m_dialogs.isEmpty()) {
        return;
    }
    IModalDialog *dialog = m_dialogs.front();
    m_shownDialog = dialog;
    QMessageBox box(m_parentWidget);
    box.setWindowTitle(QString::fromUtf8(dialog->title()));
    box.setTextFormat(Qt::PlainText);
    const nanoem_u32_t buttons = dialog->buttons();
    if (buttons & IModalDialog::kButtonTypeSave) {
        box.setIcon(QMessageBox::Warning);
    }
    else if ((buttons & (IModalDialog::kButtonTypeOk | IModalDialog::kButtonTypeCancel)) ==
        (IModalDialog::kButtonTypeOk | IModalDialog::kButtonTypeCancel)) {
        box.setIcon(QMessageBox::Question);
    }
    else {
        box.setIcon(QMessageBox::Information);
    }
    const QString message(QString::fromUtf8(dialog->text()));
    QVBoxLayout *layout = qobject_cast<QVBoxLayout *>(box.layout());
    QTextEdit *editor = nullptr;
    if (message.size() > 600 && layout) {
        editor = new QTextEdit(&box);
        editor->setReadOnly(true);
        editor->setPlainText(message);
        editor->setMinimumWidth(480);
        editor->setMaximumHeight(320);
    }
    else {
        box.setText(message);
    }
    nanoem_u32_t standardButtons = 0;
    if (buttons & IModalDialog::kButtonTypeSave) {
        standardButtons |= QMessageBox::Save;
    }
    if (buttons & IModalDialog::kButtonTypeDiscard) {
        standardButtons |= QMessageBox::Discard;
    }
    if (buttons & IModalDialog::kButtonTypeOk) {
        standardButtons |= QMessageBox::Ok;
    }
    if (buttons & IModalDialog::kButtonTypeCancel) {
        standardButtons |= QMessageBox::Cancel;
    }
    box.setStandardButtons(standardButtons != 0 ? int(standardButtons) : int(QMessageBox::Ok));
    if (buttons & IModalDialog::kButtonTypeSave) {
        box.setDefaultButton(QMessageBox::Save);
    }
    else if (buttons & IModalDialog::kButtonTypeOk) {
        box.setDefaultButton(QMessageBox::Ok);
    }
    QProgressBar *progressBar = nullptr;
    const nanoem_f32_t initialProgress = dialog->progress();
    if (initialProgress >= 0.0f && layout) {
        progressBar = new QProgressBar(&box);
        progressBar->setRange(0, kProgressMaximum);
        progressBar->setValue(int(initialProgress * kProgressMaximum));
    }
    if (layout) {
        int index = layout->count() - 1; /* insert above the button box */
        if (progressBar) {
            layout->insertWidget(index++, progressBar);
        }
        if (editor) {
            layout->insertWidget(index++, editor);
        }
    }
    QString lastMessage(message);
    QTimer timer;
    timer.setInterval(kProgressUpdateIntervalMs);
    QObject::connect(&timer, &QTimer::timeout, [&]() {
        const nanoem_f32_t value = dialog->progress();
        if (progressBar && value >= 0.0f) {
            progressBar->setValue(int(value * kProgressMaximum));
        }
        const char *text = dialog->text();
        const QString current(QString::fromUtf8(text ? text : ""));
        if (current != lastMessage) {
            lastMessage = current;
            box.setText(lastMessage);
        }
    });
    timer.start();
    m_activeDialog = &box;
    const int result = box.exec();
    timer.stop();
    m_activeDialog = nullptr;
    if (m_clearRequested) {
        m_clearRequested = false;
        m_shownDialog = nullptr;
        removeAllDialogs();
        return;
    }
    m_shownDialog = nullptr;
    Project *project = m_service->projectHolder()->currentProject();
    IModalDialog *next = nullptr;
    switch (result) {
    case QMessageBox::Ok:
        next = dialog->onAccepted(project);
        break;
    case QMessageBox::Save:
        next = dialog->onSaved(project);
        break;
    case QMessageBox::Discard:
        next = dialog->onDiscarded(project);
        break;
    default:
        next = dialog->onCancelled(project);
        break;
    }
    m_dialogs.removeAll(dialog);
    nanoem_delete(dialog);
    if (next) {
        m_dialogs.prepend(next);
    }
    if (m_clearRequested) {
        m_clearRequested = false;
        removeAllDialogs();
    }
    else if (m_dialogs.isEmpty()) {
        m_service->eventPublisher()->publishClearModalDialogEvent();
    }
    else {
        scheduleShow();
    }
}

} /* namespace qt */
} /* namespace nanoem */
