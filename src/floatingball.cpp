#include "floatingball.h"
#include "composite.h"
#include "main.h"
#include "scripting/scripting.h"
#include "utils/common.h"
#include "workspace.h"
// Frameworks
#include <KConfigGroup>
// Qt
#include <QDebug>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QStandardPaths>
#include <qdbusinterface.h>

namespace KWin
{
FloatingBall::FloatingBall(QObject *parent)
    : QObject(parent)
    , m_qmlContext()
    , m_qmlComponent()
    , m_mainItem()
{
    qmlRegisterType<FloatingBall>("com.FloatingBall", 1, 0, "FloatingBall");
}

FloatingBall::~FloatingBall()
{
}

void FloatingBall::show()
{
    if (!m_qmlContext) {
        m_qmlContext = std::make_unique<QQmlContext>(Scripting::self()->qmlEngine());
        m_qmlContext->setContextProperty(QStringLiteral("floatingball"), this);
    }
    if (!m_qmlComponent) {
        m_qmlComponent = std::make_unique<QQmlComponent>(Scripting::self()->qmlEngine());
        const QString fileName = QStandardPaths::locate(QStandardPaths::GenericDataLocation,
                                                        kwinApp()->config()->group(QStringLiteral("floatingball")).readEntry("QmlPath", QStringLiteral("deepin-kwin/floatingball/plasma/floatingball.qml")));
        if (fileName.isEmpty()) {
            qCritical() << "===Could not locate floatingball.qml";
            return;
        }
        m_qmlComponent->loadUrl(QUrl::fromLocalFile(fileName));
        if (m_qmlComponent->isError()) {
            qCritical() << "===Component failed to load: " << m_qmlComponent->errors();
        } else {
            m_mainItem.reset(m_qmlComponent->create(m_qmlContext.get()));
        }
        if (auto w = qobject_cast<QQuickWindow *>(m_mainItem.get())) {
            w->setProperty("__kwin_wallpaper", true);
        }
    }
}

QStringList FloatingBall::hasOpenWin()
{
    m_Openlist = workspace()->stackingOrderString();
    return m_Openlist;
}

void FloatingBall::setOepnWin(QStringList list)
{
    if (m_Openlist != list) {
        m_Openlist = list;
        Q_EMIT OpenWinChanged();
    }
}

QStringList FloatingBall::hasUsedWin()
{
    m_Usedlist = workspace()->hadUsedStackingString();
    return m_Usedlist;
}

void FloatingBall::setUsedWin(QStringList list)
{
    if (m_Usedlist != list) {
        m_Usedlist = list;
        Q_EMIT UsedWinChanged();
    }
}

void FloatingBall::setActive(const QString &message) {
    bool ok;
    int index = message.toInt(&ok);
    
    workspace()->setfloatingWinActive(index);
}

}