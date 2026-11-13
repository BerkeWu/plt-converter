#include "BatchCore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPainter>
#include <QStandardPaths>
#include <QThread>
#include <chrono>
#include <cmath>
#include <thread>

#include "PdfExporterQt.h"
#include "../core/docops.h"
#include "../core/naming.h"

QString friendlyError(const QString& m) {
    if (m.contains("no drawable lines")) return QObject::tr("文件中没有可绘制的线条（0 条线）。");
    if (m.contains("no commands found")) return QObject::tr("这不是有效的 PLT（HP-GL）文件，无法识别其中的绘图指令。");
    if (m.contains("margin/overlap")) return QObject::tr("页边距或重叠区过大，当前纸张无可打印区域，请调小。");
    if (m.contains("cannot open")) return QObject::tr("源文件不可访问（已被移动、删除、占用或没有读取权限）。");
    return m;
}

ParseResult parseJob(const QString& path) {
    ParseResult r;
    try {
        QFile f(path);  // QFile handles Unicode paths on every platform (std::ifstream would not on Windows)
        if (!f.open(QIODevice::ReadOnly)) throw std::runtime_error("cannot open file");
        r.size = f.size();
        if (r.size == 0) { r.err = QObject::tr("文件为空（0 字节）。"); return r; }
        const QByteArray data = f.readAll();
        r.doc = std::make_shared<plt::Document>(plt::parseString(std::string(data.constData(), size_t(data.size()))));
        r.frameIdx = plt::findFrame(*r.doc);
    } catch (const std::exception& e) {
        r.err = friendlyError(QString::fromUtf8(e.what()));
        r.doc.reset();
    }
    return r;
}

std::shared_ptr<plt::Document> buildFullDoc(const plt::Document& raw, const State& s, int* frameRemoved) {
    auto d = std::make_shared<plt::Document>(raw);
    const int removed = s.ignoreFrame ? plt::stripFrame(*d) : 0;
    if (frameRemoved) *frameRemoved = removed;
    docops::transform(*d, s.rot, s.mirror);
    return d;
}

std::shared_ptr<plt::Document> cropWorkDoc(const std::shared_ptr<plt::Document>& full, const State& s) {
    if (!s.hasSel) return full;
    const double bx = full->bounds.minX, by = full->bounds.maxY;
    const QRectF r = s.sel;
    plt::Document c = docops::crop(*full, bx + r.left(), by - r.bottom(), bx + r.right(), by - r.top(), s.selClip);
    if (c.lines.empty()) return nullptr;
    return std::make_shared<plt::Document>(std::move(c));
}

QString fileTagFor(const QString& path) {
    QString s = QFileInfo(path).completeBaseName();
    if (s.size() > 20) s = s.left(19) + QStringLiteral("…");
    return s;
}

layout::Params batchParams(const State& s, const BatchOptions& o, int queueIndex, const QString& path) {
    layout::Params p = toParams(s);
    if (o.calibFirstOnly && queueIndex > 0) p.calibBox = false;
    if (o.fileTag && !path.isEmpty()) p.fileTag = fileTagFor(path).toUtf8().toStdString();
    return p;
}

ScanResult scanFile(int id, const QString& path) {
    ScanResult r;
    r.id = id;
    ParseResult pr = parseJob(path);
    r.size = pr.size;
    if (!pr.doc) { r.err = pr.err.isEmpty() ? QObject::tr("文件无法解析。") : pr.err; return r; }
    const plt::Document& d = *pr.doc;
    r.w = d.bounds.w(); r.h = d.bounds.h();
    r.lines = int(d.lines.size());
    r.segments = qint64(d.segmentCount);
    r.frameIdx = pr.frameIdx;
    // thumbnail
    const int W = 96, H = 72;
    QImage img(W, H, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::white);
    QPainter p(&img);
    const double sc = std::min((W - 8) / std::max(r.w, 1e-6), (H - 8) / std::max(r.h, 1e-6));
    const double ox = (W - r.w * sc) / 2, oy = (H - r.h * sc) / 2;
    QPen pen(QColor("#0B5E57"));
    pen.setWidthF(1.0);
    p.setPen(pen);
    QPolygonF poly;
    for (const auto& pl : d.lines) {
        poly.clear();
        for (const auto& q : pl.pts) poly << QPointF(ox + (q.x - d.bounds.minX) * sc, oy + (d.bounds.maxY - q.y) * sc);
        p.drawPolyline(poly);
    }
    p.setPen(QPen(QColor("#C3CECD"), 1));
    p.setBrush(Qt::NoBrush);
    p.drawRect(img.rect().adjusted(0, 0, -1, -1));
    p.end();
    r.thumb = img;
    return r;
}

int estimatePages(double w, double h, const State& s) {
    if (s.singlePage) return 1;
    int best = 1 << 30;
    for (bool land : {false, true}) {
        State t = s;
        t.landscape = land;
        const layout::TileGeom g = layout::tileGeometry(toParams(t), w, h);
        if (!g.ok) continue;
        best = std::min(best, g.nx * g.ny);
    }
    return best == (1 << 30) ? 0 : best + (s.overview ? 1 : 0);
}

AutoResult autoFromDoc(int id, const plt::Document& work, State st, const BatchOptions& o, int queueIndex, const QString& path) {
    AutoResult r;
    r.id = id;
    try {
        layout::Params p = batchParams(st, o, queueIndex, path);
        p.offX = p.offY = 0;
        if (st.autoOrient && !st.singlePage) {
            const layout::OrientChoice oc = layout::chooseOrientation(work, p);
            st.landscape = oc.best == layout::Orient::Landscape;
            p.orient = oc.best;
        }
        st.offX = st.offY = 0;
        if (o.autoPosition && !st.singlePage) {
            const layout::OffsetChoice oc = layout::autoOffset(work, p);
            st.offX = oc.x; st.offY = oc.y;
        }
        p = batchParams(st, o, queueIndex, path);
        p.orient = st.landscape ? layout::Orient::Landscape : layout::Orient::Portrait;
        const layout::Plan plan = layout::buildPlan(work, p);
        r.pages = int(plan.pages.size());
        r.hint = !st.singlePage && plan.gridPages >= 2 && layout::shiftHint(work, p).found;
        r.state = st;
    } catch (const std::exception& e) {
        r.err = friendlyError(QString::fromUtf8(e.what()));
    }
    return r;
}

AutoResult computeAuto(int id, const QString& path, State st, const BatchOptions& o, int queueIndex) {
    AutoResult r;
    r.id = id;
    ParseResult pr = parseJob(path);
    if (!pr.doc) { r.err = pr.err.isEmpty() ? QObject::tr("文件无法解析。") : pr.err; return r; }
    auto full = buildFullDoc(*pr.doc, st);
    auto work = cropWorkDoc(full, st);
    if (!work) { r.err = QObject::tr("选区内没有任何线条。"); return r; }
    return autoFromDoc(id, *work, st, o, queueIndex, path);
}

ExportResult exportOne(int id, const QString& srcPath, const State& st, const layout::Params& pin, const QString& outPath, bool big,
                       ExportControl& ctl, const std::function<void(int, int)>& onProgress) {
    ExportResult r;
    r.id = id;
    r.outPath = outPath;
    const auto t0 = std::chrono::steady_clock::now();
    auto waitIfPaused = [&] { while (ctl.paused.load() && !ctl.cancel.load()) std::this_thread::sleep_for(std::chrono::milliseconds(50)); };
    waitIfPaused();
    if (ctl.cancel.load()) { r.cancelled = true; return r; }
    // large files run alone (PRD 4.6), everything else may run side by side
    std::shared_lock<std::shared_mutex> shared(ctl.bigLock, std::defer_lock);
    std::unique_lock<std::shared_mutex> exclusive(ctl.bigLock, std::defer_lock);
    if (big) exclusive.lock(); else shared.lock();
    waitIfPaused();
    if (ctl.cancel.load()) { r.cancelled = true; return r; }
    ParseResult pr = parseJob(srcPath);
    if (!pr.doc) { r.err = pr.err.isEmpty() ? QObject::tr("文件无法解析。") : pr.err; return r; }
    try {
        auto full = buildFullDoc(*pr.doc, st);
        pr.doc.reset();
        auto work = cropWorkDoc(full, st);
        if (!work) { r.err = QObject::tr("选区内没有任何线条。"); return r; }
        full.reset();
        layout::Params p = pin;
        layout::Plan plan = layout::buildPlan(*work, p);
        r.pages = int(plan.pages.size());
        r.areaM2 = r.pages * plan.pageW * plan.pageH / 1e6;
        work.reset();
        QString err = PdfExporterQt::write(plan, outPath, [&](int i, int n) {
            waitIfPaused();
            if (onProgress) onProgress(i, n);
            return !ctl.cancel.load();
        });
        if (err == QStringLiteral("已取消")) r.cancelled = true;
        else r.err = err;
    } catch (const std::exception& e) {
        r.err = friendlyError(QString::fromUtf8(e.what()));
    }
    r.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    return r;
}

QString allocateOutputPath(QSet<QString>& taken, const QString& dir, const QString& stem, const State& s) {
    QDir d(dir);
    auto key = [&](const std::string& n) { return QDir::cleanPath(d.filePath(QString::fromUtf8(n.c_str()))).toLower(); };
    auto exists = [&](const std::string& n) { return taken.contains(key(n)) || QFileInfo::exists(d.filePath(QString::fromUtf8(n.c_str()))); };
    auto make = [&](const QString& st) {
        const std::string u = st.toUtf8().toStdString();
        return s.singlePage ? naming::uniquePdfNameTagged(u, "单页", exists)
                            : naming::uniquePdfName(u, s.paper.toStdString(), s.landscape ? layout::Orient::Landscape : layout::Orient::Portrait, exists);
    };
    QString stemUse = stem;
    std::string name = make(stemUse);
    if (d.filePath(QString::fromUtf8(name.c_str())).size() > 240) {  // PRD 4.5.1: cut the original name, keep the suffix
        const int over = d.filePath(QString::fromUtf8(name.c_str())).size() - 240;
        stemUse = stem.left(std::max(8, int(stem.size()) - over));
        name = make(stemUse);
    }
    taken.insert(key(name));
    return d.filePath(QString::fromUtf8(name.c_str()));
}

QString reportCsv(const QVector<ReportRow>& rows) {
    auto q = [](QString s) { s.replace('"', "\"\""); return '"' + s + '"'; };
    QString out = QStringLiteral("\uFEFF文件名,纸张,方向,页数,状态,失败原因,输出文件\r\n");
    for (const auto& r : rows)
        out += q(r.name) + ',' + q(r.paper) + ',' + q(r.orient) + ',' + QString::number(r.pages) + ',' + q(r.status) + ',' + q(r.reason) + ',' + q(r.output) + "\r\n";
    return out;
}

QString batchSessionPath() {
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return QDir(dir).filePath("batch_session.json");
}

QByteArray sessionToJson(const State& tmpl, const BatchOptions& o, const QVector<std::shared_ptr<BatchItem>>& items) {
    QJsonObject root;
    root["version"] = 1;
    root["template"] = stateToJson(tmpl);
    QJsonObject oo;
    oo["calibFirstOnly"] = o.calibFirstOnly; oo["fileTag"] = o.fileTag; oo["autoPosition"] = o.autoPosition;
    oo["outDir"] = o.outDir; oo["concurrency"] = o.concurrency;
    root["options"] = oo;
    QJsonArray arr;
    for (const auto& it : items) {
        QJsonObject j;
        j["path"] = it->path;
        j["state"] = stateToJson(it->state);
        j["autoState"] = stateToJson(it->autoState);
        j["haveAuto"] = it->haveAuto;
        j["confirmed"] = it->confirmed;
        j["adjusted"] = it->adjusted;
        j["pages"] = it->pages; j["autoPages"] = it->autoPages; j["hasHint"] = it->hasHint;
        arr.append(j);
    }
    root["items"] = arr;
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

bool sessionFromJson(const QByteArray& data, State& tmpl, BatchOptions& o, QVector<std::shared_ptr<BatchItem>>& items) {
    QJsonParseError e;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &e);
    if (e.error != QJsonParseError::NoError || !doc.isObject()) return false;
    const QJsonObject root = doc.object();
    tmpl = stateFromJson(root["template"].toObject());
    const QJsonObject oo = root["options"].toObject();
    o.calibFirstOnly = oo["calibFirstOnly"].toBool(false); o.fileTag = oo["fileTag"].toBool(false);
    o.autoPosition = oo["autoPosition"].toBool(false); o.outDir = oo["outDir"].toString();
    o.concurrency = std::clamp(oo["concurrency"].toInt(2), 1, 4);
    items.clear();
    int id = 1;
    for (const auto& v : root["items"].toArray()) {
        const QJsonObject j = v.toObject();
        auto it = std::make_shared<BatchItem>();
        it->id = id++;
        it->path = j["path"].toString();
        if (it->path.isEmpty()) continue;
        it->state = stateFromJson(j["state"].toObject());
        it->autoState = stateFromJson(j["autoState"].toObject());
        it->haveAuto = j["haveAuto"].toBool(false);
        it->confirmed = j["confirmed"].toBool(false);
        it->adjusted = j["adjusted"].toBool(false);
        it->pages = j["pages"].toInt(0); it->autoPages = j["autoPages"].toInt(0); it->hasHint = j["hasHint"].toBool(false);
        it->restoredState = true;
        items.push_back(it);
        if (items.size() >= kBatchLimit) break;
    }
    return !items.isEmpty();
}
