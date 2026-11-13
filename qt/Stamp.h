// "一键添加水印": our own watermark, added to finished PDFs (after watermark removal or after PLT->PDF), and as its own
// top-level mode ("加水印") for stamping PDFs directly. The text is turned into outlines here (Qt fonts) and handed to
// wm::addWatermark, which writes it into the page content in a form PDF editors do not recognise as a watermark (see wm.h).
#pragma once
#include <QColor>
#include <QDialog>
#include <QString>
#include <QStringList>
#include <QWidget>

#include "../core/wm/wm.h"

class QCheckBox;
class QDoubleSpinBox;
class QFontComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QSpinBox;

// Density presets for the space between stamps: a shorthand for "gapXMm = gapYMm = ..." that the three preset buttons
// set in one click; dragging either spin box independently is "自定义" and keeps whatever two values result.
namespace StampGap {
constexpr double kSparseMm = 45, kMediumMm = 25, kDenseMm = 10;
}

struct StampSettings {
    QString text;              // one or more lines
    QString family;            // empty: a CJK system font
    double sizePt = 18;        // font size in points
    QColor color = QColor(214, 64, 84);
    int opacity = 25;          // %
    double angle = 30;         // degrees, counter-clockwise
    double gapXMm = StampGap::kMediumMm;   // horizontal space between stamps
    double gapYMm = StampGap::kMediumMm;   // vertical space between stamps
    QString suffix = QStringLiteral("_水印");   // output: <name><suffix>.pdf next to the input (empty: overwrite the input)
    bool askEveryTime = true;  // false: the button adds the watermark straight away with these settings

    static StampSettings load();
    void save() const;
    bool valid() const { return !text.trimmed().isEmpty(); }
};

// Text -> outlines (one piece per character, so the stamp is scattered in many small parts).
wm::StampSpec buildStampSpec(const StampSettings& s);
QString stampOutputPath(const QString& input, const StampSettings& s);

// The settings form (text / font / size / color / opacity / angle / density preset + X·Y gap / output suffix) with a live
// Multiply preview. Shared by the modal StampDialog (used from "添加水印" buttons elsewhere) and StampPanel (the
// standalone "加水印" mode), so the two stay in sync without duplicating the form.
class StampForm : public QWidget {
    Q_OBJECT
public:
    explicit StampForm(QWidget* parent = nullptr);
    StampSettings settings() const;
    void setSettings(const StampSettings& s);
signals:
    void changed();
private:
    void updatePreview();
    void setGapPreset(double mm);
    QPlainTextEdit* text_ = nullptr;
    QFontComboBox* font_ = nullptr;
    QDoubleSpinBox *size_ = nullptr, *angle_ = nullptr, *gapX_ = nullptr, *gapY_ = nullptr;
    QSpinBox* opacity_ = nullptr;
    QPushButton *color_ = nullptr, *gapSparse_ = nullptr, *gapMedium_ = nullptr, *gapDense_ = nullptr;
    QLineEdit* suffix_ = nullptr;
    QLabel* preview_ = nullptr;
    QColor col_;
};

class StampDialog : public QDialog {
    Q_OBJECT
public:
    explicit StampDialog(int fileCount, QWidget* parent = nullptr);
    StampSettings settings() const;
private:
    StampForm* form_ = nullptr;
    QCheckBox* direct_ = nullptr;
};

// Shows the dialog when needed, then stamps `files` in the background with a progress dialog.  Returns the files written
// (empty when cancelled).  `parent` gets the summary message. Used from the "添加水印" buttons in the other modes
// (去水印「运行与报告」页、PLT→PDF 单文件/批量导出完成后), where a modal one-off flow fits better than a standing panel.
QStringList runStampFlow(const QStringList& files, QWidget* parent, bool forceDialog = false);

// The standalone "加水印" top-level mode: its own file list (drag & drop, add/remove/clear) plus an embedded StampForm
// and a run button with progress, so stamping a batch of PDFs does not need going through "去水印" or "PLT→PDF" first.
class StampPanel : public QWidget {
    Q_OBJECT
public:
    explicit StampPanel(QWidget* parent = nullptr);
    int addFiles(const QStringList& paths);     // returns how many new PDFs were added (dedup by canonical path)
    QStringList files() const;
    bool clearQueue();                          // false (no-op) while a run is in progress
    bool running() const { return running_; }
    StampForm* form() const { return form_; }    // tests / demos: set the stamp text etc. without going through the UI
protected:
    void dragEnterEvent(QDragEnterEvent*) override;
    void dropEvent(QDropEvent*) override;
signals:
    void done(const QStringList& written);      // after a run finishes (even if some files failed)
private:
    void refreshList();
    void startRun();
    QListWidget* list_ = nullptr;
    StampForm* form_ = nullptr;
    QPushButton *addBtn_ = nullptr, *removeBtn_ = nullptr, *clearBtn_ = nullptr, *runBtn_ = nullptr;
    QProgressBar* progress_ = nullptr;
    QLabel* status_ = nullptr;
    QStringList files_;
    bool running_ = false;
};
