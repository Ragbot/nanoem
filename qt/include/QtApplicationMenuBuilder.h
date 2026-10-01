/*
   Copyright (c) 2015-2023 hkrn All rights reserved

   This file is part of emapp component and it's licensed under Mozilla Public License. see LICENSE.md for more details.
 */

#pragma once
#ifndef NANOEM_EMAPP_QT_QTAPPLICATIONMENUBUILDER_H_
#define NANOEM_EMAPP_QT_QTAPPLICATIONMENUBUILDER_H_

#include "emapp/ApplicationMenuBuilder.h"

class QAction;
class QMenu;
class QMenuBar;

namespace nanoem {

class BaseApplicationService;
class IEventPublisher;
class IFileManager;
class ITranslator;

namespace qt {

/**
 * @brief Native Qt implementation of the application menu bar.
 *
 * The whole menu structure is defined by ApplicationMenuBuilder (the same UI agnostic definition
 * the Dear ImGui implementation shares) while every node is materialized as QMenu/QAction so the
 * Qt shell presents a native menu bar and native file dialogs instead of ImGui ones.
 */
class QtApplicationMenuBuilder NANOEM_DECL_SEALED : public ApplicationMenuBuilder {
public:
    QtApplicationMenuBuilder(BaseApplicationClient *client, IEventPublisher *eventPublisher, IFileManager *fileManager,
        const ITranslator *translator, BaseApplicationService *application, QMenuBar *menuBar,
        bool enableModelEditing);
    ~QtApplicationMenuBuilder() NANOEM_DECL_NOEXCEPT;

private:
    struct NewProjectEventHandler;
    struct OpenProjectEventHandler;
    struct ExportImageCallbackHandler;
    struct ExportVideoCallbackHandler;

    void createAllMenus() NANOEM_DECL_OVERRIDE;
    MenuBarHandle createMenuBar() NANOEM_DECL_OVERRIDE;
    MenuBarHandle createMenuBar(MenuItemType type) NANOEM_DECL_OVERRIDE;
    MenuItemHandle createMenuItem(MainMenuBarHandle menu) NANOEM_DECL_OVERRIDE;
    MenuItemHandle appendMenuItem(MenuBarHandle menu, MenuItemType type) NANOEM_DECL_OVERRIDE;
    void createSelectAccessoryMenuItem(MenuBarHandle menu, nanoem_u16_t handle, const char *name) NANOEM_DECL_OVERRIDE;
    void createSelectModelMenuItem(MenuBarHandle menu, nanoem_u16_t handle, const char *name) NANOEM_DECL_OVERRIDE;
    void createSelectBoneMenuItem(MenuBarHandle menu, const char *name) NANOEM_DECL_OVERRIDE;
    void createSelectMorphMenuItem(
        MenuBarHandle menu, nanoem_model_morph_category_t category, const char *name) NANOEM_DECL_OVERRIDE;
    void createPluginMenuItem(MenuBarHandle menu, MenuItemType type, nanoem_u16_t handle, const String &name,
        const StringList &items) NANOEM_DECL_OVERRIDE;
    void updateAllSelectDrawableItems(MenuBarHandle menu, nanoem_u16_t handle) NANOEM_DECL_OVERRIDE;
    void removeMenuItemById(MenuBarHandle menu, int index) NANOEM_DECL_NOEXCEPT_OVERRIDE;

    void appendMenuSeparator(MenuBarHandle menu) NANOEM_DECL_OVERRIDE;
    void clearAllMenuItems(MenuBarHandle menu) NANOEM_DECL_NOEXCEPT_OVERRIDE;
    void setParentMenu(MenuItemHandle parent, MenuBarHandle menu) NANOEM_DECL_OVERRIDE;
    void setMenuItemEnabled(MenuItemHandle item, bool value) NANOEM_DECL_OVERRIDE;
    void setMenuItemChecked(MenuItemHandle item, bool value) NANOEM_DECL_OVERRIDE;
    bool isMenuItemEnabled(MenuItemHandle item) const NANOEM_DECL_NOEXCEPT_OVERRIDE;
    bool isMenuItemChecked(MenuItemHandle item) const NANOEM_DECL_NOEXCEPT_OVERRIDE;

    struct QtMenuObject {
        virtual ~QtMenuObject() NANOEM_DECL_NOEXCEPT
        {
        }
    };
    struct QtMenuBar;
    struct QtMenuItem;
    typedef tinystl::vector<QtMenuBar *, TinySTLAllocator> QtMenuBarList;
    typedef tinystl::vector<QtMenuItem *, TinySTLAllocator> QtMenuItemList;
    typedef tinystl::vector<QtMenuObject *, TinySTLAllocator> QtMenuObjectList;
    typedef tinystl::vector<QMenu *, TinySTLAllocator> QtMenuList;

    void clearAllMenuItems();
    void createHelpMenu(MainMenuBarHandle handle);
    QAction *registerAction(QtMenuBar *parent, QtMenuItem *item, const String &text);
    void handleTriggered(QtMenuItem *item);
    void dispatchDefaultAction(QtMenuItem *item);
    void showOpenFileDialog(nanoem_u32_t type, const StringList &allowedExtensions);
    void showSaveFileDialog(nanoem_u32_t type, const StringList &allowedExtensions);
    String translatedText(MenuItemType type) const;

    const ITranslator *m_translator;
    IEventPublisher *m_eventPublisher;
    IFileManager *m_fileManager;
    BaseApplicationService *m_applicationPtr;
    QMenuBar *m_menuBar;
    QtMenuObjectList m_objects;
    QtMenuList m_menus;
};

} /* namespace qt */
} /* namespace nanoem */

#endif /* NANOEM_EMAPP_QT_QTAPPLICATIONMENUBUILDER_H_ */
