// "一键添加水印": our own watermark, added to finished PDFs (after watermark removal or after PLT->PDF).
// The text is turned into outlines here (Qt fonts) and handed to wm::addWatermark, which writes it into the page content
// in a form PDF editors do not recognise as a watermark (see wm.h).
#pragma once
#include <QColor>
#include <QDialog>
#include <QString>
#include <QStringList>

#include "../core/wm/wm.h"

class QCheckBox;
class QDoubleSpinBox;
class QFontComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QSpinBox;
class QPushButton;
class QWidget;

struct StampSettings {
    QString text;              // one or more lines
    QString family;            // empty: a CJK system font
    double sizePt = 18;        // font size in points
    QColor color = QColor(214, 64, 84);
    int opacity = 25;          // %
    double angle = 30;         // degrees, counter-clockwise
    double gapMm = 25;         // space between stamps
    QString suffix = QStringLiteral("_水印");   // output: <name><suffix>.pdf next to the input (empty: overwrite the input)
    bool askEveryTime = true;  // false: the button adds the watermark straight away with these settings

    static StampSettings load();
    void save() const;
    bool valid() const { return !text.trimmed().isEmpty(); }
};

// Text -> outlines (one piece per character, so the stamp is scattered in many small parts).
wm::StampSpec buildStampSpec(const StampSettings& s);
QString stampOutputPath(const QString& input, const StampSettings& s);

class StampDialog : public QDialog {
    Q_OBJECT
public:
    explicit StampDialog(int fileCount, QWidget* parent = nullptr);
    StampSettings settings() const;

private:
    void updatePreview();
    QPlainTextEdit* text_ = nullptr;
    QFontComboBox* font_ = nullptr;
    QDoubleSpinBox *size_ = nullptr, *angle_ = nullptr, *gap_ = nullptr;
    QSpinBox* opacity_ = nullptr;
    QPushButton* color_ = nullptr;
    QLineEdit* suffix_ = nullptr;
    QCheckBox* direct_ = nullptr;
    QLabel* preview_ = nullptr;
    QColor col_;
};

// Shows the dialog when needed, then stamps `files` in the background with a progress dialog.  Returns the files written
// (empty when cancelled).  `parent` gets the summary message.
QStringList runStampFlow(const QStringList& files, QWidget* parent, bool forceDialog = false);
