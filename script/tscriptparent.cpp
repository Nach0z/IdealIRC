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

#include "tscriptparent.h"
#include "iconnection.h"
#include "iwin.h"
#include <iostream>
#include <QStringList>
#include <QUrl>
#include <QDesktopServices>
#include <QApplication>

TScriptParent::TScriptParent(QObject *parent, QWidget *dialogParent, config *cfg,
                             QHash<int,IConnection*> *cl, QHash<int,subwindow_t> *wl,
                             int *aWid, int *aConn) :
    QObject(parent),
    conf(cfg),
    cmdhndl(parent, cl, wl, aConn, aWid),
    activeWid(aWid),
    activeConn(aConn),
    conlist(cl),
    winlist(wl),
    dlgParent(dialogParent),
    displayURL(false)
{

    connect(&cmdhndl, SIGNAL(RequestWindow(QString,int,int,bool)),
            this, SIGNAL(RequestWindow(QString,int,int,bool)));

}



bool TScriptParent::loadScript(QString path, bool starting)
{
  std::cout << "Loading '" << path.toStdString().c_str() << "'" << std::endl;

  TScript *s = new TScript(this, dlgParent, path);
  connect(s,    SIGNAL(error(QString)),
          this, SLOT(gotScriptError(QString)));

  connect(s,    SIGNAL(warning(QString)),
          this, SLOT(gotScriptWarning(QString)));

  connect(s,    SIGNAL(echo(QString)),
          this, SLOT(echo(QString)));

  connect(s,    SIGNAL(echo(QString,QString)),
          this, SLOT(echo(QString,QString)));

  connect(s,    SIGNAL(stopURLDisplay()),
          this, SLOT(stopURLDisplay()));


  if (! loader(s)) {
    gotScriptError("Unable to load script '"+path+"'.");
    disconnect(s,    SIGNAL(error(QString)),
               this, SLOT(gotScriptError(QString)));
    return false;
  }

  connect(s,    SIGNAL(execCmdSignal(QString)),
          this, SLOT(execCmdSlot(QString)));

  connect(s, SIGNAL(toolbarAdd(QString,QString,QString)),
          this, SLOT(toolbarAdd(QString,QString,QString)));

  connect(s, SIGNAL(toolbarDel(QString)),
          this, SLOT(toolbarDel(QString)));

  connect(s, SIGNAL(toolbarSetIcon(QString,QString)),
          this, SLOT(toolbarSetIcon(QString,QString)));

  connect(s, SIGNAL(toolbarSetFunction(QString,QString,QString)),
          this, SLOT(toolbarSetFunction(QString,QString,QString)));


  scriptlist.push_back(s);

  QStringList para;
  if (starting)
      para.push_back("1"); // 1 -> IIRC starting
  else
      para.push_back("0"); // 0 -> Script loading
  s->runEvent(te_load, para);

  return true;
}

bool TScriptParent::unloadScript(QString name)
{
  QVectorIterator<TScript*> i(scriptlist);
  int c = -1;
  TScript* script = NULL;

  while (i.hasNext()) {
    script = i.next();
    ++c;

    if (script->getName() == name)
      break;

  }

  if (c == -1)
    return false;

  if (script != NULL) {
      QStringList para;
    script->runEvent(te_unload, para);
  }

  scriptlist.remove(c);
  return true;
}

bool TScriptParent::reloadScript(QString name)
{
  QVectorIterator<TScript*> i(scriptlist);
  int c = -1;
  TScript *script = NULL;
  while (i.hasNext()) {
    script = i.next();
    ++c;

    if (script->getName() == name)
      break;

  }

  if (c == -1)
    return false;
/*
  bool ok = script->loadScript(); // load is also reload.
  if (! ok) {
    gotScriptError("Unable to reload script '"+name+"'.");
    disconnect(script,    SIGNAL(error(QString)),
               this, SLOT(gotScriptError(QString)));
  }
  else {
   // echo("Script reloaded: " + name, PT_LOCALINFO);
    QStringList para;
    para.push_back("1"); // 1 -> reload script
    script->runEvent(te_load, para);
  }
  */

  return loader(script);
}

bool TScriptParent::runevent(e_iircevent event, QStringList param)
{
  bool found = false;
  displayURL = true;

  for (int i = 0; i <= scriptlist.count()-1; i++) {
    TScript *s = scriptlist.at(i);
    found = s->runEvent(event, param);
    // 'found' is true if event is actually run (function assigned to it)
  }

  // URL clicking requires special treatment if we got events on it.
  if ((event == te_urlclick) && (found == true) && (displayURL == true)) {
    QUrl url = QString(param.at(0));
    bool b = QDesktopServices::openUrl(url);
    if (! b)
      echo("$ACTIVE$", "Unable to open URL.", PT_LOCALINFO);
  }

  return found;
}

bool TScriptParent::runevent(e_iircevent event)
{
  QStringList empty;
  return runevent(event, empty);
}

bool TScriptParent::command(QString cmd)
{
  for (int i = 0; i <= scriptlist.count()-1; i++) {
    TScript *s = scriptlist.at(i);
    if (s->runCommand(cmd) == true)
      return true;
  }
  return false;
}

void TScriptParent::execCmdSlot(QString cmd)
{
  // this slot runs when a command from a script is executed.
  // this slot also runs when a command is ran from an input box.

  // 1. see if it's an internal command
  if (cmdhndl.parse(cmd))
      return; // command found here

  // 2. see if the command is in another script.
  if (command(cmd))
      return; // command found here.

  // 3. push to server for last resort; only if connected.
  if (cmd.left(1) == "/")
    cmd = cmd.mid(2);
  cmdhndl.sockwrite(cmd);
}

void TScriptParent::getLoadedScripts(QHash<QString,QString> &list)
{
  QVectorIterator<TScript*> i(scriptlist);
  list.clear(); // make sure we get it empty.

  while (i.hasNext()) {
    TScript *s = i.next();
    list.insert(s->getName(), s->getPath());
  }
}

void TScriptParent::loadAllScripts()
{
  IniFile ini(CONF_FILE);

  int c = ini.CountItems("Script");

  for (int i = 1; i <= c; ++i)
    loadScript(ini.ReadIni("Script", i), true);
}

void TScriptParent::saveLoadedScripts()
{
  IniFile ini(CONF_FILE);
  ini.DelSection("Script");

  QVectorIterator<TScript*> i(scriptlist);
  while (i.hasNext()) {
    TScript *s = i.next();
    ini.WriteIni("Script", s->getName(), s->getPath());
  }
}

bool TScriptParent::loader(TScript *script)
{
  // Handle errors from loadeing here, this is used both on load and reload.
  // Note: Line numbers are incorrect because the scripts truncate blank lines and comments.
    // Solution: Map actual line numbers with internal numbers?

  e_scriptresult sr = script->loadScript2();
  bool ok = false;
  switch (sr) {
    case se_FileCannotOpen:
      gotScriptError("Cannot open file " + script->getPath());
      break;
    case se_FileEmpty:
      gotScriptError("File " + script->getPath() + " is empty");
      break;
    case se_UnexpectedToken:
      gotScriptError("Unexpected token at '" + script->getErrorKeyword() + "', line " + QString::number( script->getCurrentLine() ) );
      break;
    case se_UnexpectedNewline:
      gotScriptError("Unexpected line break at '" + script->getErrorKeyword() + "', line " + QString::number( script->getCurrentLine() ) );
      break;
    case se_UnexpectedFinish:
      gotScriptError("Unexpected finish at '" + script->getErrorKeyword() + "', line " + QString::number( script->getCurrentLine() ) );
      break;
    case se_InvalidMetaCommand:
      gotScriptError("Invalid meta command '" + script->getErrorKeyword() + "', line " + QString::number( script->getCurrentLine() ) );
      break;
    case se_InvalidEvent:
      gotScriptError("Invalid event on line " + QString::number( script->getCurrentLine() ) );
      break;
    case se_InvalidBlockType:
      gotScriptError("Invalid block type at line " + QString::number( script->getCurrentLine() ) );
      break;
    case se_InvalidParamCount:
      gotScriptError("Invalid parameter count at line " + QString::number( script->getCurrentLine() ) );
      break;
    case se_InvalidFileDescriptor:
      gotScriptError("Invalid file descriptor at line " + QString::number( script->getCurrentLine() ) );
      break;


    case se_None:
      ok = true;
      break;
    default:
      ok = false;
      gotScriptWarning("Script loader returned abnormally, code " + QString::number(sr));
      break;
  }

  if (! ok)
    gotScriptError("Unable to load script " + script->getPath());

  return ok;
}

void TScriptParent::toolbarAdd(QString toolname, QString scriptname, QString tooltip)
{
  toolbar_t tool;

  if (toolbar.count(toolname.toUpper()) > 0)
    tool = toolbar.value(toolname.toUpper());

  tool.scriptname = scriptname;
  tool.tooltip = tooltip;

  toolbar.insert(toolname.toUpper(), tool);
  emit refreshToolbar();
}

void TScriptParent::toolbarDel(QString toolname)
{
  if (toolbar.count(toolname.toUpper()) == 0) {
    gotScriptWarning("Toolbar button '" + toolname + "' does not exist.");
    return;
  }

  toolbar.remove(toolname.toUpper());
  emit refreshToolbar();
}

void TScriptParent::toolbarSetIcon(QString toolname, QString path)
{
  if (toolbar.count(toolname.toUpper()) == 0) {
    gotScriptWarning("Toolbar button '" + toolname + "' does not exist.");
    return;
  }

  toolbar_t tool;
  tool = toolbar.value(toolname.toUpper());

  tool.iconpath = path;

  toolbar.insert(toolname.toUpper(), tool);
  emit refreshToolbar();
}

void TScriptParent::toolbarSetFunction(QString toolname, QString scriptname, QString function)
{
  if (toolbar.count(toolname.toUpper()) == 0) {
    gotScriptWarning("Toolbar button '" + toolname + "' does not exist.");
    return;
  }

  toolbar_t tool;
  tool = toolbar.value(toolname.toUpper());

  tool.function = function;
  tool.scriptname = scriptname;

  toolbar.insert(toolname.toUpper(), tool);
}

void TScriptParent::runScriptFunction(QString script, QString function)
{
  QVectorIterator<TScript*> i(scriptlist);

  while (i.hasNext()) {
    TScript *s = i.next();

    if (s->getName().toUpper() == script.toUpper()) {
      QStringList param;
      QString res;
      s->runf(function, param, res, true);
      break;
    }
  }
}