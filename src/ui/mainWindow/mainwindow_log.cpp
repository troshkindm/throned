#include "include/ui/mainwindow.h"
#include "include/ui/setting/ThemeManager.hpp"

#include <QAction>
#include <QApplication>
#include <QMenu>
#include <QMutexLocker>
#include <QScrollBar>
#include <QTextCursor>

#include <v2/ui/LogHighlighter.hpp>
#include <algorithm>
#include "include/ui/widget/MaterialIcon.h"

namespace {
constexpr qsizetype MAX_PENDING_LOG_CHARS = 2 * 1024 * 1024;
// Slack so a fractional layout height still counts as scrolled to the bottom.
constexpr int LOG_BOTTOM_SLACK = 4;
constexpr int LOG_SEARCH_DEBOUNCE_MS = 150;

int logLineLimit() {
    const int limit = Configs::dataManager->settingsRepo->max_log_line;
    return limit > 0 ? limit : 500;
}

// Every log line must stay exactly one block: the view maps visible m_logLines to blocks by count.
void appendDocumentLines(QTextDocument *doc, const QString &lines) {
    if (lines.isEmpty()) return;
    QTextCursor cursor(doc);
    cursor.movePosition(QTextCursor::End);
    cursor.beginEditBlock();
    if (!doc->isEmpty()) cursor.insertBlock();
    cursor.insertText(lines);
    cursor.endEditBlock();
}

void removeLeadingBlocks(QTextDocument *doc, int count) {
    if (count <= 0) return;
    if (count >= doc->blockCount()) {
        doc->clear();
        return;
    }
    QTextCursor cursor(doc);
    cursor.movePosition(QTextCursor::Start);
    cursor.movePosition(QTextCursor::NextBlock, QTextCursor::KeepAnchor, count);
    cursor.removeSelectedText();
}
} // namespace

void MainWindow::applyLogBrowserFont() {
    const auto &settings = Configs::dataManager->settingsRepo;
    // The whole list, not just the resolved family, so glyphs missing from the first font still fall back in order.
    QFont logFont;
    logFont.setFamilies(LogFontFamilies(settings->log_font_family));
    logFont.setStyleHint(QFont::Monospace);
    logFont.setFixedPitch(true);
    int pt = settings->log_font_size;
    if (pt <= 0) pt = qApp->font().pointSize();
    if (pt <= 0) pt = settings->font_size;
    if (pt > 0) logFont.setPointSize(pt);
    ui->masterLogBrowser->setFont(logFont);
}

void MainWindow::setLogHighlighter(bool darkMode) {
    // A QSyntaxHighlighter is never evicted by constructing another, so the old one must be deleted.
    delete logHighlighter;
    logHighlighter = new SyntaxHighlighter(darkMode, qvLogDocument);
    logHighlighter->setSearchPattern(m_logSearch);
}

void MainWindow::setupLogView() {
    qvLogDocument->setUndoRedoEnabled(false);
    ui->masterLogBrowser->setUndoRedoEnabled(false);
    ui->masterLogBrowser->setDocument(qvLogDocument);
    applyLogBrowserFont();

    // Follow mode tracks where the user leaves the scrollbar; content changes only snap it back down.
    auto bar = ui->masterLogBrowser->verticalScrollBar();
    connect(bar, &QScrollBar::valueChanged, this, [this, bar](int value) {
        const bool atBottom = value >= bar->maximum() - LOG_BOTTOM_SLACK;
        if (atBottom == m_logFollow) return;
        m_logFollow = atBottom;
        if (m_logFollow && !m_logHeld.empty()) {
            // Deferred: merging relayouts the document while the scrollbar is still mid-update.
            QTimer::singleShot(0, this, [this] {
                if (m_logFollow) releaseHeldLogs();
            });
        }
        updateLogStatus();
    });
    // The document lays out lazily, so the range keeps growing after an append.
    connect(bar, &QScrollBar::rangeChanged, this, [this, bar](int, int max) {
        if (m_logFollow) bar->setValue(max);
    });

    // Both sit in the stats tab corner, which setupConnectionFilter() builds.
    logJumpLatestButton = new QToolButton(this);
    logJumpLatestButton->setToolTip(tr("New logs are held while the view is scrolled up"));
    connect(logJumpLatestButton, &QToolButton::clicked, this, [this] { releaseHeldLogs(); });
    logFilterButton = new QToolButton(this);
    logFilterButton->setObjectName(QStringLiteral("panelIconButton"));
    logFilterButton->setCursor(Qt::PointingHandCursor);
    logFilterButton->setFocusPolicy(Qt::NoFocus);
    logFilterButton->setFixedSize(28, 28);
    logFilterButton->setIconSize(QSize(18, 18));
    logFilterButton->setCheckable(true);
    const auto refreshFilterIcon = [this] {
        const auto colors = themeManager()->Colors();
        logFilterButton->setIcon(MaterialIcon::icon(MaterialIcon::Glyph::Tune,
                                                    logFilterButton->isChecked() ? colors.accent : colors.textMuted, 18));
    };
    refreshFilterIcon();
    connect(themeManager(), &ThemeManager::themeChanged, logFilterButton, refreshFilterIcon);
    connect(logFilterButton, &QToolButton::toggled, logFilterButton, refreshFilterIcon);
    connect(logFilterButton, &QToolButton::toggled, this, &MainWindow::setLogFilterVisible);
    for (auto *w: ui->logToolbar->findChildren<QWidget *>()) w->installEventFilter(this);

    m_logSearchDebounce = new QTimer(this);
    m_logSearchDebounce->setSingleShot(true);
    m_logSearchDebounce->setInterval(LOG_SEARCH_DEBOUNCE_MS);
    connect(m_logSearchDebounce, &QTimer::timeout, this, [this] { applyLogSearch(); });
    connect(ui->logSearchEdit, &QLineEdit::textChanged, m_logSearchDebounce, qOverload<>(&QTimer::start));
    connect(ui->logSearchCase, &QToolButton::toggled, this, [this] { applyLogSearch(); });
    connect(ui->logSearchRegex, &QToolButton::toggled, this, [this] { applyLogSearch(); });
    setLogFilterVisible(false);
    updateLogStatus();
}

void MainWindow::append_log(const QString &log) {
    if (log.size() > 20000) {
        append_log(QString("TRUNCATED LONG LOG: ") + log.first(1000) + "...");
        return;
    }
    QMutexLocker locker(&logMutex);
    if (logQueue.size() > 1000) {
        return;
    }
    logQueue.enqueue(log);
    if (logQueue.size() == 1) logWaiter.wakeOne();
}

void MainWindow::log_process_loop() {
    while (true) {
        logMutex.lock();
        while (logQueue.isEmpty()) {
            logWaiter.wait(&logMutex);
        }
        QQueue<QString> pending;
        pending.swap(logQueue);
        const LogFilter filter{
            Configs::dataManager->settingsRepo->log_enable_include,
            Configs::dataManager->settingsRepo->log_enable_exclude,
            includeKeywords,
            excludeKeywords,
            includeCombined,
            excludeCombined,
            minLogLevelRank,
        };
        logMutex.unlock();

        QString batchToPrint;
        for (const auto &entry: pending) {
            for (auto logLine: entry.split('\n')) {
                if (logLine.endsWith('\r')) logLine.chop(1);
                if (!should_print_log(logLine, filter)) continue;
                // QTextCursor::insertText starts a new block at these; keep each log line a single block.
                for (QChar &c: logLine) {
                    if (c == '\r' || c == QChar::ParagraphSeparator || c == QChar(0xfdd0) || c == QChar(0xfdd1)) c = ' ';
                }
                if (!batchToPrint.isEmpty()) batchToPrint += '\n';
                batchToPrint += logLine;
            }
        }
        if (batchToPrint.isEmpty()) continue;

        bool needsPost;
        {
            QMutexLocker pendingLocker(&logPendingMutex);
            if (!logPendingText.isEmpty()) logPendingText += '\n';
            logPendingText += batchToPrint;
            if (logPendingText.size() > MAX_PENDING_LOG_CHARS) {
                const auto cut = logPendingText.indexOf('\n', logPendingText.size() - MAX_PENDING_LOG_CHARS);
                logPendingText = cut < 0 ? QString() : logPendingText.mid(cut + 1);
            }
            needsPost = !logFlushScheduled;
            logFlushScheduled = true;
        }
        if (needsPost) runOnUiThread([this] { flush_log_batch(); });
    }
}

void MainWindow::flush_log_batch() {
    QString batch;
    {
        QMutexLocker pendingLocker(&logPendingMutex);
        batch.swap(logPendingText);
        logFlushScheduled = false;
    }
    if (batch.isEmpty()) return;

    // Everything passes through the hold queue; while the user is scrolled up it only accumulates
    // (keeping the newest max_log_line) and the view stays untouched.
    auto lines = batch.split('\n', Qt::SkipEmptyParts);
    for (auto &line: lines) m_logHeld.push_back(std::move(line));
    const auto limit = static_cast<size_t>(logLineLimit());
    while (m_logHeld.size() > limit) m_logHeld.pop_front();

    if (m_logFollow) {
        releaseHeldLogs();
    } else {
        updateLogStatus();
    }
}

void MainWindow::releaseHeldLogs() {
    // Set first so the range growth from the append below already snaps to the bottom.
    m_logFollow = true;
    if (!m_logHeld.empty()) {
        const bool filtering = !m_logSearch.pattern().isEmpty();
        QString shown;
        for (auto &text: m_logHeld) {
            const bool visible = !filtering || m_logSearch.match(text).hasMatch();
            if (visible) {
                if (!shown.isEmpty()) shown += '\n';
                shown += text;
            }
            m_logLines.push_back({std::move(text), visible});
        }
        m_logHeld.clear();
        appendDocumentLines(qvLogDocument, shown);
        removeLeadingBlocks(qvLogDocument, trimLogLines());
    }
    auto bar = ui->masterLogBrowser->verticalScrollBar();
    bar->setValue(bar->maximum());
    updateLogStatus();
}

int MainWindow::trimLogLines() {
    const auto limit = static_cast<size_t>(logLineLimit());
    int droppedVisible = 0;
    while (m_logLines.size() > limit) {
        if (m_logLines.front().visible) ++droppedVisible;
        m_logLines.pop_front();
    }
    while (m_logHeld.size() > limit) m_logHeld.pop_front();
    return droppedVisible;
}

void MainWindow::rebuildLogView() {
    const bool filtering = !m_logSearch.pattern().isEmpty();
    QString shown;
    for (auto &line: m_logLines) {
        line.visible = !filtering || m_logSearch.match(line.text).hasMatch();
        if (line.visible) {
            if (!shown.isEmpty()) shown += '\n';
            shown += line.text;
        }
    }
    m_logFollow = true;
    qvLogDocument->setPlainText(shown);
    auto bar = ui->masterLogBrowser->verticalScrollBar();
    bar->setValue(bar->maximum());
    updateLogStatus();
}

void MainWindow::applyLogSearch() {
    // A hidden bar keeps its text and toggles but stops filtering.
    const QString text = logFilterButton->isChecked() ? ui->logSearchEdit->text() : QString();
    QRegularExpression search;
    if (!text.isEmpty()) {
        search.setPattern(ui->logSearchRegex->isChecked() ? text : QRegularExpression::escape(text));
        if (!ui->logSearchCase->isChecked()) search.setPatternOptions(QRegularExpression::CaseInsensitiveOption);
        if (!search.isValid()) {
            // Keep the last valid filter rather than blanking the view while a pattern is half-typed.
            ui->logSearchEdit->setStyleSheet(QStringLiteral("QLineEdit { color: #e05252; }"));
            ui->logSearchEdit->setToolTip(search.errorString());
            return;
        }
        search.optimize();
    }
    ui->logSearchEdit->setStyleSheet({});
    ui->logSearchEdit->setToolTip({});
    if (search == m_logSearch) return;

    m_logSearch = search;
    if (logHighlighter) logHighlighter->setSearchPattern(m_logSearch);
    // A new filter jumps to the newest results, so fold in whatever was held.
    for (auto &held: m_logHeld) m_logLines.push_back({std::move(held), true});
    m_logHeld.clear();
    trimLogLines();
    rebuildLogView();
}

void MainWindow::setLogFilterVisible(bool visible) {
    ui->logToolbar->setVisible(visible);
    logFilterButton->setToolTip(visible ? tr("Disable Filter") : tr("Enable Filter"));
    if (visible) ui->logSearchEdit->setFocus(Qt::OtherFocusReason);
    m_logSearchDebounce->stop();
    applyLogSearch();
}

void MainWindow::updateLogStatus() {
    if (m_logSearch.pattern().isEmpty()) {
        ui->logMatchCount->clear();
    } else {
        const auto matched = std::count_if(m_logLines.begin(), m_logLines.end(), [](const LogLine &line) { return line.visible; });
        ui->logMatchCount->setText(QStringLiteral("%1 / %2").arg(matched).arg(m_logLines.size()));
    }
    logJumpLatestButton->setVisible(!m_logFollow);
    logJumpLatestButton->setText(m_logHeld.empty() ? tr("Jump to latest") : tr("Jump to latest (%1 new)").arg(m_logHeld.size()));
}

void MainWindow::clear_log_view() {
    {
        // Without this the batch already in flight lands right after the clear.
        QMutexLocker pendingLocker(&logPendingMutex);
        logPendingText.clear();
    }
    qvLogDocument->clear();
    ui->masterLogBrowser->clear();
}

bool MainWindow::should_print_log(const QString &log, const LogFilter &filter) {
    if (QStringView(log).trimmed().isEmpty()) return false;
    // Lines without a level are ours, and are never hidden by the level selector.
    if (const auto rank = Configs::SingBox::LogLineRank(log); rank >= 0 && rank < filter.minLevelRank) return false;
    bool result = true;
    if (filter.enableInclude) {
        result = false;
        for (const auto &includeKeyword: filter.includeKeywords) {
            if (log.contains(includeKeyword)) {
                result = true;
                break;
            }
        }
        if (!result && !filter.includeCombined.pattern().isEmpty() && filter.includeCombined.match(log).hasMatch()) {
            result = true;
        }
    }
    if (result && filter.enableExclude) {
        for (const auto &excludeKeyword: filter.excludeKeywords) {
            if (log.contains(excludeKeyword)) {
                result = false;
                break;
            }
        }
        if (result && !filter.excludeCombined.pattern().isEmpty() && filter.excludeCombined.match(log).hasMatch()) {
            result = false;
        }
    }
    return result;
}

void MainWindow::on_masterLogBrowser_customContextMenuRequested(const QPoint &pos) {
    QMenu *menu = ui->masterLogBrowser->createStandardContextMenu();

    auto sep = new QAction(this);
    sep->setSeparator(true);
    menu->addAction(sep);

    auto action_clear = new QAction(this);
    action_clear->setText(tr("Clear"));
    connect(action_clear, &QAction::triggered, this, [=, this] {
        {
            // Otherwise a flush already in flight repaints what was just cleared.
            QMutexLocker pendingLocker(&logPendingMutex);
            logPendingText.clear();
        }
        m_logLines.clear();
        m_logHeld.clear();
        qvLogDocument->clear();
        m_logFollow = true;
        updateLogStatus();
    });
    menu->addAction(action_clear);

    menu->exec(ui->masterLogBrowser->viewport()->mapToGlobal(pos));
}
