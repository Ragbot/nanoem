/*
   Copyright (c) 2015-2023 hkrn All rights reserved

   This file is part of emapp component and it's licensed under Mozilla Public License. see LICENSE.md for more details.
 */

#pragma once
#ifndef NANOEM_EMAPP_QT_MAINWINDOW_H_
#define NANOEM_EMAPP_QT_MAINWINDOW_H_

#include "emapp/IFileManager.h"

#include <QMainWindow>
#include <QOpenGLWidget>
#include <QString>

class QCloseEvent;
class QDragEnterEvent;
class QDropEvent;
class QEvent;
class QKeyEvent;
class QMouseEvent;
class QResizeEvent;
class QTimer;
class QWheelEvent;

namespace nanoem {
namespace qt {

class QtApplicationClient;
class QtApplicationMenuBuilder;
class QtApplicationService;
class QtViewportOverlay;

class Viewport final : public QOpenGLWidget {
public:
    Viewport(QtApplicationService *service, QtApplicationClient *client, QWidget *parent);
    ~Viewport() override;

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void resizeEvent(QResizeEvent *event) override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;

private:
    QtApplicationService *m_service = nullptr;
    QtApplicationClient *m_client = nullptr;
    QtViewportOverlay *m_overlay = nullptr;
    Vector2SI32 m_lastCursorPosition = Vector2SI32(0);
    int m_pressedCursorType = 0;
    int m_pressedCursorModifiers = 0;
};

class MainWindow final : public QMainWindow {
public:
    MainWindow(QtApplicationService *service, QtApplicationClient *client, QWidget *parent = nullptr);
    ~MainWindow() override;

    bool initialize();
    void setFileToLoad(const QString &value);
    void setTitle(const URI &fileURI);

protected:
    void closeEvent(QCloseEvent *event) override;
    void changeEvent(QEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    void setupBottomPanels();
    void requestClose();
    void destroyWindow();

    QtApplicationService *m_service = nullptr;
    QtApplicationClient *m_client = nullptr;
    QtApplicationMenuBuilder *m_menuBuilder = nullptr;
    Viewport *m_viewport = nullptr;
    QTimer *m_timer = nullptr;
    QString m_fileToLoad;
    bool m_shuttingDown = false;
};

} /* namespace qt */
} /* namespace nanoem */

#endif /* NANOEM_EMAPP_QT_MAINWINDOW_H_ */
