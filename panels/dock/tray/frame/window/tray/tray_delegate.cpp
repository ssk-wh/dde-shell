// Copyright (C) 2018 ~ 2025 Deepin Technology Co., Ltd.
// SPDX-FileCopyrightText: 2018 - 2023 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "tray_delegate.h"
#include "tray_gridview.h"
#include "tray_model.h"
#include "widgets/xembedtrayitemwidget.h"
#include "widgets/indicatortrayitem.h"
#include "widgets/indicatorplugin.h"
#include "widgets/snitrayitemwidget.h"
#include "widgets/expandiconwidget.h"
#include "utils.h"
#include "constants.h"
#include "pluginsiteminterface.h"
#include "quicksettingcontroller.h"
#include "systempluginitem.h"

#include <DGuiApplicationHelper>

#include <QPointer>
#include <QDebug>
#include <QEvent>
#include <QKeyEvent>
#include <QApplication>
#include <QPainterPath>
#include <QPainter>

#include <xcb/xcb_icccm.h>
#include <X11/Xlib.h>

TrayDelegate *TrayDelegate::getDockTrayDelegate(QListView *view, QObject *parent)
{
    static TrayDelegate *delegate = nullptr;
    if (!delegate) {
        delegate = new TrayDelegate(view, parent);
    }
    return delegate;
}

TrayDelegate *TrayDelegate::getIconTrayDelegate(QListView *view, QObject *parent)
{
    static TrayDelegate *delegate = nullptr;
    if (!delegate) {
        delegate = new TrayDelegate(view, parent);
    }
    return delegate;
}

TrayDelegate::TrayDelegate(QListView *view, QObject *parent)
    : QStyledItemDelegate(parent)
    , m_position(Dock::Position::Bottom)
    , m_listView(view)
{
    connect(this, &TrayDelegate::requestDrag, this, &TrayDelegate::onUpdateExpand);
}

void TrayDelegate::setPositon(Dock::Position position)
{
    m_position = position;
    SNITrayItemWidget::setDockPostion(position);
    SystemPluginItem::setDockPostion(m_position);
}

QWidget *TrayDelegate::createEditor(QWidget *parent, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    Q_UNUSED(option);

    auto *model = static_cast<TrayModel*>(m_listView->model());
    const WinInfo info = index.data(TrayModel::InfoRole).value<WinInfo>();;

    BaseTrayWidget *trayWidget = nullptr;
    if(info.type == TrayIconType::XEmbed) {
        if (Utils::IS_WAYLAND_DISPLAY) {
            static Display *display = XOpenDisplay(nullptr);
            static int screenp = 0;
            static xcb_connection_t *xcb_connection = xcb_connect(qgetenv("DISPLAY"), &screenp);
            trayWidget = new XEmbedTrayItemWidget(info.winId, xcb_connection, display, parent);
        } else {
            trayWidget = new XEmbedTrayItemWidget(info.winId, nullptr, nullptr, parent);
        }
        const TrayModel *model = qobject_cast<const TrayModel *>(index.model());
        if (model)
            connect(model, &TrayModel::requestUpdateIcon, trayWidget, &BaseTrayWidget::updateIcon);
    } else if (info.type == TrayIconType::Sni) {
        trayWidget = new SNITrayItemWidget(info.servicePath, parent);
    } else if (info.type == TrayIconType::ExpandIcon) {
        ExpandIconWidget *expandWidget = new ExpandIconWidget(parent);
        expandWidget->setPositon(m_position);
        if (info.expand)
            expandWidget->setTrayPanelVisible(true);

        trayWidget = expandWidget;
    } else if (info.type == TrayIconType::Incicator) {
        QString indicateName = info.key;
        int flagIndex = indicateName.indexOf("indicator:");
        if (flagIndex >= 0)
            indicateName = indicateName.right(indicateName.length() - QString("indicator:").length());
        IndicatorTrayItem *indicatorWidget = new IndicatorTrayItem(indicateName, parent);
        TrayModel *dataModel = qobject_cast<TrayModel *>(m_listView->model());
        if (IndicatorTrayItem *sourceIndicatorWidget = dataModel->indicatorWidget(info.key)) {
            connect(indicatorWidget, &IndicatorTrayItem::clicked, sourceIndicatorWidget, &IndicatorTrayItem::clicked);
            connect(sourceIndicatorWidget, &IndicatorTrayItem::textChanged, indicatorWidget, &IndicatorTrayItem::setText);
            const QByteArray pixmapData = sourceIndicatorWidget->pixmapData();
            if (!pixmapData.isEmpty())
                indicatorWidget->setPixmapData(pixmapData);
            const QString text = sourceIndicatorWidget->text();
            if (!text.isEmpty())
                indicatorWidget->setText(text);
        }
        trayWidget = indicatorWidget;
    } else if (info.type == TrayIconType::SystemItem) {
        const QString itemKey = QuickSettingController::instance()->itemKey(info.pluginInter);
        SystemPluginItem *trayItem = new SystemPluginItem(info.pluginInter, itemKey, parent);
        connect(trayItem, &SystemPluginItem::execActionFinished, this, &TrayDelegate::requestHide);
        trayWidget = trayItem;
    }

    if (trayWidget)
        trayWidget->setFixedSize(ICON_SIZE, ICON_SIZE);

    return trayWidget;
}

void TrayDelegate::onUpdateExpand(bool on)
{
    ExpandIconWidget *expandwidget = expandWidget();

    if (on) {
        if (expandwidget) {
            expandwidget->setTrayPanelVisible(true);
        } else {
            // 如果三角按钮不存在，那么就设置三角按钮可见，此时它会自动创建一个三角按钮
            TrayModel *model = qobject_cast<TrayModel *>(m_listView->model());
            if (model)
                model->setExpandVisible(true, true);
        }
    } else {
        // 获取托盘内图标的数量
        int trayIconCount = TrayModel::getIconModel()->rowCount();
        if (expandwidget) {
            // 如果释放鼠标，则判断当前鼠标的位置是否在托盘内部，如果在，则无需隐藏
            QPoint currentPoint = QCursor::pos();
            TrayGridWidget *view = ExpandIconWidget::popupTrayView();
            expandwidget->setTrayPanelVisible(view->geometry().contains(currentPoint) && (trayIconCount > 0));
        } else if (trayIconCount == 0) {
            ExpandIconWidget::popupTrayView()->hide();
        }
    }
}

void TrayDelegate::setEditorData(QWidget *editor, const QModelIndex &index) const
{
    BaseTrayWidget *widget = static_cast<BaseTrayWidget *>(editor);
    if (widget) {
        widget->setNeedShow(!index.data(TrayModel::Blank).toBool());
    }
}

QSize TrayDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    Q_UNUSED(option);
    Q_UNUSED(index);

    // 如果是弹出托盘，则显示正常大小
    if (isPopupTray())
        return QSize(ITEM_SIZE, ITEM_SIZE);

    // 如果是任务栏的托盘，则高度显示为listView的高度或宽度
    if (m_position == Dock::Position::Top || m_position == Dock::Position::Bottom)
        return QSize(ITEM_SIZE, m_listView->height());

    return QSize(m_listView->width(), ITEM_SIZE);
}

void TrayDelegate::updateEditorGeometry(QWidget *editor, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    Q_UNUSED(index);
    QRect rect = option.rect;
    // 让控件居中显示
    editor->setGeometry(rect.x() + (rect.width() - ICON_SIZE) / 2,
                        rect.y() + (rect.height() - ICON_SIZE) / 2,
                        ICON_SIZE, ICON_SIZE);
}

void TrayDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    Q_UNUSED(index);

    // 如果不是弹出菜单（在任务栏上显示的），在鼠标没有移入的时候无需绘制背景
    if (!isPopupTray() && !(option.state & QStyle::State_MouseOver)) {
        return QStyledItemDelegate::paint(painter, option, index);
    }

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);

    if (isPopupTray()) {
        QPainterPath path;
        path.addRoundedRect(option.rect, 8, 8);
        QColor borderColor;
        QColor backColor;
        if (DGuiApplicationHelper::instance()->themeType() == DGuiApplicationHelper::LightType) {
            // 白色主题的情况下
            borderColor = Qt::black;
            borderColor.setAlpha(static_cast<int>(255 * 0.05));
            backColor = Qt::white;
            if (option.state & QStyle::State_MouseOver) {
                backColor.setAlphaF(0.4);
            } else
                backColor.setAlphaF(0.2);
        } else {
            borderColor = Qt::black;
            borderColor.setAlpha(static_cast<int>(255 * 0.2));
            backColor = Qt::black;
            if (option.state & QStyle::State_MouseOver)
                backColor.setAlphaF(0.4);
            else
                backColor.setAlphaF(0.2);
        }

        painter->fillPath(path, backColor);
        painter->setPen(borderColor);
        painter->drawPath(path);
    } else {
        // 如果是任务栏上面的托盘图标，则绘制背景色
        int borderRadius = 8;
        // if (qApp->property(PROP_DISPLAY_MODE).value<Dock::DisplayMode>() == Dock::DisplayMode::Fashion) {
        //     borderRadius = qApp->property("trayBorderRadius").toInt() - 4;
        // }
        QRect rectBackground;
        QPainterPath path;
        if (m_position == Dock::Position::Top || m_position == Dock::Position::Bottom) {
            int backHeight = qBound(20, option.rect.height() - 4, 30);
            rectBackground.setLeft(option.rect.left());
            rectBackground.setTop(option.rect.top() + (option.rect.height() - backHeight) / 2);
            rectBackground.setHeight(backHeight);
            rectBackground.setWidth(option.rect.width());
            path.addRoundedRect(rectBackground.marginsRemoved({1, 1, 1, 1}), borderRadius, borderRadius);
        } else {
            int backWidth = qBound(20, option.rect.width() - 4, 30);
            rectBackground.setLeft(option.rect.left() + (option.rect.width() - backWidth) / 2);
            rectBackground.setTop(option.rect.top());
            rectBackground.setWidth(backWidth);
            rectBackground.setHeight(option.rect.height());
            path.addRoundedRect(rectBackground.marginsRemoved({1, 1, 1, 1}), borderRadius, borderRadius);
        }
        QColor backColor;
        QColor borderColor;
        if (DGuiApplicationHelper::instance()->themeType() == DGuiApplicationHelper::LightType) {
            // 白色主题的情况下
            backColor = Qt::white;
            backColor.setAlphaF(0.4);
            borderColor = Qt::black;
            borderColor.setAlphaF(0.1);
        } else {
            backColor = Qt::white;
            backColor.setAlphaF(0.1);
            borderColor = Qt::white;
            borderColor.setAlphaF(0.2);
        }

        painter->fillPath(path, backColor);
        painter->setPen(QPen(borderColor, 1));
        painter->drawPath(path);
    }

    painter->restore();
}

ExpandIconWidget *TrayDelegate::expandWidget()
{
    if (!m_listView)
        return nullptr;

    QAbstractItemModel *dataModel = m_listView->model();
    if (!dataModel)
        return nullptr;

    for (int i = 0; i < dataModel->rowCount(); i++) {
        QModelIndex index = dataModel->index(i, 0);
        ExpandIconWidget *widget = qobject_cast<ExpandIconWidget *>(m_listView->indexWidget(index));
        if (widget)
            return widget;
    }

    return nullptr;
}

bool TrayDelegate::isPopupTray() const
{
    if (!m_listView)
        return false;

    TrayModel *dataModel = qobject_cast<TrayModel *>(m_listView->model());
    if (!dataModel)
        return false;

    return dataModel->isIconTray();
}
