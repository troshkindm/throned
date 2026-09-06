#pragma once

class QString;
class QWidget;

namespace UiPreview {

// The widget tree as text: names, boxes, visibility, and whether a label had to
// drop characters to fit. Answers what a screenshot cannot without depending on
// a single rasterised glyph, and its diffs are readable in a review.
QString GeometryReport(QWidget *root);

// Writes that report beside a capture, as the same name with a .json suffix.
void SaveGeometryReport(QWidget *root, const QString &imagePath);

} // namespace UiPreview
