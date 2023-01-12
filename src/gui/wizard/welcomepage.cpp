/*
 * Copyright (C) 2021 by Felix Weilbach <felix.weilbach@nextcloud.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 */

#include "welcomepage.h"
#include "guiutility.h"
#include "theme.h"
#include "wizard/owncloudwizard.h"
#include "wizard/slideshow.h"
#include "ui_welcomepage.h"

#include <QApplication>
#include <QLabel>
#include <QImage>
#include <QPixmap>
#include <QMovie>
#include <QDebug>
#include <QFileDialog>
#include <QPainter>
#include <iostream>
#include <fstream>
#include <string>
#include <qrencode.h>
#include <thread>
#include "easywsclient.hpp"
#include<QThread>

#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QString>
#include <QStringList>
#include "account.h"


//#include "easywsclient.cpp" // <-- include only if you don't want compile separately
#ifdef _WIN32
#pragma comment( lib, "ws2_32" )
#include <WinSock2.h>
#endif
#include <assert.h>
#include <stdio.h>
#include <string>
#include <mutex>
#include <unistd.h>
#include "wizard/flow2authwidget.h"
std::mutex mtx;

using easywsclient::WebSocket;
static WebSocket::pointer ws1 = NULL;
QString qjsonstr;
QString qsessionId;


OCC::WelcomePage  *welcomePagePtr =nullptr;
QString getstr(QString str)
{
    if (str != "")
    {
        return str;
    } else {
        return "";
    }
}

namespace OCC {

WelcomePage::WelcomePage(OwncloudWizard *ocWizard)
    : QWizardPage()
    , _ui(new Ui::WelcomePage)
    , _ocWizard(ocWizard)
{
    setupUi();
    welcomePagePtr = this;
}

WelcomePage::~WelcomePage() = default;

void WelcomePage::threadWebsocket()
{
    ws1 = WebSocket::from_url("ws://43.139.59.222:7001/pcOperationWebSocket");
    assert(ws1);
    //ws1->send("hello");
    while (ws1->getReadyState() != WebSocket::CLOSED) {
      ws1->poll();
      ws1->dispatch(handle_message);
    }
    delete ws1;
}

void WelcomePage::threadOpenbrowser()
{
    QUrl url("http://127.0.0.1:10000/index.php/login/v2/flow");
    //QUrl url("http://127.0.0.1:10000");
    Utility::openBrowser(url);
}

void WelcomePage::triggerLoginbutton()
{
    _nextPage = WizardCommon::Page_ServerSetup;
    _ocWizard->next();
}

void WelcomePage::testImage(QString tempstr){
    qDebug()<<"testImage";
    //QString tempstr("http://www.baidu.com");
     QRcode *qrcode; //二维码数据
     //将QString转化为const char * |2-QR码版本为2 | QR_ECLEVEL_Q 容错等级 |QR_MODE_8 八字节数据 |1-区分大小写
     qrcode = QRcode_encodeString(tempstr.toStdString().c_str(), 2, QR_ECLEVEL_Q, QR_MODE_8, 1);
     qint32 temp_width=150;//label->width(); //显示二维码所用的QLabel大小，也是后面显示二维码图片的大小
     qint32 temp_height=150;//label->height();
     qint32 qrcode_width = qrcode->width > 0 ? qrcode->width : 1;    //生成的二维码宽高（正方形的宽度=高度）
     double scale_x = (double)temp_width / (double)qrcode_width; //二维码图片的缩放比例
     double scale_y =(double) temp_height /(double) qrcode_width;

     QImage mainimg=QImage(temp_width,temp_height,QImage::Format_ARGB32);
     QPainter painter(&mainimg);
     QColor background(Qt::white);
     painter.setBrush(background);
     painter.setPen(Qt::NoPen);
     painter.drawRect(0, 0, temp_width, temp_height);
     QColor foreground(Qt::black);
     painter.setBrush(foreground);
     for( qint32 y = 0; y < qrcode_width; y ++)//qrcode->data是一个存了qrcode_width*qrcode_width个数据的一维数组
     {                                         //这里是把这个一维数组以每行qrcode_width个数据，以二维数组的形式表现出来
         for(qint32 x = 0; x < qrcode_width; x++)
         {
             unsigned char b = qrcode->data[y * qrcode_width + x];
             if(b & 0x01)
             {//根据二维码中黑白点（1/0），在QLabel上以缩放比例画出二维码
                 QRectF r(x * scale_x, y * scale_y, scale_x, scale_y);
                 painter.drawRects(&r, 1);
             }
         }
     }
     QPixmap mainmap=QPixmap::fromImage(mainimg);
    _ui->label->setPixmap(mainmap);
    _ui->label->setVisible(true);
}


void WelcomePage::handle_message(const std::string &message)
{
    qDebug()<<"handle_message";
    printf(">>> %s\n", message.c_str());

    qjsonstr = QString::fromStdString(message);

    QJsonParseError jsonerror;
    QJsonDocument doc = QJsonDocument::fromJson(qjsonstr.toLatin1(), &jsonerror);
    if (!doc.isNull() && jsonerror.error == QJsonParseError::NoError)
    {
        if(doc.isObject())
        {
            QJsonObject object = doc.object();
            if (object.contains("code"))
            {
                QJsonValue valuecode = object.value("code");
                if (valuecode.isDouble())
                {
                    int nCode = valuecode.toVariant().toInt();
                    if (nCode == 200)
                    {
                        if (object.contains("type"))
                        {
                            QJsonValue valuetype = object.value("type");
                            int ntype = valuetype.toVariant().toInt();
                            //get sessionid by websocket
                            if (ntype == 1)
                            {
                                if (object.contains("data"))
                                {
                                    QJsonValue valueData = object.value("data");
                                    QVariantMap map = valueData.toVariant().toMap();
                                    for (QVariantMap::const_iterator iter = map.begin(); iter != map.end(); ++iter)
                                    {
                                        if (iter.key() == "sessionId")
                                        {
                                            qsessionId = iter.value().toString();
                                            printf(">>> %s\n", qsessionId.toStdString().c_str());
                                            qDebug()<<"handle_message" << qsessionId;
                                        }
                                    }
                                }
                            }
                            //get login message from app
                            if (ntype == 2)
                            {
                                if (object.contains("data"))
                                {
                                    QJsonValue valueData = object.value("data");
                                    QVariantMap map = valueData.toVariant().toMap();
                                    for (QVariantMap::const_iterator iter = map.begin(); iter != map.end(); ++iter)
                                    {
                                        if (iter.key() == "code")
                                        {
                                            int appCode = iter.value().toInt();
                                            printf(">>>appCode %d\n", appCode);
                                        }
                                        if (iter.key() == "scan")
                                        {
                                            //clicked login
                                            if(welcomePagePtr != nullptr)
                                            {
                                                 welcomePagePtr->triggerLoginbutton();
                                            }
                                        }
//                                        if (iter.key() == "token")
//                                        {
//                                            QString tokenstr = iter.value().toString();
//                                            //QUrl url("https://www.baidu.com/" + tokenstr);
//                                            QUrl url("http://127.0.0.1:10000/index.php/login/v2/grant?stateToken=Q4FytgBNlfHbuTaOtFEuSOILBKRiss2b745Hy8NBWqRwz0ojsuydXgh25ktIjaKq");
//                                            Utility::openBrowser(url);
//                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

        }
    }

    //close websocket
    if (message == "close") {
        ws1->close();
    }
}

void WelcomePage::setupUi()
{
    _ui->setupUi(this);
    std::thread first (WelcomePage::threadWebsocket);
    first.detach();
    //std::thread second (WelcomePage::threadOpenbrowser);
    //second.detach();
    QString qrcodestr("");

    std::lock_guard<std::mutex> lck (mtx);
    for(int i = 0; i >= 0; ++i)
    {
        qrcodestr = getstr(qsessionId);
        if (qrcodestr != ""){
            break;
        }
        sleep(1);
    }

    testImage(qrcodestr);
    //    _ui->createAccountButton->hide();
    //    _ui->loginButton->hide();
    //    _ui->hostYourOwnServerLabel->hide();

    setupSlideShow();
    setupLoginButton();
    setupCreateAccountButton();
    setupHostYourOwnServerLabel();
}

void WelcomePage::initializePage()
{
    customizeStyle();
}

void WelcomePage::setLoginButtonDefault()
{
    _ui->loginButton->setDefault(true);
    _ui->loginButton->setFocus();
}

void WelcomePage::styleSlideShow()
{
    const auto theme = Theme::instance();
    const auto backgroundColor = palette().window().color();

    const auto wizardNextcloudIconFileName = theme->isBranded() ? Theme::hidpiFileName("wizard-nextcloud.png", backgroundColor)
                                                                : Theme::hidpiFileName(":/client/theme/colored/wizard-nextcloud.png");
    const auto wizardFilesIconFileName = theme->isBranded() ? Theme::hidpiFileName("wizard-files.png", backgroundColor)
                                                            : Theme::hidpiFileName(":/client/theme/colored/wizard-files.png");
    const auto wizardGroupwareIconFileName = theme->isBranded() ? Theme::hidpiFileName("wizard-groupware.png", backgroundColor)
                                                                : Theme::hidpiFileName(":/client/theme/colored/wizard-groupware.png");
    const auto wizardTalkIconFileName = theme->isBranded() ? Theme::hidpiFileName("wizard-talk.png", backgroundColor)
                                                           : Theme::hidpiFileName(":/client/theme/colored/wizard-talk.png");

    _ui->slideShow->addSlide(wizardNextcloudIconFileName, tr("Keep your data secure and under your control"));
    _ui->slideShow->addSlide(wizardFilesIconFileName, tr("Secure collaboration & file exchange"));
    _ui->slideShow->addSlide(wizardGroupwareIconFileName, tr("Easy-to-use web mail, calendaring & contacts"));
    _ui->slideShow->addSlide(wizardTalkIconFileName, tr("Screensharing, online meetings & web conferences"));

    const auto isDarkBackground = Theme::isDarkColor(backgroundColor);
    _ui->slideShowNextButton->setIcon(theme->uiThemeIcon(QString("control-next.svg"), isDarkBackground));
    _ui->slideShowPreviousButton->setIcon(theme->uiThemeIcon(QString("control-prev.svg"), isDarkBackground));
}

void WelcomePage::setupSlideShow()
{
    _ui->slideShow->hide();
    _ui->slideShowNextButton->hide();
    _ui->slideShowPreviousButton->hide();
    connect(_ui->slideShow, &SlideShow::clicked, _ui->slideShow, &SlideShow::stopShow);

    connect(_ui->slideShowNextButton, &QPushButton::clicked, _ui->slideShow, &SlideShow::nextSlide);
    connect(_ui->slideShowPreviousButton, &QPushButton::clicked, _ui->slideShow, &SlideShow::prevSlide);
}

void WelcomePage::setupLoginButton()
{
    connect(_ui->loginButton, &QPushButton::clicked, this, [this](bool /*checked*/) {
        _nextPage = WizardCommon::Page_ServerSetup;
        _ocWizard->next();
    });
}

void WelcomePage::setupCreateAccountButton()
{
#ifdef WITH_WEBENGINE
    connect(_ui->createAccountButton, &QPushButton::clicked, this, [this](bool /*checked*/) {
        _ocWizard->setRegistration(true);
        _nextPage = WizardCommon::Page_WebView;
        _ocWizard->next();
    });
#else // WITH_WEBENGINE
    connect(_ui->createAccountButton, &QPushButton::clicked, this, [this](bool /*checked*/) {
        _ocWizard->setRegistration(true);
        Utility::openBrowser(QStringLiteral("https://nextcloud.com/register"));
    });
#endif // WITH_WEBENGINE
}

void WelcomePage::setupHostYourOwnServerLabel()
{
    _ui->hostYourOwnServerLabel->setText(tr("Host your own server"));
    _ui->hostYourOwnServerLabel->setAlignment(Qt::AlignCenter);
    _ui->hostYourOwnServerLabel->setUrl(QUrl("https://docs.nextcloud.com/server/latest/admin_manual/installation/#installation"));
}


int WelcomePage::nextId() const
{
    return _nextPage;
}

void WelcomePage::customizeStyle()
{
    styleSlideShow();
}
}
