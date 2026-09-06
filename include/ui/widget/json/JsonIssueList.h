#pragma once

#include <QListWidget>

namespace JsonEdit {
class JsonCodeEdit;

class JsonIssueList : public QListWidget {
    Q_OBJECT

public:
    explicit JsonIssueList(QWidget* parent = nullptr);

    void attach(JsonCodeEdit* editor);

private:
    void refresh();

    JsonCodeEdit* m_editor = nullptr;
};
} // namespace JsonEdit
