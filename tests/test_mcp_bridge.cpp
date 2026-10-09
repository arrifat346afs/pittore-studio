// test_mcp_bridge.cpp — live-session bridge round trips: ping, document
// listing/info, layer visibility (undo-safe), PNG snapshot, unknown-cmd
// and malformed-line handling. Headless (QCoreApplication + QEventLoop,
// no widgets).
#include <cstdio>

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QTimer>

#include "engine/core/log.h"
#include "test_util.h"
#include "ui/app_state.h"
#include "ui/mcp/mcp_bridge.h"

using namespace pittore::ui;

namespace {

// One bridge round trip over a client socket, pumped through the same
// event loop the server lives on. Empty on timeout.
QJsonObject rpc(McpBridge& bridge, const QJsonObject& req) {
    QLocalSocket sock;
    sock.connectToServer(bridge.socketPath());
    if (!sock.waitForConnected(5000)) return {};
    sock.write(QJsonDocument(req).toJson(QJsonDocument::Compact));
    sock.write("\n", 1);
    sock.flush();
    QByteArray buf;
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&sock, &QLocalSocket::readyRead, [&] {
        buf += sock.readAll();
        if (buf.contains('\n')) loop.quit();
    });
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(5000);
    loop.exec();
    if (!buf.contains('\n')) return {};
    QJsonParseError perr{};
    const QJsonDocument doc =
        QJsonDocument::fromJson(buf.left(buf.indexOf('\n') + 1), &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isObject()) return {};
    return doc.object();
}

QJsonObject call(McpBridge& bridge, int id, const QString& cmd,
                 const QJsonObject& params = {}) {
    return rpc(bridge, QJsonObject{{"id", id},
                                   {"cmd", cmd},
                                   {"params", params}});
}

}  // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const QString logDir = QDir::tempPath() + QStringLiteral("/pittore-mcp-log");
    QDir().mkpath(logDir);
    ::pittore::core::log::set_log_dir(logDir.toStdString().c_str());

    AppState state;
    McpBridge bridge(&state);
    const QString sockPath =
        QDir::tempPath() + QStringLiteral("/pittore-test-mcp.sock");
    QLocalServer::removeServer(sockPath);
    CHECK(bridge.start(sockPath));

    // Ping echoes the id and reports the app.
    {
        const QJsonObject r = call(bridge, 1, QStringLiteral("ping"));
        CHECK(r.value("ok").toBool(false));
        CHECK(r.value("id").toInt(-1) == 1);
        CHECK(r.value("result").toObject().value("app").toString() ==
              QStringLiteral("pittore-studio"));
    }
    // Empty session lists no documents; info refuses without one.
    {
        const QJsonObject r = call(bridge, 2, QStringLiteral("list_documents"));
        CHECK(r.value("ok").toBool(false));
        CHECK(r.value("result").toObject().value("documents").toArray().isEmpty());
        const QJsonObject info = call(bridge, 3, QStringLiteral("document_info"));
        CHECK(!info.value("ok").toBool(true));
    }
    DocumentItem* d = state.addDocument(QStringLiteral("m"), QSize(64, 48), 72);
    CHECK(d != nullptr);
    if (!d) return 1;
    {
        const QJsonObject r = call(bridge, 4, QStringLiteral("list_documents"));
        const QJsonArray docs =
            r.value("result").toObject().value("documents").toArray();
        CHECK(docs.size() == 1);
        CHECK(docs[0].toObject().value("title").toString() ==
              QStringLiteral("m"));
        CHECK(docs[0].toObject().value("width").toInt() == 64);
    }
    {
        const QJsonObject r = call(bridge, 5, QStringLiteral("document_info"));
        CHECK(r.value("ok").toBool(false));
        const QJsonObject info = r.value("result").toObject();
        CHECK(info.value("layers").toArray().size() == d->layers.size());
        CHECK(info.value("layers").toArray()[0].toObject().value("visible").toBool());
    }
    // Visibility toggles through the undo-safe entry point.
    {
        const QJsonObject r = call(bridge, 6, QStringLiteral("set_layer_visible"),
                                   QJsonObject{{"layer", 0}, {"visible", false}});
        CHECK(r.value("ok").toBool(false));
        CHECK(r.value("result").toObject().value("changed").toBool());
        CHECK(!d->layers[0].visible);
        const QJsonObject back = call(bridge, 7, QStringLiteral("set_layer_visible"),
                                      QJsonObject{{"layer", 0}, {"visible", true}});
        CHECK(back.value("result").toObject().value("changed").toBool());
        CHECK(d->layers[0].visible);
        // Out of range is an honest error, not a crash.
        const QJsonObject bad = call(bridge, 8, QStringLiteral("set_layer_visible"),
                                     QJsonObject{{"layer", 9999}, {"visible", true}});
        CHECK(!bad.value("ok").toBool(true));
    }
    // Snapshot is a real PNG (magic bytes after base64).
    {
        const QJsonObject r = call(bridge, 9, QStringLiteral("composite_png"),
                                   QJsonObject{{"maxDim", 32}});
        CHECK(r.value("ok").toBool(false));
        const QJsonObject snap = r.value("result").toObject();
        CHECK(snap.value("width").toInt() == 32);
        const QByteArray raw =
            QByteArray::fromBase64(snap.value("png_base64").toString().toLatin1());
        CHECK(QImage::fromData(raw, "PNG").width() == 32);
    }
    // New document + shape layers + file export.
    {
        const QJsonObject r = call(bridge, 20, QStringLiteral("new_document"),
                                   QJsonObject{{"title", QStringLiteral("t2")},
                                               {"width", 200},
                                               {"height", 100}});
        CHECK(r.value("ok").toBool(false));
        CHECK(r.value("result").toObject().value("width").toInt() == 200);
        CHECK(state.documents().size() == 2);
        const QJsonObject bad = call(bridge, 21, QStringLiteral("new_document"),
                                     QJsonObject{{"width", 0}, {"height", 100}});
        CHECK(!bad.value("ok").toBool(true));
        const QJsonObject sky = call(bridge, 22, QStringLiteral("add_shape"),
                                     QJsonObject{{"shape", QStringLiteral("rectangle")},
                                                 {"rect", QJsonArray{0, 0, 200, 100}},
                                                 {"fill", QStringLiteral("#112233")}});
        CHECK(sky.value("ok").toBool(false));
        DocumentItem* nd = state.activeDocument();
        CHECK(nd != nullptr && nd->title == QStringLiteral("t2"));
        CHECK(nd->layers.size() == 2);  // Background + shape
        const QJsonObject nope = call(bridge, 23, QStringLiteral("add_shape"),
                                      QJsonObject{{"shape", QStringLiteral("star")},
                                                  {"rect", QJsonArray{0, 0, 10, 10}},
                                                  {"fill", QStringLiteral("#ffffff")}});
        CHECK(!nope.value("ok").toBool(true));
        const QJsonObject exp = call(bridge, 24, QStringLiteral("export_png"),
                                     QJsonObject{{"path", logDir + QStringLiteral("/t2.png")}});
        CHECK(exp.value("ok").toBool(false));
        QImage saved;
        CHECK(saved.load(logDir + QStringLiteral("/t2.png")));
        CHECK(saved.width() == 200 && saved.height() == 100);
        // Filters run on the active (just added) layer, one undo step.
        const QJsonObject blur = call(bridge, 25, QStringLiteral("apply_filter"),
                                      QJsonObject{{"filter", QStringLiteral("gaussian_blur")},
                                                  {"params", QJsonArray{2.0}}});
        CHECK(blur.value("ok").toBool(false));
        const QJsonObject bogus = call(bridge, 26, QStringLiteral("apply_filter"),
                                       QJsonObject{{"filter", QStringLiteral("nope")}});
        CHECK(!bogus.value("ok").toBool(true));
        // Blend mode + opacity through their undo-safe paths.
        const QJsonObject blend = call(bridge, 27, QStringLiteral("set_layer_blend"),
                                       QJsonObject{{"layer", 0}, {"mode", QStringLiteral("Multiply")}});
        CHECK(blend.value("ok").toBool(false));
        CHECK(blend.value("result").toObject().value("changed").toBool());
        CHECK(nd->layers[0].blendMode == QStringLiteral("Multiply"));
        const QJsonObject badBlend = call(bridge, 28, QStringLiteral("set_layer_blend"),
                                          QJsonObject{{"layer", 0}, {"mode", QStringLiteral("Nope")}});
        CHECK(!badBlend.value("ok").toBool(true));
        const QJsonObject op = call(bridge, 29, QStringLiteral("set_layer_opacity"),
                                    QJsonObject{{"layer", 0}, {"opacity", 40}});
        CHECK(op.value("ok").toBool(false));
        CHECK(nd->layers[0].opacity == 40);
        // Full surface: filters catalog, undo/redo, selection, text, files.
        // Runs last: open_image switches the active document.
        const QJsonObject cats = call(bridge, 30, QStringLiteral("list_filters"));
        CHECK(cats.value("ok").toBool(false));
        const QJsonArray all =
            cats.value("result").toObject().value("filters").toArray();
        CHECK(!all.isEmpty());
        bool sawClouds = false, sawParams = false;
        for (const QJsonValue& v : all) {
            const QJsonObject f = v.toObject();
            if (f.value("id").toString() == QStringLiteral("clouds")) {
                sawClouds = true;
                sawParams = !f.value("params").toArray().isEmpty();
            }
        }
        CHECK(sawClouds && sawParams);
        DocumentItem* cur = state.activeDocument();
        CHECK(cur != nullptr);
        const QJsonObject sel = call(bridge, 31, QStringLiteral("set_active_layer"),
                                     QJsonObject{{"layer", 0}});
        CHECK(sel.value("ok").toBool(false));
        CHECK(cur->activeLayer == 0);
        const QJsonObject txt = call(bridge, 32, QStringLiteral("add_text"),
                                     QJsonObject{{"text", QStringLiteral("Hi")},
                                                 {"x", 10.0},
                                                 {"y", 10.0}});
        CHECK(txt.value("ok").toBool(false));
        const QJsonObject undone = call(bridge, 33, QStringLiteral("undo"));
        CHECK(undone.value("result").toObject().value("undone").toBool());
        const QJsonObject redone = call(bridge, 34, QStringLiteral("redo"));
        CHECK(redone.value("result").toObject().value("redone").toBool());
        const QJsonObject savedFile = call(bridge, 35, QStringLiteral("save_project"),
                                           QJsonObject{{"path", logDir + QStringLiteral("/m.psc")}});
        CHECK(savedFile.value("ok").toBool(false));
        CHECK(QFile::exists(logDir + QStringLiteral("/m.psc")));
        QImage pick(16, 12, QImage::Format_ARGB32);
        pick.fill(QColor(10, 20, 30));
        CHECK(pick.save(logDir + QStringLiteral("/pick.png")));
        const QJsonObject opened = call(bridge, 36, QStringLiteral("open_image"),
                                        QJsonObject{{"path", logDir + QStringLiteral("/pick.png")}});
        CHECK(opened.value("ok").toBool(false));
        CHECK(opened.value("result").toObject().value("width").toInt() == 16);
        // Tools, options, brushes, strokes.
        const QJsonObject tools = call(bridge, 37, QStringLiteral("list_tools"));
        CHECK(tools.value("ok").toBool(false));
        const QJsonArray toolList =
            tools.value("result").toObject().value("tools").toArray();
        CHECK(!toolList.isEmpty());
        bool sawBrush = false;
        for (const QJsonValue& v : toolList) {
            if (v.toObject().value("key").toString() == QStringLiteral("brush")) {
                sawBrush = true;
                break;
            }
        }
        CHECK(sawBrush);
        const QJsonObject brushBad = call(bridge, 38, QStringLiteral("list_tool_options"),
                                          QJsonObject{{"tool", QStringLiteral("nope")}});
        CHECK(!brushBad.value("ok").toBool(true));
        const QJsonObject opts = call(bridge, 39, QStringLiteral("list_tool_options"),
                                      QJsonObject{{"tool", QStringLiteral("brush")}});
        CHECK(opts.value("ok").toBool(false));
        const QJsonArray optList =
            opts.value("result").toObject().value("options").toArray();
        bool sawSize = false;
        for (const QJsonValue& v : optList) {
            if (v.toObject().value("id").toString() == QStringLiteral("brush_size")) {
                sawSize = true;
                break;
            }
        }
        CHECK(sawSize);
        const QJsonObject setBad = call(bridge, 40, QStringLiteral("set_tool_option"),
                                        QJsonObject{{"tool", QStringLiteral("brush")},
                                                    {"id", QStringLiteral("nope")},
                                                    {"value", 1}});
        CHECK(!setBad.value("ok").toBool(true));
        const QJsonObject setOpt = call(bridge, 41, QStringLiteral("set_tool_option"),
                                        QJsonObject{{"tool", QStringLiteral("brush")},
                                                    {"id", QStringLiteral("brush_size")},
                                                    {"value", 33}});
        CHECK(setOpt.value("ok").toBool(false));
        CHECK(setOpt.value("result").toObject().value("value").toInt() == 33);
        const QJsonObject brushes = call(bridge, 42, QStringLiteral("list_brushes"));
        CHECK(brushes.value("ok").toBool(false));
        const QJsonArray brushList =
            brushes.value("result").toObject().value("brushes").toArray();
        CHECK(!brushList.isEmpty());
        const QString firstBrush =
            brushList[0].toObject().value("name").toString();
        const QJsonObject picked = call(bridge, 43, QStringLiteral("select_brush"),
                                        QJsonObject{{"name", firstBrush}});
        CHECK(picked.value("ok").toBool(false));
        const QJsonObject noBrush = call(bridge, 44, QStringLiteral("select_brush"),
                                         QJsonObject{{"name", QStringLiteral("nope")}});
        CHECK(!noBrush.value("ok").toBool(true));
        const QJsonObject strokeBad = call(bridge, 45, QStringLiteral("paint_stroke"),
                                           QJsonObject{{"points", QJsonArray{}}});
        CHECK(!strokeBad.value("ok").toBool(true));
        const QJsonObject stroke = call(bridge, 46, QStringLiteral("paint_stroke"),
                                        QJsonObject{{"points", QJsonArray{QJsonArray{2.0, 6.0},
                                                                         QJsonArray{13.0, 6.0}}},
                                                    {"radius", 3.0},
                                                    {"color", QStringLiteral("#ff0000")}});
        CHECK(stroke.value("ok").toBool(false));
        CHECK(stroke.value("result").toObject().value("dabs").toInt() > 0);
        // Mixer: pure color math, no document needed.
        const QJsonObject mixed = call(bridge, 47, QStringLiteral("mix_colors"),
                                       QJsonObject{{"a", QStringLiteral("#ff0000")},
                                                   {"b", QStringLiteral("#0000ff")}});
        CHECK(mixed.value("ok").toBool(false));
        const QColor mid(mixed.value("result").toObject().value("color").toString());
        CHECK(mid.isValid());
        CHECK(mid.red() > 60 && mid.blue() > 60 && mid.green() < 120);
        const QJsonObject ramp = call(bridge, 48, QStringLiteral("mix_colors"),
                                      QJsonObject{{"a", QStringLiteral("#000000")},
                                                  {"b", QStringLiteral("#ffffff")},
                                                  {"steps", 5}});
        CHECK(ramp.value("ok").toBool(false));
        CHECK(ramp.value("result").toObject().value("colors").toArray().size() == 5);
        const QJsonObject badMix = call(bridge, 49, QStringLiteral("mix_colors"),
                                        QJsonObject{{"a", QStringLiteral("nope")},
                                                    {"b", QStringLiteral("#ffffff")}});
        CHECK(!badMix.value("ok").toBool(true));
    }
    // Unknown commands and garbage lines never break the session.
    {
        const QJsonObject r = call(bridge, 10, QStringLiteral("nope"));
        CHECK(!r.value("ok").toBool(true));
        CHECK(r.value("id").toInt(-1) == 10);
        QLocalSocket sock;
        sock.connectToServer(bridge.socketPath());
        CHECK(sock.waitForConnected(5000));
        sock.write("this is not json\n");
        sock.flush();
        const QJsonObject after =
            rpc(bridge, QJsonObject{{"id", 11},
                                    {"cmd", QStringLiteral("ping")},
                                    {"params", QJsonObject{}}});
        CHECK(after.value("ok").toBool(false));
    }

    std::printf("  checks=%d failures=%d\n", pittore_test::checks(),
                pittore_test::failures());
    return pittore_test::failures() == 0 ? 0 : 1;
}
