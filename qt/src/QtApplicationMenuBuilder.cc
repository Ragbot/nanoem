/*
   Copyright (c) 2015-2023 hkrn All rights reserved

   This file is part of emapp component and it's licensed under Mozilla Public License. see LICENSE.md for more details.
 */

#include "QtApplicationMenuBuilder.h"

#include "emapp/Accessory.h"
#include "emapp/ApplicationPreference.h"
#include "emapp/BaseApplicationClient.h"
#include "emapp/BaseApplicationService.h"
#include "emapp/BaseAudioPlayer.h"
#include "emapp/FileUtils.h"
#include "emapp/IEventPublisher.h"
#include "emapp/IFileManager.h"
#include "emapp/IProjectHolder.h"
#include "emapp/ITranslator.h"
#include "emapp/Model.h"
#include "emapp/Motion.h"
#include "emapp/Project.h"
#include "emapp/model/BindPose.h"
#include "emapp/private/CommonInclude.h"

#include <QAction>
#include <QFileDialog>
#include <QFileInfo>
#include <QMenu>
#include <QMenuBar>
#include <QObject>

#include "bx/handlealloc.h"
#include "bx/os.h"

namespace nanoem {
namespace qt {
namespace {

static bool
isProjectDialogType(nanoem_u32_t type) NANOEM_DECL_NOEXCEPT
{
    return type == IFileManager::kDialogTypeOpenProject || type == IFileManager::kDialogTypeSaveProjectFile;
}

static String
homeDirectory()
{
    char buffer[1024] = { 0 };
    uint32_t size = static_cast<uint32_t>(sizeof(buffer) - 1);
    String path;
    if (bx::getEnv(buffer, &size, "HOME") && buffer[0] != '\0') {
        path = String(buffer);
    }
    return path;
}

/**
 * @brief Returns the conventional directory holding the content of the given dialog type when the
 * user has never opened such a dialog before.
 */
static String
preferredDirectoryOfDialogType(nanoem_u32_t type)
{
    String path;
    const String home(homeDirectory());
    if (!home.empty()) {
        const char *relativePath = nullptr;
        switch (type) {
        case IFileManager::kDialogTypeOpenModelFile:
        case IFileManager::kDialogTypeSaveModelFile:
            relativePath = "/Projects/MMD/Models";
            break;
        case IFileManager::kDialogTypeOpenModelMotionFile:
        case IFileManager::kDialogTypeOpenCameraMotionFile:
        case IFileManager::kDialogTypeOpenLightMotionFile:
        case IFileManager::kDialogTypeSaveModelMotionFile:
        case IFileManager::kDialogTypeSaveCameraMotionFile:
        case IFileManager::kDialogTypeSaveLightMotionFile:
            relativePath = "/Projects/MMD/Motions";
            break;
        default:
            break;
        }
        if (relativePath) {
            path = String(home);
            path.append(relativePath);
            if (!FileUtils::exists(path.c_str())) {
                path = String();
            }
        }
    }
    return path;
}

static String
projectDirectory(BaseApplicationService *application)
{
    String path;
    if (application) {
        if (const Project *project = application->projectHolder()->currentProject()) {
            const URI &fileURI = project->fileURI();
            if (!fileURI.isEmpty()) {
                path = fileURI.absolutePathByDeletingLastPathComponent();
            }
        }
    }
    return path;
}

/**
 * @brief Resolves the directory the file dialog of the given type starts at.
 *
 * The last directory the user visited for the same kind of dialog wins, then the directory of the
 * currently opened project for the project dialogs, then the conventional content directory and
 * finally the directory of the currently opened project.
 */
static String
resolveInitialDirectory(nanoem_u32_t type, BaseApplicationService *application)
{
    String path;
    if (application) {
        if (const char *value = ApplicationPreference(application).lastDialogDirectory(type)) {
            path = String(value);
        }
    }
    if (path.empty() && isProjectDialogType(type)) {
        path = projectDirectory(application);
    }
    if (path.empty()) {
        path = preferredDirectoryOfDialogType(type);
    }
    if (path.empty()) {
        path = projectDirectory(application);
    }
    if (path.empty()) {
        path = String(".");
    }
    return path;
}

static QString
buildFileFilter(const StringList &extensions)
{
    QStringList patterns;
    for (StringList::const_iterator it = extensions.begin(), end = extensions.end(); it != end; ++it) {
        const QString ext(QString::fromUtf8(it->c_str()).trimmed());
        if (!ext.isEmpty()) {
            if (ext.startsWith(QLatin1Char('*'))) {
                patterns.append(ext);
            }
            else if (ext.startsWith(QLatin1Char('.'))) {
                patterns.append(QStringLiteral("*") + ext);
            }
            else {
                patterns.append(QStringLiteral("*.") + ext);
            }
        }
    }
    if (patterns.isEmpty()) {
        return QStringLiteral("All files (*)");
    }
    return QStringLiteral("Allowed files (%1);;All files (*)").arg(patterns.join(QLatin1Char(' ')));
}

} /* namespace anonymous */

struct QtApplicationMenuBuilder::NewProjectEventHandler {
    static void
    saveProject(QtApplicationMenuBuilder *self)
    {
        self->m_eventPublisher->publishQuerySaveFileDialogEvent(
            IFileManager::kDialogTypeSaveProjectFile, Project::loadableExtensions());
    }
    static void
    handleCompleteSavingFile(void *userData, const URI & /* fileURI */, nanoem_u32_t /* type */, nanoem_u64_t /* ticks */)
    {
        QtApplicationMenuBuilder *self = static_cast<QtApplicationMenuBuilder *>(userData);
        self->m_client->sendNewProjectMessage();
    }
    static void
    handleSaveProjectProjectAfterConfirm(void *userData)
    {
        QtApplicationMenuBuilder *self = static_cast<QtApplicationMenuBuilder *>(userData);
        self->m_client->addCompleteSavingFileEventListener(handleCompleteSavingFile, self, true);
        saveProject(self);
    }
    static void
    handleDiscardProjectAfterConfirm(void *userData)
    {
        QtApplicationMenuBuilder *self = static_cast<QtApplicationMenuBuilder *>(userData);
        self->m_client->sendNewProjectMessage();
    }
    static void
    handleIsProjectDirtyResponse(void *userData, bool dirty)
    {
        QtApplicationMenuBuilder *self = static_cast<QtApplicationMenuBuilder *>(userData);
        BaseApplicationClient *client = self->m_client;
        if (dirty) {
            client->clearAllProjectAfterConfirmOnceEventListeners();
            client->addSaveProjectAfterConfirmEventListener(handleSaveProjectProjectAfterConfirm, self, true);
            client->addDiscardProjectAfterConfirmEventListener(handleDiscardProjectAfterConfirm, self, true);
            client->sendConfirmBeforeOpenProjectMessage();
        }
        else {
            client->sendNewProjectMessage();
        }
    }
};

struct QtApplicationMenuBuilder::OpenProjectEventHandler {
    static void
    openProject(QtApplicationMenuBuilder *self)
    {
        self->m_eventPublisher->publishQueryOpenSingleFileDialogEvent(
            IFileManager::kDialogTypeOpenProject, Project::loadableExtensions());
    }
    static void
    handleCompleteSavingFile(void *userData, const URI & /* fileURI */, nanoem_u32_t /* type */, nanoem_u64_t /* ticks */)
    {
        QtApplicationMenuBuilder *self = static_cast<QtApplicationMenuBuilder *>(userData);
        openProject(self);
    }
    static void
    handleSaveProjectAfterConfirm(void *userData)
    {
        QtApplicationMenuBuilder *self = static_cast<QtApplicationMenuBuilder *>(userData);
        self->m_client->addCompleteSavingFileEventListener(handleCompleteSavingFile, self, true);
        NewProjectEventHandler::saveProject(self);
    }
    static void
    handleDiscardProjectAfterConfirm(void *userData)
    {
        QtApplicationMenuBuilder *self = static_cast<QtApplicationMenuBuilder *>(userData);
        openProject(self);
    }
    static void
    handleIsProjectDirtyResponse(void *userData, bool dirty)
    {
        QtApplicationMenuBuilder *self = static_cast<QtApplicationMenuBuilder *>(userData);
        BaseApplicationClient *client = self->m_client;
        if (dirty) {
            client->clearAllProjectAfterConfirmOnceEventListeners();
            client->addSaveProjectAfterConfirmEventListener(handleSaveProjectAfterConfirm, self, true);
            client->addDiscardProjectAfterConfirmEventListener(handleDiscardProjectAfterConfirm, self, true);
            client->sendConfirmBeforeOpenProjectMessage();
        }
        else {
            openProject(self);
        }
    }
};

struct QtApplicationMenuBuilder::ExportImageCallbackHandler {
    static void
    handleTransientQueryFileDialog(const URI &fileURI, Project *, Error &, void *userData)
    {
        QtApplicationMenuBuilder *self = static_cast<QtApplicationMenuBuilder *>(userData);
        self->m_client->sendExecuteExportingImageMessage(fileURI);
    }
    static void
    handleCompleteExportImageConfiguration(void *userData, const StringList &availableExtensions)
    {
        QtApplicationMenuBuilder *self = static_cast<QtApplicationMenuBuilder *>(userData);
        if (!availableExtensions.empty()) {
            IFileManager *fileManager = self->m_fileManager;
            if (!fileManager->hasTransientQueryFileDialogCallback()) {
                const IFileManager::QueryFileDialogCallbacks callbacks = { self, handleTransientQueryFileDialog,
                    nullptr, nullptr };
                fileManager->setTransientQueryFileDialogCallback(callbacks);
                self->m_eventPublisher->publishQuerySaveFileDialogEvent(
                    IFileManager::kDialogTypeUserCallback, availableExtensions);
            }
        }
        else {
            self->m_client->sendExecuteExportingImageMessage(URI());
        }
    }
    static void
    handleSaveProjectAfterConfirm(void *userData)
    {
        QtApplicationMenuBuilder *self = static_cast<QtApplicationMenuBuilder *>(userData);
        self->m_client->addCompleteSavingFileEventListener(handleCompleteSavingFile, self, true);
        NewProjectEventHandler::saveProject(self);
    }
    static void
    handleCompleteSavingFile(void *userData, const URI &, nanoem_u32_t, nanoem_u64_t)
    {
        QtApplicationMenuBuilder *self = static_cast<QtApplicationMenuBuilder *>(userData);
        handleDiscardProjectAfterConfirm(self);
    }
    static void
    handleDiscardProjectAfterConfirm(void *userData)
    {
        QtApplicationMenuBuilder *self = static_cast<QtApplicationMenuBuilder *>(userData);
        self->m_client->addCompleteExportImageConfigurationEventListener(
            handleCompleteExportImageConfiguration, self, true);
        self->m_client->sendRequestExportImageConfigurationMessage();
    }
};

struct QtApplicationMenuBuilder::ExportVideoCallbackHandler {
    static void
    handleTransientQueryFileDialog(const URI &fileURI, Project *, Error &, void *userData)
    {
        QtApplicationMenuBuilder *self = static_cast<QtApplicationMenuBuilder *>(userData);
        self->m_client->sendExecuteExportingVideoMessage(fileURI);
    }
    static void
    handleCompleteExportVideoConfiguration(void *userData, const StringList &availableExtensions)
    {
        QtApplicationMenuBuilder *self = static_cast<QtApplicationMenuBuilder *>(userData);
        if (!availableExtensions.empty()) {
            IFileManager *fileManager = self->m_fileManager;
            if (!fileManager->hasTransientQueryFileDialogCallback()) {
                const IFileManager::QueryFileDialogCallbacks callbacks = { self, handleTransientQueryFileDialog,
                    nullptr, nullptr };
                fileManager->setTransientQueryFileDialogCallback(callbacks);
                self->m_eventPublisher->publishQuerySaveFileDialogEvent(
                    IFileManager::kDialogTypeUserCallback, availableExtensions);
            }
        }
        else {
            self->m_client->sendExecuteExportingVideoMessage(URI());
        }
    }
    static void
    handleSaveProjectAfterConfirm(void *userData)
    {
        QtApplicationMenuBuilder *self = static_cast<QtApplicationMenuBuilder *>(userData);
        self->m_client->addCompleteSavingFileEventListener(handleCompleteSavingFile, self, true);
        NewProjectEventHandler::saveProject(self);
    }
    static void
    handleCompleteSavingFile(void *userData, const URI &, nanoem_u32_t, nanoem_u64_t)
    {
        QtApplicationMenuBuilder *self = static_cast<QtApplicationMenuBuilder *>(userData);
        handleDiscardProjectAfterConfirm(self);
    }
    static void
    handleDiscardProjectAfterConfirm(void *userData)
    {
        QtApplicationMenuBuilder *self = static_cast<QtApplicationMenuBuilder *>(userData);
        self->m_client->addCompleteExportVideoConfigurationEventListener(
            handleCompleteExportVideoConfiguration, self, true);
        self->m_client->sendRequestExportVideoConfigurationMessage();
    }
};

struct QtApplicationMenuBuilder::QtMenuBar : QtApplicationMenuBuilder::QtMenuObject {
    QtMenuBar(QtApplicationMenuBuilder *parent)
        : m_parent(parent)
        , m_menu(new QMenu(parent->m_menuBar))
        , m_type(kMenuItemTypeMaxEnum)
        , m_handle(bx::kInvalidHandle)
    {
        parent->m_menus.push_back(m_menu);
    }
    QtMenuBar(QtApplicationMenuBuilder *parent, MenuItemType type)
        : m_parent(parent)
        , m_menu(new QMenu(parent->m_menuBar))
        , m_type(type)
        , m_handle(bx::kInvalidHandle)
    {
        if (type != kMenuItemTypeMaxEnum) {
            const String text(parent->translatedText(type));
            m_menu->setTitle(QString::fromUtf8(text.c_str()));
        }
        parent->m_menus.push_back(m_menu);
    }
    QtApplicationMenuBuilder *m_parent;
    QMenu *m_menu;
    QtMenuItemList m_items;
    MenuItemType m_type;
    nanoem_u16_t m_handle;
};

struct QtApplicationMenuBuilder::QtMenuItem : QtApplicationMenuBuilder::QtMenuObject {
    QtMenuItem(QtMenuBar *parent)
        : m_parent(parent)
        , m_action(nullptr)
        , m_type(kMenuItemTypeMaxEnum)
        , m_handle(bx::kInvalidHandle)
        , m_enabled(true)
        , m_checked(false)
        , m_separator(false)
    {
    }
    QtMenuItem(QtMenuBar *parent, MenuItemType type)
        : m_parent(parent)
        , m_action(nullptr)
        , m_type(type)
        , m_handle(bx::kInvalidHandle)
        , m_enabled(true)
        , m_checked(false)
        , m_separator(false)
    {
    }
    QtMenuBar *m_parent;
    QAction *m_action;
    MenuItemType m_type;
    String m_name;
    nanoem_u16_t m_handle;
    bool m_enabled;
    bool m_checked;
    bool m_separator;
};

QtApplicationMenuBuilder::QtApplicationMenuBuilder(BaseApplicationClient *client, IEventPublisher *eventPublisher,
    IFileManager *fileManager, const ITranslator *translator, BaseApplicationService *application, QMenuBar *menuBar,
    bool enableModelEditing)
    : ApplicationMenuBuilder(client, enableModelEditing)
    , m_translator(translator)
    , m_eventPublisher(eventPublisher)
    , m_fileManager(fileManager)
    , m_applicationPtr(application)
    , m_menuBar(menuBar)
{
    m_client->addQueryOpenSingleFileDialogEventListener(
        [](void *userData, nanoem_u32_t value, const StringList &allowedExtensions) {
            static_cast<QtApplicationMenuBuilder *>(userData)->showOpenFileDialog(value, allowedExtensions);
        },
        this, false);
    m_client->addQuerySaveFileDialogEventListener(
        [](void *userData, nanoem_u32_t value, const StringList &allowedExtensions) {
            static_cast<QtApplicationMenuBuilder *>(userData)->showSaveFileDialog(value, allowedExtensions);
        },
        this, false);
    build();
    createAllMenus();
}

QtApplicationMenuBuilder::~QtApplicationMenuBuilder() NANOEM_DECL_NOEXCEPT
{
    clearAllMenuItems();
}

void
QtApplicationMenuBuilder::createAllMenus()
{
    clearAllMenuItems();
    initialize();
    MainMenuBarHandle handle = reinterpret_cast<MainMenuBarHandle>(this);
    createFileMenu(handle);
    createEditMenu(handle);
    appendMenuSeparator(m_editMenu);
    appendMenuItem(m_editMenu, kMenuItemTypeEditPreference);
    createProjectMenu(handle);
    createCameraMenu(handle);
    createLightMenu(handle);
    createModelMenu(handle);
    createAccessoryMenu(handle);
    createHelpMenu(handle);
}

QtApplicationMenuBuilder::MenuBarHandle
QtApplicationMenuBuilder::createMenuBar()
{
    QtMenuBar *menu = nanoem_new(QtMenuBar(this));
    m_objects.push_back(menu);
    return reinterpret_cast<MenuBarHandle>(menu);
}

QtApplicationMenuBuilder::MenuBarHandle
QtApplicationMenuBuilder::createMenuBar(MenuItemType type)
{
    QtMenuBar *menu = nanoem_new(QtMenuBar(this, type));
    m_objects.push_back(menu);
    return reinterpret_cast<MenuBarHandle>(menu);
}

QtApplicationMenuBuilder::MenuItemHandle
QtApplicationMenuBuilder::createMenuItem(MainMenuBarHandle menu)
{
    BX_UNUSED_1(menu);
    QtMenuItem *item = nanoem_new(QtMenuItem(nullptr));
    m_objects.push_back(item);
    return reinterpret_cast<MenuItemHandle>(item);
}

QtApplicationMenuBuilder::MenuItemHandle
QtApplicationMenuBuilder::appendMenuItem(MenuBarHandle menu, MenuItemType type)
{
    QtMenuBar *parent = reinterpret_cast<QtMenuBar *>(menu);
    QtMenuItem *item = nanoem_new(QtMenuItem(parent, type));
    registerAction(parent, item, translatedText(type));
    m_menuItems.insert(tinystl::make_pair(type, reinterpret_cast<MenuItemHandle>(item)));
    return reinterpret_cast<MenuItemHandle>(item);
}

void
QtApplicationMenuBuilder::createSelectAccessoryMenuItem(MenuBarHandle menu, nanoem_u16_t handle, const char *name)
{
    QtMenuBar *parent = reinterpret_cast<QtMenuBar *>(menu);
    QtMenuItem *item = nanoem_new(QtMenuItem(parent, kMenuItemTypeAccessorySelectTitle));
    item->m_name = name;
    item->m_handle = handle;
    registerAction(parent, item, item->m_name);
}

void
QtApplicationMenuBuilder::createSelectModelMenuItem(MenuBarHandle menu, nanoem_u16_t handle, const char *name)
{
    QtMenuBar *parent = reinterpret_cast<QtMenuBar *>(menu);
    QtMenuItem *item = nanoem_new(QtMenuItem(parent, kMenuItemTypeModelSelectTitle));
    item->m_name = name;
    item->m_handle = handle;
    registerAction(parent, item, item->m_name);
}

void
QtApplicationMenuBuilder::createSelectBoneMenuItem(MenuBarHandle menu, const char *name)
{
    QtMenuBar *parent = reinterpret_cast<QtMenuBar *>(menu);
    QtMenuItem *item = nanoem_new(QtMenuItem(parent, kMenuItemTypeModelSelectBoneTitle));
    item->m_name = name;
    registerAction(parent, item, item->m_name);
}

void
QtApplicationMenuBuilder::createSelectMorphMenuItem(
    MenuBarHandle menu, nanoem_model_morph_category_t /* category */, const char *name)
{
    QtMenuBar *parent = reinterpret_cast<QtMenuBar *>(menu);
    QtMenuItem *item = nanoem_new(QtMenuItem(parent, kMenuItemTypeModelSelectMorphTitle));
    item->m_name = name;
    registerAction(parent, item, item->m_name);
}

void
QtApplicationMenuBuilder::createPluginMenuItem(
    MenuBarHandle menu, MenuItemType type, nanoem_u16_t handle, const String &name, const StringList &items)
{
    QtMenuBar *parent = reinterpret_cast<QtMenuBar *>(menu);
    QtMenuBar *baseMenuBar = nanoem_new(QtMenuBar(this));
    QtMenuItem *baseItem = nanoem_new(QtMenuItem(parent, type));
    baseMenuBar->m_handle = handle;
    baseItem->m_name = name;
    registerAction(parent, baseItem, name);
    nanoem_u16_t functionIndex = 0;
    for (StringList::const_iterator it = items.begin(), end = items.end(); it != end; ++it) {
        QtMenuItem *childMenuItem = nanoem_new(QtMenuItem(baseMenuBar, type));
        childMenuItem->m_handle = functionIndex++;
        childMenuItem->m_name = *it;
        registerAction(baseMenuBar, childMenuItem, *it);
    }
    setParentMenu(reinterpret_cast<MenuItemHandle>(baseItem), reinterpret_cast<MenuBarHandle>(baseMenuBar));
    m_objects.push_back(baseMenuBar);
}

void
QtApplicationMenuBuilder::updateAllSelectDrawableItems(MenuBarHandle menu, nanoem_u16_t handle)
{
    QtMenuBar *parent = reinterpret_cast<QtMenuBar *>(menu);
    for (QtMenuItemList::const_iterator it = parent->m_items.begin(), end = parent->m_items.end(); it != end; ++it) {
        QtMenuItem *item = *it;
        setMenuItemChecked(reinterpret_cast<MenuItemHandle>(item), item->m_handle == handle);
    }
}

void
QtApplicationMenuBuilder::removeMenuItemById(MenuBarHandle menu, int index) NANOEM_DECL_NOEXCEPT
{
    QtMenuBar *parent = reinterpret_cast<QtMenuBar *>(menu);
    if (parent && index >= 0 && index < Inline::saturateInt32(parent->m_items.size())) {
        QtMenuItem *item = parent->m_items[index];
        parent->m_items.erase(parent->m_items.begin() + index);
        if (item && item->m_action) {
            /* the action stays owned by the menu and is destroyed together with it */
            parent->m_menu->removeAction(item->m_action);
        }
    }
}

void
QtApplicationMenuBuilder::appendMenuSeparator(MenuBarHandle menu)
{
    QtMenuBar *parent = reinterpret_cast<QtMenuBar *>(menu);
    QtMenuItem *item = nanoem_new(QtMenuItem(parent));
    QAction *action = new QAction(parent->m_menu);
    action->setSeparator(true);
    parent->m_menu->addAction(action);
    item->m_separator = true;
    item->m_action = action;
    parent->m_items.push_back(item);
    m_objects.push_back(item);
}

void
QtApplicationMenuBuilder::clearAllMenuItems(MenuBarHandle menu) NANOEM_DECL_NOEXCEPT
{
    QtMenuBar *parent = reinterpret_cast<QtMenuBar *>(menu);
    if (parent && parent->m_menu) {
        parent->m_menu->clear();
        parent->m_items.clear();
    }
}

void
QtApplicationMenuBuilder::setParentMenu(MenuItemHandle parent, MenuBarHandle menu)
{
    QtMenuItem *parentItem = reinterpret_cast<QtMenuItem *>(parent);
    QtMenuBar *childMenu = reinterpret_cast<QtMenuBar *>(menu);
    if (parentItem->m_action) {
        parentItem->m_action->setMenu(childMenu->m_menu);
    }
    else {
        /* the placeholder item created by createMenuItem() hosts a top level menu */
        m_menuBar->addMenu(childMenu->m_menu);
    }
}

void
QtApplicationMenuBuilder::setMenuItemEnabled(MenuItemHandle item, bool value)
{
    QtMenuItem *menuItem = reinterpret_cast<QtMenuItem *>(item);
    menuItem->m_enabled = value;
    if (menuItem->m_action) {
        menuItem->m_action->setEnabled(value);
    }
}

void
QtApplicationMenuBuilder::setMenuItemChecked(MenuItemHandle item, bool value)
{
    QtMenuItem *menuItem = reinterpret_cast<QtMenuItem *>(item);
    menuItem->m_checked = value;
    if (menuItem->m_action) {
        menuItem->m_action->setCheckable(value);
        menuItem->m_action->setChecked(value);
    }
}

bool
QtApplicationMenuBuilder::isMenuItemEnabled(MenuItemHandle item) const NANOEM_DECL_NOEXCEPT
{
    const QtMenuItem *menuItem = reinterpret_cast<const QtMenuItem *>(item);
    return menuItem->m_enabled;
}

bool
QtApplicationMenuBuilder::isMenuItemChecked(MenuItemHandle item) const NANOEM_DECL_NOEXCEPT
{
    const QtMenuItem *menuItem = reinterpret_cast<const QtMenuItem *>(item);
    return menuItem->m_checked;
}

void
QtApplicationMenuBuilder::clearAllMenuItems()
{
    const QList<QAction *> actions(m_menuBar->actions());
    for (QList<QAction *>::const_iterator it = actions.begin(), end = actions.end(); it != end; ++it) {
        m_menuBar->removeAction(*it);
    }
    for (QtMenuObjectList::const_iterator it = m_objects.begin(), end = m_objects.end(); it != end; ++it) {
        nanoem_delete(*it);
    }
    m_objects.clear();
    /* destroy in reverse creation order so a submenu never outlives the menu owning its action */
    for (nanoem_rsize_t i = m_menus.size(); i > 0; i--) {
        delete m_menus[i - 1];
    }
    m_menus.clear();
}

void
QtApplicationMenuBuilder::createHelpMenu(MainMenuBarHandle handle)
{
    MenuItemHandle helpMenu = createMenuItem(handle);
    m_helpMenu = createMenuBar(kMenuItemTypeHelpTitle);
    setParentMenu(helpMenu, m_helpMenu);
    appendMenuItem(m_helpMenu, kMenuItemTypeHelpAbout);
}

QAction *
QtApplicationMenuBuilder::registerAction(QtMenuBar *parent, QtMenuItem *item, const String &text)
{
    QAction *action = new QAction(QString::fromUtf8(text.c_str()), parent->m_menu);
    action->setEnabled(item->m_enabled);
    QObject::connect(action, &QAction::triggered, [this, item]() { this->handleTriggered(item); });
    parent->m_menu->addAction(action);
    item->m_action = action;
    parent->m_items.push_back(item);
    m_objects.push_back(item);
    return action;
}

void
QtApplicationMenuBuilder::handleTriggered(QtMenuItem *item)
{
    QAction *action = item->m_action;
    if (action && action->isCheckable()) {
        /* checkable actions toggle themselves on click: restore the mirrored state first so a
           command dropped by the service (e.g. while a modal dialog is up) cannot leave the menu
           visually out of sync with the project */
        action->setChecked(item->m_checked);
    }
    switch (item->m_type) {
    case kMenuItemTypeModelSelectTitle: {
        m_client->sendSetActiveModelMessage(item->m_handle);
        break;
    }
    case kMenuItemTypeAccessorySelectTitle: {
        m_client->sendSetActiveAccessoryMessage(item->m_handle);
        break;
    }
    case kMenuItemTypeModelSelectBoneTitle: {
        m_client->sendSetActiveModelBoneMessage(item->m_name.c_str());
        break;
    }
    case kMenuItemTypeModelSelectMorphTitle: {
        m_client->sendSetActiveModelMorphMessage(item->m_name.c_str(), true);
        break;
    }
    case kMenuItemTypeModelPluginExecute: {
        m_client->sendExecuteModelPluginMessage(item->m_parent->m_handle, item->m_handle);
        break;
    }
    case kMenuItemTypeMotionPluginExecute: {
        m_client->sendExecuteMotionPluginMessage(item->m_parent->m_handle, item->m_handle);
        break;
    }
    default: {
        dispatchDefaultAction(item);
        break;
    }
    }
}

void
QtApplicationMenuBuilder::dispatchDefaultAction(QtMenuItem *item)
{
    switch (item->m_type) {
    case kMenuItemTypeFileNewProject: {
        m_client->sendIsProjectDirtyRequestMessage(NewProjectEventHandler::handleIsProjectDirtyResponse, this);
        break;
    }
    case kMenuItemTypeFileOpenProject: {
        m_client->sendIsProjectDirtyRequestMessage(OpenProjectEventHandler::handleIsProjectDirtyResponse, this);
        break;
    }
    case kMenuItemTypeFileImportAudioSource: {
        m_eventPublisher->publishQueryOpenSingleFileDialogEvent(
            IFileManager::kDialogTypeOpenAudioFile, BaseAudioPlayer::loadableExtensions());
        break;
    }
    case kMenuItemTypeFileImportAccessory: {
        m_eventPublisher->publishQueryOpenSingleFileDialogEvent(
            IFileManager::kDialogTypeOpenModelFile, Accessory::loadableExtensions());
        break;
    }
    case kMenuItemTypeFileImportModel: {
        m_eventPublisher->publishQueryOpenSingleFileDialogEvent(
            IFileManager::kDialogTypeOpenModelFile, Model::loadableExtensions());
        break;
    }
    case kMenuItemTypeFileImportModelPose: {
        m_eventPublisher->publishQueryOpenSingleFileDialogEvent(
            IFileManager::kDialogTypeOpenModelMotionFile, model::BindPose::loadableExtensions());
        break;
    }
    case kMenuItemTypeFileImportModelMotion: {
        m_eventPublisher->publishQueryOpenSingleFileDialogEvent(
            IFileManager::kDialogTypeOpenModelMotionFile, Motion::loadableExtensions());
        break;
    }
    case kMenuItemTypeFileImportLightMotion: {
        m_eventPublisher->publishQueryOpenSingleFileDialogEvent(
            IFileManager::kDialogTypeOpenLightMotionFile, Motion::loadableExtensions());
        break;
    }
    case kMenuItemTypeFileImportCameraMotion: {
        m_eventPublisher->publishQueryOpenSingleFileDialogEvent(
            IFileManager::kDialogTypeOpenCameraMotionFile, Motion::loadableExtensions());
        break;
    }
    case kMenuItemTypeFileSaveProject:
    case kMenuItemTypeFileSaveAsProject: {
        m_eventPublisher->publishQuerySaveFileDialogEvent(
            IFileManager::kDialogTypeSaveProjectFile, Project::loadableExtensions());
        break;
    }
    case kMenuItemTypeFileExportModelPose: {
        m_eventPublisher->publishQuerySaveFileDialogEvent(
            IFileManager::kDialogTypeSaveModelMotionFile, model::BindPose::loadableExtensions());
        break;
    }
    case kMenuItemTypeFileExportModel: {
        m_eventPublisher->publishQuerySaveFileDialogEvent(
            IFileManager::kDialogTypeSaveModelFile, Model::loadableExtensions());
        break;
    }
    case kMenuItemTypeFileExportModelMotion: {
        m_eventPublisher->publishQuerySaveFileDialogEvent(
            IFileManager::kDialogTypeSaveModelMotionFile, Motion::loadableExtensions());
        break;
    }
    case kMenuItemTypeFileExportLightMotion: {
        m_eventPublisher->publishQuerySaveFileDialogEvent(
            IFileManager::kDialogTypeSaveLightMotionFile, Motion::loadableExtensions());
        break;
    }
    case kMenuItemTypeFileExportCameraMotion: {
        m_eventPublisher->publishQuerySaveFileDialogEvent(
            IFileManager::kDialogTypeSaveCameraMotionFile, Motion::loadableExtensions());
        break;
    }
    case kMenuItemTypeFileExportImage: {
        m_client->addSaveProjectAfterConfirmEventListener(
            ExportImageCallbackHandler::handleSaveProjectAfterConfirm, this, true);
        m_client->addDiscardProjectAfterConfirmEventListener(
            ExportImageCallbackHandler::handleDiscardProjectAfterConfirm, this, true);
        m_client->sendConfirmBeforeExportingImageMessage();
        break;
    }
    case kMenuItemTypeFileExportVideo: {
        m_client->addSaveProjectAfterConfirmEventListener(
            ExportVideoCallbackHandler::handleSaveProjectAfterConfirm, this, true);
        m_client->addDiscardProjectAfterConfirmEventListener(
            ExportVideoCallbackHandler::handleDiscardProjectAfterConfirm, this, true);
        m_client->sendConfirmBeforeExportingVideoMessage();
        break;
    }
    case kMenuItemTypeFileExit: {
        m_eventPublisher->publishQuitApplicationEvent();
        break;
    }
    default: {
        m_client->sendMenuActionMessage(item->m_type);
        break;
    }
    }
}

void
QtApplicationMenuBuilder::showOpenFileDialog(nanoem_u32_t type, const StringList &allowedExtensions)
{
    /* listeners run inside the paint/dispatch cycle: defer the modal dialog to the next event loop
       tick so it never nests inside the dispatch (the frame timer keeps painting meanwhile) */
    const String initial(resolveInitialDirectory(type, m_applicationPtr));
    const StringList extensions(allowedExtensions);
    QMetaObject::invokeMethod(
        m_menuBar,
        [this, type, initial, extensions]() {
            const QString filter(buildFileFilter(extensions));
            const QString path(QFileDialog::getOpenFileName(
                m_menuBar, QStringLiteral("Open File"), QString::fromUtf8(initial.c_str()), filter));
            URI fileURI;
            if (!path.isEmpty()) {
                fileURI = URI::createFromFilePath(String(path.toUtf8().constData()));
                const QByteArray directory(QFileInfo(path).absolutePath().toUtf8());
                ApplicationPreference(m_applicationPtr).setLastDialogDirectory(type, directory.constData());
            }
            /* an empty URI cancels the pending query (mirrors the ImGui file dialog behaviour) */
            m_client->sendLoadFileMessage(fileURI, type);
        },
        Qt::QueuedConnection);
}

void
QtApplicationMenuBuilder::showSaveFileDialog(nanoem_u32_t type, const StringList &allowedExtensions)
{
    const String initial(resolveInitialDirectory(type, m_applicationPtr));
    const StringList extensions(allowedExtensions);
    QMetaObject::invokeMethod(
        m_menuBar,
        [this, type, initial, extensions]() {
            const QString filter(buildFileFilter(extensions));
            const QString path(QFileDialog::getSaveFileName(
                m_menuBar, QStringLiteral("Save File"), QString::fromUtf8(initial.c_str()), filter));
            URI fileURI;
            if (!path.isEmpty()) {
                fileURI = URI::createFromFilePath(String(path.toUtf8().constData()));
                if (fileURI.pathExtension().empty() && !extensions.empty()) {
                    String resolved(fileURI.absolutePathConstString());
                    resolved.append(".");
                    resolved.append(extensions[0].c_str());
                    fileURI = URI::createFromFilePath(resolved);
                }
                const QByteArray directory(QFileInfo(path).absolutePath().toUtf8());
                ApplicationPreference(m_applicationPtr).setLastDialogDirectory(type, directory.constData());
            }
            /* an empty URI cancels the pending query (mirrors the ImGui file dialog behaviour) */
            m_client->sendSaveFileMessage(fileURI, type);
        },
        Qt::QueuedConnection);
}

String
QtApplicationMenuBuilder::translatedText(MenuItemType type) const
{
    const char *key = menuItemString(type);
    const char *text = m_translator->translate(key);
    return String(text ? text : key);
}

} /* namespace qt */
} /* namespace nanoem */
