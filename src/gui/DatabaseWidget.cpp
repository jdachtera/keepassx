/*
 *  Copyright (C) 2010 Felix Geyer <debfx@fobos.de>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 or (at your option)
 *  version 3 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "DatabaseWidget.h"
#include "ui_SearchWidget.h"

#include <QAction>
#include <QDesktopServices>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QSplitter>
#include <QTimer>

#include "autotype/AutoType.h"
#include "core/Config.h"
#include "core/FilePath.h"
#include "core/Group.h"
#include "core/Metadata.h"
#include "core/Tools.h"
#include "gui/ChangeMasterKeyWidget.h"
#include "gui/Clipboard.h"
#include "gui/DatabaseOpenWidget.h"
#include "gui/DatabaseSettingsWidget.h"
#include "gui/KeePass1OpenWidget.h"
#include "gui/MessageBox.h"
#include "gui/UnlockDatabaseWidget.h"
#include "gui/entry/EditEntryWidget.h"
#include "gui/entry/EntryView.h"
#include "gui/group/EditGroupWidget.h"
#include "gui/group/GroupView.h"

DatabaseWidget::DatabaseWidget(Database* db, QWidget* parent)
    : QStackedWidget(parent)
    , m_db(db)
    , m_searchUi(new Ui::SearchWidget())
    , m_searchWidget(new QWidget())
    , m_newGroup(Q_NULLPTR)
    , m_newEntry(Q_NULLPTR)
    , m_newParent(Q_NULLPTR)
    , m_searchAllGroups(false)
    , m_searchSensitivity(Qt::CaseInsensitive)
{
    m_searchUi->setupUi(m_searchWidget);

    m_searchTimer = new QTimer(this);
    m_searchTimer->setSingleShot(true);

    m_mainWidget = new QWidget(this);
    QLayout* layout = new QHBoxLayout(m_mainWidget);
    QSplitter* splitter = new QSplitter(m_mainWidget);

    QWidget* rightHandSideWidget = new QWidget(splitter);
    m_searchWidget->setParent(rightHandSideWidget);

    m_groupView = new GroupView(db, splitter);
    m_groupView->setObjectName("groupView");
    m_groupView->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_groupView, SIGNAL(customContextMenuRequested(QPoint)),
            SLOT(emitGroupContextMenuRequested(QPoint)));

    m_entryView = new EntryView(rightHandSideWidget);
    m_entryView->setObjectName("entryView");
    m_entryView->setContextMenuPolicy(Qt::CustomContextMenu);
    m_entryView->setGroup(db->rootGroup());
    connect(m_entryView, SIGNAL(customContextMenuRequested(QPoint)),
            SLOT(emitEntryContextMenuRequested(QPoint)));

    QSizePolicy policy;
    policy = m_groupView->sizePolicy();
    policy.setHorizontalStretch(30);
    m_groupView->setSizePolicy(policy);
    policy = rightHandSideWidget->sizePolicy();
    policy.setHorizontalStretch(70);
    rightHandSideWidget->setSizePolicy(policy);

    QAction* closeAction = new QAction(m_searchWidget);
    QIcon closeIcon = filePath()->icon("actions", "dialog-close");
    closeAction->setIcon(closeIcon);
    m_searchUi->closeSearchButton->setDefaultAction(closeAction);
    m_searchUi->closeSearchButton->setShortcut(Qt::Key_Escape);
    int iconsize = style()->pixelMetric(QStyle::PM_SmallIconSize);
    m_searchUi->closeSearchButton->setIconSize(QSize(iconsize,iconsize));
    m_searchWidget->hide();

    QVBoxLayout* vLayout = new QVBoxLayout(rightHandSideWidget);
    vLayout->setMargin(0);
    vLayout->addWidget(m_searchWidget);
    vLayout->addWidget(m_entryView);

    rightHandSideWidget->setLayout(vLayout);

    splitter->addWidget(m_groupView);
    splitter->addWidget(rightHandSideWidget);

    layout->addWidget(splitter);
    m_mainWidget->setLayout(layout);

    m_editEntryWidget = new EditEntryWidget();
    m_editEntryWidget->setObjectName("editEntryWidget");
    m_historyEditEntryWidget = new EditEntryWidget();
    m_editGroupWidget = new EditGroupWidget();
    m_editGroupWidget->setObjectName("editGroupWidget");
    m_changeMasterKeyWidget = new ChangeMasterKeyWidget();
    m_changeMasterKeyWidget->headlineLabel()->setText(tr("Change master key"));
    QFont headlineLabelFont = m_changeMasterKeyWidget->headlineLabel()->font();
    headlineLabelFont.setBold(true);
    headlineLabelFont.setPointSize(headlineLabelFont.pointSize() + 2);
    m_changeMasterKeyWidget->headlineLabel()->setFont(headlineLabelFont);
    m_databaseSettingsWidget = new DatabaseSettingsWidget();
    m_databaseSettingsWidget->setObjectName("databaseSettingsWidget");
    m_databaseOpenWidget = new DatabaseOpenWidget();
    m_databaseOpenWidget->setObjectName("databaseOpenWidget");
    m_keepass1OpenWidget = new KeePass1OpenWidget();
    m_keepass1OpenWidget->setObjectName("keepass1OpenWidget");
    m_unlockDatabaseWidget = new UnlockDatabaseWidget();
    m_unlockDatabaseWidget->setObjectName("unlockDatabaseWidget");
    addWidget(m_mainWidget);
    addWidget(m_editEntryWidget);
    addWidget(m_editGroupWidget);
    addWidget(m_changeMasterKeyWidget);
    addWidget(m_databaseSettingsWidget);
    addWidget(m_historyEditEntryWidget);
    addWidget(m_databaseOpenWidget);
    addWidget(m_keepass1OpenWidget);
    addWidget(m_unlockDatabaseWidget);

    connect(m_groupView, SIGNAL(groupChanged(Group*)), this, SLOT(clearLastGroup(Group*)));
    connect(m_groupView, SIGNAL(groupChanged(Group*)), SIGNAL(groupChanged()));
    connect(m_groupView, SIGNAL(groupChanged(Group*)), m_entryView, SLOT(setGroup(Group*)));
    connect(m_entryView, SIGNAL(entryActivated(Entry*, EntryModel::ModelColumn)),
            SLOT(entryActivationSignalReceived(Entry*, EntryModel::ModelColumn)));
    connect(m_entryView, SIGNAL(entrySelectionChanged()), SIGNAL(entrySelectionChanged()));
    connect(m_editEntryWidget, SIGNAL(editFinished(bool)), SLOT(switchToView(bool)));
    connect(m_editEntryWidget, SIGNAL(historyEntryActivated(Entry*)), SLOT(switchToHistoryView(Entry*)));
    connect(m_historyEditEntryWidget, SIGNAL(editFinished(bool)), SLOT(switchBackToEntryEdit()));
    connect(m_editGroupWidget, SIGNAL(editFinished(bool)), SLOT(switchToView(bool)));
    connect(m_changeMasterKeyWidget, SIGNAL(editFinished(bool)), SLOT(updateMasterKey(bool)));
    connect(m_databaseSettingsWidget, SIGNAL(editFinished(bool)), SLOT(switchToView(bool)));
    connect(m_databaseOpenWidget, SIGNAL(editFinished(bool)), SLOT(openDatabase(bool)));
    connect(m_keepass1OpenWidget, SIGNAL(editFinished(bool)), SLOT(openDatabase(bool)));
    connect(m_unlockDatabaseWidget, SIGNAL(editFinished(bool)), SLOT(unlockDatabase(bool)));
    connect(this, SIGNAL(currentChanged(int)), this, SLOT(emitCurrentModeChanged()));
    connect(m_searchUi->searchResults, SIGNAL(linkActivated(QString)), this, SLOT(onLinkActivated(QString)));
    connect(m_searchTimer, SIGNAL(timeout()), this, SLOT(search()));
    connect(closeAction, SIGNAL(triggered()), this, SLOT(closeSearch()));

    setCurrentWidget(m_mainWidget);
}

DatabaseWidget::~DatabaseWidget()
{
}

DatabaseWidget::Mode DatabaseWidget::currentMode()
{
    if (currentWidget() == Q_NULLPTR) {
        return DatabaseWidget::None;
    }
    else if (currentWidget() == m_mainWidget) {
        return DatabaseWidget::ViewMode;
    }
    else if (currentWidget() == m_unlockDatabaseWidget) {
        return DatabaseWidget::LockedMode;
    }
    else if (currentWidget() == m_databaseOpenWidget) {
        return DatabaseWidget::OpenMode;
    }
    else {
        return DatabaseWidget::EditMode;
    }
}

void DatabaseWidget::emitCurrentModeChanged()
{
    Q_EMIT currentModeChanged(currentMode());
}

GroupView* DatabaseWidget::groupView()
{
    return m_groupView;
}

EntryView* DatabaseWidget::entryView()
{
    return m_entryView;
}

Database* DatabaseWidget::database()
{
    return m_db;
}

void DatabaseWidget::createEntry()
{
    if (!m_groupView->currentGroup()) {
        Q_ASSERT(false);
        return;
    }

    m_newEntry = new Entry();
    m_newEntry->setUuid(Uuid::random());
    m_newEntry->setUsername(m_db->metadata()->defaultUserName());
    m_newParent = m_groupView->currentGroup();
    setIconFromParent();
    switchToEntryEdit(m_newEntry, true);
}

void DatabaseWidget::setIconFromParent()
{
    if (!config()->get("UseGroupIconOnEntryCreation").toBool()) {
        return;
    }

    if (m_newParent->iconNumber() == Group::DefaultIconNumber && m_newParent->iconUuid().isNull()) {
        return;
    }

    if (m_newParent->iconUuid().isNull()) {
        m_newEntry->setIcon(m_newParent->iconNumber());
    }
    else {
        m_newEntry->setIcon(m_newParent->iconUuid());
    }
}

void DatabaseWidget::cloneEntry()
{
    Entry* currentEntry = m_entryView->currentEntry();
    if (!currentEntry) {
        Q_ASSERT(false);
        return;
    }

    Entry* entry = currentEntry->clone(Entry::CloneNewUuid | Entry::CloneResetTimeInfo);
    entry->setGroup(currentEntry->group());
    m_entryView->setFocus();
    m_entryView->setCurrentEntry(entry);
}

void DatabaseWidget::deleteEntries()
{
    const QModelIndexList selected = m_entryView->selectionModel()->selectedRows();

    if (selected.isEmpty()) {
        Q_ASSERT(false);
        return;
    }

    // get all entry pointers as the indexes change when removing multiple entries
    QList<Entry*> selectedEntries;
    Q_FOREACH (const QModelIndex& index, selected) {
        selectedEntries.append(m_entryView->entryFromIndex(index));
    }

    bool inRecylceBin = Tools::hasChild(m_db->metadata()->recycleBin(), selectedEntries.first());
    if (inRecylceBin || !m_db->metadata()->recycleBinEnabled()) {
        QMessageBox::StandardButton result;

        if (selected.size() == 1) {
            result = MessageBox::question(
                this, tr("Delete entry?"),
                tr("Do you really want to delete the entry \"%1\" for good?")
                .arg(selectedEntries.first()->title()),
                QMessageBox::Yes | QMessageBox::No);
        }
        else {
            result = MessageBox::question(
                this, tr("Delete entries?"),
                tr("Do you really want to delete %1 entries for good?")
                .arg(selected.size()),
                QMessageBox::Yes | QMessageBox::No);
        }

        if (result == QMessageBox::Yes) {
            Q_FOREACH (Entry* entry, selectedEntries) {
                delete entry;
            }
        }
    }
    else {
        if (selected.size() > 1) {
            QMessageBox::StandardButton result = MessageBox::question(
                this, tr("Move entries to recycle bin?"),
                tr("Do you really want to move %1 entries to the recycle bin?")
                .arg(selected.size()),
                QMessageBox::Yes | QMessageBox::No);
            if (result == QMessageBox::No) {
                return;
            }
        }

        Q_FOREACH (Entry* entry, selectedEntries) {
            m_db->recycleEntry(entry);
        }
    }
}

void DatabaseWidget::copyTitle()
{
    Entry* currentEntry = m_entryView->currentEntry();
    if (!currentEntry) {
        Q_ASSERT(false);
        return;
    }

    setClipboardTextAndMinimize(currentEntry->title());
}

void DatabaseWidget::copyUsername()
{
    Entry* currentEntry = m_entryView->currentEntry();
    if (!currentEntry) {
        Q_ASSERT(false);
        return;
    }

    setClipboardTextAndMinimize(currentEntry->username());
}

void DatabaseWidget::copyPassword()
{
    Entry* currentEntry = m_entryView->currentEntry();
    if (!currentEntry) {
        Q_ASSERT(false);
        return;
    }

    setClipboardTextAndMinimize(currentEntry->password());
}

void DatabaseWidget::copyURL()
{
    Entry* currentEntry = m_entryView->currentEntry();
    if (!currentEntry) {
        Q_ASSERT(false);
        return;
    }

    setClipboardTextAndMinimize(currentEntry->url());
}

void DatabaseWidget::copyNotes()
{
    Entry* currentEntry = m_entryView->currentEntry();
    if (!currentEntry) {
        Q_ASSERT(false);
        return;
    }

    setClipboardTextAndMinimize(currentEntry->notes());
}

void DatabaseWidget::copyAttribute(QAction* action)
{
    Entry* currentEntry = m_entryView->currentEntry();
    if (!currentEntry) {
        Q_ASSERT(false);
        return;
    }

    setClipboardTextAndMinimize(currentEntry->attributes()->value(action->text()));
}

void DatabaseWidget::setClipboardTextAndMinimize(const QString& text)
{
    clipboard()->setText(text);
    if (config()->get("MinimizeOnCopy").toBool()) {
        window()->showMinimized();
    }
}

void DatabaseWidget::performAutoType()
{
    Entry* currentEntry = m_entryView->currentEntry();
    if (!currentEntry) {
        Q_ASSERT(false);
        return;
    }

    autoType()->performAutoType(currentEntry, window());
}

void DatabaseWidget::openUrl()
{
    Entry* currentEntry = m_entryView->currentEntry();
    if (!currentEntry) {
        Q_ASSERT(false);
        return;
    }

    openUrlForEntry(currentEntry);
}

void DatabaseWidget::openUrlForEntry(Entry* entry)
{
    if (!entry->url().isEmpty()) {
        QDesktopServices::openUrl(entry->url());
    }
}

void DatabaseWidget::createGroup()
{
    if (!m_groupView->currentGroup()) {
        Q_ASSERT(false);
        return;
    }

    m_newGroup = new Group();
    m_newGroup->setUuid(Uuid::random());
    m_newParent = m_groupView->currentGroup();
    switchToGroupEdit(m_newGroup, true);
}

void DatabaseWidget::deleteGroup()
{
    Group* currentGroup = m_groupView->currentGroup();
    if (!currentGroup || !canDeleteCurrentGoup()) {
        Q_ASSERT(false);
        return;
    }

    bool inRecylceBin = Tools::hasChild(m_db->metadata()->recycleBin(), currentGroup);
    if (inRecylceBin || !m_db->metadata()->recycleBinEnabled()) {
        QMessageBox::StandardButton result = MessageBox::question(
            this, tr("Delete group?"),
            tr("Do you really want to delete the group \"%1\" for good?")
            .arg(currentGroup->name()),
            QMessageBox::Yes | QMessageBox::No);
        if (result == QMessageBox::Yes) {
            delete currentGroup;
        }
    }
    else {
        m_db->recycleGroup(currentGroup);
    }
}

int DatabaseWidget::addWidget(QWidget* w)
{
    w->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);

    int index = QStackedWidget::addWidget(w);

    adjustSize();

    return index;
}

void DatabaseWidget::setCurrentIndex(int index)
{
    // use setCurrentWidget() instead
    // index is not reliable
    Q_UNUSED(index);
    Q_ASSERT(false);
}

void DatabaseWidget::setCurrentWidget(QWidget* widget)
{
    if (currentWidget()) {
        currentWidget()->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    }

    QStackedWidget::setCurrentWidget(widget);

    if (currentWidget()) {
        currentWidget()->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    }

    adjustSize();
}

void DatabaseWidget::switchToView(bool accepted)
{
    if (m_newGroup) {
        if (accepted) {
            m_newGroup->setParent(m_newParent);
            m_groupView->setCurrentGroup(m_newGroup);
            m_groupView->expandGroup(m_newParent);
        }
        else {
            delete m_newGroup;
        }

        m_newGroup = Q_NULLPTR;
        m_newParent = Q_NULLPTR;
    }
    else if (m_newEntry) {
        if (accepted) {
            m_newEntry->setGroup(m_newParent);
            m_entryView->setFocus();
            m_entryView->setCurrentEntry(m_newEntry);
        }
        else {
            delete m_newEntry;
        }

        m_newEntry = Q_NULLPTR;
        m_newParent = Q_NULLPTR;
    }

    setCurrentWidget(m_mainWidget);
}

void DatabaseWidget::switchToHistoryView(Entry* entry)
{
    m_historyEditEntryWidget->loadEntry(entry, false, true, m_editEntryWidget->entryTitle(), m_db);
    setCurrentWidget(m_historyEditEntryWidget);
}

void DatabaseWidget::switchBackToEntryEdit()
{
    setCurrentWidget(m_editEntryWidget);
}

void DatabaseWidget::switchToEntryEdit(Entry* entry)
{
    switchToEntryEdit(entry, false);
}

void DatabaseWidget::switchToEntryEdit(Entry* entry, bool create)
{
    Group* group = m_groupView->currentGroup();
    if (!group) {
        Q_ASSERT(m_entryView->inEntryListMode());
        group = m_lastGroup;
    }
    Q_ASSERT(group);

    m_editEntryWidget->loadEntry(entry, create, false, group->name(), m_db);
    setCurrentWidget(m_editEntryWidget);
}

void DatabaseWidget::switchToGroupEdit(Group* group, bool create)
{
    m_editGroupWidget->loadGroup(group, create, m_db);
    setCurrentWidget(m_editGroupWidget);
}

void DatabaseWidget::updateMasterKey(bool accepted)
{
    if (accepted) {
        QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
        m_db->setKey(m_changeMasterKeyWidget->newMasterKey());
        QApplication::restoreOverrideCursor();
    }
    else if (!m_db->hasKey()) {
        Q_EMIT closeRequest();
        return;
    }

    setCurrentWidget(m_mainWidget);
}

void DatabaseWidget::openDatabase(bool accepted)
{
    if (accepted) {
        Database* oldDb = m_db;
        m_db = static_cast<DatabaseOpenWidget*>(sender())->database();
        m_groupView->changeDatabase(m_db);
        Q_EMIT databaseChanged(m_db);
        delete oldDb;
        setCurrentWidget(m_mainWidget);

        // We won't need those anymore and KeePass1OpenWidget closes
        // the file in its dtor.
        delete m_databaseOpenWidget;
        m_databaseOpenWidget = Q_NULLPTR;
        delete m_keepass1OpenWidget;
        m_keepass1OpenWidget = Q_NULLPTR;
    }
    else {
        if (m_databaseOpenWidget->database()) {
            delete m_databaseOpenWidget->database();
        }
        Q_EMIT closeRequest();
    }
}

void DatabaseWidget::unlockDatabase(bool accepted)
{
    // cancel button is disabled
    Q_ASSERT(accepted);
    Q_UNUSED(accepted);

    setCurrentWidget(widgetBeforeLock);
    Q_EMIT unlockedDatabase();
}

void DatabaseWidget::entryActivationSignalReceived(Entry* entry, EntryModel::ModelColumn column)
{
    if (column == EntryModel::Url && !entry->url().isEmpty()) {
        openUrlForEntry(entry);
    }
    else {
        switchToEntryEdit(entry);
    }
}

void DatabaseWidget::switchToEntryEdit()
{
    Entry* entry = m_entryView->currentEntry();
    Q_ASSERT(entry);
    if (!entry) {
        return;
    }

    switchToEntryEdit(entry, false);
}

void DatabaseWidget::switchToGroupEdit()
{
    Group* group = m_groupView->currentGroup();
    Q_ASSERT(group);
    if (!group) {
        return;
    }

    switchToGroupEdit(group, false);
}

void DatabaseWidget::switchToMasterKeyChange()
{
    m_changeMasterKeyWidget->clearForms();
    setCurrentWidget(m_changeMasterKeyWidget);
}

void DatabaseWidget::switchToDatabaseSettings()
{
    m_databaseSettingsWidget->load(m_db);
    setCurrentWidget(m_databaseSettingsWidget);
}

void DatabaseWidget::switchToOpenDatabase(const QString& fileName)
{
    updateFilename(fileName);
    m_databaseOpenWidget->load(fileName);
    setCurrentWidget(m_databaseOpenWidget);
}

void DatabaseWidget::switchToOpenDatabase(const QString& fileName, const QString& password,
                                          const QString& keyFile)
{
    updateFilename(fileName);
    switchToOpenDatabase(fileName);
    m_databaseOpenWidget->enterKey(password, keyFile);
}

void DatabaseWidget::switchToOpenDatabase(const QString &fileName, const CompositeKey& masterKey)
{
    updateFilename(fileName);
    switchToOpenDatabase(fileName);
    m_databaseOpenWidget->enterKey(masterKey);
}

void DatabaseWidget::switchToImportKeepass1(const QString& fileName)
{
    updateFilename(fileName);
    m_keepass1OpenWidget->load(fileName);
    setCurrentWidget(m_keepass1OpenWidget);
}

void DatabaseWidget::search(const QString& searchString, bool caseSensitive, bool allGroups)
{
    m_searchSensitivity = caseSensitive ? Qt::CaseSensitive
                                        : Qt::CaseInsensitive;
    m_searchAllGroups = allGroups;
    search(searchString);
}

void DatabaseWidget::search(const QString& text)
{
    if (text.isEmpty()) {
        if (m_entryView->inEntryListMode())
            closeSearch();
    }
    else if (m_entryView->inEntryListMode()) {
        m_searchText = text;
        startSearchTimer();
    }
    else {
        showSearch(text);
    }
}

bool DatabaseWidget::caseSensitiveSearch() const
{
    return m_searchSensitivity == Qt::CaseSensitive;
}

void DatabaseWidget::setCaseSensitiveSearch(bool caseSensitive)
{
    if (caseSensitive != caseSensitiveSearch()) {
        m_searchSensitivity = caseSensitive ? Qt::CaseSensitive
                                            : Qt::CaseInsensitive;
        if (m_entryView->inEntryListMode())
            startSearchTimer();
    }
}

bool DatabaseWidget::isAllGroupsSearch() const
{
    return m_searchAllGroups;
}

bool DatabaseWidget::canChooseSearchScope() const
{
    return currentGroup() != m_db->rootGroup();
}

Group*DatabaseWidget::currentGroup() const
{
    return m_entryView->inEntryListMode() ? m_lastGroup
                                          : m_groupView->currentGroup();
}

void DatabaseWidget::setAllGroupsSearch(bool allGroups)
{
    if (allGroups != isAllGroupsSearch()) {
        m_searchAllGroups = allGroups;
        if (m_entryView->inEntryListMode())
            startSearchTimer();
    }
}

void DatabaseWidget::closeSearch()
{
    Q_ASSERT(m_lastGroup);
    m_searchTimer->stop();
    m_groupView->setCurrentGroup(m_lastGroup);
}

void DatabaseWidget::showSearch(const QString & searchString)
{
    m_searchText = searchString;
    m_lastGroup = m_groupView->currentGroup();

    Q_ASSERT(m_lastGroup);
    m_groupView->setCurrentIndex(QModelIndex());

    m_searchWidget->show();
    search();
}

void DatabaseWidget::onLinkActivated(const QString& link)
{
    if (link == "searchAll")
        setAllGroupsSearch(true);
    else if (link == "searchCurrent")
        setAllGroupsSearch(false);
}

void DatabaseWidget::search()
{
    Q_ASSERT(m_lastGroup);

    Group* searchGroup = m_searchAllGroups ? m_db->rootGroup()
                                           : m_lastGroup;
    QList<Entry*> searchResult = searchGroup->search(m_searchText, m_searchSensitivity);

    QString message;
    switch(searchResult.count()) {
    case 0: message = tr("No result found"); break;
    case 1: message = tr("1 result found"); break;
    default: message = tr("%1 results found").arg(searchResult.count()); break;
    }
    if (searchGroup != m_db->rootGroup())
        message += tr(" in \"%1\". <a href='searchAll'>Search all groups...</a>").arg(searchGroup->name());
    else if (m_lastGroup != m_db->rootGroup())
        message += tr(". <a href='searchCurrent'>Search in \"%1\"...</a>").arg(m_lastGroup->name());
    else
        message += tr(".");
    m_searchUi->searchResults->setText(message);

    m_entryView->setEntryList(searchResult);
}

void DatabaseWidget::startSearchTimer()
{
    if (!m_searchTimer->isActive()) {
        m_searchTimer->stop();
    }
    m_searchTimer->start(100);
}

void DatabaseWidget::startSearch()
{
    if (!isInSearchMode())
        return;

    if (!m_searchTimer->isActive()) {
        m_searchTimer->stop();
    }
    search();
}

void DatabaseWidget::emitGroupContextMenuRequested(const QPoint& pos)
{
    Q_EMIT groupContextMenuRequested(m_groupView->viewport()->mapToGlobal(pos));
}

void DatabaseWidget::emitEntryContextMenuRequested(const QPoint& pos)
{
    Q_EMIT entryContextMenuRequested(m_entryView->viewport()->mapToGlobal(pos));
}

bool DatabaseWidget::dbHasKey()
{
    return m_db->hasKey();
}

bool DatabaseWidget::canDeleteCurrentGoup()
{
    bool isRootGroup = m_db->rootGroup() == m_groupView->currentGroup();
    bool isRecycleBin = m_db->metadata()->recycleBin() == m_groupView->currentGroup();
    return !isRootGroup && !isRecycleBin;
}

bool DatabaseWidget::isInSearchMode() const
{
    return m_entryView->inEntryListMode();
}

QString DatabaseWidget::searchText() const
{
    return m_entryView->inEntryListMode() ? m_searchText : QString();
}

void DatabaseWidget::clearLastGroup(Group* group)
{
    if (group) {
        m_lastGroup = Q_NULLPTR;
        m_searchWidget->hide();
    }
}

void DatabaseWidget::lock()
{
    Q_ASSERT(currentMode() != DatabaseWidget::LockedMode);

    widgetBeforeLock = currentWidget();
    m_unlockDatabaseWidget->load(m_filename, m_db);
    setCurrentWidget(m_unlockDatabaseWidget);
}

void DatabaseWidget::updateFilename(const QString& fileName)
{
    m_filename = fileName;
}
