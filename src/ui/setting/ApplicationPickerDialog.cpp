#include "include/global/Utils.hpp"
#include "include/ui/setting/ApplicationPickerDialog.h"

#include "include/ui/widget/MaterialIcon.h"

#include <QAbstractTableModel>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QFormLayout>
#include <QHeaderView>
#include <QHash>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QProcess>
#include <QPointer>
#include <QProxyStyle>
#include <QRegularExpression>
#include <QPushButton>
#include <QSettings>
#include <QSortFilterProxyModel>
#include <QStandardPaths>
#include <QTabBar>
#include <QTabWidget>
#include <QTableView>
#include <QThreadPool>
#include <QTimer>
#include <QVBoxLayout>
#include <QSet>
#include <QStyleOption>

#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <tlhelp32.h>
#endif

#include <algorithm>
#include <iterator>
#include <ranges>
#include <utility>

namespace {

constexpr int PathRole = Qt::UserRole + 1;
constexpr int ExecutableNameRole = Qt::UserRole + 2;

struct ApplicationEntry {
    QString name;
    QString executableName;
    QString path;
};

class HeaderArrowStyle final : public QProxyStyle {
public:
    void drawPrimitive(PrimitiveElement element, const QStyleOption *option, QPainter *painter,
                       const QWidget *widget = nullptr) const override {
        if (element != PE_IndicatorHeaderArrow) {
            QProxyStyle::drawPrimitive(element, option, painter, widget);
            return;
        }
        QStyleOption shifted(*option);
        shifted.rect.translate(0, 3);
        QProxyStyle::drawPrimitive(element, &shifted, painter, widget);
    }
};

QString normalizedExecutablePath(QString value) {
    value = value.trimmed();
    if (value.startsWith('"')) {
        const qsizetype closingQuote = value.indexOf('"', 1);
        if (closingQuote > 1) value = value.mid(1, closingQuote - 1);
    } else {
        const qsizetype exeEnd = value.indexOf(QStringLiteral(".exe"), 0, Qt::CaseInsensitive);
        if (exeEnd >= 0) value = value.left(exeEnd + 4);
    }
    value = QDir::fromNativeSeparators(value);
    const QFileInfo info(value);
    // DisplayIcon frequently names an .ico rather than the program. A routing rule on
    // one can never match a process, so an entry without a real executable is dropped.
    if (info.suffix().compare(QStringLiteral("exe"), Qt::CaseInsensitive) != 0) return {};
    return info.exists() ? QDir::toNativeSeparators(info.absoluteFilePath()) : QString();
}

void appendUnique(QList<ApplicationEntry> &entries, QSet<QString> &seen, QString name, QString path) {
    path = QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());
    const QString executableName = QFileInfo(path).fileName();
    if (path.isEmpty() || executableName.isEmpty()) return;
    const QString key = path.toCaseFolded();
    if (seen.contains(key)) return;
    seen.insert(key);
    if (name.trimmed().isEmpty()) name = QFileInfo(path).completeBaseName();
    entries.append({name.trimmed(), executableName, path});
}

void appendRunningUnique(QList<ApplicationEntry> &entries, QSet<QString> &seen, QString executableName, QString path) {
    executableName = executableName.trimmed();
    if (executableName.isEmpty()) return;
    if (!path.isEmpty()) path = QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());
    const QString key = (path.isEmpty() ? executableName : path).toCaseFolded();
    if (seen.contains(key)) return;
    seen.insert(key);
    entries.append({QFileInfo(executableName).completeBaseName(), executableName, path});
}

#ifdef Q_OS_WIN
QList<ApplicationEntry> installedApplications() {
    QList<ApplicationEntry> result;
    QSet<QString> seen;
    const QStringList uninstallRoots = {
        QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall"),
        QStringLiteral("HKEY_LOCAL_MACHINE\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall"),
        QStringLiteral("HKEY_LOCAL_MACHINE\\Software\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall"),
    };
    for (const QString &rootName: uninstallRoots) {
        QSettings root(rootName, QSettings::NativeFormat);
        for (const QString &group: root.childGroups()) {
            root.beginGroup(group);
            const QString name = root.value(QStringLiteral("DisplayName")).toString();
            const QString path = normalizedExecutablePath(root.value(QStringLiteral("DisplayIcon")).toString());
            root.endGroup();
            if (!name.isEmpty() && !path.isEmpty()) appendUnique(result, seen, name, path);
        }
    }

    const QStringList appPathRoots = {
        QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\App Paths"),
        QStringLiteral("HKEY_LOCAL_MACHINE\\Software\\Microsoft\\Windows\\CurrentVersion\\App Paths"),
    };
    for (const QString &rootName: appPathRoots) {
        QSettings root(rootName, QSettings::NativeFormat);
        for (const QString &group: root.childGroups()) {
            root.beginGroup(group);
            const QString path = normalizedExecutablePath(root.value(QString()).toString());
            root.endGroup();
            if (!path.isEmpty()) appendUnique(result, seen, QFileInfo(path).completeBaseName(), path);
        }
    }
    std::ranges::sort(result, {}, [](const ApplicationEntry &entry) { return entry.name.toCaseFolded(); });
    return result;
}

QList<ApplicationEntry> runningApplications() {
    QList<ApplicationEntry> result;
    QSet<QString> seen;
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return result;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry)) {
        do {
            QString path;
            const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID);
            if (process) {
                wchar_t buffer[32768];
                DWORD length = static_cast<DWORD>(std::size(buffer));
                if (QueryFullProcessImageNameW(process, 0, buffer, &length)) path = QString::fromWCharArray(buffer, length);
                CloseHandle(process);
            }
            const QString executableName = QString::fromWCharArray(entry.szExeFile);
            appendRunningUnique(result, seen, executableName, path);
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    std::ranges::sort(result, {}, [](const ApplicationEntry &item) { return item.name.toCaseFolded(); });
    return result;
}
#else
QString desktopExecPath(QString command) {
    command.replace(QRegularExpression(QStringLiteral("%[a-zA-Z]")), QString());
    const QStringList parts = QProcess::splitCommand(command);
    if (parts.isEmpty()) return {};
    const QString executable = parts.first();
    if (QFileInfo(executable).isAbsolute() && QFileInfo(executable).exists()) return executable;
    return QStandardPaths::findExecutable(executable);
}

QList<ApplicationEntry> installedApplications() {
    QList<ApplicationEntry> result;
    QSet<QString> seen;
#ifdef Q_OS_MACOS
    const QStringList roots = {QStringLiteral("/Applications"), QDir::homePath() + QStringLiteral("/Applications")};
    for (const QString &root: roots) {
        QDir directory(root);
        for (const QFileInfo &bundle: directory.entryInfoList({QStringLiteral("*.app")}, QDir::Dirs | QDir::NoDotAndDotDot))
            appendUnique(result, seen, bundle.completeBaseName(), bundle.absoluteFilePath());
    }
#else
    QStringList roots = QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation);
    roots << QStringLiteral("/usr/share/applications") << QStringLiteral("/usr/local/share/applications");
    roots.removeDuplicates();
    for (const QString &root: roots) {
        QDir directory(root);
        for (const QFileInfo &desktopFile: directory.entryInfoList({QStringLiteral("*.desktop")}, QDir::Files)) {
            QSettings desktop(desktopFile.absoluteFilePath(), QSettings::IniFormat);
            desktop.beginGroup(QStringLiteral("Desktop Entry"));
            if (desktop.value(QStringLiteral("NoDisplay")).toBool()) {
                desktop.endGroup();
                continue;
            }
            const QString name = desktop.value(QStringLiteral("Name")).toString();
            const QString path = desktopExecPath(desktop.value(QStringLiteral("Exec")).toString());
            desktop.endGroup();
            if (!path.isEmpty()) appendUnique(result, seen, name, path);
        }
    }
#endif
    std::ranges::sort(result, {}, [](const ApplicationEntry &entry) { return entry.name.toCaseFolded(); });
    return result;
}

QList<ApplicationEntry> runningApplications() {
    QList<ApplicationEntry> result;
    QSet<QString> seen;
#ifdef Q_OS_LINUX
    QDir proc(QStringLiteral("/proc"));
    for (const QString &pid: proc.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        bool numeric = false;
        pid.toUInt(&numeric);
        if (!numeric) continue;
        const QString path = QFileInfo(proc.filePath(pid + QStringLiteral("/exe"))).symLinkTarget();
        if (!path.isEmpty()) appendRunningUnique(result, seen, QFileInfo(path).fileName(), path);
    }
#else
    QProcess process;
    process.start(QStringLiteral("ps"), {QStringLiteral("-axo"), QStringLiteral("comm=")});
    if (process.waitForFinished(5000)) {
        for (const QByteArray &line: process.readAllStandardOutput().split('\n')) {
            const QString path = QString::fromUtf8(line).trimmed();
            if (!path.isEmpty()) appendRunningUnique(result, seen, QFileInfo(path).fileName(), path);
        }
    }
#endif
    std::ranges::sort(result, {}, [](const ApplicationEntry &entry) { return entry.name.toCaseFolded(); });
    return result;
}
#endif

class ApplicationListModel final : public QAbstractTableModel {
public:
    explicit ApplicationListModel(QObject *parent = nullptr) : QAbstractTableModel(parent) {}

    int rowCount(const QModelIndex &parent = {}) const override { return parent.isValid() ? 0 : entries_.size(); }
    int columnCount(const QModelIndex &parent = {}) const override { return parent.isValid() ? 0 : 2; }

    QVariant headerData(int section, Qt::Orientation orientation, int role) const override {
        if (orientation != Qt::Horizontal || role != Qt::DisplayRole) return {};
        return section == 0 ? QObject::tr("Application") : QObject::tr("Executable path");
    }

    QVariant data(const QModelIndex &index, int role) const override {
        if (!index.isValid() || index.row() >= entries_.size()) return {};
        const auto &entry = entries_.at(index.row());
        if (role == Qt::DisplayRole) return index.column() == 0 ? entry.name : entry.path;
        if (role == Qt::ToolTipRole) return entry.path;
        if (role == PathRole) return entry.path;
        if (role == ExecutableNameRole) return entry.executableName;
        if (role == Qt::DecorationRole && index.column() == 0) {
            const auto cached = icons_.constFind(entry.path);
            if (cached != icons_.cend()) return *cached;
            if (!pendingIcons_.contains(entry.path)) {
                pendingIcons_.insert(entry.path);
                const QString path = entry.path;
                const QPersistentModelIndex persistent(index);
                auto *model = const_cast<ApplicationListModel *>(this);
                QTimer::singleShot(0, model, [model, path, persistent] {
                    QFileIconProvider provider;
                    QIcon icon = provider.icon(QFileInfo(path));
                    if (icon.isNull()) icon = MaterialIcon::icon(MaterialIcon::Glyph::Apps, QColor("#237AE9"), 20);
                    model->pendingIcons_.remove(path);
                    if (model->icons_.size() >= 256 && !model->iconOrder_.isEmpty())
                        model->icons_.remove(model->iconOrder_.takeFirst());
                    model->icons_.insert(path, icon);
                    model->iconOrder_.append(path);
                    if (persistent.isValid()) emit model->dataChanged(persistent, persistent, {Qt::DecorationRole});
                });
            }
            return MaterialIcon::icon(MaterialIcon::Glyph::Apps, QColor("#237AE9"), 20);
        }
        return {};
    }

    void replace(QList<ApplicationEntry> entries) {
        beginResetModel();
        entries_ = std::move(entries);
        endResetModel();
    }

private:
    QList<ApplicationEntry> entries_;
    mutable QHash<QString, QIcon> icons_;
    mutable QSet<QString> pendingIcons_;
    mutable QStringList iconOrder_;
};

QTableView *makeTable(ApplicationListModel *model, QSortFilterProxyModel **proxyOut, QWidget *parent) {
    auto *proxy = new QSortFilterProxyModel(parent);
    proxy->setSourceModel(model);
    proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
    proxy->setFilterKeyColumn(-1);
    proxy->setSortCaseSensitivity(Qt::CaseInsensitive);
    proxy->sort(0);
    auto *table = new QTableView(parent);
    table->setModel(proxy);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setAlternatingRowColors(false);
    table->setShowGrid(false);
    table->setSortingEnabled(true);
    table->setIconSize(QSize(22, 22));
    table->verticalHeader()->hide();
    table->verticalHeader()->setDefaultSectionSize(34);
    auto *headerStyle = new HeaderArrowStyle;
    headerStyle->setParent(table->horizontalHeader());
    table->horizontalHeader()->setStyle(headerStyle);
    table->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    table->horizontalHeader()->setStretchLastSection(true);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setMinimumSectionSize(190);
    *proxyOut = proxy;
    return table;
}

} // namespace

class ApplicationPickerDialog::Private {
public:
    ApplicationPickerDialog *q;
    QLineEdit *search = nullptr;
    QTabWidget *tabs = nullptr;
    ApplicationListModel *installedModel = nullptr;
    ApplicationListModel *runningModel = nullptr;
    QSortFilterProxyModel *installedProxy = nullptr;
    QSortFilterProxyModel *runningProxy = nullptr;
    QTableView *installedTable = nullptr;
    QTableView *runningTable = nullptr;
    QLineEdit *filePath = nullptr;
    QLabel *filePreview = nullptr;
    QComboBox *matcher = nullptr;
    QPushButton *accept = nullptr;
    quint64 generation = 0;

    explicit Private(ApplicationPickerDialog *dialog) : q(dialog) {}

    void reload() {
        const quint64 requestedGeneration = ++generation;
        installedModel->replace({});
        runningModel->replace({});
        q->setCursor(Qt::BusyCursor);
        accept->setEnabled(false);
        QCoreApplication *application = QCoreApplication::instance();
        QThreadPool::globalInstance()->start([application, dialog = QPointer<ApplicationPickerDialog>(q), requestedGeneration] {
            QList<ApplicationEntry> installed = installedApplications();
            QList<ApplicationEntry> running = runningApplications();
            QMetaObject::invokeMethod(application, [dialog, requestedGeneration, installed = std::move(installed), running = std::move(running)]() mutable {
                if (!dialog || dialog->d->generation != requestedGeneration) return;
                dialog->d->installedModel->replace(std::move(installed));
                dialog->d->runningModel->replace(std::move(running));
                dialog->unsetCursor();
                dialog->d->updateAcceptState();
            });
        });
    }

    void updateAcceptState() const {
        bool enabled = false;
        if (tabs->currentIndex() == 0)
            enabled = installedTable->selectionModel()->hasSelection();
        else if (tabs->currentIndex() == 1)
            enabled = runningTable->selectionModel()->hasSelection();
        else
            enabled = !filePath->text().trimmed().isEmpty();
        accept->setEnabled(enabled);
    }

    void updateFilePreview() const {
        const QFileInfo info(filePath->text().trimmed());
        if (!info.exists()) {
            filePreview->setPixmap({});
            filePreview->setText(QObject::tr("Choose an executable file or paste its full path."));
            return;
        }
        QFileIconProvider provider;
        const QIcon icon = provider.icon(info);
        filePreview->setPixmap(icon.pixmap(26, 26));
        filePreview->setToolTip(info.fileName());
    }

    QStringList rulesForTable(QTableView *table) const {
        QStringList result;
        const bool exactPath = matcher->currentData().toString() == QStringLiteral("processPath");
        QModelIndexList rows = table->selectionModel()->selectedRows(0);
        std::ranges::sort(rows, {}, [](const QModelIndex &index) { return index.row(); });
        for (const QModelIndex &index: rows) {
            const QString path = index.data(PathRole).toString();
            const QString executableName = index.data(ExecutableNameRole).toString();
            const QString rule = exactPath && !path.isEmpty()
                                     ? QStringLiteral("processPath:") + path
                                     : QStringLiteral("processName:") + executableName;
            if (!result.contains(rule)) result.append(rule);
        }
        return result;
    }
};

ApplicationPickerDialog::ApplicationPickerDialog(QWidget *parent) : QDialog(parent), d(new Private(this)) {
    setObjectName(QStringLiteral("routeProfileEditor"));
    setWindowTitle(tr("Choose applications"));
    setMinimumSize(660, 440);
    FitWindowToScreen(this, QSize(760, 500));

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(16, 16, 16, 14);
    root->setSpacing(12);
    auto *title = new QLabel(tr("Choose traffic sources"));
    title->setObjectName(QStringLiteral("routeHeading"));
    root->addWidget(title);
    auto *subtitle = new QLabel(tr("Select one or more applications, running processes, or an executable file."));
    subtitle->setObjectName(QStringLiteral("routeMuted"));
    root->addWidget(subtitle);

    auto *searchRow = new QHBoxLayout;
    d->search = new QLineEdit;
    d->search->setPlaceholderText(tr("Search applications or executable paths…"));
    d->search->addAction(MaterialIcon::icon(MaterialIcon::Glyph::Search, QColor("#A4ABB4"), 18), QLineEdit::LeadingPosition);
    d->search->setFixedHeight(36);
    searchRow->addWidget(d->search, 1);
    auto *reload = new QPushButton(tr("Reload"));
    reload->setObjectName(QStringLiteral("applicationPickerReload"));
    reload->setIcon(MaterialIcon::icon(MaterialIcon::Glyph::Reload, QColor("#DDE2E7"), 18));
    reload->setFixedHeight(36);
    searchRow->addWidget(reload);
    root->addLayout(searchRow);

    d->installedModel = new ApplicationListModel(this);
    d->runningModel = new ApplicationListModel(this);
    d->tabs = new QTabWidget;
    d->tabs->setObjectName(QStringLiteral("applicationPickerTabs"));
    d->tabs->tabBar()->setExpanding(false);
    d->installedTable = makeTable(d->installedModel, &d->installedProxy, d->tabs);
    d->runningTable = makeTable(d->runningModel, &d->runningProxy, d->tabs);
    d->tabs->addTab(d->installedTable, tr("Installed applications"));
    d->tabs->addTab(d->runningTable, tr("Running processes"));

    auto *filePage = new QWidget;
    auto *fileLayout = new QVBoxLayout(filePage);
    fileLayout->setContentsMargins(12, 18, 12, 12);
    auto *fileLabel = new QLabel(tr("Executable file"));
    fileLabel->setObjectName(QStringLiteral("routeSectionTitle"));
    fileLayout->addWidget(fileLabel);
    auto *fileRow = new QHBoxLayout;
    d->filePath = new QLineEdit;
    d->filePath->setPlaceholderText(tr("Full path to an executable"));
    fileRow->addWidget(d->filePath, 1);
    auto *browse = new QPushButton(tr("Browse…"));
    browse->setIcon(MaterialIcon::icon(MaterialIcon::Glyph::Folder, QColor("#DDE2E7"), 18));
    fileRow->addWidget(browse);
    fileLayout->addLayout(fileRow);
    d->filePreview = new QLabel(tr("Choose an executable file or paste its full path."));
    d->filePreview->setObjectName(QStringLiteral("routeMuted"));
    d->filePreview->setMinimumHeight(34);
    fileLayout->addWidget(d->filePreview);
    fileLayout->addStretch();
    d->tabs->addTab(filePage, tr("Executable file"));
    root->addWidget(d->tabs, 1);

    auto *bottom = new QHBoxLayout;
    auto *matchLabel = new QLabel(tr("Match by"));
    matchLabel->setObjectName(QStringLiteral("routeMuted"));
    bottom->addWidget(matchLabel);
    d->matcher = new QComboBox;
    d->matcher->addItem(tr("Executable name"), QStringLiteral("processName"));
    d->matcher->addItem(tr("Exact path"), QStringLiteral("processPath"));
    bottom->addWidget(d->matcher);
    bottom->addStretch();
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel | QDialogButtonBox::Ok);
    d->accept = buttons->button(QDialogButtonBox::Ok);
    d->accept->setText(tr("Add selected"));
    d->accept->setObjectName(QStringLiteral("routeSaveButton"));
    d->accept->setEnabled(false);
    bottom->addWidget(buttons);
    root->addLayout(bottom);

    connect(d->search, &QLineEdit::textChanged, this, [this](const QString &text) {
        d->installedProxy->setFilterFixedString(text);
        d->runningProxy->setFilterFixedString(text);
    });
    connect(reload, &QPushButton::clicked, this, [this] { d->reload(); });
    connect(d->tabs, &QTabWidget::currentChanged, this, [this](int index) {
        d->search->setEnabled(index != 2);
        d->matcher->setCurrentIndex(index == 2 ? 1 : 0);
        d->updateAcceptState();
    });
    connect(d->installedTable->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this] { d->updateAcceptState(); });
    connect(d->runningTable->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this] { d->updateAcceptState(); });
    connect(d->filePath, &QLineEdit::textChanged, this, [this] {
        d->updateFilePreview();
        d->updateAcceptState();
    });
    connect(browse, &QPushButton::clicked, this, [this] {
        const QString file = QFileDialog::getOpenFileName(this, tr("Choose executable"));
        if (!file.isEmpty()) d->filePath->setText(QDir::toNativeSeparators(file));
    });
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(d->installedTable, &QTableView::doubleClicked, this, [this] { if (d->accept->isEnabled()) accept(); });
    connect(d->runningTable, &QTableView::doubleClicked, this, [this] { if (d->accept->isEnabled()) accept(); });

    d->matcher->setCurrentIndex(0);
    d->reload();
}

ApplicationPickerDialog::~ApplicationPickerDialog() {
    ++d->generation;
    delete d;
}

QStringList ApplicationPickerDialog::selectedRules() const {
    if (d->tabs->currentIndex() == 0) return d->rulesForTable(d->installedTable);
    if (d->tabs->currentIndex() == 1) return d->rulesForTable(d->runningTable);
    const QString path = QDir::toNativeSeparators(d->filePath->text().trimmed());
    if (path.isEmpty()) return {};
    if (d->matcher->currentData().toString() == QStringLiteral("processName"))
        return {QStringLiteral("processName:") + QFileInfo(path).fileName()};
    return {QStringLiteral("processPath:") + path};
}

namespace ApplicationIcons {
namespace {

// All of this state is touched from the UI thread only.
QHash<QString, QIcon> g_icons;
QHash<QString, QString> g_paths;
QList<std::function<void()>> g_waiting;
bool g_indexReady = false;
bool g_indexBuilding = false;

QHash<QString, QString> buildExecutableIndex() {
    QHash<QString, QString> index;
    // Running processes win over installed applications: they name the binary
    // that a process rule will actually match.
    for (const QList<ApplicationEntry> &entries: {runningApplications(), installedApplications()}) {
        for (const ApplicationEntry &entry: entries) {
            if (entry.path.isEmpty()) continue;
            const QString key = QFileInfo(entry.path).fileName().toCaseFolded();
            if (!key.isEmpty() && !index.contains(key)) index.insert(key, entry.path);
        }
    }
    return index;
}

QIcon iconFor(const QString &key) {
    if (const auto cached = g_icons.constFind(key); cached != g_icons.cend()) return *cached;
    QIcon icon;
    const QString path = g_paths.value(key);
    if (!path.isEmpty()) {
        QFileIconProvider provider;
        icon = provider.icon(QFileInfo(path));
    }
    g_icons.insert(key, icon);
    return icon;
}

} // namespace

void resolve(const QString &executableName, QObject *context, std::function<void(const QIcon &)> ready) {
    const QString key = executableName.trimmed().toCaseFolded();
    if (key.isEmpty() || !context || !ready) return;

    auto deliver = [key, guard = QPointer<QObject>(context), ready = std::move(ready)] {
        if (!guard) return;
        const QIcon icon = iconFor(key);
        if (!icon.isNull()) ready(icon);
    };

    if (g_indexReady || g_icons.contains(key)) {
        // Extraction still hits the shell, so keep it off the construction path.
        QTimer::singleShot(0, context, std::move(deliver));
        return;
    }

    g_waiting.append(std::move(deliver));
    if (g_indexBuilding) return;
    g_indexBuilding = true;
    QCoreApplication *application = QCoreApplication::instance();
    QThreadPool::globalInstance()->start([application] {
        QHash<QString, QString> index = buildExecutableIndex();
        QMetaObject::invokeMethod(application, [index = std::move(index)]() mutable {
            g_paths = std::move(index);
            g_indexReady = true;
            g_indexBuilding = false;
            const QList<std::function<void()>> waiting = std::exchange(g_waiting, {});
            for (const auto &callback: waiting) callback();
        });
    });
}

} // namespace ApplicationIcons
