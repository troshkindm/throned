#pragma once

#include <QPlainTextEdit>
#include <QSet>
#include <memory>

#include "include/ui/widget/json/JsonValidator.h"

class QSyntaxHighlighter;
class QTimer;

namespace JsonEdit {
class JsonCodeEdit : public QPlainTextEdit {
    Q_OBJECT

public:
    explicit JsonCodeEdit(QWidget* parent = nullptr);

    void setValidator(std::shared_ptr<Validator> validator);

    [[nodiscard]] const QList<Issue>& issues() const { return m_issues; }
    [[nodiscard]] bool hasErrors() const;
    [[nodiscard]] QString statusText() const;

    bool formatDocument();
    void goToOffset(int offset);

    [[nodiscard]] int gutterWidth() const;
    void paintGutter(QPaintEvent* event);

signals:
    void issuesChanged();

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void changeEvent(QEvent* event) override;
    bool event(QEvent* event) override;

private:
    void revalidate();
    void refreshDecorations();
    void applyTheme();
    bool handleAutoEdit(QKeyEvent* event);
    [[nodiscard]] QChar charBeforeCursor() const;
    [[nodiscard]] QChar charAfterCursor() const;

    QWidget* m_gutter = nullptr;
    QSyntaxHighlighter* m_highlighter = nullptr;
    QTimer* m_debounce = nullptr;
    std::shared_ptr<Validator> m_validator;
    QList<Issue> m_issues;
    QSet<int> m_errorBlocks;
    QSet<int> m_warningBlocks;
};
} // namespace JsonEdit
