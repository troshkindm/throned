#pragma once

#include <QDialog>
#include <QJsonObject>
#include <memory>

#include "include/ui/widget/json/JsonValidator.h"

class QLabel;

namespace JsonEdit {
class JsonCodeEdit;
class JsonIssueList;

// OpenEditor() returns an empty object when the document was cleared, the original one when cancelled.
class JsonEditorDialog : public QDialog {
    Q_OBJECT

public:
    explicit JsonEditorDialog(const QJsonObject& root, QWidget* parent = nullptr);

    void SetValidator(std::shared_ptr<Validator> validator);

    QJsonObject OpenEditor();

private:
    QJsonObject m_original;
    JsonCodeEdit* m_editor = nullptr;
    JsonIssueList* m_issues = nullptr;
    QLabel* m_status = nullptr;
};
} // namespace JsonEdit
