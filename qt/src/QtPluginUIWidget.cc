/*
   Copyright (c) 2015-2023 hkrn All rights reserved

   This file is part of emapp component and it's licensed under Mozilla Public License. see LICENSE.md for more details.
 */

#include "QtPluginUIWidget.h"

#include "emapp/private/CommonInclude.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <climits>
#include <cstdint>
#include <cstring>

namespace nanoem {
namespace qt {
namespace {

typedef Nanoem__Application__Plugin__UIComponent__DataType DataType;
typedef Nanoem__Application__Plugin__UIComponent Component;

static nanoem_rsize_t
requiredByteSize(DataType from, nanoem_rsize_t numComponents)
{
    switch (from) {
    case NANOEM__APPLICATION__PLUGIN__UICOMPONENT__DATA_TYPE__S8:
    case NANOEM__APPLICATION__PLUGIN__UICOMPONENT__DATA_TYPE__U8:
        return sizeof(nanoem_u8_t) * numComponents;
    case NANOEM__APPLICATION__PLUGIN__UICOMPONENT__DATA_TYPE__S16:
    case NANOEM__APPLICATION__PLUGIN__UICOMPONENT__DATA_TYPE__U16:
        return sizeof(nanoem_u16_t) * numComponents;
    case NANOEM__APPLICATION__PLUGIN__UICOMPONENT__DATA_TYPE__S32:
    case NANOEM__APPLICATION__PLUGIN__UICOMPONENT__DATA_TYPE__U32:
        return sizeof(nanoem_u32_t) * numComponents;
    case NANOEM__APPLICATION__PLUGIN__UICOMPONENT__DATA_TYPE__FLOAT:
        return sizeof(nanoem_f32_t) * numComponents;
    case NANOEM__APPLICATION__PLUGIN__UICOMPONENT__DATA_TYPE__DOUBLE:
        return sizeof(nanoem_f64_t) * numComponents;
    default:
        break;
    }
    return ~nanoem_rsize_t(0);
}

/**
 * @brief Mirrors the PluginUI::validate gate: the widget is only drawn when the value buffer
 *        covers the declared data type and component count (and, for drag/slider, when the same
 *        holds for the min/max buffers).
 */
static bool
validate(DataType from, nanoem_rsize_t numComponents, const ProtobufCBinaryData &value)
{
    const nanoem_rsize_t required = requiredByteSize(from, numComponents);
    return numComponents > 0 && required != ~nanoem_rsize_t(0) && value.data != nullptr && value.len >= required;
}

static bool
isFloatingPoint(DataType from)
{
    return from == NANOEM__APPLICATION__PLUGIN__UICOMPONENT__DATA_TYPE__FLOAT ||
        from == NANOEM__APPLICATION__PLUGIN__UICOMPONENT__DATA_TYPE__DOUBLE;
}

template <typename T>
static double
readScalar(const nanoem_u8_t *data, nanoem_rsize_t index)
{
    T value;
    memcpy(&value, data + sizeof(T) * index, sizeof(T));
    return double(value);
}

template <typename T>
static void
writeScalar(nanoem_u8_t *data, nanoem_rsize_t index, double value)
{
    const T v = static_cast<T>(value);
    memcpy(data + sizeof(T) * index, &v, sizeof(T));
}

static double
readValue(const ProtobufCBinaryData &value, DataType from, nanoem_rsize_t index)
{
    switch (from) {
    case NANOEM__APPLICATION__PLUGIN__UICOMPONENT__DATA_TYPE__S8:
        return readScalar<int8_t>(value.data, index);
    case NANOEM__APPLICATION__PLUGIN__UICOMPONENT__DATA_TYPE__U8:
        return readScalar<nanoem_u8_t>(value.data, index);
    case NANOEM__APPLICATION__PLUGIN__UICOMPONENT__DATA_TYPE__S16:
        return readScalar<nanoem_i16_t>(value.data, index);
    case NANOEM__APPLICATION__PLUGIN__UICOMPONENT__DATA_TYPE__U16:
        return readScalar<nanoem_u16_t>(value.data, index);
    case NANOEM__APPLICATION__PLUGIN__UICOMPONENT__DATA_TYPE__S32:
        return readScalar<nanoem_i32_t>(value.data, index);
    case NANOEM__APPLICATION__PLUGIN__UICOMPONENT__DATA_TYPE__U32:
        return readScalar<nanoem_u32_t>(value.data, index);
    case NANOEM__APPLICATION__PLUGIN__UICOMPONENT__DATA_TYPE__FLOAT:
        return readScalar<nanoem_f32_t>(value.data, index);
    case NANOEM__APPLICATION__PLUGIN__UICOMPONENT__DATA_TYPE__DOUBLE:
        return readScalar<nanoem_f64_t>(value.data, index);
    default:
        break;
    }
    return 0.0;
}

static void
writeValue(ProtobufCBinaryData &value, DataType from, nanoem_rsize_t index, double v)
{
    switch (from) {
    case NANOEM__APPLICATION__PLUGIN__UICOMPONENT__DATA_TYPE__S8:
        writeScalar<int8_t>(value.data, index, v);
        break;
    case NANOEM__APPLICATION__PLUGIN__UICOMPONENT__DATA_TYPE__U8:
        writeScalar<nanoem_u8_t>(value.data, index, v);
        break;
    case NANOEM__APPLICATION__PLUGIN__UICOMPONENT__DATA_TYPE__S16:
        writeScalar<nanoem_i16_t>(value.data, index, v);
        break;
    case NANOEM__APPLICATION__PLUGIN__UICOMPONENT__DATA_TYPE__U16:
        writeScalar<nanoem_u16_t>(value.data, index, v);
        break;
    case NANOEM__APPLICATION__PLUGIN__UICOMPONENT__DATA_TYPE__S32:
        writeScalar<nanoem_i32_t>(value.data, index, v);
        break;
    case NANOEM__APPLICATION__PLUGIN__UICOMPONENT__DATA_TYPE__U32:
        writeScalar<nanoem_u32_t>(value.data, index, v);
        break;
    case NANOEM__APPLICATION__PLUGIN__UICOMPONENT__DATA_TYPE__FLOAT:
        writeScalar<nanoem_f32_t>(value.data, index, v);
        break;
    case NANOEM__APPLICATION__PLUGIN__UICOMPONENT__DATA_TYPE__DOUBLE:
        writeScalar<nanoem_f64_t>(value.data, index, v);
        break;
    default:
        break;
    }
}

/**
 * @brief Fallback bounds for a component without min/max (ImGui's InputScalarN is unbounded and
 *        clamps to the data type instead).
 */
static void
typeRange(DataType from, double &min, double &max)
{
    switch (from) {
    case NANOEM__APPLICATION__PLUGIN__UICOMPONENT__DATA_TYPE__S8:
        min = SCHAR_MIN, max = SCHAR_MAX;
        break;
    case NANOEM__APPLICATION__PLUGIN__UICOMPONENT__DATA_TYPE__U8:
        min = 0, max = UCHAR_MAX;
        break;
    case NANOEM__APPLICATION__PLUGIN__UICOMPONENT__DATA_TYPE__S16:
        min = SHRT_MIN, max = SHRT_MAX;
        break;
    case NANOEM__APPLICATION__PLUGIN__UICOMPONENT__DATA_TYPE__U16:
        min = 0, max = USHRT_MAX;
        break;
    case NANOEM__APPLICATION__PLUGIN__UICOMPONENT__DATA_TYPE__S32:
        min = double(INT_MIN), max = double(INT_MAX);
        break;
    case NANOEM__APPLICATION__PLUGIN__UICOMPONENT__DATA_TYPE__U32:
        min = 0, max = double(UINT_MAX);
        break;
    case NANOEM__APPLICATION__PLUGIN__UICOMPONENT__DATA_TYPE__FLOAT:
        min = -3.4e38, max = 3.4e38;
        break;
    case NANOEM__APPLICATION__PLUGIN__UICOMPONENT__DATA_TYPE__DOUBLE:
        min = -1.7e308, max = 1.7e308;
        break;
    default:
        min = 0, max = 0;
        break;
    }
}

} /* namespace anonymous */

QtPluginUIWidget::QtPluginUIWidget(internal::PluginUI::IReactor *reactor, nanoem_f32_t devicePixelRatio, QWidget *parent)
    : QWidget(parent)
    , m_reactor(reactor)
    , m_devicePixelRatio(devicePixelRatio)
    , m_layout(new QVBoxLayout(this))
{
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(4);
}

QtPluginUIWidget::~QtPluginUIWidget()
{
}

void
QtPluginUIWidget::rebuild(const Nanoem__Application__Plugin__UIWindow *window)
{
    clearLayout(m_layout);
    if (window) {
        buildItems(m_layout, window->items, window->n_items);
    }
}

void
QtPluginUIWidget::buildItems(
    QBoxLayout *layout, Nanoem__Application__Plugin__UIComponent **items, nanoem_rsize_t numItems)
{
    QHBoxLayout *row = nullptr;
    bool pendingSameLine = false;
    for (nanoem_rsize_t i = 0; i < numItems; i++) {
        auto *component = items[i];
        if (component->type_case == NANOEM__APPLICATION__PLUGIN__UICOMPONENT__TYPE_SAME_LINE) {
            pendingSameLine = true;
            continue;
        }
        if (QWidget *widget = createComponent(component)) {
            appendRow(layout, widget, &row, &pendingSameLine);
        }
        /* an unknown/canvas component draws nothing in ImGui either, so the pending SameLine
           carries over to the next component exactly like it does there */
    }
}

void
QtPluginUIWidget::appendRow(QBoxLayout *layout, QWidget *widget, QHBoxLayout **row, bool *pendingSameLine)
{
    if (*pendingSameLine) {
        if (!*row) {
            *row = new QHBoxLayout;
            (*row)->setContentsMargins(0, 0, 0, 0);
            (*row)->setSpacing(6);
            layout->addLayout(*row);
        }
        (*row)->addWidget(widget);
    }
    else {
        *row = nullptr;
        layout->addWidget(widget);
    }
    *pendingSameLine = false;
}

QWidget *
QtPluginUIWidget::createComponent(Nanoem__Application__Plugin__UIComponent *component)
{
    switch (component->type_case) {
    case NANOEM__APPLICATION__PLUGIN__UICOMPONENT__TYPE_LABEL: {
        const Nanoem__Application__Plugin__UIComponent__Label *label = component->label;
        auto *widget = new QLabel(QString::fromUtf8(label->text), this);
        if (label->has_color) {
            const nanoem_u32_t color = label->color;
            widget->setStyleSheet(QStringLiteral("color: rgb(%1, %2, %3);")
                                      .arg(color & 0xff)
                                      .arg((color >> 8) & 0xff)
                                      .arg((color >> 16) & 0xff));
        }
        return widget;
    }
    case NANOEM__APPLICATION__PLUGIN__UICOMPONENT__TYPE_BUTTON: {
        Nanoem__Application__Plugin__UIComponent__Button *button = component->button;
        auto *widget = new QPushButton(QString::fromUtf8(button->text), this);
        const char *id = button->id;
        connect(widget, &QPushButton::clicked, widget, [this, component, id]() {
            m_reactor->handleUIComponent(id, component);
        });
        return widget;
    }
    case NANOEM__APPLICATION__PLUGIN__UICOMPONENT__TYPE_SELECTABLE: {
        Nanoem__Application__Plugin__UIComponent__Selectable *selectable = component->selectable;
        auto *widget = new QPushButton(QString::fromUtf8(selectable->text), this);
        widget->setFlat(true);
        widget->setFocusPolicy(Qt::NoFocus);
        const char *id = selectable->id;
        connect(widget, &QPushButton::clicked, widget, [this, component, id]() {
            m_reactor->handleUIComponent(id, component);
        });
        return widget;
    }
    case NANOEM__APPLICATION__PLUGIN__UICOMPONENT__TYPE_CHECK_BOX: {
        Nanoem__Application__Plugin__UIComponent__CheckBox *checkbox = component->check_box;
        auto *widget = new QCheckBox(QString::fromUtf8(checkbox->text), this);
        widget->setChecked(checkbox->value != 0);
        connect(widget, &QCheckBox::toggled, widget, [this, component, checkbox](bool checked) {
            checkbox->value = checked ? 1 : 0;
            m_reactor->handleUIComponent(checkbox->id, component);
        });
        return widget;
    }
    case NANOEM__APPLICATION__PLUGIN__UICOMPONENT__TYPE_RADIO_BUTTON: {
        Nanoem__Application__Plugin__UIComponent__RadioButton *radio = component->radio_button;
        auto *widget = new QRadioButton(QString::fromUtf8(radio->text), this);
        /* ImGui renders every radio independently (the component keeps its own active flag), so
           the Qt copy must not auto-exclusive its siblings */
        widget->setAutoExclusive(false);
        widget->setChecked(radio->active != 0);
        connect(widget, &QRadioButton::toggled, widget, [this, component, radio](bool checked) {
            if (checked) {
                m_reactor->handleUIComponent(radio->id, component);
            }
        });
        return widget;
    }
    case NANOEM__APPLICATION__PLUGIN__UICOMPONENT__TYPE_COMBO_BOX: {
        Nanoem__Application__Plugin__UIComponent__ComboBox *combobox = component->combo_box;
        const nanoem_rsize_t numItems = combobox->n_selectables;
        if (numItems == 0) {
            return nullptr;
        }
        auto *widget = new QComboBox(this);
        for (nanoem_rsize_t i = 0; i < numItems; i++) {
            widget->addItem(QString::fromUtf8(combobox->selectables[i]->text));
        }
        widget->setCurrentIndex(combobox->selected_index < numItems ? int(combobox->selected_index) : 0);
        /* activated() only fires for a user selection, so the initial index never notifies */
        connect(widget, QOverload<int>::of(&QComboBox::activated), widget, [this, component, combobox](int index) {
            combobox->selected_index = Inline::saturateInt32U(index);
            m_reactor->handleUIComponent(combobox->id, component);
        });
        return widget;
    }
    case NANOEM__APPLICATION__PLUGIN__UICOMPONENT__TYPE_SEPARATOR: {
        auto *widget = new QFrame(this);
        widget->setFrameShape(QFrame::HLine);
        widget->setFrameShadow(QFrame::Sunken);
        return widget;
    }
    case NANOEM__APPLICATION__PLUGIN__UICOMPONENT__TYPE_DRAG_SCALAR_N: {
        Nanoem__Application__Plugin__UIComponent__DragScalarN *drag = component->drag_scalar_n;
        return createScalarRow(drag->id, drag->text, component, drag->data_type, drag->num_components, &drag->value,
            &drag->min, &drag->max, drag->speed, true);
    }
    case NANOEM__APPLICATION__PLUGIN__UICOMPONENT__TYPE_SLIDER_SCALAR_N: {
        Nanoem__Application__Plugin__UIComponent__SliderScalarN *slider = component->slider_scalar_n;
        return createScalarRow(slider->id, slider->text, component, slider->data_type, slider->num_components,
            &slider->value, &slider->min, &slider->max, 0.0, true);
    }
    case NANOEM__APPLICATION__PLUGIN__UICOMPONENT__TYPE_INPUT_SCALAR_N: {
        Nanoem__Application__Plugin__UIComponent__InputScalarN *input = component->input_scalar_n;
        const nanoem_rsize_t required = requiredByteSize(input->data_type, input->num_components);
        if (!input->step.data || input->step.len < required || !input->step_fast.data ||
            input->step_fast.len < required) {
            return nullptr;
        }
        return createScalarRow(input->id, input->text, component, input->data_type, input->num_components,
            &input->value, nullptr, nullptr, readValue(input->step, input->data_type, 0), false);
    }
    case NANOEM__APPLICATION__PLUGIN__UICOMPONENT__TYPE_CHILD_WINDOW: {
        Nanoem__Application__Plugin__UIComponent__ChildWindow *child = component->child_window;
        auto *widget = new QWidget(this);
        auto *layout = new QVBoxLayout(widget);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(4);
        buildItems(layout, child->components, child->n_components);
        if (child->border) {
            widget->setStyleSheet(QStringLiteral("border: 1px solid gray;"));
        }
        if (child->has_width && child->has_height) {
            widget->setFixedSize(
                int(child->width * m_devicePixelRatio), int(child->height * m_devicePixelRatio));
        }
        return widget;
    }
    case NANOEM__APPLICATION__PLUGIN__UICOMPONENT__TYPE_DUMMY: {
        const Nanoem__Application__Plugin__UIComponent__Dummy *dummy = component->dummy;
        auto *widget = new QWidget(this);
        widget->setFixedSize(
            int(dummy->width * m_devicePixelRatio), int(dummy->height * m_devicePixelRatio));
        return widget;
    }
    case NANOEM__APPLICATION__PLUGIN__UICOMPONENT__TYPE_TREE: {
        Nanoem__Application__Plugin__UIComponent__Tree *tree = component->tree;
        auto *widget = new QGroupBox(QString::fromUtf8(tree->id), this);
        auto *layout = new QVBoxLayout(widget);
        layout->setContentsMargins(4, 4, 4, 4);
        buildItems(layout, tree->components, tree->n_components);
        return widget;
    }
    case NANOEM__APPLICATION__PLUGIN__UICOMPONENT__TYPE_SAME_LINE:
    case NANOEM__APPLICATION__PLUGIN__UICOMPONENT__TYPE_CANVAS:
    default:
        break;
    }
    return nullptr;
}

QWidget *
QtPluginUIWidget::createScalarRow(const char *id, const char *text,
    Nanoem__Application__Plugin__UIComponent *component, Nanoem__Application__Plugin__UIComponent__DataType dataType,
    nanoem_rsize_t numComponents, ProtobufCBinaryData *value, const ProtobufCBinaryData *min,
    const ProtobufCBinaryData *max, double singleStep, bool bounded)
{
    if (!value || !validate(dataType, numComponents, *value)) {
        return nullptr;
    }
    if (bounded && (!min || !max || !validate(dataType, numComponents, *min) || !validate(dataType, numComponents, *max))) {
        return nullptr;
    }
    double fallbackMin = 0.0, fallbackMax = 0.0;
    if (!bounded) {
        typeRange(dataType, fallbackMin, fallbackMax);
    }
    const double first = bounded ? readValue(*min, dataType, 0) : fallbackMin;
    const double last = bounded ? readValue(*max, dataType, 0) : fallbackMax;
    const bool integral =
        !isFloatingPoint(dataType) && first >= double(INT_MIN) && last <= double(INT_MAX);
    auto *row = new QWidget(this);
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);
    layout->addWidget(new QLabel(QString::fromUtf8(text), row));
    for (nanoem_rsize_t i = 0; i < numComponents; i++) {
        double lower = bounded ? readValue(*min, dataType, i) : fallbackMin;
        double upper = bounded ? readValue(*max, dataType, i) : fallbackMax;
        if (lower > upper) {
            const double tmp = lower;
            lower = upper;
            upper = tmp;
        }
        if (integral) {
            auto *spinBox = new QSpinBox(row);
            spinBox->setRange(int(lower), int(upper));
            spinBox->setSingleStep(singleStep >= 1.0 ? int(singleStep) : 1);
            spinBox->setValue(int(readValue(*value, dataType, i)));
            const nanoem_rsize_t index = i;
            connect(spinBox, QOverload<int>::of(&QSpinBox::valueChanged), spinBox,
                [this, component, id, value, dataType, index](int v) {
                    writeValue(*value, dataType, index, double(v));
                    m_reactor->handleUIComponent(id, component);
                });
            layout->addWidget(spinBox, 1);
        }
        else {
            auto *spinBox = new QDoubleSpinBox(row);
            spinBox->setRange(lower, upper);
            spinBox->setDecimals(isFloatingPoint(dataType) ? 6 : 0);
            spinBox->setSingleStep(singleStep > 0.0 ? singleStep : (upper - lower) * 0.01);
            spinBox->setValue(readValue(*value, dataType, i));
            const nanoem_rsize_t index = i;
            connect(spinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged), spinBox,
                [this, component, id, value, dataType, index](double v) {
                    writeValue(*value, dataType, index, v);
                    m_reactor->handleUIComponent(id, component);
                });
            layout->addWidget(spinBox, 1);
        }
    }
    layout->addStretch(1);
    return row;
}

void
QtPluginUIWidget::clearLayout(QBoxLayout *layout)
{
    while (QLayoutItem *item = layout->takeAt(0)) {
        if (QLayout *child = item->layout()) {
            if (QBoxLayout *box = qobject_cast<QBoxLayout *>(child)) {
                clearLayout(box);
            }
            delete child;
        }
        else if (QWidget *widget = item->widget()) {
            /* never delete a widget while its own signal is on the stack: the plugin can ask for
               a layout reload from inside a value change */
            widget->deleteLater();
        }
        delete item;
    }
}

} /* namespace qt */
} /* namespace nanoem */
