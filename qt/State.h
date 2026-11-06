// Single source of truth for everything the user can change (drives UI, preview, export, undo/redo).
#pragma once
#include <QColor>
#include <QJsonArray>
#include <QJsonObject>
#include <QRectF>
#include <QString>
#include <QStringList>

#include "../core/layout.h"

struct State {
    // --- layout (step 2) ---
    QString paper = QStringLiteral("A4");
    bool landscape = true;
    bool autoOrient = true;   // "最省纸" orientation chosen by the engine
    double margin = 0, overlap = 10;   // defaults: 页边距 0 mm, 重叠区 10 mm
    double offX = 0, offY = 0;
    bool singlePage = false, pageNumbers = true, calibBox = true, alignMarks = true, overview = true, keepBlank = false;
    QColor alignColor = QColor("#2563EB");   // 拼接对位标记颜色（默认蓝色）
    double calibCm = 5, lineWidth = 0.1;   // 校准框 5 cm, 线宽 0.1 mm
    bool fileTag = false;     // 页码旁附加文件名（单文件；批量用 BatchOptions::fileTag）
    QStringList blanked;      // pages the user set to blank by hand (labels "行-列"); cleared when the drawing geometry changes
    QColor color = Qt::black;
    bool origColor = true;    // "原色": pen colours defined in the PLT (PC); pens without one print black
    // --- import (step 1) ---
    bool ignoreFrame = true;  // 忽略纸张边框：默认开启
    int rot = 0;              // counter-clockwise quarter turns
    bool mirror = false;
    bool hasSel = false;
    QRectF sel;               // selection in the (transformed) drawing's uv space: mm from top-left, v down
    bool selClip = true;      // true: cut lines at the selection border, false: keep whole lines

    bool operator==(const State& o) const {
        return paper == o.paper && landscape == o.landscape && autoOrient == o.autoOrient && margin == o.margin &&
               overlap == o.overlap && offX == o.offX && offY == o.offY && singlePage == o.singlePage &&
               pageNumbers == o.pageNumbers && calibBox == o.calibBox && alignMarks == o.alignMarks &&
               overview == o.overview && keepBlank == o.keepBlank && calibCm == o.calibCm && lineWidth == o.lineWidth &&
               fileTag == o.fileTag && blanked == o.blanked && color == o.color && alignColor == o.alignColor && origColor == o.origColor && ignoreFrame == o.ignoreFrame && rot == o.rot &&
               mirror == o.mirror && hasSel == o.hasSel && sel == o.sel && selClip == o.selClip;
    }
    bool operator!=(const State& o) const { return !(*this == o); }
    // Fields that change the drawing geometry (re-run the import pipeline).
    bool sameGeometryAs(const State& o) const {
        return ignoreFrame == o.ignoreFrame && rot == o.rot && mirror == o.mirror;
    }
    bool sameSelectionAs(const State& o) const { return hasSel == o.hasSel && sel == o.sel && selClip == o.selClip; }
};

inline layout::Params toParams(const State& s) {
    layout::Params p;
    p.paper = layout::paperByName(s.paper.toStdString());
    p.orient = s.landscape ? layout::Orient::Landscape : layout::Orient::Portrait;
    p.margin = s.margin;
    p.overlap = s.overlap;
    p.offX = s.offX;
    p.offY = s.offY;
    p.singlePage = s.singlePage;
    p.pageNumbers = s.pageNumbers;
    p.calibBox = s.calibBox;
    p.calibSize = s.calibCm * 10;
    p.alignMarks = s.alignMarks;
    p.overview = s.overview;
    p.skipEmpty = !s.keepBlank;
    for (const QString& b : s.blanked) p.blankLabels.push_back(b.toStdString());
    p.lineWidth = s.lineWidth;
    p.useOriginalColors = s.origColor;
    const QColor c = s.origColor ? QColor(Qt::black) : s.color;   // black = colour of pens without a definition
    p.lineR = c.redF(); p.lineG = c.greenF(); p.lineB = c.blueF();
    p.alignR = s.alignColor.redF(); p.alignG = s.alignColor.greenF(); p.alignB = s.alignColor.blueF();
    return p;
}

// ---- (de)serialisation: parameter templates and the batch session file ----
inline QJsonObject stateToJson(const State& s) {
    QJsonObject o;
    o["paper"] = s.paper; o["landscape"] = s.landscape; o["autoOrient"] = s.autoOrient;
    o["margin"] = s.margin; o["overlap"] = s.overlap; o["offX"] = s.offX; o["offY"] = s.offY;
    o["singlePage"] = s.singlePage; o["pageNumbers"] = s.pageNumbers; o["calibBox"] = s.calibBox;
    o["alignMarks"] = s.alignMarks; o["overview"] = s.overview; o["keepBlank"] = s.keepBlank;
    o["calibCm"] = s.calibCm; o["lineWidth"] = s.lineWidth; o["fileTag"] = s.fileTag;
    o["blanked"] = QJsonArray::fromStringList(s.blanked);
    o["color"] = s.color.name(); o["alignColor"] = s.alignColor.name(); o["origColor"] = s.origColor;
    o["ignoreFrame"] = s.ignoreFrame; o["rot"] = s.rot; o["mirror"] = s.mirror;
    o["hasSel"] = s.hasSel; o["selClip"] = s.selClip;
    o["sel"] = QJsonArray{s.sel.x(), s.sel.y(), s.sel.width(), s.sel.height()};
    return o;
}

inline State stateFromJson(const QJsonObject& o) {
    State s;
    auto str = [&](const char* k, const QString& d) { return o.contains(k) ? o[k].toString(d) : d; };
    auto num = [&](const char* k, double d) { return o.contains(k) ? o[k].toDouble(d) : d; };
    auto boo = [&](const char* k, bool d) { return o.contains(k) ? o[k].toBool(d) : d; };
    s.paper = str("paper", s.paper);
    if (s.paper != "A0" && s.paper != "A3" && s.paper != "A4") s.paper = "A4";
    s.landscape = boo("landscape", s.landscape); s.autoOrient = boo("autoOrient", s.autoOrient);
    s.margin = num("margin", s.margin); s.overlap = num("overlap", s.overlap);
    s.offX = std::max(0.0, num("offX", 0)); s.offY = std::max(0.0, num("offY", 0));
    s.singlePage = boo("singlePage", s.singlePage); s.pageNumbers = boo("pageNumbers", s.pageNumbers);
    s.calibBox = boo("calibBox", s.calibBox); s.alignMarks = boo("alignMarks", s.alignMarks);
    s.overview = boo("overview", s.overview); s.keepBlank = boo("keepBlank", s.keepBlank);
    s.calibCm = num("calibCm", s.calibCm); s.lineWidth = num("lineWidth", s.lineWidth);
    QColor c(str("color", s.color.name())); if (c.isValid()) s.color = c;
    QColor a(str("alignColor", s.alignColor.name())); if (a.isValid()) s.alignColor = a;
    s.origColor = boo("origColor", s.origColor);
    s.fileTag = boo("fileTag", false);
    for (const auto& v : o["blanked"].toArray()) if (v.isString()) s.blanked << v.toString();
    s.ignoreFrame = boo("ignoreFrame", s.ignoreFrame); s.rot = ((o["rot"].toInt(0) % 4) + 4) % 4; s.mirror = boo("mirror", false);
    s.hasSel = boo("hasSel", false); s.selClip = boo("selClip", true);
    const QJsonArray r = o["sel"].toArray();
    if (r.size() == 4) s.sel = QRectF(r[0].toDouble(), r[1].toDouble(), r[2].toDouble(), r[3].toDouble());
    if (!s.sel.isValid()) s.hasSel = false;
    return s;
}
