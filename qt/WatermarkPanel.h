// 水印处理: add up to 20 PDFs -> files are analysed and grouped by watermark type -> one recipe per group (reviewed on a
// sample file with a before/after vector preview) -> run all, with an integrity check and a CSV report.
// A single file is simply a group of one.
#pragma once
#include <QHash>
#include <QPointer>
#include <QSet>
#include <QTemporaryDir>
#include <QVector>
#include <QWidget>
#include <atomic>
#include <functional>
#include <memory>

#include "wm/wm.h"

class DrawView;
class QCheckBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QProgressBar;
class QPushButton;
class QStackedWidget;
class QTabWidget;
class QTableWidget;
class QVBoxLayout;

struct WmFile {
    enum Status { Pending, Waiting, Running, Done, NeedsCheck, Failed, Skipped };
    QString path, name;
    bool analyzed = false, selected = true;
    wm::Report rep;
    int group = -9;            // >= 0 group index (the last one may be the "manual" group), -2 bitmap only, -3 unreadable
    int conformity = 0;
    Status status = Pending;
    QString note;
    wm::FileResult res;
};

class WatermarkPanel : public QWidget {
    Q_OBJECT
public:
    explicit WatermarkPanel(QWidget* parent = nullptr);
    ~WatermarkPanel() override;

    int addFiles(const QStringList& pathsOrFolders);        // returns how many were added (max wm::kMaxBatchFiles in total)
    void goStep(int step);                                  // 0 添加文件, 1 审阅与配方, 2 运行与报告
    int step() const;
    const QVector<WmFile>& files() const { return files_; }
    int groupCount() const { return nGroups_; }
    bool hasManualGroup() const { return manual_ >= 0; }
    wm::Recipe& recipe(int group) { return recipes_[group]; }
    int currentGroup() const { return curGroup_; }
    void selectGroup(int g);
    void setFileSelected(int fileIndex, bool on);
    void setSample(int fileIndex);
    void setPage(int page);
    bool busy() const { return jobs_ > 0 || running_; }
    QString outputDir() const { return outDir_; }
    void setOutputDir(const QString& d);
    // text / shape recipe editing (also used by the UI)
    void addNeedle(const QString& text);
    void removeNeedle(int i);
    void boxShape(double x0, double y0, double x1, double y1);   // as if the user dragged a box on the preview (page points, top-left origin)
    bool commitShape(const QString& name);                       // add the boxed shape to the current group's recipe
    QString shapeStatus() const;
    DrawView* view() const { return view_; }
    // run
    void startRun(int maxFiles = 0);                         // maxFiles > 0: only the first N files of the current group (试跑)
    void cancelRun() { cancel_ = true; }
    bool running() const { return running_; }
    bool writeReport(const QString& csvPath) const;
    QString summaryText() const;
    void setSilent(bool on) { silent_ = on; }

signals:
    void leaveRequested(bool batchConversion);              // user switched to 单文件 / 批量转换
    void filesAnalyzed();
    void runFinished();

protected:
    void dragEnterEvent(QDragEnterEvent*) override;
    void dropEvent(QDropEvent*) override;

private:
    struct Preview { std::shared_ptr<wm::PageDrawing> before, after; QString key; };
    QWidget* buildTop();
    QWidget* buildAddPage();
    QWidget* buildReviewPage();
    QWidget* buildRunPage();
    QWidget* buildBottom();
    void async(std::function<void()> work, std::function<void()> done);
    void onAnalyzed(int i, const wm::Report& r);
    void regroup();
    void refreshTables();
    void refreshGroups();
    void refreshReview();
    void refreshRecipePanel();
    void refreshPreview();
    void refreshRun();
    void syncChrome();
    void chooseOutput();
    void openOutput();
    void exportReport();
    QString letter(int g) const;
    QColor colorOf(int g) const;
    QString statusText(const WmFile&) const;
    bool recipeHasContent(const wm::Recipe&) const;
    QString uniqueOut(const WmFile&) const;
    std::vector<int> extraFor(int fileIndex) const;

    // ui
    QStackedWidget* pages_ = nullptr;
    QPushButton* stepBtn_[3] = {};
    QPushButton *back_ = nullptr, *next_ = nullptr;
    QLabel* info_ = nullptr;
    QTableWidget* table_ = nullptr;
    QLabel *limitNote_ = nullptr;
    QWidget* groupsHost_ = nullptr;
    QVBoxLayout* groupsLay_ = nullptr;
    QWidget* groupBtnHost_ = nullptr;
    QVBoxLayout* groupBtnLay_ = nullptr;
    QListWidget* sampleList_ = nullptr;
    DrawView* view_ = nullptr;
    QLabel *pageLbl_ = nullptr, *removedLbl_ = nullptr;
    QPushButton *prevPage_ = nullptr, *nextPage_ = nullptr, *modeOrig_ = nullptr, *modeRem_ = nullptr;
    QTabWidget* tabs_ = nullptr;
    QCheckBox* autoChk_ = nullptr;
    QListWidget* findings_ = nullptr;
    QLabel* autoNote_ = nullptr;
    QLineEdit* needleEdit_ = nullptr;
    QListWidget *needleList_ = nullptr, *docTexts_ = nullptr;
    QLabel* needleNote_ = nullptr;
    QLabel *shapeHint_ = nullptr, *shapeLbl_ = nullptr;
    QPushButton* shapeAdd_ = nullptr;
    QListWidget* shapeList_ = nullptr;
    QLabel* outLbl_ = nullptr;
    QPushButton *trial_ = nullptr;
    QProgressBar* runBar_ = nullptr;
    QLabel* runInfo_ = nullptr;
    QTableWidget* runTable_ = nullptr;
    QPushButton *runStart_ = nullptr, *runCancel_ = nullptr, *runCsv_ = nullptr, *runOpen_ = nullptr;

    // data
    QVector<WmFile> files_;
    int nGroups_ = 0, manual_ = -1, curGroup_ = 0, sample_ = -1, page_ = 1;
    std::vector<std::string> sigs_;
    QVector<wm::Recipe> recipes_;
    QHash<int, std::vector<int>> extra_;          // file index -> ticked "需确认" finding indices
    QString outDir_;
    int pendingAnalysis_ = 0;
    int jobs_ = 0;
    bool running_ = false, silent_ = false;
    std::atomic<bool> cancel_{false};
    quint64 previewGen_ = 0;
    QHash<QString, std::shared_ptr<wm::PageDrawing>> beforeCache_;
    QHash<QString, std::shared_ptr<std::vector<wm::TextItem>>> textCache_;
    QTemporaryDir tmp_;
    int tmpCounter_ = 0;
    // pending boxed shape
    wm::ShapeTemplate shape_;
    bool haveShape_ = false;
    QString shapeStatus_;
    int shapeCopies_ = 0;
};
