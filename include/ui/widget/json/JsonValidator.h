#pragma once

#include <QList>
#include <QString>

#include "include/ui/widget/json/JsonTree.h"

namespace JsonEdit {
enum class Severity { Error,
                      Warning };

struct Issue {
    Severity severity = Severity::Error;
    QString message;
    QString pointer;
    Span span;
    // Set when a union rejected the value outright: the wrong variant, not a bad field.
    bool variantMismatch = false;
};

// Type checking is injected into the editor: a null validator leaves syntax checking only.
class Validator {
public:
    virtual ~Validator() = default;
    virtual QList<Issue> Validate(const Value& root) const = 0;
};
} // namespace JsonEdit
