/* This file is part of FSView.
   Copyright (C) 2002, 2003 Josef Weidendorfer <Josef.Weidendorfer@gmx.de>
   Some file management code taken from the Dolphin file manager (C) 2006-2009,
   by Peter Penz <peter.penz19@mail.com>

   KCachegrind is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public
   License as published by the Free Software Foundation, version 2.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; see the file COPYING.  If not, write to
   the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301, USA.
*/

/*
 * The KPart embedding the FSView widget
 */

#include "fsview_part.h"

#include <QClipboard>
#include <QTimer>
#include <QStyle>

#include <kfileitem.h>
#include <kpluginfactory.h>
#include <kaboutdata.h>

#include <kglobalsettings.h>
#include <kprotocolmanager.h>
#include <kio/copyjob.h>
#include <kio/deletejob.h>
#include <kio/paste.h>
#include <kmessagebox.h>
#include <kactionmenu.h>
#include <kactioncollection.h>
#include <kpropertiesdialog.h>
#include <KMimeTypeEditor>
#include <kio/jobuidelegate.h>
#include <KIO/FileUndoManager>
#include <KJobWidgets>
#include <ktoolinvocation.h>
#include <kconfiggroup.h>
#include <KDebug>

#include <KLocalizedString>

#include <QApplication>
#include <QMimeData>

K_PLUGIN_FACTORY(FSViewPartFactory, registerPlugin<FSViewPart>();)
K_EXPORT_PLUGIN(FSViewPartFactory(KAboutData(
                                      "fsview",
                                      0,
                                      ki18n("FSView"),
                                      "0.1.1",
                                      ki18n("Filesystem Utilization Viewer"),
                                      KAboutData::License_GPL,
                                      ki18n("(c) 2003-2005, Josef Weidendorfer"))))

// FSJob, for progress

FSJob::FSJob(FSView *v)
    : KIO::Job()
{
    _view = v;
    QObject::connect(v, SIGNAL(progress(int,int,QString)),
                     this, SLOT(progressSlot(int,int,QString)));
}

void FSJob::kill(bool /*quietly*/)
{
    _view->stop();

    Job::kill();
}

void FSJob::progressSlot(int percent, int dirs, const QString &cDir)
{
    if (percent < 100) {
        emitPercent(percent, 100);
        slotInfoMessage(this, i18np("Read 1 folder, in %2",
                                    "Read %1 folders, in %2",
                                    dirs, cDir), QString());
    } else {
        slotInfoMessage(this, i18np("1 folder", "%1 folders", dirs), QString());
    }
}

// FSViewPart

FSViewPart::FSViewPart(QWidget *parentWidget,
                       QObject *parent,
                       const QList<QVariant> & /* args */)
    : KParts::ReadOnlyPart(parent)
{
    KAboutData aboutData(QStringLiteral("fsview"), i18n("FSView"), QStringLiteral("0.1"),
                         i18n("Filesystem Viewer"),
                         KAboutLicense::GPL,
                         i18n("(c) 2002, Josef Weidendorfer"));
    setComponentData(aboutData);

    _view = new FSView(new Inode(), parentWidget);
    _view->setWhatsThis(i18n("<p>This is the FSView plugin, a graphical "
                             "browsing mode showing filesystem utilization "
                             "by using a tree map visualization.</p>"
                             "<p>Note that in this mode, automatic updating "
                             "when filesystem changes are made "
                             "is intentionally <b>not</b> done.</p>"
                             "<p>For details on usage and options available, "
                             "see the online help under "
                             "menu 'Help/FSView Manual'.</p>"));

    _view->show();
    setWidget(_view);

    _ext = new FSViewBrowserExtension(this);
    _job = 0;

    _areaMenu = new KActionMenu(i18n("Stop at Area"),
                                actionCollection());
    actionCollection()->addAction(QStringLiteral("treemap_areadir"), _areaMenu);
    _depthMenu = new KActionMenu(i18n("Stop at Depth"),
                                 actionCollection());
    actionCollection()->addAction(QStringLiteral("treemap_depthdir"), _depthMenu);
    _visMenu = new KActionMenu(i18n("Visualization"),
                               actionCollection());
    actionCollection()->addAction(QStringLiteral("treemap_visdir"), _visMenu);

    _colorMenu = new KActionMenu(i18n("Color Mode"),
                                 actionCollection());
    actionCollection()->addAction(QStringLiteral("treemap_colordir"), _colorMenu);

    QAction *action;
    action = actionCollection()->addAction(QStringLiteral("help_fsview"));
    action->setText(i18n("&FSView Manual"));
    action->setIcon(QIcon::fromTheme(QStringLiteral("fsview")));
    action->setToolTip(i18n("Show FSView manual"));
    action->setWhatsThis(i18n("Opens the help browser with the "
                              "FSView documentation"));
    connect(action, SIGNAL(triggered()), this, SLOT(showHelp()));

    QObject::connect(_visMenu->menu(), SIGNAL(aboutToShow()),
                     SLOT(slotShowVisMenu()));
    QObject::connect(_areaMenu->menu(), SIGNAL(aboutToShow()),
                     SLOT(slotShowAreaMenu()));
    QObject::connect(_depthMenu->menu(), SIGNAL(aboutToShow()),
                     SLOT(slotShowDepthMenu()));
    QObject::connect(_colorMenu->menu(), SIGNAL(aboutToShow()),
                     SLOT(slotShowColorMenu()));

    slotSettingsChanged(KGlobalSettings::SETTINGS_MOUSE);
    connect(KGlobalSettings::self(), SIGNAL(settingsChanged(int)),
            SLOT(slotSettingsChanged(int)));

    QObject::connect(_view, SIGNAL(returnPressed(TreeMapItem*)),
                     _ext, SLOT(selected(TreeMapItem*)));
    QObject::connect(_view, SIGNAL(selectionChanged()),
                     this, SLOT(updateActions()));
    QObject::connect(_view,
                     SIGNAL(contextMenuRequested(TreeMapItem*,QPoint)),
                     this,
                     SLOT(contextMenu(TreeMapItem*,QPoint)));

    QObject::connect(_view, SIGNAL(started()), this, SLOT(startedSlot()));
    QObject::connect(_view, SIGNAL(completed(int)),
                     this, SLOT(completedSlot(int)));

    // Create common file management actions - this is necessary in KDE4
    // as these common actions are no longer automatically part of KParts.
    // Much of this is taken from Dolphin.
    // FIXME: Renaming didn't even seem to work in KDE3! Implement (non-inline) renaming
    // functionality.
    //QAction* renameAction = m_actionCollection->addAction("rename");
    //rename->setText(i18nc("@action:inmenu Edit", "Rename..."));
    //rename->setShortcut(Qt::Key_F2);

    QAction *moveToTrashAction = actionCollection()->addAction(QStringLiteral("move_to_trash"));
    moveToTrashAction->setText(i18nc("@action:inmenu File", "Move to Trash"));
    moveToTrashAction->setIcon(QIcon::fromTheme(QStringLiteral("user-trash")));
    actionCollection()->setDefaultShortcut(moveToTrashAction, QKeySequence(QKeySequence::Delete));
    connect(moveToTrashAction, SIGNAL(triggered(Qt::MouseButtons,Qt::KeyboardModifiers)),
            _ext, SLOT(trash(Qt::MouseButtons,Qt::KeyboardModifiers)));

    QAction *deleteAction = actionCollection()->addAction(QStringLiteral("delete"));
    deleteAction->setIcon(QIcon::fromTheme(QStringLiteral("edit-delete")));
    deleteAction->setText(i18nc("@action:inmenu File", "Delete"));
    actionCollection()->setDefaultShortcut(deleteAction, QKeySequence(Qt::SHIFT | Qt::Key_Delete));
    connect(deleteAction, SIGNAL(triggered()), _ext, SLOT(del()));

    QAction *editMimeTypeAction = actionCollection()->addAction(QStringLiteral("editMimeType"));
    editMimeTypeAction->setText(i18nc("@action:inmenu Edit", "&Edit File Type..."));
    connect(editMimeTypeAction, SIGNAL(triggered()), _ext, SLOT(editMimeType()));

    QAction *propertiesAction = actionCollection()->addAction(QStringLiteral("properties"));
    propertiesAction->setText(i18nc("@action:inmenu File", "Properties"));
    propertiesAction->setIcon(QIcon::fromTheme(QStringLiteral("document-properties")));
    propertiesAction->setShortcut(Qt::ALT | Qt::Key_Return);
    connect(propertiesAction, SIGNAL(triggered()), SLOT(slotProperties()));

    QTimer::singleShot(1, this, SLOT(showInfo()));

    updateActions();

    setXMLFile(QStringLiteral("fsview_part.rc"));
}

FSViewPart::~FSViewPart()
{
    kDebug(90100) << "FSViewPart Destructor";

    delete _job;
    _view->saveFSOptions();
}

void FSViewPart::slotSettingsChanged(int category)
{
    if (category != KGlobalSettings::SETTINGS_MOUSE) {
        return;
    }

    QObject::disconnect(_view, SIGNAL(clicked(TreeMapItem*)),
                        _ext, SLOT(selected(TreeMapItem*)));
    QObject::disconnect(_view, SIGNAL(doubleClicked(TreeMapItem*)),
                        _ext, SLOT(selected(TreeMapItem*)));

    if (_view->style()->styleHint(QStyle::SH_ItemView_ActivateItemOnSingleClick))
        QObject::connect(_view, SIGNAL(clicked(TreeMapItem*)),
                         _ext, SLOT(selected(TreeMapItem*)));
    else
        QObject::connect(_view, SIGNAL(doubleClicked(TreeMapItem*)),
                         _ext, SLOT(selected(TreeMapItem*)));
}

void FSViewPart::showInfo()
{
    QString info;
    info = i18n("FSView intentionally does not support automatic updates "
                "when changes are made to files or directories, "
                "currently visible in FSView, from the outside.\n"
                "For details, see the 'Help/FSView Manual'.");

    KMessageBox::information(_view, info, QString(), QStringLiteral("ShowFSViewInfo"));
}

void FSViewPart::showHelp()
{
    KToolInvocation::startServiceByDesktopName(QStringLiteral("khelpcenter"),
            QStringLiteral("help:/konqueror/index.html#fsview"));
}

void FSViewPart::startedSlot()
{
    _job = new FSJob(_view);
    _job->setUiDelegate(new KIO::JobUiDelegate());
    emit started(_job);
}

void FSViewPart::completedSlot(int dirs)
{
    if (_job) {
        _job->progressSlot(100, dirs, QString());
        delete _job;
        _job = 0;
    }

    KConfigGroup cconfig(_view->config(), "MetricCache");
    _view->saveMetric(&cconfig);

    emit completed();
}

void FSViewPart::slotShowVisMenu()
{
    _visMenu->menu()->clear();
    _view->addVisualizationItems(_visMenu->menu(), 1301);
}

void FSViewPart::slotShowAreaMenu()
{
    _areaMenu->menu()->clear();
    _view->addAreaStopItems(_areaMenu->menu(), 1001, 0);
}

void FSViewPart::slotShowDepthMenu()
{
    _depthMenu->menu()->clear();
    _view->addDepthStopItems(_depthMenu->menu(), 1501, 0);
}

void FSViewPart::slotShowColorMenu()
{
    _colorMenu->menu()->clear();
    _view->addColorItems(_colorMenu->menu(), 1401);
}

bool FSViewPart::openFile() // never called since openUrl is reimplemented
{
    kDebug(90100) << "FSViewPart::openFile " << localFilePath();
    _view->setPath(localFilePath());

    return true;
}

bool FSViewPart::openUrl(const QUrl &url)
{
    kDebug(90100) << "FSViewPart::openUrl " << url.path();

    if (!url.isValid()) {
        return false;
    }
    if (!url.isLocalFile()) {
        return false;
    }

    setUrl(url);
    emit setWindowCaption(this->url().toDisplayString(QUrl::PreferLocalFile));

    _view->setPath(this->url().path());

    return true;
}

bool FSViewPart::closeUrl()
{
    kDebug(90100) << "FSViewPart::closeUrl ";

    _view->stop();

    return true;
}

void FSViewPart::setNonStandardActionEnabled(const char *actionName, bool enabled)
{
    QAction *action = actionCollection()->action(actionName);
    action->setEnabled(enabled);
}

void FSViewPart::updateActions()
{
    int canDel = 0, canCopy = 0, canMove = 0;
    QList<QUrl> urls;

    foreach (TreeMapItem *i, _view->selection()) {
        QUrl u = QUrl::fromLocalFile(((Inode *)i)->path());
        urls.append(u);
        canCopy++;
        if (KProtocolManager::supportsDeleting(u)) {
            canDel++;
        }
        if (KProtocolManager::supportsMoving(u)) {
            canMove++;
        }
    }

    // Standard KBrowserExtension actions.
    emit _ext->enableAction("copy", canCopy > 0);
    emit _ext->enableAction("cut", canMove > 0);
    // Custom actions.
    //setNonStandardActionEnabled("rename", canMove > 0 ); // FIXME
    setNonStandardActionEnabled("move_to_trash", (canDel > 0 && canMove > 0));
    setNonStandardActionEnabled("delete", canDel > 0);
    setNonStandardActionEnabled("editMimeType", _view->selection().count() == 1);
    setNonStandardActionEnabled("properties", _view->selection().count() == 1);

    emit _ext->selectionInfo(urls);

    if (canCopy > 0) {
        stateChanged(QStringLiteral("has_selection"));
    } else {
        stateChanged(QStringLiteral("has_no_selection"));
    }

    kDebug(90100) << "FSViewPart::updateActions, deletable " << canDel;
}

void FSViewPart::contextMenu(TreeMapItem * /*item*/, const QPoint &p)
{
    int canDel = 0, canCopy = 0, canMove = 0;
    KFileItemList items;

    foreach (TreeMapItem *i, _view->selection()) {
        QUrl u = QUrl::fromLocalFile(((Inode *)i)->path());
        QString mimetype = ((Inode *)i)->mimeType().name();
        const QFileInfo &info = ((Inode *)i)->fileInfo();
        mode_t mode =
            info.isFile() ? S_IFREG :
            info.isDir() ? S_IFDIR :
            info.isSymLink() ? S_IFLNK : (mode_t) - 1;
        items.append(KFileItem(u, mimetype, mode));

        canCopy++;
        if (KProtocolManager::supportsDeleting(u)) {
            canDel++;
        }
        if (KProtocolManager::supportsMoving(u)) {
            canMove++;
        }
    }

    QList<QAction *> editActions;
    KParts::BrowserExtension::ActionGroupMap actionGroups;
    KParts::BrowserExtension::PopupFlags flags = KParts::BrowserExtension::ShowUrlOperations |
            KParts::BrowserExtension::ShowProperties;

    bool addTrash = (canMove > 0);
    bool addDel = false;
    if (canDel == 0) {
        flags |= KParts::BrowserExtension::NoDeletion;
    } else {
        if (!url().isLocalFile()) {
            addDel = true;
        } else if (QApplication::keyboardModifiers() & Qt::ShiftModifier) {
            addTrash = false;
            addDel = true;
        } else {
            KSharedConfig::Ptr globalConfig = KSharedConfig::openConfig(QStringLiteral("kdeglobals"), KConfig::IncludeGlobals);
            KConfigGroup configGroup(globalConfig, "KDE");
            addDel = configGroup.readEntry("ShowDeleteCommand", false);
        }
    }

    if (addTrash) {
        editActions.append(actionCollection()->action(QStringLiteral("move_to_trash")));
    }
    if (addDel) {
        editActions.append(actionCollection()->action(QStringLiteral("delete")));
    }

// FIXME: rename is currently unavailable. Requires popup renaming.
//     if (canMove)
//       editActions.append(actionCollection()->action("rename"));
    actionGroups.insert(QStringLiteral("editactions"), editActions);

    if (items.count() > 0)
        emit _ext->popupMenu(_view->mapToGlobal(p), items,
                             KParts::OpenUrlArguments(),
                             KParts::BrowserArguments(),
                             flags,
                             actionGroups);
}

void FSViewPart::slotProperties()
{
    QList<QUrl> urlList;
    if (view()) {
        urlList = view()->selectedUrls();
    }

    if (!urlList.isEmpty()) {
        KPropertiesDialog::showDialog(urlList.first(), view());
    }
}

// FSViewBrowserExtension

FSViewBrowserExtension::FSViewBrowserExtension(FSViewPart *viewPart)
    : KParts::BrowserExtension(viewPart)
{
    _view = viewPart->view();
}

FSViewBrowserExtension::~FSViewBrowserExtension()
{}

void FSViewBrowserExtension::del()
{
    const QList<QUrl> urls = _view->selectedUrls();
    KIO::JobUiDelegate uiDelegate;
    uiDelegate.setWindow(_view);
    if (uiDelegate.askDeleteConfirmation(urls,
                                         KIO::JobUiDelegate::Delete, KIO::JobUiDelegate::DefaultConfirmation)) {
        KIO::Job *job = KIO::del(urls);
        KJobWidgets::setWindow(job, _view);
        job->ui()->setAutoErrorHandlingEnabled(true);
        connect(job, SIGNAL(result(KJob*)),
                this, SLOT(refresh()));
    }
}

void FSViewBrowserExtension::trash(Qt::MouseButtons, Qt::KeyboardModifiers modifiers)
{
    bool deleteNotTrash = ((modifiers & Qt::ShiftModifier) != 0);
    if (deleteNotTrash) {
        del();
    } else {
        KIO::JobUiDelegate uiDelegate;
        uiDelegate.setWindow(_view);
        const QList<QUrl> urls = _view->selectedUrls();
        if (uiDelegate.askDeleteConfirmation(urls,
                                             KIO::JobUiDelegate::Trash, KIO::JobUiDelegate::DefaultConfirmation)) {
            KIO::Job *job = KIO::trash(urls);
            KIO::FileUndoManager::self()->recordJob(KIO::FileUndoManager::Trash, urls, QUrl("trash:/"), job);
            KJobWidgets::setWindow(job, _view);
            job->ui()->setAutoErrorHandlingEnabled(true);
            connect(job, SIGNAL(result(KJob*)),
                    this, SLOT(refresh()));
        }
    }
}

void FSViewBrowserExtension::copySelection(bool move)
{
    QMimeData *data = new QMimeData;
    data->setUrls(_view->selectedUrls());
    KIO::setClipboardDataCut(data, move);
    QApplication::clipboard()->setMimeData(data);
}

void FSViewBrowserExtension::editMimeType()
{
    Inode *i = (Inode *) _view->selection().first();
    if (i) {
        KMimeTypeEditor::editMimeType(i->mimeType().name(), _view);
    }
}

// refresh treemap at end of KIO jobs
void FSViewBrowserExtension::refresh()
{
    // only need to refresh common ancestor for all selected items
    TreeMapItem *commonParent = _view->selection().commonParent();
    if (!commonParent) {
        return;
    }

    /* if commonParent is a file, update parent directory */
    if (!((Inode *)commonParent)->isDir()) {
        commonParent = commonParent->parent();
        if (!commonParent) {
            return;
        }
    }

    kDebug(90100) << "FSViewPart::refreshing "
                  << ((Inode *)commonParent)->path() << endl;

    _view->requestUpdate((Inode *)commonParent);
}

void FSViewBrowserExtension::selected(TreeMapItem *i)
{
    if (!i) {
        return;
    }

    QUrl url = QUrl::fromLocalFile(((Inode *)i)->path());
    emit openUrlRequest(url);
}

#include "fsview_part.moc"
