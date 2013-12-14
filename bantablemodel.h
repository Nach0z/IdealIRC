/*
 *   IdealIRC - Internet Relay Chat client
 *   Copyright (C) 2013  Tom-Andre Barstad
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

#ifndef BANTABLEMODEL_H
#define BANTABLEMODEL_H

#include <QAbstractTableModel>
#include <QStringList>

// This class is used within IChanConfig for both banView and exceptionsView.

class BanTableModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    explicit BanTableModel(QObject *parent = 0);
    void setBanList(QStringList m, QStringList d, QStringList a);
    void addBan(QString m, QString d, QString a); // Used to add ban to list if someone sets it while this is open
    void delBan(QString m); // Used to delete ban to list if someone removes it while this is open

protected:
    int rowCount(const QModelIndex &parent = QModelIndex()) const ;
    int columnCount(const QModelIndex &parent = QModelIndex()) const;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const;

private:
    QStringList mask;
    QStringList date;
    QStringList author;

signals:
    void updatedItems();

};

#endif // BANTABLEMODEL_H
