/**************************************************************************
**
** This file is part of Qt Creator
**
** Copyright (c) 2012 Nokia Corporation and/or its subsidiary(-ies).
**
** Contact: Nokia Corporation (qt-info@nokia.com)
**
** GNU Lesser General Public License Usage
**
** This file may be used under the terms of the GNU Lesser General Public
** License version 2.1 as published by the Free Software Foundation and
** appearing in the file LICENSE.LGPL included in the packaging of this file.
** Please review the following information to ensure the GNU Lesser General
** Public License version 2.1 requirements will be met:
** http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Nokia gives you certain additional
** rights. These rights are described in the Nokia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
** Other Usage
**
** Alternatively, this file may be used in accordance with the terms and
** conditions contained in a signed written agreement between you and Nokia.
**
** If you have questions regarding the use of this file, please contact
** Nokia at qt-info@nokia.com.
**
**************************************************************************/

#ifndef BASEENGINEDEBUGCLIENT_H
#define BASEENGINEDEBUGCLIENT_H

#include "qmldebug_global.h"
#include "qmldebugclient.h"
#include <qurl.h>
#include <qvariant.h>

namespace QmlDebug {

class QmlDebugConnection;
class PropertyReference;
class ContextReference;
class ObjectReference;
class FileReference;
class EngineReference;

class QMLDEBUG_EXPORT BaseEngineDebugClient : public QmlDebugClient
{
    Q_OBJECT
public:
    BaseEngineDebugClient(const QString &clientName,
                         QmlDebugConnection *conn);

    quint32 addWatch(const PropertyReference &property);
    quint32 addWatch(const ContextReference &context, const QString &id);
    quint32 addWatch(const ObjectReference &object, const QString &expr);
    quint32 addWatch(const ObjectReference &object);
    quint32 addWatch(const FileReference &file);

    void removeWatch(quint32 watch);

    quint32 queryAvailableEngines();
    quint32 queryRootContexts(const EngineReference &context);
    quint32 queryObject(const ObjectReference &object);
    quint32 queryObjectRecursive(const ObjectReference &object);
    quint32 queryExpressionResult(int objectDebugId,
                                  const QString &expr, int engineId = -1);
    virtual quint32 setBindingForObject(int objectDebugId, const QString &propertyName,
                                const QVariant &bindingExpression,
                                bool isLiteralValue,
                                QString source, int line);
    virtual quint32 resetBindingForObject(int objectDebugId,
                                  const QString &propertyName);
    virtual quint32 setMethodBody(int objectDebugId, const QString &methodName,
                          const QString &methodBody);

signals:
    void newStatus(QmlDebug::ClientStatus status);
    void newObjects();
    void valueChanged(int debugId, const QByteArray &name,
                      const QVariant &value);
    void result(quint32 queryId, const QVariant &result, const QByteArray &type);

protected:
    virtual void statusChanged(ClientStatus status);
    virtual void messageReceived(const QByteArray &);

    quint32 getId() { return m_nextId++; }

    void decode(QDataStream &d, ContextReference &context);
    void decode(QDataStream &d, ObjectReference &object, bool simple);

private:
    quint32 m_nextId;
};

class FileReference
{
public:
    FileReference() : m_lineNumber(-1), m_columnNumber(-1) {}

    QUrl url() const { return m_url; }
    int lineNumber() const { return m_lineNumber; }
    int columnNumber() const { return m_columnNumber; }

private:
    friend class BaseEngineDebugClient;
    QUrl m_url;
    int m_lineNumber;
    int m_columnNumber;
};

class EngineReference
{
public:
    EngineReference() : m_debugId(-1) {}
    explicit EngineReference(int id) : m_debugId(id) {}

    int debugId() const { return m_debugId; }
    QString name() const { return m_name; }

private:
    friend class BaseEngineDebugClient;
    int m_debugId;
    QString m_name;
};

class ObjectReference
{
public:
    ObjectReference() : m_debugId(-1), m_parentId(-1), m_contextDebugId(-1), m_needsMoreData(false) {}
    explicit ObjectReference(int id) : m_debugId(id), m_parentId(-1), m_contextDebugId(-1), m_needsMoreData(false) {}

    int debugId() const { return m_debugId; }
    int parentId() const { return m_parentId; }
    QString className() const { return m_className; }
    QString idString() const { return m_idString; }
    QString name() const { return m_name; }

    FileReference source() const { return m_source; }
    int contextDebugId() const { return m_contextDebugId; }
    bool needsMoreData() const { return m_needsMoreData; }

    QList<PropertyReference> properties() const { return m_properties; }
    QList<ObjectReference> children() const { return m_children; }

    int insertObjectInTree(const ObjectReference &obj)
    {
        for (int i = 0; i < m_children.count(); i++) {
            if (m_children[i].debugId() == obj.debugId()) {
                m_children.replace(i, obj);
                return debugId();
            } else {
                if (m_children[i].insertObjectInTree(obj))
                    return debugId();
            }
        }
        return -1;
    }

    bool operator ==(const ObjectReference &obj)
    {
        return m_debugId == obj.debugId();
    }

private:
    friend class BaseEngineDebugClient;
    int m_debugId;
    int m_parentId;
    QString m_className;
    QString m_idString;
    QString m_name;
    FileReference m_source;
    int m_contextDebugId;
    bool m_needsMoreData;
    QList<PropertyReference> m_properties;
    QList<ObjectReference> m_children;
};

class ContextReference
{
public:
    ContextReference() : m_debugId(-1) {}

    int debugId() const { return m_debugId; }
    QString name() const { return m_name; }

    QList<ObjectReference> objects() const { return m_objects; }
    QList<ContextReference> contexts() const { return m_contexts; }

private:
    friend class BaseEngineDebugClient;
    int m_debugId;
    QString m_name;
    QList<ObjectReference> m_objects;
    QList<ContextReference> m_contexts;
};

class PropertyReference
{
public:
    PropertyReference() : m_objectDebugId(-1), m_hasNotifySignal(false) {}

    int debugId() const { return m_objectDebugId; }
    QString name() const { return m_name; }
    QVariant value() const { return m_value; }
    QString valueTypeName() const { return m_valueTypeName; }
    QString binding() const { return m_binding; }
    bool hasNotifySignal() const { return m_hasNotifySignal; }

private:
    friend class BaseEngineDebugClient;
    int m_objectDebugId;
    QString m_name;
    QVariant m_value;
    QString m_valueTypeName;
    QString m_binding;
    bool m_hasNotifySignal;
};

} // namespace QmlDebug

Q_DECLARE_METATYPE(QmlDebug::ObjectReference)
Q_DECLARE_METATYPE(QmlDebug::EngineReference)
Q_DECLARE_METATYPE(QList<QmlDebug::EngineReference>)
Q_DECLARE_METATYPE(QmlDebug::ContextReference)

QT_BEGIN_NAMESPACE
inline QDebug operator<<(QDebug dbg, const QmlDebug::EngineReference &ref) {
    dbg.nospace() << "(Engine " << ref.debugId() << "/" << ref.name() <<  ")";
    return dbg.space();
}

inline QDebug operator<<(QDebug dbg, const QmlDebug::ContextReference &ref) {
    dbg.nospace() << "(Context " << ref.debugId() << "/" << ref.name() <<  ")";
    return dbg.space();
}

inline QDebug operator<<(QDebug dbg, const QmlDebug::ObjectReference &ref) {
    dbg.nospace() << "(Object " << ref.debugId() << "/"
                  << (ref.idString().isEmpty() ? ref.idString() : ref.className()) <<  ")";
    return dbg.space();
}
QT_END_NAMESPACE

#endif // BASEENGINEDEBUGCLIENT_H
