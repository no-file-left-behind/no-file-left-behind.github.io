// Runs scenarios/scenarios.json against ClosingModel, the same way scenarios/reference/run.mjs runs
// it against the reference model.

#include "closingmodel.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>

using nflb::ClosingModel;

namespace {

QJsonValue orNull(const QString &s)
{
    return s.isNull() ? QJsonValue(QJsonValue::Null) : QJsonValue(s);
}

QJsonArray strings(const QList<QString> &list)
{
    QJsonArray a;
    for (const QString &s : list)
        a.append(s);
    return a;
}

// The snapshot in the scenarios' own shape (scenarios/README.md).
QJsonObject toJson(const ClosingModel::Snapshot &s)
{
    using P = ClosingModel::Prompt;
    using O = ClosingModel::Outcome;
    using RS = ClosingModel::RowState;
    using NK = ClosingModel::NoteKind;
    using RT = ClosingModel::RequestType;

    QJsonObject o;
    o["prompt"] = s.prompt == P::List ? "list" : s.prompt == P::Single ? "single" : "none";
    o["count"] = s.count;

    QJsonArray rows;
    for (const ClosingModel::Row &r : s.rows) {
        QJsonObject row;
        row["id"] = r.id;
        row["name"] = r.name;
        row["where"] = orNull(r.where);
        row["readOnly"] = r.readOnly;
        row["ticked"] = r.ticked;
        row["state"] = r.state == RS::Unsaved ? "unsaved" : r.state == RS::Saving ? "saving" : "saved";
        if (r.note.kind == NK::None) {
            row["note"] = QJsonValue(QJsonValue::Null);
        } else {
            QJsonObject note;
            note["kind"] = r.note.kind == NK::Failed ? "failed" : "cancelled";
            note["text"] = orNull(r.note.text);
            row["note"] = note;
        }
        rows.append(row);
    }
    o["rows"] = rows;

    if (s.request.type == RT::None) {
        o["request"] = QJsonValue(QJsonValue::Null);
    } else {
        QJsonObject req;
        req["id"] = s.request.id;
        if (s.request.type == RT::Pick) {
            req["type"] = "pick";
            req["why"] = s.request.why == ClosingModel::PickReason::NeverSaved ? "never-saved" : "read-only";
        } else {
            req["type"] = "save";
            req["path"] = s.request.path;
        }
        o["request"] = req;
    }

    o["outcome"] = s.outcome == O::Pending ? QJsonValue(QJsonValue::Null)
                 : QJsonValue(s.outcome == O::Proceeded ? "proceeded" : "stopped");
    o["open"] = strings(s.open);
    o["closed"] = strings(s.closed);
    QJsonObject saved;
    for (auto it = s.saved.cbegin(); it != s.saved.cend(); ++it)
        saved[it.key()] = it.value();
    o["saved"] = saved;
    o["lost"] = strings(s.lost);
    o["abandoned"] = strings(s.abandoned);
    return o;
}

QString text(const QJsonValue &v)
{
    if (v.isObject())
        return QString::fromUtf8(QJsonDocument(v.toObject()).toJson(QJsonDocument::Compact));
    if (v.isArray())
        return QString::fromUtf8(QJsonDocument(v.toArray()).toJson(QJsonDocument::Compact));
    if (v.isString())
        return QLatin1Char('"') + v.toString() + QLatin1Char('"');
    if (v.isNull() || v.isUndefined())
        return QStringLiteral("null");
    if (v.isBool())
        return v.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    return QString::number(v.toDouble());
}

// `want` is a partial picture: every field it names must match; arrays must match in length and
// order, each element matched the same partial way. Returns an empty string when it matches.
QString differs(const QJsonValue &got, const QJsonValue &want, const QString &at)
{
    const QString mismatch = QStringLiteral("%1: wanted %2, got %3").arg(at, text(want), text(got));
    if (want.isArray()) {
        const QJsonArray w = want.toArray();
        if (!got.isArray() || got.toArray().size() != w.size())
            return mismatch;
        const QJsonArray g = got.toArray();
        for (int i = 0; i < w.size(); ++i) {
            const QString d = differs(g.at(i), w.at(i), QStringLiteral("%1[%2]").arg(at).arg(i));
            if (!d.isEmpty())
                return d;
        }
        return QString();
    }
    if (want.isObject()) {
        if (!got.isObject())
            return mismatch;
        const QJsonObject w = want.toObject();
        const QJsonObject g = got.toObject();
        for (auto it = w.begin(); it != w.end(); ++it) {
            const QString d = differs(g.value(it.key()), it.value(), at + QLatin1Char('.') + it.key());
            if (!d.isEmpty())
                return d;
        }
        return QString();
    }
    if (want.isNull())
        return got.isNull() ? QString() : mismatch;
    return got == want ? QString() : mismatch;
}

bool apply(ClosingModel &m, const QJsonObject &step, bool *known)
{
    const QString what = step.value("do").toString();
    const QString id = step.value("id").toString();
    *known = true;
    if (what == "tick") return m.tick(id, step.value("on").toBool());
    if (what == "saveSelected") return m.saveSelected();
    if (what == "dontSave") return m.dontSave();
    if (what == "cancel") return m.cancel();
    if (what == "rowSave") return m.rowSave(id);
    if (what == "rowDiscard") return m.rowDiscard(id);
    if (what == "picked") return m.picked(step.value("path").toString());
    if (what == "pickCancelled") return m.pickCancelled();
    if (what == "saveDone") return m.saveDone();
    if (what == "saveFailed") return m.saveFailed(step.value("reason").toString());
    if (what == "docChanged") return m.docChanged(id);
    *known = false;
    return false;
}

} // namespace

class TestScenarios : public QObject
{
    Q_OBJECT

private slots:
    void scenarios_data();
    void scenarios();
    void allScenariosPresent();

private:
    static QJsonArray load();
};

QJsonArray TestScenarios::load()
{
    QFile file(QStringLiteral(NFLB_SCENARIOS_FILE));
    if (!file.open(QIODevice::ReadOnly))
        return QJsonArray();
    return QJsonDocument::fromJson(file.readAll()).object().value("scenarios").toArray();
}

void TestScenarios::scenarios_data()
{
    QTest::addColumn<QJsonObject>("scenario");
    for (const QJsonValue &v : load()) {
        const QJsonObject sc = v.toObject();
        QTest::newRow(qPrintable(sc.value("id").toString())) << sc;
    }
}

void TestScenarios::scenarios()
{
    QFETCH(QJsonObject, scenario);

    QList<ClosingModel::Document> docs;
    for (const QJsonValue &v : scenario.value("docs").toArray()) {
        const QJsonObject d = v.toObject();
        ClosingModel::Document doc;
        doc.id = d.value("id").toString();
        doc.name = d.value("name").toString();
        doc.path = d.value("path").toString(); // null or missing: never saved
        doc.dirty = d.value("dirty").toBool();
        doc.readOnly = d.value("readOnly").toBool();
        docs.append(doc);
    }

    ClosingModel model(docs);
    const QJsonArray steps = scenario.value("steps").toArray();
    for (int i = 0; i < steps.size(); ++i) {
        const QJsonObject step = steps.at(i).toObject();
        if (step.contains("expect")) {
            const QString d = differs(toJson(model.snapshot()), step.value("expect"), QStringLiteral("snapshot"));
            if (!d.isEmpty())
                QFAIL(qPrintable(QStringLiteral("step %1: %2").arg(i + 1).arg(d)));
            continue;
        }
        bool known = false;
        const bool accepted = apply(model, step, &known);
        if (!known)
            QFAIL(qPrintable(QStringLiteral("step %1: unknown input %2").arg(i + 1).arg(step.value("do").toString())));
        const bool wantAccepted = !step.value("ignored").toBool();
        if (accepted != wantAccepted) {
            QFAIL(qPrintable(QStringLiteral("step %1: %2 was %3")
                                 .arg(i + 1)
                                 .arg(step.value("do").toString(), accepted ? "accepted" : "ignored")));
        }
    }
}

// Guards against a missing or truncated file passing with no rows run.
void TestScenarios::allScenariosPresent()
{
    QVERIFY2(!load().isEmpty(), "no scenarios found in " NFLB_SCENARIOS_FILE);
}

QTEST_APPLESS_MAIN(TestScenarios)
#include "tst_scenarios.moc"
