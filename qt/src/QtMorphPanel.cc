/*
   Copyright (c) 2015-2023 hkrn All rights reserved

   This file is part of emapp component and it's licensed under Mozilla Public License. see LICENSE.md for more details.
 */

#include "QtMorphPanel.h"
#include "QtApplicationService.h"

#include "emapp/IProjectHolder.h"
#include "emapp/ITranslator.h"
#include "emapp/Model.h"
#include "emapp/Project.h"
#include "emapp/internal/DraggingMorphState.h"
#include "emapp/internal/imgui/MorphSelector.h"
#include "emapp/model/Morph.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

namespace nanoem {
namespace qt {
namespace {

static QString
translate(QtApplicationService *service, const char *key)
{
    return QString::fromUtf8(service->translator()->translate(key));
}

struct CategorySpec {
    nanoem_model_morph_category_t m_category;
    const char *m_categoryKey;
    const char *m_registerKey;
};

static const CategorySpec kCategories[] = {
    { NANOEM_MODEL_MORPH_CATEGORY_EYE, "nanoem.gui.panel.morph.eye", "nanoem.gui.panel.morph.eye.register" },
    { NANOEM_MODEL_MORPH_CATEGORY_LIP, "nanoem.gui.panel.morph.lip", "nanoem.gui.panel.morph.lip.register" },
    { NANOEM_MODEL_MORPH_CATEGORY_EYEBROW,
        "nanoem.gui.panel.morph.eyebrow", "nanoem.gui.panel.morph.eyebrow.register" },
    { NANOEM_MODEL_MORPH_CATEGORY_OTHER, "nanoem.gui.panel.morph.other", "nanoem.gui.panel.morph.other.register" },
};

} /* namespace anonymous */

QtMorphPanel::QtMorphPanel(QtApplicationService *service, QWidget *parent)
    : QGroupBox(translate(service, "nanoem.gui.panel.morph.title"), parent)
    , m_service(service)
    , m_timer(nullptr)
{
    auto *layout = new QVBoxLayout(this);
    auto *grid = new QGridLayout;
    for (int i = 0; i < 4; i++) {
        Row &row = m_rows[i];
        const CategorySpec &spec = kCategories[i];
        row.category = spec.m_category;
        row.comboBox = new QComboBox(this);
        row.comboBox->setToolTip(translate(m_service, spec.m_categoryKey));
        row.weightSpinBox = new QDoubleSpinBox(this);
        row.weightSpinBox->setRange(0.0, 1.0);
        row.weightSpinBox->setSingleStep(0.01);
        row.weightSpinBox->setDecimals(2);
        row.weightSpinBox->setKeyboardTracking(false);
        row.weightSpinBox->setToolTip(translate(m_service, "nanoem.gui.panel.morph.weight.format"));
        row.registerButton = new QPushButton(translate(m_service, spec.m_registerKey), this);
        /* eye/lip share the first block of rows, eyebrow/other the second (like the ImGui panel) */
        const int block = i / 2;
        const int column = i % 2;
        grid->addWidget(row.comboBox, block * 3 + 0, column);
        grid->addWidget(row.weightSpinBox, block * 3 + 1, column);
        grid->addWidget(row.registerButton, block * 3 + 2, column);
        connect(row.comboBox, &QComboBox::activated, this, [this, &row](int index) { selectMorph(row, index); });
        connect(row.weightSpinBox, &QDoubleSpinBox::valueChanged, this,
            [this, &row](double value) { handleWeightChanged(row, value); });
        connect(row.weightSpinBox, &QDoubleSpinBox::editingFinished, this, [this, &row]() { commitWeight(row); });
    }
    layout->addLayout(grid);
    m_timer = new QTimer(this);
    m_timer->setInterval(100);
    connect(m_timer, &QTimer::timeout, this, [this]() { refresh(); });
    m_timer->start();
    refresh();
}

Project *
QtMorphPanel::currentProject()
{
    return m_service->projectHolder()->currentProject();
}

Model *
QtMorphPanel::activeModel()
{
    Project *project = currentProject();
    return project ? project->activeModel() : nullptr;
}

void
QtMorphPanel::selectMorph(Row &row, int index)
{
    Model *model = activeModel();
    if (model) {
        const nanoem_model_morph_category_t category = static_cast<nanoem_model_morph_category_t>(row.category);
        const internal::imgui::MorphSelector selector(model, m_service->translator(), category);
        model->setActiveMorph(category, selector.activeMorph(index));
        refresh();
    }
}

void
QtMorphPanel::handleWeightChanged(Row &row, double value)
{
    Project *project = currentProject();
    if (project && !project->isPlaying()) {
        Model *model = project->activeModel();
        if (model) {
            const nanoem_model_morph_category_t category = static_cast<nanoem_model_morph_category_t>(row.category);
            const nanoem_model_morph_t *morphPtr = model->activeMorph(category);
            if (morphPtr) {
                if (!row.state) {
                    const model::Morph *morph = model::Morph::cast(morphPtr);
                    row.state = new internal::DraggingMorphSliderState(
                        morphPtr, model, project, morph->weight());
                    row.stateModel = model;
                }
                row.state->deform(static_cast<nanoem_f32_t>(value));
            }
        }
    }
}

void
QtMorphPanel::commitWeight(Row &row)
{
    if (row.state) {
        row.state->commit();
        delete row.state;
        row.state = nullptr;
        row.stateModel = nullptr;
    }
}

void
QtMorphPanel::refresh()
{
    Project *project = currentProject();
    Model *model = project ? project->activeModel() : nullptr;
    const bool visible = project != nullptr && model != nullptr;
    if (isVisible() != visible) {
        setVisible(visible);
    }
    if (!project || !model) {
        for (Row &row : m_rows) {
            if (row.state) {
                delete row.state;
                row.state = nullptr;
                row.stateModel = nullptr;
            }
        }
        return;
    }
    for (Row &row : m_rows) {
        refreshRow(row, project, model);
    }
}

void
QtMorphPanel::refreshRow(Row &row, Project *project, Model *model)
{
    const nanoem_model_morph_category_t category = static_cast<nanoem_model_morph_category_t>(row.category);
    const internal::imgui::MorphSelector selector(model, m_service->translator(), category);
    const int count = selector.count();
    bool rebuild = row.comboBox->count() != count || row.selectorModel != model;
    for (int i = 0; !rebuild && i < count; i++) {
        const char *text = nullptr;
        selector.select(i, &text);
        if (row.comboBox->itemText(i) != QString::fromUtf8(text)) {
            rebuild = true;
        }
    }
    if (rebuild) {
        row.comboBox->clear();
        for (int i = 0; i < count; i++) {
            const char *text = nullptr;
            selector.select(i, &text);
            row.comboBox->addItem(QString::fromUtf8(text));
        }
        row.selectorModel = model;
    }
    row.comboBox->setCurrentIndex(selector.index());
    const nanoem_model_morph_t *morphPtr = model->activeMorph(category);
    const model::Morph *morph = model::Morph::cast(morphPtr);
    const bool canRegister = selector.canRegister(model);
    row.weightSpinBox->blockSignals(true);
    if (!row.state && !row.weightSpinBox->hasFocus()) {
        row.weightSpinBox->setValue(morph ? morph->weight() : 0.0f);
    }
    row.weightSpinBox->setEnabled(canRegister || row.weightSpinBox->hasFocus());
    row.weightSpinBox->blockSignals(false);
    row.registerButton->setEnabled(canRegister);
    if (row.state && (row.stateModel != model || project->isPlaying())) {
        delete row.state;
        row.state = nullptr;
        row.stateModel = nullptr;
    }
}

} /* namespace qt */
} /* namespace nanoem */
