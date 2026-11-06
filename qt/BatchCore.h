// Batch conversion engine pieces that do not touch widgets: scanning, automatic layout, single-file export,
// output-name allocation, report and session (de)serialisation.  Everything here is safe to run on worker threads.
#pragma once
#include <QDateTime>
#include <QImage>
#include <QSet>
#include <QString>
#include <QVector>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <shared_mutex>

#include "State.h"
#include "../core/plt.h"

constexpr int kBatchLimit = 20;          // PRD 4.1: queue limit
constexpr int kBigFileSegments = 500000;  // PRD 4.6: "大文件"
constexpr int kBigFilePages = 100;

struct ParseResult {
    std::shared_ptr<plt::Document> doc;
    QString err;
    qint64 size = 0;
    int frameIdx = -1;
};
QString friendlyError(const QString& m);   // maps engine errors to Chinese user text
ParseResult parseJob(const QString& path); // Unicode-safe read + parse

enum class ItemStatus { Scanning, Parsed, ToConfirm, Confirmed, Adjusted, Exporting, Done, Failed, Skipped };

struct BatchOptions {
    bool calibFirstOnly = false;   // 校准框: false = 每个文件第一页, true = 仅批量首个文件
    bool fileTag = false;          // 页码旁附加文件名（≤20 字符）
    bool autoPosition = false;     // 图形位置初始策略: false = 左上对齐, true = 自动优化位置
    QString outDir;                // empty = 源文件所在目录
    int concurrency = 2;           // 导出并发
};

struct BatchItem {
    int id = 0;
    QString path;
    qint64 size = 0;
    // light-weight scan result (the geometry itself is NOT kept)
    double w = 0, h = 0;
    int lines = 0;
    qint64 segments = 0;
    int frameIdx = -1;
    QImage thumb;
    bool suspectDup = false;
    // workflow
    ItemStatus status = ItemStatus::Scanning;
    QString error;                 // scan or export failure reason
    State state;                   // current parameters (template + overrides + manual edits)
    State autoState;               // the automatic plan
    int autoPages = 0, pages = 0;  // PDF pages of the automatic / current plan
    bool hasHint = false;          // a <= 20 mm shift would save pages
    bool confirmed = false, adjusted = false, haveAuto = false;
    bool userEdited = false;       // the user changed something by hand (keeps it out of automatic template updates)
    bool exportFailed = false;     // failed during export (can be retried), as opposed to failing at scan time
    bool restoredState = false;    // state came from a saved session
    QVector<State> undo, redo;     // per-file history (PRD 4.4)
    std::shared_ptr<plt::Document> doc;  // lazily loaded raw geometry (only current + prefetched item)
    // export
    QString outPath;
    int progress = 0;              // 0..100
    bool big() const { return segments > kBigFileSegments || pages > kBigFilePages; }
    bool valid() const { return status != ItemStatus::Failed && status != ItemStatus::Scanning && status != ItemStatus::Skipped; }
};

// ---- document pipeline shared by the editor and the batch exporter ----
std::shared_ptr<plt::Document> buildFullDoc(const plt::Document& raw, const State& s, int* frameRemoved = nullptr);
// Applies the selection. Returns `full` itself when there is none, nullptr when the selection holds no line.
std::shared_ptr<plt::Document> cropWorkDoc(const std::shared_ptr<plt::Document>& full, const State& s);

QString fileTagFor(const QString& path);  // base name, at most 20 characters
layout::Params batchParams(const State& s, const BatchOptions& o, int queueIndex, const QString& path);

struct ScanResult {
    int id = 0;
    QString err;
    qint64 size = 0;
    double w = 0, h = 0;
    int lines = 0;
    qint64 segments = 0;
    int frameIdx = -1;
    QImage thumb;
};
ScanResult scanFile(int id, const QString& path);

struct AutoResult {
    int id = 0;
    QString err;
    State state;       // with orientation / start position chosen
    int pages = 0;
    bool hint = false;
};
AutoResult computeAuto(int id, const QString& path, State st, const BatchOptions& o, int queueIndex);
// Same decision on an already loaded (and already cropped) drawing: orientation, start position, page count, hint.
AutoResult autoFromDoc(int id, const plt::Document& work, State st, const BatchOptions& o, int queueIndex, const QString& path);
int estimatePages(double w, double h, const State& s);  // quick grid estimate from the bounding box (best orientation)

// ---- export ----
struct ExportControl {
    std::atomic<bool> cancel{false};
    std::atomic<bool> paused{false};
    std::shared_mutex bigLock;  // large files take it exclusively, everything else shared
};
struct ExportResult {
    int id = 0;
    QString err;               // empty = success
    bool cancelled = false;
    QString outPath;
    int pages = 0;
    double areaM2 = 0;
    double seconds = 0;
};
ExportResult exportOne(int id, const QString& srcPath, const State& st, const layout::Params& p, const QString& outPath, bool big,
                       ExportControl& ctl, const std::function<void(int, int)>& onProgress);

// Pre-assigns collision-free output paths (PRD 4.5.1). `taken` accumulates the names handed out in this batch.
QString allocateOutputPath(QSet<QString>& taken, const QString& dir, const QString& stem, const State& s);

struct ReportRow { QString name, paper, orient, status, reason, output; int pages = 0; };
QString reportCsv(const QVector<ReportRow>& rows);  // UTF-8 with BOM (opens correctly in Excel)

QString batchSessionPath();
QByteArray sessionToJson(const State& tmpl, const BatchOptions& o, const QVector<std::shared_ptr<BatchItem>>& items);
bool sessionFromJson(const QByteArray& data, State& tmpl, BatchOptions& o, QVector<std::shared_ptr<BatchItem>>& items);
