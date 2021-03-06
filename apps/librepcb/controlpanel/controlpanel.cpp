/*
 * LibrePCB - Professional EDA for everyone!
 * Copyright (C) 2013 LibrePCB Developers, see AUTHORS.md for contributors.
 * http://librepcb.org/
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/*****************************************************************************************
 *  Includes
 ****************************************************************************************/
#include <QtCore>
#include <QtWidgets>
#include <QFileDialog>
#include "controlpanel.h"
#include "ui_controlpanel.h"
#include <librepcb/workspace/workspace.h>
#include <librepcb/workspace/settings/workspacesettings.h>
#include <librepcb/project/project.h>
#include <librepcb/workspace/projecttreemodel.h>
#include <librepcb/workspace/projecttreeitem.h>
#include <librepcb/workspace/library/workspacelibrarydb.h>
#include <librepcb/librarymanager/librarymanager.h>
#include <librepcb/library/library.h>
#include <librepcb/libraryeditor/libraryeditor.h>
#include <librepcb/projecteditor/projecteditor.h>
#include <librepcb/projecteditor/newprojectwizard/newprojectwizard.h>
#include <librepcb/common/application.h>
#include "../markdown/markdownconverter.h"

/*****************************************************************************************
 *  Namespace
 ****************************************************************************************/
namespace librepcb {
namespace application {

using namespace project;
using namespace project::editor;
using namespace library::manager;
using namespace workspace;

/*****************************************************************************************
 *  Constructors / Destructor
 ****************************************************************************************/

ControlPanel::ControlPanel(Workspace& workspace) :
    QMainWindow(nullptr), mWorkspace(workspace), mUi(new Ui::ControlPanel),
    mLibraryManager(new LibraryManager(mWorkspace, this))
{
    mUi->setupUi(this);

    setWindowTitle(QString(tr("Control Panel - LibrePCB %1"))
                   .arg(qApp->getAppVersion().toPrettyStr(2)));

    // show workspace path in status bar
    QString wsPath = mWorkspace.getPath().toNative();
    QLabel* statusBarLabel = new QLabel(QString(tr("Workspace: %1")).arg(wsPath));
    mUi->statusBar->addWidget(statusBarLabel, 1);

    // initialize status bar
    mUi->statusBar->setFields(StatusBar::ProgressBar);
    mUi->statusBar->setProgressBarTextFormat(tr("Scanning libraries (%p%)"));
    connect(&mWorkspace.getLibraryDb(), &WorkspaceLibraryDb::scanStarted,
            mUi->statusBar, &StatusBar::showProgressBar, Qt::QueuedConnection);
    connect(&mWorkspace.getLibraryDb(), &WorkspaceLibraryDb::scanSucceeded,
            mUi->statusBar, &StatusBar::hideProgressBar, Qt::QueuedConnection);
    connect(&mWorkspace.getLibraryDb(), &WorkspaceLibraryDb::scanProgressUpdate,
            mUi->statusBar, &StatusBar::setProgressBarPercent, Qt::QueuedConnection);

    // decive if we have to show the warning about a newer workspace file format version
    Version actualVersion = qApp->getFileFormatVersion();
    Version highestVersion = Workspace::getHighestFileFormatVersionOfWorkspace(workspace.getPath());
    mUi->lblWarnForNewerAppVersions->setVisible(highestVersion > actualVersion);

    // decide if we have to show the warning about missing workspace libraries
    if (mWorkspace.getLocalLibraries().isEmpty() && mWorkspace.getRemoteLibraries().isEmpty()) {
        mUi->lblWarnForNoLibraries->setVisible(true);
        connect(&mWorkspace, &Workspace::libraryAdded,
                mUi->lblWarnForNoLibraries, &QLabel::hide);
    } else {
        mUi->lblWarnForNoLibraries->setVisible(false);
    }

    // connect some actions which are created with the Qt Designer
    connect(mUi->actionQuit, &QAction::triggered,
            this, &ControlPanel::close);
    //connect(mUi->actionOpen_Library_Editor, &QAction::triggered,
    //        &Workspace::instance(), &Workspace::openLibraryEditor);
    connect(mUi->actionAbout_Qt, &QAction::triggered,
            qApp, &QApplication::aboutQt);
    connect(mUi->actionWorkspace_Settings, &QAction::triggered,
            &mWorkspace.getSettings(), &WorkspaceSettings::showSettingsDialog);
    connect(mLibraryManager.data(), &LibraryManager::openLibraryEditorTriggered,
            this, &ControlPanel::openLibraryEditor);

    mUi->projectTreeView->setModel(&mWorkspace.getProjectTreeModel());
    mUi->recentProjectsListView->setModel(&mWorkspace.getRecentProjectsModel());
    mUi->favoriteProjectsListView->setModel(&mWorkspace.getFavoriteProjectsModel());

    loadSettings();

    // parse command line arguments and open all project files
    foreach (const QString& arg, qApp->arguments())
    {
        FilePath filepath(arg);
        if ((filepath.isExistingFile()) && (filepath.getSuffix() == "lpp"))
            openProject(filepath);
    }

    // start scanning the workspace library (asynchronously)
    mWorkspace.getLibraryDb().startLibraryRescan();
}

ControlPanel::~ControlPanel()
{
    closeAllProjects(false);
    closeAllLibraryEditors(false);
    mLibraryManager.reset();
    mUi.reset();
}

void ControlPanel::closeEvent(QCloseEvent *event)
{
    // close all projects, unsaved projects will ask for saving
    if (!closeAllProjects(true)) {
        event->ignore();
        return; // do NOT close the application, there are still open projects!
    }

    // close all library editors, unsaved libraries will ask for saving
    if (!closeAllLibraryEditors(true)) {
        event->ignore();
        return; // do NOT close the application, there are still open library editors!
    }

    saveSettings();

    QMainWindow::closeEvent(event);

    // if the control panel is closed, we will quit the whole application
    QApplication::quit();
}

void ControlPanel::showControlPanel() noexcept
{
    show();
    raise();
    activateWindow();
}

/*****************************************************************************************
 *  General private methods
 ****************************************************************************************/

void ControlPanel::saveSettings()
{
    QSettings clientSettings;
    clientSettings.beginGroup("controlpanel");

    // main window
    clientSettings.setValue("window_geometry", saveGeometry());
    clientSettings.setValue("window_state", saveState());
    clientSettings.setValue("splitter_h_state", mUi->splitter_h->saveState());
    clientSettings.setValue("splitter_v_state", mUi->splitter_v->saveState());

    // projects treeview (expanded items)
    ProjectTreeModel* model = dynamic_cast<ProjectTreeModel*>(mUi->projectTreeView->model());
    if (model)
    {
        QStringList list;
        foreach (QModelIndex index, model->getPersistentIndexList())
        {
            if (mUi->projectTreeView->isExpanded(index))
            {
                list.append(FilePath(index.data(Qt::UserRole).toString())
                            .toRelative(mWorkspace.getPath()));
            }
        }
        clientSettings.setValue("expanded_projecttreeview_items", QVariant::fromValue(list));
    }

    clientSettings.endGroup();
}

void ControlPanel::loadSettings()
{
    QSettings clientSettings;
    clientSettings.beginGroup("controlpanel");

    // main window
    restoreGeometry(clientSettings.value("window_geometry").toByteArray());
    restoreState(clientSettings.value("window_state").toByteArray());
    mUi->splitter_h->restoreState(clientSettings.value("splitter_h_state").toByteArray());
    mUi->splitter_v->restoreState(clientSettings.value("splitter_v_state").toByteArray());

    // projects treeview (expanded items)
    ProjectTreeModel* model = dynamic_cast<ProjectTreeModel*>(mUi->projectTreeView->model());
    if (model)
    {
        QStringList list = clientSettings.value("expanded_projecttreeview_items").toStringList();
        foreach (QString item, list)
        {
            FilePath filepath = FilePath::fromRelative(mWorkspace.getPath(), item);
            QModelIndexList items = model->match(model->index(0, 0), Qt::UserRole,
                QVariant::fromValue(filepath.toStr()), 1,
                Qt::MatchFlags(Qt::MatchExactly | Qt::MatchWrap | Qt::MatchRecursive));
            if (!items.isEmpty())
                mUi->projectTreeView->setExpanded(items.first(), true);
        }
    }

    clientSettings.endGroup();
}

void ControlPanel::showProjectReadmeInBrowser(const FilePath& projectFilePath) noexcept
{
    if (projectFilePath.isValid()) {
        FilePath readmeFilePath = projectFilePath.getParentDir().getPathTo("README.md");
        mUi->textBrowser->setSearchPaths(QStringList(projectFilePath.getParentDir().toStr()));
        mUi->textBrowser->setHtml(MarkdownConverter::convertMarkdownToHtml(readmeFilePath));
    } else {
        mUi->textBrowser->clear();
    }
}

/*****************************************************************************************
 *  Project Management
 ****************************************************************************************/

ProjectEditor* ControlPanel::openProject(Project& project) noexcept
{
    try
    {
        ProjectEditor* editor = getOpenProject(project.getFilepath());
        if (!editor) {
            editor = new ProjectEditor(mWorkspace, project);
            connect(editor, &ProjectEditor::projectEditorClosed, this, &ControlPanel::projectEditorClosed);
            connect(editor, &ProjectEditor::showControlPanelClicked, this, &ControlPanel::showControlPanel);
            mOpenProjectEditors.insert(project.getFilepath().toUnique().toStr(), editor);
            mWorkspace.setLastRecentlyUsedProject(project.getFilepath());
        }
        editor->showAllRequiredEditors();
        return editor;
    }
    catch (UserCanceled& e)
    {
        // do nothing
        return nullptr;
    }
    catch (Exception& e)
    {
        QMessageBox::critical(this, tr("Could not open project"), e.getMsg());
        return nullptr;
    }
}

ProjectEditor* ControlPanel::openProject(const FilePath& filepath) noexcept
{
    try
    {
        ProjectEditor* editor = getOpenProject(filepath);
        if (!editor)
        {
            Project* project = new Project(filepath, false);
            editor = new ProjectEditor(mWorkspace, *project);
            connect(editor, &ProjectEditor::projectEditorClosed, this, &ControlPanel::projectEditorClosed);
            connect(editor, &ProjectEditor::showControlPanelClicked, this, &ControlPanel::showControlPanel);
            mOpenProjectEditors.insert(filepath.toUnique().toStr(), editor);
            mWorkspace.setLastRecentlyUsedProject(filepath);
        }
        editor->showAllRequiredEditors();
        return editor;
    }
    catch (UserCanceled& e)
    {
        // do nothing
        return nullptr;
    }
    catch (Exception& e)
    {
        QMessageBox::critical(this, tr("Could not open project"), e.getMsg());
        return nullptr;
    }
}

bool ControlPanel::closeProject(ProjectEditor& editor, bool askForSave) noexcept
{
    Q_ASSERT(mOpenProjectEditors.contains(editor.getProject().getFilepath().toUnique().toStr()));
    return editor.closeAndDestroy(askForSave, this); // this will implicitly call the slot "projectEditorClosed()"!
}

bool ControlPanel::closeProject(const FilePath& filepath, bool askForSave) noexcept
{
    ProjectEditor* editor = getOpenProject(filepath);
    if (editor)
        return closeProject(*editor, askForSave);
    else
        return false;
}

bool ControlPanel::closeAllProjects(bool askForSave) noexcept
{
    bool success = true;
    foreach (ProjectEditor* editor, mOpenProjectEditors)
    {
        if (!closeProject(*editor, askForSave))
            success = false;
    }
    return success;
}

ProjectEditor* ControlPanel::getOpenProject(const FilePath& filepath) const noexcept
{
    if (mOpenProjectEditors.contains(filepath.toUnique().toStr()))
        return mOpenProjectEditors.value(filepath.toUnique().toStr());
    else
        return nullptr;
}

/*****************************************************************************************
 *  Library Management
 ****************************************************************************************/

void ControlPanel::openLibraryEditor(QSharedPointer<library::Library> lib) noexcept
{
    using library::editor::LibraryEditor;
    LibraryEditor* editor = mOpenLibraryEditors.value(lib.data());
    if (!editor) {
        try {
            editor = new LibraryEditor(mWorkspace, lib);
            connect(editor, &LibraryEditor::destroyed,
                    this, &ControlPanel::libraryEditorDestroyed);
            mOpenLibraryEditors.insert(lib.data(), editor);
        } catch (const Exception& e) {
            QMessageBox::critical(this, tr("Error"), e.getMsg());
        }
    }
    editor->show();
    editor->raise();
    editor->activateWindow();
}

void ControlPanel::libraryEditorDestroyed() noexcept
{
    using library::editor::LibraryEditor;
    // Note: Actually we should dynamic_cast the QObject* to LibraryEditor*, but as this
    // slot is called in the destructor of QObject (base class of LibraryEditor), the
    // dynamic_cast does no longer work at this point, so a static_cast is used instead ;)
    LibraryEditor* editor = static_cast<LibraryEditor*>(QObject::sender()); Q_ASSERT(editor);
    library::Library* library = mOpenLibraryEditors.key(editor); Q_ASSERT(library);
    mOpenLibraryEditors.remove(library);
}

bool ControlPanel::closeAllLibraryEditors(bool askForSave) noexcept
{
    using library::editor::LibraryEditor;
    bool success = true;
    foreach (LibraryEditor* editor, mOpenLibraryEditors) {
        if (editor->closeAndDestroy(askForSave)) {
            delete editor; // this calls the slot "libraryEditorDestroyed()"
        } else {
            success = false;
        }
    }
    return success;
}

/*****************************************************************************************
 *  Private Slots
 ****************************************************************************************/

void ControlPanel::projectEditorClosed() noexcept
{
    ProjectEditor* editor = dynamic_cast<ProjectEditor*>(QObject::sender());
    Q_ASSERT(editor); if (!editor) return;

    Project* project = &editor->getProject();
    mOpenProjectEditors.remove(project->getFilepath().toStr());
    delete project;
}

/*****************************************************************************************
 *  Actions
 ****************************************************************************************/

void ControlPanel::on_actionAbout_triggered()
{
    QMessageBox::about(this, tr("About LibrePCB"), QString(tr(
        "<h1>About LibrePCB</h1>"
        "<p>LibrePCB is a free & open source schematic/layout-editor.</p>"
        "<p>Version: %1 (%2)</p>"
        "<p>Please see <a href='http://librepcb.org/'>librepcb.org</a> for more information.</p>"
        "You can find the project on GitHub:<br>"
        "<a href='https://github.com/LibrePCB/LibrePCB'>https://github.com/LibrePCB/LibrePCB</a>"))
        .arg(qApp->getAppVersion().toPrettyStr(3), qApp->getGitVersion()));
}

void ControlPanel::on_actionNew_Project_triggered()
{
    NewProjectWizard wizard(mWorkspace, this);
    wizard.setLocation(mWorkspace.getProjectsPath());
    if (wizard.exec() == QWizard::Accepted) {
        try {
            QScopedPointer<Project> project(wizard.createProject()); // can throw
            openProject(*project.take());
        } catch (Exception& e) {
            QMessageBox::critical(this, tr("Could not create project"), e.getMsg());
        }
    }
}

void ControlPanel::on_actionOpen_Project_triggered()
{
    QSettings settings; // client settings
    QString lastOpenedFile = settings.value("controlpanel/last_open_project",
                             mWorkspace.getPath().toStr()).toString();

    FilePath filepath(QFileDialog::getOpenFileName(this, tr("Open Project"), lastOpenedFile,
                                    tr("LibrePCB project files (%1)").arg("*.lpp")));

    if (!filepath.isValid())
        return;

    settings.setValue("controlpanel/last_open_project", filepath.toNative());

    openProject(filepath);
}

void ControlPanel::on_actionOpen_Library_Manager_triggered()
{
    mLibraryManager->show();
    mLibraryManager->raise();
    mLibraryManager->activateWindow();
}

void ControlPanel::on_actionClose_all_open_projects_triggered()
{
    closeAllProjects(true);
}

void ControlPanel::on_actionSwitch_Workspace_triggered()
{
    FilePath wsPath = Workspace::chooseWorkspacePath();
    if (!wsPath.isValid())
        return;

    Workspace::setMostRecentlyUsedWorkspacePath(wsPath);
    QMessageBox::information(this, tr("Workspace changed"),
        tr("The chosen workspace will be used after restarting the application."));
}

void ControlPanel::on_projectTreeView_clicked(const QModelIndex& index)
{
    ProjectTreeItem* item = static_cast<ProjectTreeItem*>(index.internalPointer());
    if (!item) return;

    if ((item->getType() == ProjectTreeItem::ProjectFolder) || (item->getType() == ProjectTreeItem::ProjectFile)) {
        showProjectReadmeInBrowser(item->getFilePath());
    } else {
        showProjectReadmeInBrowser(FilePath());
    }
}

void ControlPanel::on_projectTreeView_doubleClicked(const QModelIndex& index)
{
    ProjectTreeItem* item = static_cast<ProjectTreeItem*>(index.internalPointer());

    if (!item)
        return;

    switch (item->getType())
    {
        case ProjectTreeItem::File:
            QDesktopServices::openUrl(QUrl::fromLocalFile(item->getFilePath().toStr()));
            break;

        case ProjectTreeItem::Folder:
        case ProjectTreeItem::ProjectFolder:
            mUi->projectTreeView->setExpanded(index, !mUi->projectTreeView->isExpanded(index));
            break;

        case ProjectTreeItem::ProjectFile:
            openProject(item->getFilePath());
            break;

        default:
            break;
    }
}

void ControlPanel::on_projectTreeView_customContextMenuRequested(const QPoint& pos)
{
    // get clicked tree item
    QModelIndex index = mUi->projectTreeView->indexAt(pos);
    if (!index.isValid()) return;
    ProjectTreeItem* item = static_cast<ProjectTreeItem*>(index.internalPointer());
    if (!item) return;

    // build context menu with actions
    QMenu menu;
    QMap<unsigned int, QAction*> actions;
    if (item->getType() == ProjectTreeItem::ProjectFile)
    {
        if (!getOpenProject(item->getFilePath()))
        {
            // this project is not open
            actions.insert(1, menu.addAction(tr("Open Project")));
            actions.value(1)->setIcon(QIcon(":/img/actions/open.png"));
        }
        else
        {
            // this project is open
            actions.insert(2, menu.addAction(tr("Close Project")));
            actions.value(2)->setIcon(QIcon(":/img/actions/close.png"));
        }
        if (mWorkspace.isFavoriteProject(item->getFilePath()))
        {
            // this is a favorite project
            actions.insert(3, menu.addAction(tr("Remove from favorites")));
            actions.value(3)->setIcon(QIcon(":/img/actions/bookmark.png"));
        }
        else
        {
            // this is not a favorite project
            actions.insert(4, menu.addAction(tr("Add to favorites")));
            actions.value(4)->setIcon(QIcon(":/img/actions/bookmark_gray.png"));
        }
        actions.insert(100, menu.addSeparator());
    }
    else
    {
        // a folder or a file is selected
        actions.insert(10, menu.addAction(tr("New Project")));
        actions.value(10)->setIcon(QIcon(":/img/actions/new.png"));
    }
    actions.insert(20, menu.addAction(tr("New Folder")));
    actions.value(20)->setIcon(QIcon(":/img/actions/new_folder.png"));
    actions.insert(101, menu.addSeparator());
    actions.insert(21, menu.addAction(tr("Open Directory")));
    actions.value(21)->setIcon(QIcon(":/img/places/folder_open.png"));
    actions.insert(102, menu.addSeparator());

    // show context menu and execute the clicked action
    switch (actions.key(menu.exec(QCursor::pos()), 0))
    {
        case 1: // open project
            openProject(item->getFilePath());
            break;
        case 2: // close project
            closeProject(item->getFilePath(), true);
            break;
        case 3: // remove project from favorites
            mWorkspace.removeFavoriteProject(item->getFilePath());
            break;
        case 4: // add project to favorites
            mWorkspace.addFavoriteProject(item->getFilePath());
            break;
        case 10: // new project
            break;
        case 20: // new folder
            break;
        case 21: // open project directory
            QDesktopServices::openUrl(QUrl::fromLocalFile(item->getFilePath().toStr()));
            break;
        default:
            break;
    }

    // clean up
    qDeleteAll(actions);
}

void ControlPanel::on_recentProjectsListView_entered(const QModelIndex &index)
{
    FilePath filepath(index.data(Qt::UserRole).toString());
    showProjectReadmeInBrowser(filepath);
}

void ControlPanel::on_favoriteProjectsListView_entered(const QModelIndex &index)
{
    FilePath filepath(index.data(Qt::UserRole).toString());
    showProjectReadmeInBrowser(filepath);
}

void ControlPanel::on_recentProjectsListView_clicked(const QModelIndex &index)
{
    FilePath filepath(index.data(Qt::UserRole).toString());
    openProject(filepath);
}

void ControlPanel::on_favoriteProjectsListView_clicked(const QModelIndex &index)
{
    FilePath filepath(index.data(Qt::UserRole).toString());
    openProject(filepath);
}

void ControlPanel::on_recentProjectsListView_customContextMenuRequested(const QPoint &pos)
{
    QModelIndex index = mUi->recentProjectsListView->indexAt(pos);
    if (!index.isValid())
        return;

    bool isFavorite = mWorkspace.isFavoriteProject(FilePath(index.data(Qt::UserRole).toString()));

    QMenu menu;
    QAction* action;
    if (isFavorite)
    {
        action = menu.addAction(QIcon(":/img/actions/bookmark.png"),
                                           tr("Remove from favorites"));
    }
    else
    {
        action = menu.addAction(QIcon(":/img/actions/bookmark_gray.png"),
                                           tr("Add to favorites"));
    }

    if (menu.exec(QCursor::pos()) == action)
    {
        if (isFavorite)
            mWorkspace.removeFavoriteProject(FilePath(index.data(Qt::UserRole).toString()));
        else
            mWorkspace.addFavoriteProject(FilePath(index.data(Qt::UserRole).toString()));
    }
}

void ControlPanel::on_favoriteProjectsListView_customContextMenuRequested(const QPoint &pos)
{
    QModelIndex index = mUi->favoriteProjectsListView->indexAt(pos);
    if (!index.isValid())
        return;

    QMenu menu;
    QAction* removeAction = menu.addAction(QIcon(":/img/actions/cancel.png"),
                                           tr("Remove from favorites"));

    if (menu.exec(QCursor::pos()) == removeAction)
        mWorkspace.removeFavoriteProject(FilePath(index.data(Qt::UserRole).toString()));
}

void ControlPanel::on_actionRescanLibrary_triggered()
{
    mWorkspace.getLibraryDb().startLibraryRescan();
}

/*****************************************************************************************
 *  End of File
 ****************************************************************************************/

} // namespace application
} // namespace librepcb
