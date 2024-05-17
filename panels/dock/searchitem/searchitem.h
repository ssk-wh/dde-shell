// SPDX-FileCopyrightText: 2024 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ddockapplet.h"
#include "dsglobal.h"

namespace dock {

class SearchItem : public DS_NAMESPACE::DDockApplet
{
    Q_OBJECT
public:
    explicit SearchItem(QObject *parent = nullptr);

    QString displayName() const override;
    QString itemKey() const override;
    QString settingKey() const override;

    Q_INVOKABLE void toggleGrandSearch();
    Q_INVOKABLE void toggleGrandSearchConfig();
};

}
