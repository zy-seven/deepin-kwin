/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2023 zhang yu <zhangyud@uniontech.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#ifndef KWIN_SPLITPREVIEW_H
#define KWIN_SPLITPREVIEW_H

#include <kwineffects.h>
#include <kwineffectsex.h>
#include "kwinoffscreenquickview.h"
#include <QQuickView>

class QQuickView;

namespace KWin
{
class SplitPreviewEffect : public Effect
{
    Q_OBJECT
public:
    SplitPreviewEffect();
    ~SplitPreviewEffect() override;

    void prePaintScreen(ScreenPrePaintData &data, std::chrono::milliseconds presentTime) override;
    void paintScreen(int mask, const QRegion &region, ScreenPaintData &data) override;
    void postPaintScreen() override;
    void prePaintWindow(EffectWindow *w, WindowPrePaintData &data, std::chrono::milliseconds presentTime) override;
    void paintWindow(EffectWindow *w, int mask, QRegion region, WindowPaintData &data) override;

    void windowInputMouseEvent(QEvent* e) override;
    void grabbedKeyboardEvent(QKeyEvent* e) override;

    bool isActive() const override;

private:
    bool isRelevantWithPresentWindows(EffectWindow *w) const;
    void calculateWindowTransformations(EffectWindowList windows, WindowMotionManager& wmm);
    void calculateWindowTransformationsClosest(EffectWindowList windowlist, WindowMotionManager& motionManager);
    QRect getPreviewWindowsGeometry(EffectWindow *w);
    void setActive(bool);
    void cleanup();

public Q_SLOTS:
    void toggle(KWin::EffectWindow *w);

private:
    bool                         m_activated = false;
    bool                         m_hasKeyboardGrab = false;
    QVector<WindowMotionManager> m_motionManagers;
    EffectWindow                 *m_window = nullptr;
    EffectWindow                 *m_hoverwin = nullptr;
    QRect                        m_backgroundRect;
    QRectF                       m_screenRect;
    int                          m_backgroundMode;
    std::chrono::milliseconds    lastPresentTime;
    std::unique_ptr<EffectFrameEx>  m_effectFrame;
};
}
#endif
