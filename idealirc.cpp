/*
 *   IdealIRC - Internet Relay Chat client
 *   Copyright (C) 2014  Tom-Andre Barstad
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License along
 *   with this program; if not, write to the Free Software Foundation, Inc.,
 *   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#include <iostream>
#include <QTreeWidgetItem>
#include <QHashIterator>
#include <QMapIterator>
#include <QDebug>
#include <QPalette>
#include <QMessageBox>

#include "idealirc.h"
#include "ui_idealirc.h"

#include "iabout.h"

IdealIRC::IdealIRC(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::IdealIRC),
    firstShow(true),
    windowIsActive(true),
    confDlg(NULL),
    favourites(NULL),
    chanlist(NULL),
    scriptManager(NULL),
    connectionsRemaining(-1),
    preventSocketAction(false),
    reconnect(NULL),
    scriptParent(this, this, &conf, &conlist, &winlist, &activeWid, &activeConn)
{
    ui->setupUi(this);

    connect(qApp, SIGNAL(focusChanged(QWidget*,QWidget*)),
            this, SLOT(applicationFocusChanged(QWidget*,QWidget*)));

    ui->menuIIRC->addAction(ui->actionConnect);
    ui->menuIIRC->addAction(ui->actionOptions);
    ui->menuTools->addAction(ui->actionChannel_favourites);
    ui->menuTools->addAction(ui->actionChannels_list);
    ui->menuTools->addAction(ui->actionScript_Manager);

    QString version = QString("IdealIRC %1").arg(VERSION_STRING);
    setWindowTitle(version);
    trayicon.setToolTip(version);
    trayicon.setIcon( QIcon(":/gfx/icon16x16.png") );
    trayicon.setVisible(true);

    conf.rehash();

    setGeometry(conf.mainWinGeo);
    if (conf.maximized)
      setWindowState(Qt::WindowMaximized);

    ui->toolBar->setVisible(conf.showToolBar);

    connect(&vc, SIGNAL(gotVersion()),
            this, SLOT(versionReceived()));

    if (conf.checkVersion)
        vc.runChecker();

    QFont f(conf.fontName);
    f.setPixelSize(conf.fontSize);
    ui->treeWidget->setFont(f);

    updateTreeViewColor();

    connect(&scriptParent, SIGNAL(RequestWindow(QString,int,int,bool)),
            this, SLOT(CreateSubWindow(QString,int,int,bool)));

    connect(&trayicon, SIGNAL(activated(QSystemTrayIcon::ActivationReason)),
            this, SLOT(onTrayActivated(QSystemTrayIcon::ActivationReason)));

}

IdealIRC::~IdealIRC()
{
    delete ui;
}

void IdealIRC::recreateConfDlg()
{
    if (confDlg != NULL) {
        disconnect(confDlg, SIGNAL(connectToServer(bool)));
        disconnect(confDlg, SIGNAL(configSaved()));
        delete confDlg;
    }

    IConnection *c = conlist.value(activeConn, NULL);

    confDlg = new IConfig(&conf, c, this);
    connect(confDlg, SIGNAL(connectToServer(bool)),
            this, SLOT(extConnectServer(bool)));
    connect(confDlg, SIGNAL(configSaved()),
            this, SLOT(configSaved()));

    subwindow_t sw = winlist.value(activeWid);
    if (sw.type >= WT_GRAPHIC) // Custom window
        confDlg->setConnectionEnabled(false);
    else
        confDlg->setConnectionEnabled(true);
}

void IdealIRC::recreateFavouritesDlg()
{
    if (favourites != NULL)
        favourites->deleteLater();

    favourites = new IFavourites(&conf, this);
}

void IdealIRC::recreateChanlistDlg()
{
    if (chanlist != NULL)
        chanlist->deleteLater();

    chanlist = new IChannelList(this);
}

void IdealIRC::recreateScriptManager()
{
    if (scriptManager != NULL)
        scriptManager->deleteLater();

    scriptManager = new IScriptManager(this, &scriptParent, &conf);
}


void IdealIRC::showEvent(QShowEvent *)
{
    /// Insert stuff that should run every showEvent here:


    // --
    if (! firstShow) return;
    firstShow = false;

    /// Insert stuff that should run when IIRC shows for first time here:
    ui->treeWidget->setSortingEnabled(true);
    ui->treeWidget->sortItems(0, Qt::AscendingOrder);

    CreateSubWindow("Status", WT_STATUS, 0, true);

    if (conf.showOptionsStartup)
        on_actionOptions_triggered();

    scriptParent.loadAllScripts();
    scriptParent.runevent(te_start);
}

void IdealIRC::closeEvent(QCloseEvent *e)
{
    if (connectionsRemaining > 0) {
        e->ignore(); // Still waiting for connections to close...
        return;
    }

    if (connectionsRemaining == -1) {
        // See if there is any connections active, if so, confirm on exit.
        QHashIterator<int,IConnection*> i(conlist);
        bool hasActive = false;
        while (i.hasNext()) {
            IConnection *c = i.next().value();
            if (c != NULL)
                if (c->isOnline())
                    hasActive = true;
            if (hasActive)
                break;
        }

        if (hasActive) {
            int b = QMessageBox::question(this, tr("Confirm exit"),
                                          tr("There's connections active.\r\nDo you want to exit IdealIRC?"),
                                          QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            if (b == QMessageBox::No) {
                e->ignore();
                return;
            }
        }
    }

    // Save config prior to exit.
    conf.showToolBar = ui->toolBar->isVisible();
    conf.maximized = isMaximized();
    conf.save();

    if (connectionsRemaining == 0) {
        e->accept(); // All connections closed, exit.
        return;
    }

    // Iterate through the connections and find the active ones and close them.
    // Remove and clean up their children too (windows and status)
    QHashIterator<int,IConnection*> ic(conlist);
    QList<IConnection*> cl;
    while (ic.hasNext()) {
        ic.next();
        IConnection *con = ic.value();
        if (con->isSocketOpen()) {
            if (connectionsRemaining == -1)
                connectionsRemaining = 0;
            ++connectionsRemaining;
            cl.push_back(con);
        }
    }

    readyToClose = false;
    for (int i = 0; i <= cl.count()-1; i++)
        cl[i]->closeConnection(true);
    readyToClose = true;
    close(); // final close attempt
}

void IdealIRC::resizeEvent(QResizeEvent *e)
{
    QMainWindow::resizeEvent(e);
    if (e->type() == QResizeEvent::Resize) {
     // winlist.value(activewin).window->setGeometry(0,0,ui->cont->width(), ui->cont->height());
    }

    conf.mainWinGeo = geometry();

    QList<int> sz;
    sz << conf.treeWidth;
    sz << ui->centralWidget->width() - conf.treeWidth;
    ui->splitter->setSizes(sz);
}

void IdealIRC::moveEvent(QMoveEvent *)
{
    conf.mainWinGeo = geometry();
}

bool IdealIRC::WindowExists(QString name, int parent)
{
    QHashIterator<int, subwindow_t> i(winlist);
    while (i.hasNext()) {
        i.next();

        subwindow_t sw = i.value();

        if ((sw.widget->objectName().toUpper() == name.toUpper()) && (i.value().parent == parent)) {
            if (sw.type == WT_STATUS)
                return false;
            else
                return true;
        }
    }


    return false;
}

void IdealIRC::subWinClosed(int wid)
{
    subwindow_t empty;
    empty.wid = -1;

    subwindow_t sw = winlist.value(wid, empty);

    if (sw.wid == -1)
        return; // Nope.

    std::cout << "Closing " << sw.widget->objectName().toStdString().c_str() << " (" << wid << ")" << std::endl;

    if (sw.type == WT_STATUS) {
        // Closing a status window.
        QList<QMdiSubWindow*> closeList; // List of subwindows for this status to close
        QHashIterator<int,subwindow_t> i(winlist); // Iterate all windows
        IConnection *con = conlist.value(wid); // Remember window id of status is always equal to connection id. KISS!
        while (i.hasNext()) {
            i.next();
            subwindow_t s = i.value();
            // This item may not be a subwindow of the status that's closing.

            if (s.parent == wid) {
                // This item is a child of the status
                closeList.push_back(s.subwin);
                con->freeWindow( s.widget->objectName() );
            }
        }

        if (con->isSocketOpen())
            con->closeConnection(); // Begin closing the socket.

        // Close all its subwindows
        int count = closeList.count()-1;
        std::cout << "  count : " << count << std::endl;
        for (int i = 0; i <= count; i++) {
            QMdiSubWindow *w = closeList.at(i);
            w->close();
        }

        con->freeWindow("STATUS"); // Free status window lastly
        if (! con->isSocketOpen())
            delete con; // Socket wasn't open, delete connection object

    }

    else {
        IConnection *con = conlist.value(sw.parent, NULL);
        if (con != NULL)
            con->freeWindow( sw.widget->objectName() );
        winlist.remove(sw.wid);
    }

    if (sw.type == WT_CHANNEL) {
        IConnection *con = conlist.value(sw.parent);
        con->sockwrite("PART :" + sw.widget->objectName() );
    }

    ui->treeWidget->removeItemWidget(sw.treeitem, 0);
    delete sw.treeitem;
    delete sw.widget;
    winlist.remove(wid);


    /**
       @note Do we need to delete the other pointers from subwindow_t ?
    **/
}

// Returns -1 if failed, otherwise window id.
// ID 0 is reserved to define "no parents" under 'int parent'.
//                       Name in treeview   Window type  wid parent    activate on creation
int IdealIRC::CreateSubWindow(QString name, int type, int parent, bool activate)
{

    if (WindowExists(name, parent) == true)
        return -1;

    qDebug() << "Creating new subwindow type " << type << " name " << name;

    IWin *s = new IWin(ui->mdiArea, name, type, &conf, &scriptParent);

    IConnection *connection = conlist.value(parent, NULL);
    if (type == WT_STATUS) {
        qDebug() << "Window is status, new connection added with id " << s->getId();
        connection = new IConnection(this, &chanlist, s->getId(), &conf, &scriptParent);
        connection->setActiveInfo(&activeWname, &activeConn);
        connect(connection, SIGNAL(connectionClosed()),
                this, SLOT(connectionClosed()));
        connect(connection, SIGNAL(connectedToIRC()),
                this, SLOT(connectionEstablished()));
        connect(connection, SIGNAL(RequestTrayMsg(QString,QString)),
                this, SLOT(trayMessage(QString,QString)));
    }

    qDebug() << "Connection added, setting pointers";

    s->setConnectionPtr(connection);
    if (connection != NULL)
        s->setSortRuleMap(connection->getSortRuleMapPtr());

    qDebug() << "Pointers set, setting up mdiSubWindow";

    QMdiSubWindow *previous = ui->mdiArea->currentSubWindow();
    QMdiSubWindow *sw = ui->mdiArea->addSubWindow(s, Qt::SubWindow);

    qDebug() << "Pointer to subwindow: " << sw;

    qDebug() << "Icon...";

    // Add icon to window
    QString ico = ":/window/gfx/custom.png"; // Default to this.
    if (type == WT_PRIVMSG)
      ico = ":/window/gfx/query.png";
    if (type == WT_CHANNEL)
      ico = ":/window/gfx/channel.png";
    if (type == WT_STATUS)
      ico = ":/window/gfx/status.png";
    sw->setWindowIcon(QIcon(ico));

    qDebug() << "Treeitem...";

    QTreeWidgetItem *treeitem = GetWidgetItem(parent);

    if (treeitem == NULL)
        treeitem = new QTreeWidgetItem(ui->treeWidget);
    else
        treeitem = new QTreeWidgetItem(treeitem);


    treeitem->setIcon(0, QIcon(ico));
    treeitem->setText(0, name);
    treeitem->setToolTip(0, name);

    qDebug() << "subwindow_t instance...";

    subwindow_t wt;
    wt.connection = connection;
    wt.parent = parent;
    wt.subwin = sw;
    wt.treeitem = treeitem;
    wt.type = type;
    wt.wid = s->getId();
    wt.widget = s;
    wt.highlight = HL_NONE;

    qDebug() << "Adding subwindow_t to winlist...";

    winlist.insert(s->getId(), wt);

    sw->setGeometry(0, 0, 500, 400);

    if (type == WT_STATUS) {
        qDebug() << "Adding connection to the list...";
        // The Connection class ID is the exact same ID as the status window ID.
        conlist.insert(s->getId(), connection);
        treeitem->setExpanded(true);
        connection->addWindow("STATUS", wt);
        connect(connection, SIGNAL(RequestWindow(QString,int,int,bool)),
                this, SLOT(CreateSubWindow(QString,int,int,bool)));
        connect(connection, SIGNAL(HighlightWindow(int,int)),
                this, SLOT(Highlight(int,int)));
    }

    if (connection != NULL) {
        qDebug() << "Adding this window to the connections window list...";

        connection->addWindow(name, wt); // toUpper is ran inside this function. Do not do toUpper here, it'll make it look weird in autocomplete.

        qDebug() << "Passing the command handler...";

        s->setCmdHandler( connection->getCmdHndlPtr() );

        connect(s, SIGNAL(doCommand(QString)),
                connection->getCmdHndlPtr(), SLOT(parse(QString)));

        connect(s, SIGNAL(sendToSocket(QString)),
                connection, SLOT(sockwrite(QString)));

        connect(connection, SIGNAL(updateConnectionButton()),
                this, SLOT(updateConnectionButton()));

    }

    qDebug() << "General signals...";

    connect(s, SIGNAL(closed(int)),
            this, SLOT(subWinClosed(int)));

    connect(s, SIGNAL(RequestWindow(QString,int,int,bool)),
            this, SLOT(CreateSubWindow(QString,int,int,bool)));

    connect(s, SIGNAL(Highlight(int,int)),
            this, SLOT(Highlight(int,int)));

    qDebug() << "Determining to activate window...";

    if (previous == 0) {
        sw->showMaximized();
        s->setFocus();
    }
    else {
        sw->show();
        if (! activate)
            ui->mdiArea->setActiveSubWindow(previous);
    }

    qDebug() << "'- activate=" << activate;


    qDebug() << "Returning with window id.";
    return s->getId();
}

QTreeWidgetItem* IdealIRC::GetWidgetItem(int wid)
{
    if (wid == 0)
        return NULL;

    if (! winlist.contains(wid))
        return NULL;

    subwindow_t t = winlist.value(wid);
    return t.treeitem;

}

void IdealIRC::on_mdiArea_subWindowActivated(QMdiSubWindow *arg1)
{
    QHashIterator<int,subwindow_t> i(winlist);

    while (i.hasNext()) {
        i.next();
        subwindow_t sw = i.value();

        if (sw.subwin == arg1) {
            activeWid = sw.wid;
            sw.treeitem->setForeground(0, QBrush(QColor(conf.colWindowlist)));
            sw.highlight = HL_NONE;
            ui->treeWidget->setCurrentItem(sw.treeitem);
            updateConnectionButton();
            winlist.insert(sw.wid, sw);
            break;
        }
    }
}

void IdealIRC::on_treeWidget_itemSelectionChanged()
{
    QTreeWidgetItem *item = ui->treeWidget->currentItem();

    QHashIterator<int,subwindow_t> i(winlist);
    while (i.hasNext()) {
        i.next();
        subwindow_t sw = i.value();

        if (sw.treeitem == item) {
            ui->mdiArea->setActiveSubWindow( sw.subwin );
            activeWid = i.key();
            activeWname = sw.widget->objectName();

            if (sw.connection != NULL)
                activeConn = sw.connection->getCid();
            sw.widget->setFocus();
            std::cout << "activeWid=" << activeWid << std::endl;

            if ((favourites != NULL) && (sw.connection != NULL))
                favourites->setConnection(sw.connection);

            if ((chanlist != NULL) && (sw.connection != NULL))
                chanlist->setConnection(sw.connection);

        }
    }
}

void IdealIRC::on_actionOptions_triggered()
{
    recreateConfDlg();

    confDlg->show();
    confDlg->activateWindow();
}

void IdealIRC::extConnectServer(bool newWindow)
{
    if (newWindow)
        activeWid = CreateSubWindow("Status", WT_STATUS, 0, true);

    subwindow_t current = winlist.value(activeWid);

    if (current.parent == 0) {
        // Connect to this status window
        IConnection *con = conlist.value(current.wid);
        if (con->isSocketOpen()) {
            reconnect = con;
            con->closeConnection();
        }
        else
            con->tryConnect();
    }
    else {
        // Connect to current.parent
        subwindow_t parent = winlist.value(current.parent);
        IConnection *con = conlist.value(parent.wid);
        if (con->isSocketOpen()) {
            reconnect = con;
            con->closeConnection();
        }
        else
            con->tryConnect();
    }
}

void IdealIRC::updateConnectionButton()
{
    preventSocketAction = true;

    favouritesJoinEnabler();
    chanlistEnabler();

    subwindow_t sw = winlist.value(activeWid);

    if ((sw.type != WT_CHANNEL) &&
        (sw.type != WT_PRIVMSG) &&
        (sw.type != WT_STATUS)) {

        ui->actionConnect->setChecked(false);
        ui->actionConnect->setEnabled(false);
        preventSocketAction = false;

        if (confDlg != NULL)
            confDlg->setConnectionEnabled(false);

        return;
    }
    else {
        ui->actionConnect->setEnabled(true);
        if (confDlg != NULL)
            confDlg->setConnectionEnabled(true);
    }

    IConnection *c = sw.widget->getConnection();
    bool open = false;
    if (c != NULL)
        open = c->isSocketOpen();

    if (open)
        ui->actionConnect->setChecked(true);
    else
        ui->actionConnect->setChecked(false);
    preventSocketAction = false;
}

void IdealIRC::on_actionConnect_toggled(bool arg1)
{
    if (arg1 == true)
        ui->actionConnect->setText("Disconnect");
    else
        ui->actionConnect->setText("Connect");

    if (preventSocketAction)
        return;

    subwindow_t sw = winlist.value(activeWid);
    IConnection *c = sw.widget->getConnection();

    if (arg1 == true) {
        // connect
        c->tryConnect();

    }

    if (arg1 == false) {
        // disconnect
        c->closeConnection();

    }
}

void IdealIRC::connectionClosed()
{
    if (reconnect != NULL) {
        reconnect->tryConnect();
        reconnect = NULL;
        return;
    }

    if (connectionsRemaining == -1)
        return;

    if (! readyToClose)
        return;

    if (connectionsRemaining != -1)
        --connectionsRemaining;

    close(); // Attempt closing. Will be ignored if some connections are still left.
}

void IdealIRC::on_treeWidget_clicked(const QModelIndex&)
{
    subwindow_t sw = winlist.value(activeWid);
    sw.widget->setFocus();
}

void IdealIRC::Highlight(int wid, int type)
{
    if (! winlist.contains(wid))
        return;
    if (wid == activeWid)
        return;

    subwindow_t wt = winlist.value(wid);

    if (wt.highlight >= type)
        return;

    switch (type) {
        case HL_ACTIVITY:
            wt.treeitem->setForeground(0, QBrush(Qt::darkRed));
            break;

        case HL_HIGHLIGHT:
            wt.treeitem->setForeground(0, QBrush(Qt::blue));
            break;

        case HL_MSG:
            wt.treeitem->setForeground(0, QBrush(Qt::red));
            break;
    }

    wt.highlight = type;
    winlist.insert(wid, wt);

}

void IdealIRC::on_actionAbout_IdealIRC_triggered()
{
    IAbout *a = new IAbout(this);
    a->show();
}

void IdealIRC::versionReceived()
{
    if (vc.getInternVersion() != VERSION_INTEGER) {
        QString msg = tr("A new version is released (%1). It is recommended you upgrade!\r\nDownload via the website http://www.idealirc.org/")
                        .arg(vc.getVersion());

        QMessageBox::information(this, tr("New version!"), msg);
    }
}

void IdealIRC::configSaved()
{
    QFont f(conf.fontName);
    f.setPixelSize(conf.fontSize);

    QHashIterator<int,subwindow_t> i(winlist);
    while (i.hasNext()) {
        i.next();

        subwindow_t sw = i.value();
        sw.widget->setFont(f);
        sw.widget->reloadCSS();
    }

    ui->treeWidget->setFont(f);
    updateTreeViewColor();
}

void IdealIRC::favouritesJoinEnabler()
{
    IConnection *current = conlist.value(activeConn, NULL);

    if ((current == NULL) && (favourites != NULL))
        favourites->enableJoin(false);

    if ((current == NULL) || (favourites == NULL))
        return;

    if (current->isSocketOpen())
        favourites->enableJoin(true);
    else
        favourites->enableJoin(false);

    favourites->setConnection(current);
}

void IdealIRC::chanlistEnabler()
{
    IConnection *current = conlist.value(activeConn, NULL);

    if ((current == NULL) && (chanlist != NULL))
        favourites->enableJoin(false);

    if ((current == NULL) || (chanlist == NULL))
        return;

    if (current->isSocketOpen())
        chanlist->enable();
    else
        chanlist->disable();

    chanlist->setConnection(current);
}

void IdealIRC::updateTreeViewColor()
{
    QPalette treePal = ui->treeWidget->palette();
    treePal.setColor(QPalette::Active, QPalette::Base, conf.colWindowlistBackground);
    treePal.setColor(QPalette::Active, QPalette::AlternateBase, conf.colWindowlistBackground);
    treePal.setColor(QPalette::Inactive, QPalette::Base, conf.colWindowlistBackground);
    treePal.setColor(QPalette::Inactive, QPalette::AlternateBase, conf.colWindowlistBackground);
    treePal.setColor(QPalette::Disabled, QPalette::Base, conf.colWindowlistBackground);
    treePal.setColor(QPalette::Disabled, QPalette::AlternateBase, conf.colWindowlistBackground);
    treePal.setColor(QPalette::Active, QPalette::Text, conf.colWindowlist);
    treePal.setColor(QPalette::Active, QPalette::WindowText, conf.colWindowlist);
    treePal.setColor(QPalette::Inactive, QPalette::Text, conf.colWindowlist);
    treePal.setColor(QPalette::Inactive, QPalette::WindowText, conf.colWindowlist);
    treePal.setColor(QPalette::Disabled, QPalette::Text, conf.colWindowlist);
    treePal.setColor(QPalette::Disabled, QPalette::WindowText, conf.colWindowlist);
    ui->treeWidget->setPalette(treePal);

    QHashIterator<int,subwindow_t> i(winlist);
    while (i.hasNext()) {
        subwindow_t sw = i.next().value();
        if (sw.highlight != HL_NONE)
            continue;
        sw.treeitem->setForeground(0, QBrush(QColor(conf.colWindowlist)));
    }
}

void IdealIRC::on_actionChannel_favourites_triggered()
{
    recreateFavouritesDlg();
    favourites->show();
    favouritesJoinEnabler();
}

void IdealIRC::connectionEstablished()
{
    favouritesJoinEnabler();
    chanlistEnabler();
}

void IdealIRC::on_actionChannels_list_triggered()
{
    recreateChanlistDlg();
    chanlist->show();

    chanlistEnabler();
}

void IdealIRC::on_actionScript_Manager_triggered()
{
    recreateScriptManager();
    scriptManager->show();
}

void IdealIRC::onTrayActivated(QSystemTrayIcon::ActivationReason reason)
{
    /*
QSystemTrayIcon::Unknown	0	Unknown reason
QSystemTrayIcon::Context	1	The context menu for the system tray entry was requested
QSystemTrayIcon::DoubleClick	2	The system tray entry was double clicked
QSystemTrayIcon::Trigger	3	The system tray entry was clicked
QSystemTrayIcon::MiddleClick	4	The system tray entry was clicked with the middle mouse button
*/
    switch (reason) {
        case QSystemTrayIcon::Unknown:
            break;

        case QSystemTrayIcon::Context:
            break;

        case QSystemTrayIcon::DoubleClick:
            setVisible(!isVisible());
            break;

        case QSystemTrayIcon::Trigger:
            break;

        case QSystemTrayIcon::MiddleClick:
            break;

        default:
            break;
    }
}

void IdealIRC::trayMessage(QString title, QString message, QSystemTrayIcon::MessageIcon icon)
{
    if (conf.trayNotify && !windowIsActive)
        trayicon.showMessage(title, message, icon, conf.trayNotifyDelay);
}

void IdealIRC::applicationFocusChanged(QWidget *old, QWidget *now)
{
  if (old == 0 && isAncestorOf(now) == true)
    windowIsActive = true;
  else if (isAncestorOf(old) == true && now == 0)
    windowIsActive = false;
}
