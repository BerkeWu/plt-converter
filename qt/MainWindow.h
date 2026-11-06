// Single-file workflow: 1 导入 -> 2 排版设置 -> 3 预览导出  (PRD 2.1, 模块一~四)
#pragma once
#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QImage>
#include <QHash>
#include <QMainWindow>
#include <QThreadPool>
#include <QTimer>
#include <functional>
#include <memory>

#include "BatchCore.h"
#include "CanvasWidget.h"
#include "State.h"
#include "../core/plt.h"

class QButtonGroup;
class QComboBox;
class QFrame;
class QSpinBox;
class QToolButton;
class QTreeWidgetItem;
class QueueTree;
class QVBoxLayout;
class QWidget;
class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QProgressBar;
class QPushButton;
class QSlider;
class QStackedWidget;
class QRadioButton;
class Swatch;
class QHBoxLayout;
class ToggleSwitch;
class WatermarkPanel;

struct PageMeta {
    int row = -1, col = -1;
    bool overview = false, empty = false, manual = false;   // manual: set to blank by the user
    double w = 0, h = 0;
    QString label;
};

struct PlanResult {
    quint64 gen = 0;
    QString err;
    bool landscape = true;
    layout::OrientChoice oc{layout::Orient::Portrait, 0, 0};
    bool orientEvaluated = false;
    int nx = 0, ny = 0, effective = 0, blank = 0, gridPages = 0;
    bool calibCollision = false;
    double pageW = 0, pageH = 0;
    QVector<PageMeta> meta;
    QVector<QImage> thumbs;
    layout::ShiftHint hint;
    QString largerPaper;
    int largerGain = 0;
};


// Everything that makes up "the file being edited" (swapped when switching between single-file and batch mode).
struct EditCtx {
    State s;
    QVector<State> undo, redo;
    QString lastKey;
    std::shared_ptr<plt::Document> raw, full, work;
    std::shared_ptr<const DrawingView> fullView, workView;
    QString path;
    qint64 fileSize = 0;
    bool hasFrame = false;
    int frameRemoved = 0;
    QStringList notices;
    QVector<PageMeta> meta;
    QVector<QImage> thumbs;
    PlanResult last;
    bool havePlan = false, dismissLarger = false;
    int step = 0;
};

// Widgets that only exist for batch conversion.
struct BatchUi {
    QPushButton *modeSingle = nullptr, *modeBatch = nullptr;
    // step 1: add files
    QueueTree* table = nullptr;
    QLabel *tableNote = nullptr, *scanNote = nullptr;
    QCheckBox* subfolders = nullptr;
    QPushButton *addFiles = nullptr, *addFolder = nullptr, *removeSel = nullptr, *clearAll = nullptr, *selAll = nullptr, *invSel = nullptr, *moveUp = nullptr, *moveDown = nullptr, *toTemplate = nullptr;
    QProgressBar* scanBar = nullptr;
    // step 2: template extras (inside the layout panel)
    QWidget* optsBox = nullptr;
    QComboBox *calibScope = nullptr, *startPos = nullptr, *templates = nullptr;
    ToggleSwitch* fileTag = nullptr;
    QLabel* outDirLbl = nullptr;
    QPushButton *outDirBtn = nullptr, *tplSave = nullptr, *tplDelete = nullptr;
    QPushButton *layoutBack = nullptr, *layoutNext = nullptr;
    // step 3: auto layout
    QProgressBar* autoBar = nullptr;
    QLabel* autoInfo = nullptr;
    QPushButton *autoRun = nullptr, *autoNext = nullptr;
    // step 4: per-file preview
    QWidget *queueBox = nullptr, *rightHost = nullptr, *leftHost = nullptr, *infoBox = nullptr, *actionBox = nullptr, *toggleBar = nullptr, *bottomBar = nullptr;
    QComboBox* filter = nullptr;
    QListWidget* queueList = nullptr;
    QLabel *posLbl = nullptr, *compare = nullptr, *hintLbl = nullptr, *sumLbl = nullptr, *overrideNote = nullptr;
    QPushButton *prev = nullptr, *next = nullptr, *applyHint = nullptr, *confirm = nullptr, *restoreAuto = nullptr, *restoreTmpl = nullptr, *applyFollow = nullptr, *confirmAll = nullptr, *exportBtn = nullptr, *tabLayout = nullptr, *tabShape = nullptr;
    // step 5: export
    QRadioButton *outSource = nullptr, *outCustom = nullptr;
    QLabel *outDirShow = nullptr, *expSummary = nullptr, *expTime = nullptr;
    QPushButton *outChoose = nullptr, *expStart = nullptr, *expPause = nullptr, *expCancel = nullptr, *expOpen = nullptr, *expCsv = nullptr, *expRetry = nullptr, *expBack = nullptr;
    QSpinBox* concurrency = nullptr;
    QProgressBar* expBar = nullptr;
};

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow();
    void loadFile(const QString& path);
    void goStep(int step);                       // 0 = 导入, 1 = 排版设置, 2 = 预览导出
    void edit(const std::function<void(State&)>& fn, const QString& coalesceKey = {});
    const State& state() const { return s_; }
    QString exportBlocking(const QString& path);  // synchronous export of the checked pages (tests / demo)
    int pageCount() const { return meta_.size(); }
    void undo();
    void redo();
    void setSilent(bool on) { silent_ = on; }   // tests / demos: dialogs answer themselves
    const PlanResult& lastPlan() const { return last_; }
    int frameRemoved() const { return frameRemoved_; }   // lines removed by 「忽略纸张边框」 in the current drawing
    int workLineCount() const { return work_ ? int(work_->lines.size()) : 0; }
    bool planValid() const { return havePlan_; }
    void blankPages(const QVector<int>& rows, bool blank) { setPagesBlank(rows, blank); }   // 手动空白页（预览里的按钮 / 右键菜单走同一入口）

    // ---- batch conversion (PRD module 5) ----
    void setBatchMode(bool on);
    // ---- watermark removal (needs the QPDF-based core; see CMake option PLT_WITH_WM) ----
    void showWatermark(bool on);
    bool watermarkVisible() const;
    WatermarkPanel* watermarkPanel();
    bool batchMode() const { return batch_; }
    void goBatchStep(int step);                          // 0 添加文件, 1 通用参数, 2 自动排版, 3 逐一预览, 4 批量导出
    void addBatchFiles(const QStringList& paths);        // files and/or folders
    const QVector<std::shared_ptr<BatchItem>>& batchItems() const { return items_; }
    BatchOptions& batchOptions() { return opts_; }
    void batchEditTemplate(const std::function<void(State&)>& fn);
    void batchStartAuto() { startAuto(true); }
    void batchConfirmAll() { confirmAllAuto(); }
    void batchStartExport(bool retryOnly = false);
    void batchPause(bool on);
    void batchCancel();
    bool batchExporting() const { return exporting_; }
    int batchStep() const { return bstep_; }
    int maxBatchStepForTest() const { return maxBatchStep(); }
    void batchActivate(int idx) { activateItem(idx); }   // select a file in the per-file preview (tests / demos)
    QString batchReportCsv() const;

signals:
    void loaded(bool ok);
    void planReady();
    void batchScanFinished();
    void batchAutoFinished();
    void batchExportFinished();

protected:
    void dragEnterEvent(QDragEnterEvent*) override;
    void dropEvent(QDropEvent*) override;
    void closeEvent(QCloseEvent*) override;

private:
    QStackedWidget* rootStack_ = nullptr;
    WatermarkPanel* wmPanel_ = nullptr;
    QPushButton* modeWm_ = nullptr;
    // construction
    QWidget* buildTopBar();
    QWidget* buildImportPanel();
    QWidget* buildLayoutPanel();
    QWidget* buildExportPanel();
    QWidget* buildCanvasPage();
    QWidget* buildPreviewPage();
    void applyStyle();
    // state flow
    void applyState(const State& n);
    // ---- batch internals (MainWindowBatch.cpp) ----
    QWidget* buildBatchQueueBox();
    QWidget* buildBatchRightHost();
    QWidget* buildBatchBottomBar();
    QWidget* buildBatchToggleBar();
    void setBatchTab(bool shape);
    void batchAddDialog(bool folder);
    QWidget* buildBatchAddPanel();
    QWidget* buildBatchAutoPanel();
    QWidget* buildBatchExportPanel();
    QWidget* buildBatchTablePage();
    QWidget* buildBatchOptsBox();
    EditCtx grabCtx() const;
    void putCtx(const EditCtx& c);
    void clearCtx();
    int maxBatchStep() const;
    void commitBatchEditing();
    void batchChrome();
    void refreshTable();
    void refreshRow(int idx);
    void refreshQueueList();
    void refreshBatchSummary();
    QString statusText(const BatchItem& it) const;
    void startScan(const std::shared_ptr<BatchItem>& it);
    void onScanned(const ScanResult& r);
    void startAuto(bool all);
    void onAutoResult(const AutoResult& r);
    bool isOverridden(const BatchItem& it, const State& base) const;
    State deriveState(const BatchItem& it, const State& base) const;
    void applyTemplateToItems(const State& oldT, const State& newT, int* keptCustom);
    void convertToBatch();
    void updateRowStatus(int id);
    void onExportResult(const ExportResult& r);
    void activateItem(int idx);
    void finishActivate(int idx);
    void commitCurrentItem();
    void ensureDoc(std::shared_ptr<BatchItem> it, std::function<void()> cb);
    void releaseDocs();
    QVector<int> viewOrder() const;
    void stepItem(int delta);
    void confirmCurrent(bool andNext);
    void confirmAllAuto();
    void restoreAutoPlan();
    void restoreTemplateParams();
    void applyToFollowing();
    void updateBatchInfo();
    void onBatchEdited();
    void refreshStatus(BatchItem& it);
    void syncOptsUi();
    void loadTemplates();
    void saveTemplates();
    void finishExport();
    void exportTick();
    void setItemProgress(int id, int i, int n);
    void offerRestore();
    void saveSession();
    void clearSession();
    bool ask(const QString& title, const QString& text, bool def = true);
    void notify(const QString& title, const QString& text);
    layout::Params currentParams() const;
    void syncUi();
    void rebuildFull();
    void rebuildWork();
    void refreshCanvasView();
    void updateGridLocal();
    void requestPlan();
    void runPlan();
    void onPlanResult(const PlanResult& r);
    void updateTip();
    void updateInfoLabels();
    void updateSummary();
    void rebuildThumbLists();
    void setPagesBlank(const QVector<int>& rows, bool blank);   // 手动空白页：把选中的页设为空白 / 恢复内容
    void updateBlankButtons();
    void pageContextMenu(QListWidget* list, const QPoint& pos);
    // actions
    void chooseFile();
    void onParsed(const ParseResult& r, const QString& path);
    void onExport();
    QString exportWithProgress(const QString& out, const QVector<int>& pages);
    void showExportDone(const QString& out);
    QString defaultOutputName(bool* truncated = nullptr) const;
    QVector<int> checkedPages() const;
    void applyPageRange();
    void updateSelCount();
    void loadSettings();
    void saveSettings();

    State s_;
    QVector<State> undo_, redo_;
    QString lastKey_;
    QElapsedTimer editClock_;
    State dragBase_;
    bool dragPushed_ = false;

    // document pipeline: raw -> (strip frame, rotate, mirror) = full -> (crop) = work
    std::shared_ptr<plt::Document> raw_;
    std::shared_ptr<plt::Document> full_, work_;
    std::shared_ptr<const DrawingView> fullView_, workView_;
    QString path_;
    qint64 fileSize_ = 0;
    bool hasFrame_ = false;
    int frameRemoved_ = 0;
    QStringList notices_;

    // planning
    QTimer planTimer_, saveTimer_;
    quint64 gen_ = 0;
    QVector<PageMeta> meta_;
    QStringList reselect_;                 // page labels to select again after the lists are rebuilt
    QPushButton *blankBtn_ = nullptr, *unblankBtn_ = nullptr;
    QVector<QImage> thumbs_;
    PlanResult last_;
    bool havePlan_ = false;
    bool dismissLarger_ = false;
    int step_ = 0;
    bool parsing_ = false;
    bool silent_ = false;

    // batch state
    bool batch_ = false;
    int bstep_ = 0;
    EditCtx singleCtx_;
    State tmpl_;
    BatchOptions opts_;
    QVector<std::shared_ptr<BatchItem>> items_;
    bool planRunning_ = false, planDirty_ = false;
    int nextId_ = 1, cur_ = -1;
    QThreadPool scanPool_, autoPool_, exportPool_, loadPool_;
    int scanPending_ = 0, autoPending_ = 0, autoTotal_ = 0;
    int viewFilter_ = 0;
    QTimer sessionTimer_, exportTick_;
    std::shared_ptr<ExportControl> ctl_;
    bool exporting_ = false;
    int exportPending_ = 0;
    QElapsedTimer exportClock_;
    QVector<ExportResult> results_;
    QVector<int> exportIds_;
    double pagesTotal_ = 0;
    qint64 exportElapsedMs_ = 0;
    QHash<QString, State> namedTemplates_;
    BatchUi bu_;
    QWidget* sideHolder_ = nullptr;
    State tmplBefore_;
    int resumeCur_ = -1;
    bool refreshingList_ = false;
    bool retryMode_ = false;
    bool haveExportResult_ = false;   // keep the result statistics visible until the next export starts

    // widgets
    QStackedWidget *leftStack_ = nullptr, *centerStack_ = nullptr;
    QPushButton* stepBtn_[5] = {};
    CanvasWidget* canvas_ = nullptr;
    QLabel *fileInfo_ = nullptr, *cursorInfo_ = nullptr, *cvHint_ = nullptr;
    QWidget* thumbStripBox_ = nullptr;
    QListWidget *strip_ = nullptr, *grid_ = nullptr;
    // import panel
    QPushButton *openBtn_ = nullptr, *selBtn_ = nullptr, *clearSelBtn_ = nullptr, *toLayoutBtn_ = nullptr, *resetXformBtn_ = nullptr;
    QProgressBar* busy_ = nullptr;
    QLabel *impInfo_ = nullptr, *impNotes_ = nullptr, *xformNote_ = nullptr, *selInfo_ = nullptr;
    QCheckBox* ignoreFrame_ = nullptr;
    QLabel* frameNote_ = nullptr;
    QRadioButton *clipRadio_ = nullptr, *wholeRadio_ = nullptr;
    // layout panel
    QButtonGroup *paperGrp_ = nullptr, *orientGrp_ = nullptr, *marginGrp_ = nullptr;
    QWidget *paperSeg_ = nullptr, *orientSeg_ = nullptr;
    QLabel *paperNote_ = nullptr, *orientNote_ = nullptr;
    QCheckBox* autoOrient_ = nullptr;
    QSlider* overlapSlider_ = nullptr;
    ToggleSwitch* fileTagSw_ = nullptr;
    QWidget* fileTagRow_ = nullptr;
    QDoubleSpinBox *overlapSpin_ = nullptr, *marginSpin_ = nullptr, *lineWidthSpin_ = nullptr, *calibSpin_ = nullptr, *xSpin_ = nullptr, *ySpin_ = nullptr;
    ToggleSwitch *singleSw_ = nullptr, *pageNoSw_ = nullptr, *calibSw_ = nullptr, *overviewSw_ = nullptr, *alignSw_ = nullptr, *blankSw_ = nullptr;
    QLabel* colorNote_ = nullptr;
    QList<Swatch*> swatches_;  // 0 = 原色, 1..5 preset, 6 = custom
    QList<Swatch*> alignSwatches_;  // 0..4 preset (0 = blue), 5 = custom
    QPushButton *alignTL_ = nullptr, *center_ = nullptr, *autoPos_ = nullptr;
    QWidget* orientRow_ = nullptr;
    // export panel
    QLabel *summary_ = nullptr, *rangeErr_ = nullptr, *selCount_ = nullptr, *nameLbl_ = nullptr;
    QLineEdit* rangeEdit_ = nullptr;
    QPushButton* exportBtn_ = nullptr;
};
