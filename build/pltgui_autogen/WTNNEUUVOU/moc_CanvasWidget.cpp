/****************************************************************************
** Meta object code from reading C++ file 'CanvasWidget.h'
**
** Created by: The Qt Meta Object Compiler version 68 (Qt 6.4.2)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "../../../qt/CanvasWidget.h"
#include <QtGui/qtextcursor.h>
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'CanvasWidget.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 68
#error "This file was generated using the moc from 6.4.2. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

#ifndef Q_CONSTINIT
#define Q_CONSTINIT
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
namespace {
struct qt_meta_stringdata_CanvasWidget_t {
    uint offsetsAndSizes[36];
    char stringdata0[13];
    char stringdata1[10];
    char stringdata2[1];
    char stringdata3[13];
    char stringdata4[3];
    char stringdata5[6];
    char stringdata6[7];
    char stringdata7[3];
    char stringdata8[3];
    char stringdata9[16];
    char stringdata10[4];
    char stringdata11[3];
    char stringdata12[9];
    char stringdata13[7];
    char stringdata14[9];
    char stringdata15[13];
    char stringdata16[10];
    char stringdata17[11];
};
#define QT_MOC_LITERAL(ofs, len) \
    uint(sizeof(qt_meta_stringdata_CanvasWidget_t::offsetsAndSizes) + ofs), len 
Q_CONSTINIT static const qt_meta_stringdata_CanvasWidget_t qt_meta_stringdata_CanvasWidget = {
    {
        QT_MOC_LITERAL(0, 12),  // "CanvasWidget"
        QT_MOC_LITERAL(13, 9),  // "dragBegan"
        QT_MOC_LITERAL(23, 0),  // ""
        QT_MOC_LITERAL(24, 12),  // "offsetEdited"
        QT_MOC_LITERAL(37, 2),  // "mm"
        QT_MOC_LITERAL(40, 5),  // "final"
        QT_MOC_LITERAL(46, 6),  // "nudged"
        QT_MOC_LITERAL(53, 2),  // "dx"
        QT_MOC_LITERAL(56, 2),  // "dy"
        QT_MOC_LITERAL(59, 15),  // "selectionEdited"
        QT_MOC_LITERAL(75, 3),  // "has"
        QT_MOC_LITERAL(79, 2),  // "uv"
        QT_MOC_LITERAL(82, 8),  // "cursorMm"
        QT_MOC_LITERAL(91, 6),  // "inside"
        QT_MOC_LITERAL(98, 8),  // "tipApply"
        QT_MOC_LITERAL(107, 12),  // "tipDismissed"
        QT_MOC_LITERAL(120, 9),  // "fitWindow"
        QT_MOC_LITERAL(130, 10)   // "actualSize"
    },
    "CanvasWidget",
    "dragBegan",
    "",
    "offsetEdited",
    "mm",
    "final",
    "nudged",
    "dx",
    "dy",
    "selectionEdited",
    "has",
    "uv",
    "cursorMm",
    "inside",
    "tipApply",
    "tipDismissed",
    "fitWindow",
    "actualSize"
};
#undef QT_MOC_LITERAL
} // unnamed namespace

Q_CONSTINIT static const uint qt_meta_data_CanvasWidget[] = {

 // content:
      10,       // revision
       0,       // classname
       0,    0, // classinfo
       9,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       7,       // signalCount

 // signals: name, argc, parameters, tag, flags, initial metatype offsets
       1,    0,   68,    2, 0x06,    1 /* Public */,
       3,    2,   69,    2, 0x06,    2 /* Public */,
       6,    2,   74,    2, 0x06,    5 /* Public */,
       9,    3,   79,    2, 0x06,    8 /* Public */,
      12,    2,   86,    2, 0x06,   12 /* Public */,
      14,    0,   91,    2, 0x06,   15 /* Public */,
      15,    0,   92,    2, 0x06,   16 /* Public */,

 // slots: name, argc, parameters, tag, flags, initial metatype offsets
      16,    0,   93,    2, 0x0a,   17 /* Public */,
      17,    0,   94,    2, 0x0a,   18 /* Public */,

 // signals: parameters
    QMetaType::Void,
    QMetaType::Void, QMetaType::QPointF, QMetaType::Bool,    4,    5,
    QMetaType::Void, QMetaType::Double, QMetaType::Double,    7,    8,
    QMetaType::Void, QMetaType::Bool, QMetaType::QRectF, QMetaType::Bool,   10,   11,    5,
    QMetaType::Void, QMetaType::QPointF, QMetaType::Bool,   11,   13,
    QMetaType::Void,
    QMetaType::Void,

 // slots: parameters
    QMetaType::Void,
    QMetaType::Void,

       0        // eod
};

Q_CONSTINIT const QMetaObject CanvasWidget::staticMetaObject = { {
    QMetaObject::SuperData::link<QWidget::staticMetaObject>(),
    qt_meta_stringdata_CanvasWidget.offsetsAndSizes,
    qt_meta_data_CanvasWidget,
    qt_static_metacall,
    nullptr,
    qt_incomplete_metaTypeArray<qt_meta_stringdata_CanvasWidget_t,
        // Q_OBJECT / Q_GADGET
        QtPrivate::TypeAndForceComplete<CanvasWidget, std::true_type>,
        // method 'dragBegan'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'offsetEdited'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<QPointF, std::false_type>,
        QtPrivate::TypeAndForceComplete<bool, std::false_type>,
        // method 'nudged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<double, std::false_type>,
        QtPrivate::TypeAndForceComplete<double, std::false_type>,
        // method 'selectionEdited'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<bool, std::false_type>,
        QtPrivate::TypeAndForceComplete<QRectF, std::false_type>,
        QtPrivate::TypeAndForceComplete<bool, std::false_type>,
        // method 'cursorMm'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<QPointF, std::false_type>,
        QtPrivate::TypeAndForceComplete<bool, std::false_type>,
        // method 'tipApply'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'tipDismissed'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'fitWindow'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'actualSize'
        QtPrivate::TypeAndForceComplete<void, std::false_type>
    >,
    nullptr
} };

void CanvasWidget::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<CanvasWidget *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->dragBegan(); break;
        case 1: _t->offsetEdited((*reinterpret_cast< std::add_pointer_t<QPointF>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<bool>>(_a[2]))); break;
        case 2: _t->nudged((*reinterpret_cast< std::add_pointer_t<double>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<double>>(_a[2]))); break;
        case 3: _t->selectionEdited((*reinterpret_cast< std::add_pointer_t<bool>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<QRectF>>(_a[2])),(*reinterpret_cast< std::add_pointer_t<bool>>(_a[3]))); break;
        case 4: _t->cursorMm((*reinterpret_cast< std::add_pointer_t<QPointF>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<bool>>(_a[2]))); break;
        case 5: _t->tipApply(); break;
        case 6: _t->tipDismissed(); break;
        case 7: _t->fitWindow(); break;
        case 8: _t->actualSize(); break;
        default: ;
        }
    } else if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _t = void (CanvasWidget::*)();
            if (_t _q_method = &CanvasWidget::dragBegan; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 0;
                return;
            }
        }
        {
            using _t = void (CanvasWidget::*)(QPointF , bool );
            if (_t _q_method = &CanvasWidget::offsetEdited; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 1;
                return;
            }
        }
        {
            using _t = void (CanvasWidget::*)(double , double );
            if (_t _q_method = &CanvasWidget::nudged; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 2;
                return;
            }
        }
        {
            using _t = void (CanvasWidget::*)(bool , QRectF , bool );
            if (_t _q_method = &CanvasWidget::selectionEdited; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 3;
                return;
            }
        }
        {
            using _t = void (CanvasWidget::*)(QPointF , bool );
            if (_t _q_method = &CanvasWidget::cursorMm; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 4;
                return;
            }
        }
        {
            using _t = void (CanvasWidget::*)();
            if (_t _q_method = &CanvasWidget::tipApply; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 5;
                return;
            }
        }
        {
            using _t = void (CanvasWidget::*)();
            if (_t _q_method = &CanvasWidget::tipDismissed; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 6;
                return;
            }
        }
    }
}

const QMetaObject *CanvasWidget::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *CanvasWidget::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_CanvasWidget.stringdata0))
        return static_cast<void*>(this);
    return QWidget::qt_metacast(_clname);
}

int CanvasWidget::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QWidget::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 9)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 9;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 9)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 9;
    }
    return _id;
}

// SIGNAL 0
void CanvasWidget::dragBegan()
{
    QMetaObject::activate(this, &staticMetaObject, 0, nullptr);
}

// SIGNAL 1
void CanvasWidget::offsetEdited(QPointF _t1, bool _t2)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))) };
    QMetaObject::activate(this, &staticMetaObject, 1, _a);
}

// SIGNAL 2
void CanvasWidget::nudged(double _t1, double _t2)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))) };
    QMetaObject::activate(this, &staticMetaObject, 2, _a);
}

// SIGNAL 3
void CanvasWidget::selectionEdited(bool _t1, QRectF _t2, bool _t3)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t3))) };
    QMetaObject::activate(this, &staticMetaObject, 3, _a);
}

// SIGNAL 4
void CanvasWidget::cursorMm(QPointF _t1, bool _t2)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))) };
    QMetaObject::activate(this, &staticMetaObject, 4, _a);
}

// SIGNAL 5
void CanvasWidget::tipApply()
{
    QMetaObject::activate(this, &staticMetaObject, 5, nullptr);
}

// SIGNAL 6
void CanvasWidget::tipDismissed()
{
    QMetaObject::activate(this, &staticMetaObject, 6, nullptr);
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
