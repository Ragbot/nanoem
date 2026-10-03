/*
   Copyright (c) 2015-2023 hkrn All rights reserved

   This file is part of emapp component and it's licensed under Mozilla Public License. see LICENSE.md for more details.
 */

#include "QtPreferenceDialog.h"
#include "QtApplicationService.h"

#include "emapp/ApplicationMenuBuilder.h"
#include "emapp/ApplicationPreference.h"
#include "emapp/Error.h"
#include "emapp/Grid.h"
#include "emapp/IEventPublisher.h"
#include "emapp/ITranslator.h"
#include "emapp/Project.h"
#include "emapp/private/CommonInclude.h"

#include "undo/undo.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QVector>

#include <functional>

namespace nanoem {
namespace qt {
namespace {

/* the menu backed combos/checkboxes stash their item type table so they can be re-synced after a
   dispatch; the arrays outlive the dialog (namespace scope further down) */
const char *const kMenuItemTypesProperty = "nanoem.menuItemTypes";
const char *const kNumMenuItemTypesProperty = "nanoem.numMenuItemTypes";

const ApplicationMenuBuilder::MenuItemType kEnableCheckboxItemTypes[] = {
    ApplicationMenuBuilder::kMenuItemTypeProjectEnableGrid,
    ApplicationMenuBuilder::kMenuItemTypeProjectEnableGroundShadow,
    ApplicationMenuBuilder::kMenuItemTypeProjectEnableEffect,
};
const ApplicationMenuBuilder::MenuItemType kMSAAItemTypes[] = {
    ApplicationMenuBuilder::kMenuItemTypeProjectEnableMSAAx16,
    ApplicationMenuBuilder::kMenuItemTypeProjectEnableMSAAx8,
    ApplicationMenuBuilder::kMenuItemTypeProjectEnableMSAAx4,
    ApplicationMenuBuilder::kMenuItemTypeProjectEnableMSAAx2,
    ApplicationMenuBuilder::kMenuItemTypeProjectDisableMSAA,
};
const ApplicationMenuBuilder::MenuItemType kPhysicsSimulationItemTypes[] = {
    ApplicationMenuBuilder::kMenuItemTypeProjectPhysicsSimulationEnableAnytime,
    ApplicationMenuBuilder::kMenuItemTypeProjectPhysicsSimulationEnablePlaying,
    ApplicationMenuBuilder::kMenuItemTypeProjectPhysicsSimulationEnableTracing,
    ApplicationMenuBuilder::kMenuItemTypeProjectPhysicsSimulationDisable,
};
const ApplicationMenuBuilder::MenuItemType kPreferredFPSItemTypes[] = {
    ApplicationMenuBuilder::kMenuItemTypeProjectPreferredMotionFPSUnlimited,
    ApplicationMenuBuilder::kMenuItemTypeProjectPreferredMotionFPS60,
    ApplicationMenuBuilder::kMenuItemTypeProjectPreferredMotionFPS30,
};

const sg_pixel_format kPreferredColorPixelFormatMenuItems[] = {
    SG_PIXELFORMAT_RGBA8,
    SG_PIXELFORMAT_RGB10A2,
    SG_PIXELFORMAT_RGBA16F,
};
const nanoem_u32_t kPreferredFPSMenuItems[] = { 0, 30, 60, UINT32_MAX };

QString
translateKey(BaseApplicationService *app, const char *key)
{
    return QString::fromUtf8(app->translator()->translate(key));
}

QString
translateMenuItemKey(BaseApplicationService *app, nanoem_u32_t itemType)
{
    char buffer[Inline::kNameStackBufferSize];
    ApplicationMenuBuilder::stripMnemonic(buffer, sizeof(buffer), app->translateMenuItem(itemType));
    return QString::fromUtf8(buffer);
}

ApplicationMenuBuilder::MenuItemCheckedState
menuItemCheckedState(Project *project, ApplicationMenuBuilder::MenuItemType itemType)
{
    ApplicationMenuBuilder::MenuItemCheckedState state = ApplicationMenuBuilder::kMenuItemCheckedStateIndetermine;
    ApplicationMenuBuilder::validateMenuItem(project, itemType, state);
    return state;
}

QString
selectedPixelFormatString(BaseApplicationService *app, sg_pixel_format value)
{
    switch (value) {
    case SG_PIXELFORMAT_RGBA8:
    case _SG_PIXELFORMAT_DEFAULT:
        return translateKey(app, "nanoem.gui.window.preference.global.color-format.rgba8");
    case SG_PIXELFORMAT_RGB10A2:
        return translateKey(app, "nanoem.gui.window.preference.global.color-format.rgb10a2");
    case SG_PIXELFORMAT_RGBA16F:
        return translateKey(app, "nanoem.gui.window.preference.global.color-format.rgba16f");
    default:
        break;
    }
    return QString::fromUtf8("(Unknown)");
}

QString
selectedLanguageString(BaseApplicationService *app, ITranslator::LanguageType value)
{
    switch (value) {
    case ITranslator::kLanguageTypeEnglish:
        return translateKey(app, "nanoem.gui.window.preference.project.language.english");
    case ITranslator::kLanguageTypeJapanese:
        return translateKey(app, "nanoem.gui.window.preference.project.language.japanese");
    default:
        break;
    }
    return QString::fromUtf8("(Unknown)");
}

QString
selectedFPSMenuItemString(BaseApplicationService *app, nanoem_u32_t value)
{
    switch (value) {
    case UINT32_MAX:
        return translateKey(app, "nanoem.gui.window.preference.global.fps.unlimited");
    case 60:
        return translateKey(app, "nanoem.gui.window.preference.global.fps.60");
    case 30:
        return translateKey(app, "nanoem.gui.window.preference.global.fps.30");
    case 0:
        return translateKey(app, "nanoem.gui.window.preference.global.fps.auto");
    default:
        break;
    }
    return QString::fromUtf8("(Unknown)");
}

QString
selectedHighDPIViewportModeString(BaseApplicationService *app, ApplicationPreference::HighDPIViewportModeType value)
{
    switch (value) {
    case ApplicationPreference::kHighDPIViewportModeAuto:
        return translateKey(app, "nanoem.gui.window.preference.viewport.high-dpi.mode.auto");
    case ApplicationPreference::kHighDPIViewportModeEnabled:
        return translateKey(app, "nanoem.gui.window.preference.viewport.high-dpi.mode.enabled");
    case ApplicationPreference::kHighDPIViewportModeDisabled:
        return translateKey(app, "nanoem.gui.window.preference.viewport.high-dpi.mode.disabled");
    default:
        break;
    }
    return QString::fromUtf8("(Unknown)");
}

QString
selectedFilePathModeString(BaseApplicationService *app, Project::FilePathMode value)
{
    switch (value) {
    case Project::kFilePathModeAbsolute:
        return translateKey(app, "nanoem.gui.window.preference.file-path-mode.absolute");
    case Project::kFilePathModeRelative:
        return translateKey(app, "nanoem.gui.window.preference.file-path-mode.relative");
    default:
        break;
    }
    return QString::fromUtf8("(Unknown)");
}

void
syncMenuItemCombo(QComboBox *combo, BaseApplicationService *app, Project *project)
{
    const auto items = reinterpret_cast<const ApplicationMenuBuilder::MenuItemType *>(
        combo->property(kMenuItemTypesProperty).value<quintptr>());
    const int numItems = combo->property(kNumMenuItemTypesProperty).toInt();
    for (int i = 0; i < numItems; i++) {
        if (menuItemCheckedState(project, items[i]) == ApplicationMenuBuilder::kMenuItemCheckedStateTrue) {
            combo->setCurrentIndex(i);
            break;
        }
    }
}

QComboBox *
createMenuItemCombo(BaseApplicationService *app, Project *project, const ApplicationMenuBuilder::MenuItemType *items,
    nanoem_rsize_t numItems)
{
    auto *combo = new QComboBox;
    for (nanoem_rsize_t i = 0; i < numItems; i++) {
        combo->addItem(translateMenuItemKey(app, items[i]), QVariant::fromValue<quintptr>(quintptr(items[i])));
    }
    combo->setProperty(kMenuItemTypesProperty, QVariant::fromValue<quintptr>(quintptr(items)));
    combo->setProperty(kNumMenuItemTypesProperty, int(numItems));
    syncMenuItemCombo(combo, app, project);
    QObject::connect(combo, QOverload<int>::of(&QComboBox::activated), combo, [app, project, combo]() {
        const nanoem_u32_t itemType = nanoem_u32_t(combo->currentData().value<quintptr>());
        Error error;
        app->dispatchMenuItemAction(project, itemType, error);
        error.notify(project->eventPublisher());
        syncMenuItemCombo(combo, app, project);
    });
    return combo;
}

QCheckBox *
createMenuItemCheckBox(BaseApplicationService *app, Project *project, ApplicationMenuBuilder::MenuItemType itemType)
{
    auto *checkBox = new QCheckBox(translateMenuItemKey(app, itemType));
    checkBox->setChecked(menuItemCheckedState(project, itemType) == ApplicationMenuBuilder::kMenuItemCheckedStateTrue);
    QObject::connect(checkBox, &QCheckBox::toggled, checkBox, [app, project, itemType](bool checked) {
        if ((menuItemCheckedState(project, itemType) == ApplicationMenuBuilder::kMenuItemCheckedStateTrue) != checked) {
            Error error;
            app->dispatchMenuItemAction(project, itemType, error);
            error.notify(project->eventPublisher());
        }
    });
    return checkBox;
}

QDoubleSpinBox *
createDoubleSpinBox(double minimum, double maximum, double step, int decimals)
{
    auto *spinBox = new QDoubleSpinBox;
    spinBox->setRange(minimum, maximum);
    spinBox->setSingleStep(step);
    spinBox->setDecimals(decimals);
    return spinBox;
}

QSpinBox *
createSpinBox(int minimum, int maximum)
{
    auto *spinBox = new QSpinBox;
    spinBox->setRange(minimum, maximum);
    return spinBox;
}

QWidget *
createVectorRow(const std::function<QSpinBox *(int)> &createAxis, const std::function<void(int, int)> &assign)
{
    auto *row = new QWidget;
    auto *rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    for (int i = 0; i < 2; i++) {
        auto *spinBox = createAxis(i);
        QObject::connect(spinBox, QOverload<int>::of(&QSpinBox::valueChanged), spinBox,
            [assign, i](int value) { assign(i, value); });
        rowLayout->addWidget(spinBox, 1);
    }
    return row;
}

QWidget *
createDoubleVectorRow(
    const std::function<QDoubleSpinBox *(int)> &createAxis, const std::function<void(int, double)> &assign)
{
    auto *row = new QWidget;
    auto *rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    for (int i = 0; i < 2; i++) {
        auto *spinBox = createAxis(i);
        QObject::connect(spinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged), spinBox,
            [assign, i](double value) { assign(i, value); });
        rowLayout->addWidget(spinBox, 1);
    }
    return row;
}

} /* namespace anonymous */

QtPreferenceDialog::QtPreferenceDialog(QtApplicationService *service, Project *project, QWidget *parent)
    : QDialog(parent, Qt::Dialog | Qt::Window)
    , m_service(service)
    , m_project(project)
{
    setWindowTitle(translate("nanoem.gui.window.preference.title"));
    auto *layout = new QVBoxLayout(this);
    auto *tabs = new QTabWidget(this);
    tabs->addTab(createGlobalTab(), translate("nanoem.gui.window.preference.tab.global"));
    tabs->addTab(createProjectTab(), translate("nanoem.gui.window.preference.tab.project"));
    tabs->addTab(createSpecialTab(), QString::fromUtf8("Special"));
    layout->addWidget(tabs);
    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    /* like the ImGui dialog every control applies live and Cancel reverts nothing, so both buttons
       only dismiss the window */
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QtPreferenceDialog::close);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QtPreferenceDialog::close);
    layout->addWidget(buttonBox);
}

QWidget *
QtPreferenceDialog::createGlobalTab()
{
    BaseApplicationService *app = m_service;
    auto *widget = new QWidget(this);
    auto *layout = new QVBoxLayout(widget);
    auto *form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    {
        ApplicationPreference preference(app);
        const StringList renderers(preference.allAvailableRenderers());
        const char *currentRenderer = preference.rendererBackend();
        auto *combo = new QComboBox(widget);
        for (StringList::const_iterator it = renderers.begin(), end = renderers.end(); it != end; ++it) {
            combo->addItem(QString::fromUtf8(it->c_str()));
        }
        combo->setCurrentText(QString::fromUtf8(currentRenderer ? currentRenderer : ""));
        form->addRow(translate("nanoem.gui.window.preference.global.renderer"), combo);
        QObject::connect(combo, QOverload<int>::of(&QComboBox::activated), combo, [app, combo](int index) {
            const QByteArray name(combo->itemText(index).toUtf8());
            ApplicationPreference preference(app);
            preference.setRendererBackend(name.constData());
        });
    }
    {
        ApplicationPreference preference(app);
        const sg_pixel_format currentFormat = preference.defaultColorPixelFormat();
        auto *combo = new QComboBox(widget);
        for (size_t i = 0; i < BX_COUNTOF(kPreferredColorPixelFormatMenuItems); i++) {
            const sg_pixel_format menuItemValue = kPreferredColorPixelFormatMenuItems[i];
            combo->addItem(
                selectedPixelFormatString(app, menuItemValue), QVariant::fromValue<quintptr>(quintptr(menuItemValue)));
            if (menuItemValue == currentFormat) {
                combo->setCurrentIndex(int(i));
            }
        }
        form->addRow(translate("nanoem.gui.window.preference.global.color-format"), combo);
        QObject::connect(combo, QOverload<int>::of(&QComboBox::activated), combo, [app, combo](int index) {
            ApplicationPreference preference(app);
            preference.setDefaultColorPixelFormat(sg_pixel_format(combo->itemData(index).value<quintptr>()));
        });
    }
    {
        const nanoem_u32_t currentFPS = m_project->preferredEditingFPS();
        auto *combo = new QComboBox(widget);
        for (size_t i = 0; i < BX_COUNTOF(kPreferredFPSMenuItems); i++) {
            const nanoem_u32_t menuItemValue = kPreferredFPSMenuItems[i];
            combo->addItem(
                selectedFPSMenuItemString(app, menuItemValue), QVariant::fromValue<quintptr>(quintptr(menuItemValue)));
            if (menuItemValue == currentFPS) {
                combo->setCurrentIndex(int(i));
            }
        }
        form->addRow(translate("nanoem.gui.window.preference.global.fps"), combo);
        m_fpsCombo = combo;
        QObject::connect(combo, QOverload<int>::of(&QComboBox::activated), combo, [this](int index) {
            m_project->setPreferredEditingFPS(nanoem_u32_t(m_fpsCombo->itemData(index).value<quintptr>()));
        });
    }
    {
        ApplicationPreference preference(app);
        const ApplicationPreference::HighDPIViewportModeType currentMode = preference.highDPIViewportMode();
        auto *combo = new QComboBox(widget);
        for (int i = ApplicationPreference::kHighDPIViewportModeFirstEnum;
            i < ApplicationPreference::kHighDPIViewportModeMaxEnum; i++) {
            const ApplicationPreference::HighDPIViewportModeType menuItemValue =
                static_cast<ApplicationPreference::HighDPIViewportModeType>(i);
            combo->addItem(selectedHighDPIViewportModeString(app, menuItemValue));
            if (menuItemValue == currentMode) {
                combo->setCurrentIndex(combo->count() - 1);
            }
        }
        form->addRow(translate("nanoem.gui.window.preference.viewport.high-dpi.mode"), combo);
        QObject::connect(combo, QOverload<int>::of(&QComboBox::activated), combo, [app](int index) {
            ApplicationPreference preference(app);
            preference.setHighDPIViewportMode(static_cast<ApplicationPreference::HighDPIViewportModeType>(index));
        });
    }
    layout->addLayout(form);
    {
        ApplicationPreference preference(app);
        auto *modelEditingCheckBox =
            new QCheckBox(translate("nanoem.gui.window.preference.global.editing-model.enable"), widget);
        modelEditingCheckBox->setChecked(preference.isModelEditingEnabled());
        QObject::connect(modelEditingCheckBox, &QCheckBox::toggled, modelEditingCheckBox, [app](bool checked) {
            ApplicationPreference preference(app);
            preference.setModelEditingEnabled(checked);
        });
        layout->addWidget(modelEditingCheckBox);
        auto *skinDeformerCheckBox = new QCheckBox(translate("nanoem.gui.window.preference.global.sda.enable"), widget);
        skinDeformerCheckBox->setChecked(preference.isSkinDeformAcceleratorEnabled());
        QObject::connect(skinDeformerCheckBox, &QCheckBox::toggled, skinDeformerCheckBox, [app](bool checked) {
            ApplicationPreference preference(app);
            preference.setSkinDeformAcceleratorEnabled(checked);
        });
        layout->addWidget(skinDeformerCheckBox);
        auto *naturalScrollingCheckBox = new QCheckBox(QString::fromUtf8("Natural scrolling (mouse drag)"), widget);
        naturalScrollingCheckBox->setChecked(preference.isNaturalScrollingEnabled());
        QObject::connect(
            naturalScrollingCheckBox, &QCheckBox::toggled, naturalScrollingCheckBox, [app, this](bool checked) {
                ApplicationPreference preference(app);
                preference.setNaturalScrollingEnabled(checked);
                m_project->setNaturalScrollingEnabled(checked);
            });
        layout->addWidget(naturalScrollingCheckBox);
    }
    {
        ApplicationPreference preference(app);
        /* copy the current value: writing the preference replaces the string parson owns */
        const char *currentDevice = preference.audioOutputDevice();
        const String currentDeviceValue(currentDevice ? currentDevice : "");
        const String currentLabel(preference.audioOutputDeviceDescription(currentDeviceValue.c_str()));
        const StringList devices(preference.allAvailableAudioOutputDevices());
        auto *combo = new QComboBox(widget);
        for (StringList::const_iterator it = devices.begin(), end = devices.end(); it != end; ++it) {
            const String name(*it);
            const String label(preference.audioOutputDeviceDescription(name.c_str()));
            combo->addItem(QString::fromUtf8(label.c_str()), QByteArray(name.c_str()));
        }
        combo->setCurrentText(QString::fromUtf8(currentLabel.c_str()));
        auto *audioForm = new QFormLayout;
        audioForm->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
        audioForm->addRow(QString::fromUtf8("Audio output device"), combo);
        layout->addLayout(audioForm);
        QObject::connect(combo, QOverload<int>::of(&QComboBox::activated), combo, [app, combo](int index) {
            const QByteArray name(combo->itemData(index).toByteArray());
            ApplicationPreference preference(app);
            preference.setAudioOutputDevice(name.constData());
        });
        auto *hint = new QLabel(QString::fromUtf8("Takes effect on the next playback"), widget);
        hint->setStyleSheet(QString::fromUtf8("color: gray;"));
        layout->addWidget(hint);
    }
    layout->addSpacing(8);
    auto *systemInformation = new QLabel(widget);
    systemInformation->setWordWrap(true);
    systemInformation->setText(QString::fromUtf8("CPU: %1 (%2)\nPlatform: %3\nCompiler: %4 (%5)\nCRT: %6")
            .arg(QString::fromUtf8(BX_CPU_NAME), QString::fromUtf8(BX_ARCH_NAME), QString::fromUtf8(BX_PLATFORM_NAME),
                QString::fromUtf8(BX_COMPILER_NAME), QString::fromUtf8(BX_CPP_NAME), QString::fromUtf8(BX_CRT_NAME)));
    layout->addWidget(systemInformation);
    layout->addStretch(1);
    return widget;
}

QWidget *
QtPreferenceDialog::createProjectTab()
{
    BaseApplicationService *app = m_service;
    auto *widget = new QWidget(this);
    auto *layout = new QVBoxLayout(widget);
    auto *form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    {
        ITranslator *translator = app->translator();
        const ITranslator::LanguageType language = translator->language();
        auto *combo = new QComboBox(widget);
        for (int i = ITranslator::kLanguageTypeFirstEnum; i < ITranslator::kLanguageTypeMaxEnum; i++) {
            const ITranslator::LanguageType type = static_cast<ITranslator::LanguageType>(i);
            if (translator->isSupportedLanguage(type)) {
                combo->addItem(selectedLanguageString(app, type), QVariant::fromValue<quintptr>(quintptr(type)));
                if (type == language) {
                    combo->setCurrentIndex(combo->count() - 1);
                }
            }
        }
        /* the ImGui dialog disables the combo while model editing is enabled */
        combo->setEnabled(!m_project->isModelEditingEnabled());
        form->addRow(translate("nanoem.gui.window.preference.project.language.title"), combo);
        m_languageCombo = combo;
        QObject::connect(combo, QOverload<int>::of(&QComboBox::activated), combo, [this](int index) {
            const ITranslator::LanguageType type =
                static_cast<ITranslator::LanguageType>(m_languageCombo->itemData(index).value<quintptr>());
            m_project->setLanguage(type);
            m_service->translator()->setLanguage(type);
            /* the Qt shell builds its menu bar once and has no live retranslation pass, so dismiss
               the dialog: reopening it shows every label in the new language */
            QTimer::singleShot(0, this, &QtPreferenceDialog::close);
        });
    }
    layout->addLayout(form);
    for (nanoem_rsize_t i = 0; i < BX_COUNTOF(kEnableCheckboxItemTypes); i++) {
        layout->addWidget(createMenuItemCheckBox(app, m_project, kEnableCheckboxItemTypes[i]));
    }
    {
        Grid *grid = m_project->grid();
        const Vector2 gridSize(grid->size());
        const Vector2SI32 numCells(grid->cell());
        auto *gridForm = new QFormLayout;
        gridForm->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
        gridForm->addRow(new QLabel(translate("nanoem.gui.window.preference.grid.title"), widget));
        gridForm->addRow(translate("nanoem.gui.window.preference.grid.size.cell"),
            createDoubleVectorRow(
                [gridSize](int i) {
                    auto *spinBox = createDoubleSpinBox(0.01, 1000000.0, 0.01, 3);
                    spinBox->setValue(i == 0 ? gridSize.x : gridSize.y);
                    return spinBox;
                },
                [this](int i, double value) {
                    Vector2 size(m_project->grid()->size());
                    (i == 0 ? size.x : size.y) = nanoem_f32_t(value);
                    m_project->grid()->resize(size);
                }));
        gridForm->addRow(translate("nanoem.gui.window.preference.grid.size.grid"),
            createVectorRow(
                [numCells](int i) {
                    auto *spinBox = createSpinBox(1, 0xff);
                    spinBox->setValue(i == 0 ? numCells.x : numCells.y);
                    return spinBox;
                },
                [this](int i, int value) {
                    Vector2 cells(m_project->grid()->cell());
                    (i == 0 ? cells.x : cells.y) = nanoem_f32_t(value);
                    m_project->grid()->setCell(cells);
                }));
        layout->addLayout(gridForm);
    }
    {
        auto *settingsForm = new QFormLayout;
        settingsForm->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
        settingsForm->addRow(translateMenuItemKey(app, ApplicationMenuBuilder::kMenuItemTypeProjectMSAATitle),
            createMenuItemCombo(app, m_project, kMSAAItemTypes, BX_COUNTOF(kMSAAItemTypes)));
        settingsForm->addRow(
            translateMenuItemKey(app, ApplicationMenuBuilder::kMenuItemTypeProjectPhysicsSimulationTitle),
            createMenuItemCombo(app, m_project, kPhysicsSimulationItemTypes, BX_COUNTOF(kPhysicsSimulationItemTypes)));
        settingsForm->addRow(translateMenuItemKey(app, ApplicationMenuBuilder::kMenuItemTypeProjectPreferredFPSTitle),
            createMenuItemCombo(app, m_project, kPreferredFPSItemTypes, BX_COUNTOF(kPreferredFPSItemTypes)));
        layout->addLayout(settingsForm);
    }
    {
        const Project::FilePathMode currentMode = m_project->filePathMode();
        auto *combo = new QComboBox(widget);
        for (size_t i = Project::kFilePathModeFirstEnum; i < Project::kFilePathModeMaxEnum; i++) {
            const Project::FilePathMode mode = static_cast<Project::FilePathMode>(i);
            combo->addItem(selectedFilePathModeString(app, mode), QVariant::fromValue<quintptr>(quintptr(mode)));
            if (mode == currentMode) {
                combo->setCurrentIndex(combo->count() - 1);
            }
        }
        auto *fileForm = new QFormLayout;
        fileForm->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
        fileForm->addRow(translate("nanoem.gui.window.preference.file-path-mode.title"), combo);
        layout->addLayout(fileForm);
        m_filePathModeCombo = combo;
        QObject::connect(combo, QOverload<int>::of(&QComboBox::activated), combo, [this](int index) {
            m_project->setFilePathMode(
                static_cast<Project::FilePathMode>(m_filePathModeCombo->itemData(index).value<quintptr>()));
        });
    }
    layout->addStretch(1);
    return widget;
}

QWidget *
QtPreferenceDialog::createSpecialTab()
{
    BaseApplicationService *app = m_service;
    auto *widget = new QWidget(this);
    auto *layout = new QVBoxLayout(widget);
    ApplicationPreference preference(app);
    auto *form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    form->addRow(new QLabel(QString::fromUtf8("Application Preference"), widget));
    struct PoolSetting {
        const char *m_label;
        int m_minimum;
        int m_maximum;
        int m_defaultValue;
        int m_initialValue;
        std::function<void(int)> m_setter;
    };
    const PoolSetting poolSettings[] = {
        { "Buffer Pool Size", 1024, 0xffff, ApplicationPreference::kGFXBufferPoolSizeDefaultValue,
            preference.gfxBufferPoolSize(),
            [app](int value) { ApplicationPreference(app).setGFXBufferPoolSize(value); } },
        { "Image Pool Size", 2048, 0xffff, ApplicationPreference::kGFXImagePoolSizeDefaultValue,
            preference.gfxImagePoolSize(),
            [app](int value) { ApplicationPreference(app).setGFXImagePoolSize(value); } },
        { "Shader Pool Size", 1024, 0xffff, ApplicationPreference::kGFXShaderPoolSizeDefaultValue,
            preference.gfxShaderPoolSize(),
            [app](int value) { ApplicationPreference(app).setGFXShaderPoolSize(value); } },
        { "Pass Pool Size", 512, 0xffff, ApplicationPreference::kGFXPassPoolSizeDefaultValue,
            preference.gfxPassPoolSize(), [app](int value) { ApplicationPreference(app).setGFXPassPoolSize(value); } },
        { "Pipeline Pool Size", 1024, 0xffff, ApplicationPreference::kGFXPipelinePoolSizeDefaultValue,
            preference.gfxPipelinePoolSize(),
            [app](int value) { ApplicationPreference(app).setGFXPipelinePoolSize(value); } },
        { "Uniform Buffer Size", 0x10000, 0x7fffff, ApplicationPreference::kGFXUniformBufferSizeDefaultValue,
            preference.gfxUniformBufferSize(),
            [app](int value) { ApplicationPreference(app).setGFXUniformBufferSize(value); } },
    };
    QVector<QPair<QSpinBox *, int>> defaults;
    {
        auto *spinBox = createSpinBox(64, qMax(64, undoStackGetHardLimit()));
        spinBox->setValue(preference.undoSoftLimit());
        form->addRow(QString::fromUtf8("Undo Soft Limit"), spinBox);
        QObject::connect(spinBox, QOverload<int>::of(&QSpinBox::valueChanged), spinBox,
            [app](int value) { ApplicationPreference(app).setUndoSoftLimit(value); });
        defaults.append(qMakePair(spinBox, int(ApplicationPreference::kUndoSoftLimitDefaultValue)));
    }
    for (nanoem_rsize_t i = 0; i < BX_COUNTOF(poolSettings); i++) {
        const PoolSetting &setting = poolSettings[i];
        auto *spinBox = createSpinBox(setting.m_minimum, setting.m_maximum);
        spinBox->setValue(setting.m_initialValue);
        form->addRow(QString::fromUtf8(setting.m_label), spinBox);
        const std::function<void(int)> setter(setting.m_setter);
        QObject::connect(
            spinBox, QOverload<int>::of(&QSpinBox::valueChanged), spinBox, [setter](int value) { setter(value); });
        defaults.append(qMakePair(spinBox, setting.m_defaultValue));
    }
    layout->addLayout(form);
    auto *resetButton = new QPushButton(QString::fromUtf8("Initialize"), widget);
    QObject::connect(resetButton, &QPushButton::clicked, resetButton, [defaults]() {
        for (nanoem_rsize_t i = 0; i < nanoem_rsize_t(defaults.size()); i++) {
            defaults[i].first->setValue(defaults[i].second);
        }
    });
    layout->addWidget(resetButton);
    layout->addStretch(1);
    return widget;
}

void
QtPreferenceDialog::closeEvent(QCloseEvent *event)
{
    /* preferences are applied live; persist them as soon as the dialog is dismissed */
    m_service->saveApplicationConfiguration();
    QDialog::closeEvent(event);
}

QString
QtPreferenceDialog::translate(const char *key) const
{
    return QString::fromUtf8(m_service->translator()->translate(key));
}

} /* namespace qt */
} /* namespace nanoem */
