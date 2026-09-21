/****************************************************************************
** Meta object code from reading C++ file 'AxUiFactory.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.11.0)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../Headers/Client/AxScript/AxUiFactory.h"
#include <QtGui/qtextcursor.h>
#include <QtNetwork/QSslError>
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'AxUiFactory.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 69
#error "This file was generated using the moc from 6.11.0. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

#ifndef Q_CONSTINIT
#define Q_CONSTINIT
#endif

QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
QT_WARNING_DISABLE_GCC("-Wuseless-cast")
namespace {
struct qt_meta_tag_ZN11AxUiFactoryE_t {};
} // unnamed namespace

template <> constexpr inline auto AxUiFactory::qt_create_metaobjectdata<qt_meta_tag_ZN11AxUiFactoryE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "AxUiFactory",
        "createWidget",
        "QWidget*",
        "",
        "createLine",
        "QFrame*",
        "shape",
        "createLabel",
        "QLabel*",
        "text",
        "createLineEdit",
        "QLineEdit*",
        "createComboBox",
        "QComboBox*",
        "createCheckBox",
        "QCheckBox*",
        "label",
        "createSpinBox",
        "QSpinBox*",
        "createDateEdit",
        "QDateEdit*",
        "createTimeEdit",
        "QTimeEdit*",
        "createPushButton",
        "QPushButton*",
        "createPlainTextEdit",
        "QPlainTextEdit*",
        "createListWidget",
        "QListWidget*",
        "createTableWidget",
        "QTableView*",
        "createTabWidget",
        "QTabWidget*",
        "createGroupBox",
        "QGroupBox*",
        "title",
        "createSplitter",
        "QSplitter*",
        "orientation",
        "createScrollArea",
        "QScrollArea*",
        "createStackedWidget",
        "QStackedWidget*",
        "createDialog",
        "QDialog*",
        "createFileSelector"
    };

    QtMocHelpers::UintData qt_methods {
        // Slot 'createWidget'
        QtMocHelpers::SlotData<QWidget *()>(1, 3, QMC::AccessPublic, 0x80000000 | 2),
        // Slot 'createLine'
        QtMocHelpers::SlotData<QFrame *(int)>(4, 3, QMC::AccessPublic, 0x80000000 | 5, {{
            { QMetaType::Int, 6 },
        }}),
        // Slot 'createLabel'
        QtMocHelpers::SlotData<QLabel *(const QString &)>(7, 3, QMC::AccessPublic, 0x80000000 | 8, {{
            { QMetaType::QString, 9 },
        }}),
        // Slot 'createLineEdit'
        QtMocHelpers::SlotData<QLineEdit *(const QString &)>(10, 3, QMC::AccessPublic, 0x80000000 | 11, {{
            { QMetaType::QString, 9 },
        }}),
        // Slot 'createComboBox'
        QtMocHelpers::SlotData<QComboBox *()>(12, 3, QMC::AccessPublic, 0x80000000 | 13),
        // Slot 'createCheckBox'
        QtMocHelpers::SlotData<QCheckBox *(const QString &)>(14, 3, QMC::AccessPublic, 0x80000000 | 15, {{
            { QMetaType::QString, 16 },
        }}),
        // Slot 'createSpinBox'
        QtMocHelpers::SlotData<QSpinBox *()>(17, 3, QMC::AccessPublic, 0x80000000 | 18),
        // Slot 'createDateEdit'
        QtMocHelpers::SlotData<QDateEdit *()>(19, 3, QMC::AccessPublic, 0x80000000 | 20),
        // Slot 'createTimeEdit'
        QtMocHelpers::SlotData<QTimeEdit *()>(21, 3, QMC::AccessPublic, 0x80000000 | 22),
        // Slot 'createPushButton'
        QtMocHelpers::SlotData<QPushButton *(const QString &)>(23, 3, QMC::AccessPublic, 0x80000000 | 24, {{
            { QMetaType::QString, 9 },
        }}),
        // Slot 'createPlainTextEdit'
        QtMocHelpers::SlotData<QPlainTextEdit *(const QString &)>(25, 3, QMC::AccessPublic, 0x80000000 | 26, {{
            { QMetaType::QString, 9 },
        }}),
        // Slot 'createListWidget'
        QtMocHelpers::SlotData<QListWidget *()>(27, 3, QMC::AccessPublic, 0x80000000 | 28),
        // Slot 'createTableWidget'
        QtMocHelpers::SlotData<QTableView *()>(29, 3, QMC::AccessPublic, 0x80000000 | 30),
        // Slot 'createTabWidget'
        QtMocHelpers::SlotData<QTabWidget *()>(31, 3, QMC::AccessPublic, 0x80000000 | 32),
        // Slot 'createGroupBox'
        QtMocHelpers::SlotData<QGroupBox *(const QString &)>(33, 3, QMC::AccessPublic, 0x80000000 | 34, {{
            { QMetaType::QString, 35 },
        }}),
        // Slot 'createSplitter'
        QtMocHelpers::SlotData<QSplitter *(int)>(36, 3, QMC::AccessPublic, 0x80000000 | 37, {{
            { QMetaType::Int, 38 },
        }}),
        // Slot 'createScrollArea'
        QtMocHelpers::SlotData<QScrollArea *()>(39, 3, QMC::AccessPublic, 0x80000000 | 40),
        // Slot 'createStackedWidget'
        QtMocHelpers::SlotData<QStackedWidget *()>(41, 3, QMC::AccessPublic, 0x80000000 | 42),
        // Slot 'createDialog'
        QtMocHelpers::SlotData<QDialog *(const QString &)>(43, 3, QMC::AccessPublic, 0x80000000 | 44, {{
            { QMetaType::QString, 35 },
        }}),
        // Slot 'createFileSelector'
        QtMocHelpers::SlotData<QLineEdit *()>(45, 3, QMC::AccessPublic, 0x80000000 | 11),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<AxUiFactory, qt_meta_tag_ZN11AxUiFactoryE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject AxUiFactory::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN11AxUiFactoryE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN11AxUiFactoryE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN11AxUiFactoryE_t>.metaTypes,
    nullptr
} };

void AxUiFactory::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<AxUiFactory *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: { QWidget* _r = _t->createWidget();
            if (_a[0]) *reinterpret_cast<QWidget**>(_a[0]) = std::move(_r); }  break;
        case 1: { QFrame* _r = _t->createLine((*reinterpret_cast<std::add_pointer_t<int>>(_a[1])));
            if (_a[0]) *reinterpret_cast<QFrame**>(_a[0]) = std::move(_r); }  break;
        case 2: { QLabel* _r = _t->createLabel((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<QLabel**>(_a[0]) = std::move(_r); }  break;
        case 3: { QLineEdit* _r = _t->createLineEdit((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<QLineEdit**>(_a[0]) = std::move(_r); }  break;
        case 4: { QComboBox* _r = _t->createComboBox();
            if (_a[0]) *reinterpret_cast<QComboBox**>(_a[0]) = std::move(_r); }  break;
        case 5: { QCheckBox* _r = _t->createCheckBox((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<QCheckBox**>(_a[0]) = std::move(_r); }  break;
        case 6: { QSpinBox* _r = _t->createSpinBox();
            if (_a[0]) *reinterpret_cast<QSpinBox**>(_a[0]) = std::move(_r); }  break;
        case 7: { QDateEdit* _r = _t->createDateEdit();
            if (_a[0]) *reinterpret_cast<QDateEdit**>(_a[0]) = std::move(_r); }  break;
        case 8: { QTimeEdit* _r = _t->createTimeEdit();
            if (_a[0]) *reinterpret_cast<QTimeEdit**>(_a[0]) = std::move(_r); }  break;
        case 9: { QPushButton* _r = _t->createPushButton((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<QPushButton**>(_a[0]) = std::move(_r); }  break;
        case 10: { QPlainTextEdit* _r = _t->createPlainTextEdit((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<QPlainTextEdit**>(_a[0]) = std::move(_r); }  break;
        case 11: { QListWidget* _r = _t->createListWidget();
            if (_a[0]) *reinterpret_cast<QListWidget**>(_a[0]) = std::move(_r); }  break;
        case 12: { QTableView* _r = _t->createTableWidget();
            if (_a[0]) *reinterpret_cast<QTableView**>(_a[0]) = std::move(_r); }  break;
        case 13: { QTabWidget* _r = _t->createTabWidget();
            if (_a[0]) *reinterpret_cast<QTabWidget**>(_a[0]) = std::move(_r); }  break;
        case 14: { QGroupBox* _r = _t->createGroupBox((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<QGroupBox**>(_a[0]) = std::move(_r); }  break;
        case 15: { QSplitter* _r = _t->createSplitter((*reinterpret_cast<std::add_pointer_t<int>>(_a[1])));
            if (_a[0]) *reinterpret_cast<QSplitter**>(_a[0]) = std::move(_r); }  break;
        case 16: { QScrollArea* _r = _t->createScrollArea();
            if (_a[0]) *reinterpret_cast<QScrollArea**>(_a[0]) = std::move(_r); }  break;
        case 17: { QStackedWidget* _r = _t->createStackedWidget();
            if (_a[0]) *reinterpret_cast<QStackedWidget**>(_a[0]) = std::move(_r); }  break;
        case 18: { QDialog* _r = _t->createDialog((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<QDialog**>(_a[0]) = std::move(_r); }  break;
        case 19: { QLineEdit* _r = _t->createFileSelector();
            if (_a[0]) *reinterpret_cast<QLineEdit**>(_a[0]) = std::move(_r); }  break;
        default: ;
        }
    }
}

const QMetaObject *AxUiFactory::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *AxUiFactory::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN11AxUiFactoryE_t>.strings))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int AxUiFactory::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 20)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 20;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 20)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 20;
    }
    return _id;
}
QT_WARNING_POP
