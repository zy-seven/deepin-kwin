#pragma once
#include <QObject>
#include <kwinglobals.h>
#include <memory>

class QQmlContext;
class QQmlComponent;

namespace KWin
{
class FloatingBall : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QStringList hasOpenWin READ hasOpenWin WRITE setOepnWin NOTIFY OpenWinChanged)
    Q_PROPERTY(QStringList hasUsedWin READ hasUsedWin WRITE setUsedWin NOTIFY UsedWinChanged)
public:
    FloatingBall(QObject *parent = nullptr);
    ~FloatingBall();

    void show();
    QStringList hasOpenWin();
    void setOepnWin(QStringList list);

    QStringList hasUsedWin();
    void setUsedWin(QStringList list);

    Q_INVOKABLE void setActive(const QString &message);
Q_SIGNALS:
    void OpenWinChanged();
    void UsedWinChanged();

private:
    std::unique_ptr<QQmlContext> m_qmlContext;
    std::unique_ptr<QQmlComponent> m_qmlComponent;
    std::unique_ptr<QObject> m_mainItem;

    QStringList m_Openlist;
    QStringList m_Usedlist;
};

}


