// Drives UnsavedListDialog with no display (QT_QPA_PLATFORM=offscreen). Rule numbers refer to
// SPEC.md. Set NFLB_SCREENSHOT_DIR to also save pictures of the dialog there.

#include "unsavedlistdialog.h"

#include <QAccessible>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalSpy>
#include <QToolButton>
#include <QtTest>

using nflb::ClosingModel;
using nflb::UnsavedListDialog;
using Doc = ClosingModel::Document;

namespace {

QList<Doc> threeDocs()
{
    return {
        {"report", "Report.docx", "/home/sam/Documents/Report.docx", true, false},
        {"notes", "Notes.txt", "/home/sam/Documents/Notes.txt", false, false}, // clean: no row (1.4)
        {"untitled", "Untitled", QString(), true, false},
        {"budget", "Budget.ods", "/srv/shared/Budget.ods", true, true},
    };
}

QWidget *row(UnsavedListDialog &d, const QString &id)
{
    return d.findChild<QWidget *>(QStringLiteral("row:") + id);
}

template<typename T>
T *part(UnsavedListDialog &d, const QString &id, const char *name)
{
    QWidget *r = row(d, id);
    return r ? r->findChild<T *>(QLatin1String(name)) : nullptr;
}

QPushButton *button(UnsavedListDialog &d, const char *name)
{
    return d.findChild<QPushButton *>(QLatin1String(name));
}

QString title(UnsavedListDialog &d)
{
    return d.findChild<QLabel *>(QStringLiteral("title"))->text();
}

QString note(UnsavedListDialog &d, const QString &id)
{
    QLabel *n = part<QLabel>(d, id, "note");
    return n && !n->isHidden() ? n->text() : QString();
}

// The rows currently in the list, top to bottom.
QStringList rowIds(UnsavedListDialog &d)
{
    QList<QPair<int, QString>> found;
    for (QWidget *w : d.findChildren<QWidget *>()) {
        if (w->objectName().startsWith(QLatin1String("row:")) && !w->isHidden())
            found.append({w->y(), w->objectName().mid(4)});
    }
    std::sort(found.begin(), found.end());
    QStringList ids;
    for (const auto &f : found)
        ids << f.second;
    return ids;
}

void shoot(QWidget &w, const QString &name)
{
    const QString dir = qEnvironmentVariable("NFLB_SCREENSHOT_DIR");
    if (dir.isEmpty())
        return;
    w.grab().save(dir + QLatin1Char('/') + name);
}

} // namespace

class TestDialog : public QObject
{
    Q_OBJECT

private slots:
    void rowsAndTicks();
    void appNounInTitle();
    void buttonsAndRoles();
    void failureKeepsItOpenWithTheReason();
    void cancelledPickerStopsTheRun();
    void pickerIsToldWhichDocument();
    void rowSaveAndDiscard();
    void lastRowCarriesOn();
    void escapeCancels();
    void closeButtonCancels();
    void enterIsSaveSelected();
    void accessibleNames();
    void buttonsWaitWhileSaving();
    void cancelDuringSaveAbandonsIt();
    void singlePrompt();
    void singlePromptCanBeTurnedOff();
    void nothingUnsavedShowsNothing();
    void documentChangedAddsARow();
    void longListFitsOnScreen();
    void nameAsksToShowTheDocument();
};

void TestDialog::rowsAndTicks()
{
    UnsavedListDialog d(threeDocs());
    d.show();
    QVERIFY(QTest::qWaitForWindowExposed(&d));

    // 1.4, 2.2: one row per unsaved document, in order; 2.4: all ticked.
    QCOMPARE(rowIds(d), QStringList({"report", "untitled", "budget"}));
    for (const QString &id : QStringList{"report", "untitled", "budget"})
        QVERIFY(part<QCheckBox>(d, id, "tick")->isChecked());
    QCOMPARE(title(d), QStringLiteral("Save changes to 3 documents?"));      // 2.1
    QCOMPARE(d.windowTitle(), title(d));
    QVERIFY(d.findChild<QLabel *>("help")->text().contains("the rest close without saving")); // 2.6

    QCOMPARE(part<QLabel>(d, "untitled", "where")->toolTip(), QStringLiteral("Not saved yet")); // 2.2
    QCOMPARE(part<QLabel>(d, "report", "where")->toolTip(),
             QDir::toNativeSeparators("/home/sam/Documents/Report.docx"));
    QVERIFY(note(d, "budget").contains("read-only")); // 6.1: said up front
    QVERIFY(note(d, "report").isEmpty());

    // Unticking changes that row only (4.4) and the model agrees.
    part<QCheckBox>(d, "untitled", "tick")->click();
    QVERIFY(!part<QCheckBox>(d, "untitled", "tick")->isChecked());
    QVERIFY(part<QCheckBox>(d, "report", "tick")->isChecked());
    QVERIFY(!d.model().snapshot().rows.at(1).ticked);
    QVERIFY(d.model().snapshot().rows.at(0).ticked);

    shoot(d, QStringLiteral("nflb-list.png"));
}

void TestDialog::appNounInTitle()
{
    UnsavedListDialog d(threeDocs());
    d.setNoun("design", "designs");
    QCOMPARE(title(d), QStringLiteral("Save changes to 3 designs?"));
    QVERIFY(d.findChild<QLabel *>("help")->text().startsWith("Ticked designs are saved"));
}

void TestDialog::buttonsAndRoles()
{
    UnsavedListDialog d(threeDocs());
    auto *box = d.findChild<QDialogButtonBox *>();
    QPushButton *save = button(d, "saveSelected");
    QPushButton *dont = button(d, "dontSave");
    QPushButton *cancel = button(d, "cancel");
    QCOMPARE(save->text(), QStringLiteral("Save Selected"));
    QCOMPARE(dont->text(), QStringLiteral("Don't Save"));
    QCOMPARE(cancel->text(), QStringLiteral("Cancel"));
    QVERIFY(save->isDefault());                                          // 2.3
    QVERIFY(!dont->isDefault() && !dont->autoDefault());                 // 3.2
    QCOMPARE(box->buttonRole(dont), QDialogButtonBox::DestructiveRole);
    QCOMPARE(box->buttonRole(cancel), QDialogButtonBox::RejectRole);
    QCOMPARE(box->buttonRole(save), QDialogButtonBox::AcceptRole);
    // Row buttons never take Enter from Save Selected (5.1).
    QVERIFY(!part<QPushButton>(d, "report", "save")->autoDefault());
    QVERIFY(!part<QPushButton>(d, "report", "discard")->autoDefault());
}

void TestDialog::failureKeepsItOpenWithTheReason()
{
    UnsavedListDialog d({{"a", "Bracket", "/d/Bracket.sc", true, false},
                         {"b", "Lid", "/d/Lid.sc", true, false},
                         {"c", "Hinge", "/d/Hinge.sc", true, false}});
    QStringList asked;
    connect(&d, &UnsavedListDialog::saveRequested, &d, [&](const QString &id, const QString &) {
        asked << id;
        if (id == "b")
            d.saveFailed("The disk is full");
        else
            d.saveDone();
    });
    d.show();
    QVERIFY(QTest::qWaitForWindowExposed(&d));
    button(d, "saveSelected")->click();

    QCOMPARE(asked, QStringList({"a", "b", "c"})); // 3.1.3: the failure didn't stop the others
    QVERIFY(d.isVisible());                         // 3.1.1: the list stays
    QCOMPARE(d.outcome(), UnsavedListDialog::Outcome::Pending);
    QVERIFY(d.closedIds().isEmpty());               // nothing closes
    QCOMPARE(rowIds(d), QStringList({"a", "b", "c"}));
    QVERIFY(note(d, "b").contains("The disk is full")); // 3.4
    QCOMPARE(note(d, "a"), QStringLiteral("Saved"));
    QVERIFY(!part<QCheckBox>(d, "a", "tick")->isEnabled());
    QVERIFY(!part<QPushButton>(d, "a", "save")->isEnabled());
    QVERIFY(part<QPushButton>(d, "b", "save")->isEnabled());
    QCOMPARE(title(d), QStringLiteral("Save changes to 1 document?"));
    // 2.5: a short list shows every row without scrolling, notes included.
    QCOMPARE(d.findChild<QScrollArea *>("scroll")->verticalScrollBar()->maximum(), 0);
    // 5.1: after the run, Enter is still Save Selected, not the button that kept focus meanwhile.
    QCOMPARE(d.focusWidget(), static_cast<QWidget *>(button(d, "saveSelected")));
    shoot(d, QStringLiteral("nflb-failed.png"));
}

void TestDialog::cancelledPickerStopsTheRun()
{
    UnsavedListDialog d({{"u1", "Untitled", QString(), true, false},
                         {"u2", "Untitled 2", QString(), true, false},
                         {"a", "Bracket", "/d/Bracket.sc", true, false}});
    QStringList picks;
    int saves = 0;
    d.setSaver([&](const QString &, const QString &, QString *) { ++saves; return true; });
    d.setPicker([&](const QString &id, const QString &, UnsavedListDialog::PickReason) {
        picks << id;
        return QString(); // cancelled
    });
    d.show();
    button(d, "saveSelected")->click();

    QCOMPARE(picks, QStringList({"u1"})); // 3.1.3: no second picker
    QCOMPARE(saves, 0);                   // the run stopped
    QVERIFY(d.isVisible());               // 3.1.1
    QVERIFY(note(d, "u1").contains("cancelled"));
    QVERIFY(note(d, "u2").isEmpty());
    QVERIFY(d.closedIds().isEmpty());
}

void TestDialog::pickerIsToldWhichDocument()
{
    UnsavedListDialog d({{"u1", "Untitled", QString(), true, false},
                         {"ro", "Budget.ods", "/srv/Budget.ods", true, true}});
    QSignalSpy picks(&d, &UnsavedListDialog::pickRequested);
    QSignalSpy saves(&d, &UnsavedListDialog::saveRequested);
    d.show();
    button(d, "saveSelected")->click();

    QCOMPARE(picks.size(), 1);
    QCOMPARE(picks.at(0).at(0).toString(), QStringLiteral("u1"));
    QCOMPARE(picks.at(0).at(1).toString(), QStringLiteral("Untitled")); // 3.1: name the document
    QCOMPARE(picks.at(0).at(2).value<UnsavedListDialog::PickReason>(), UnsavedListDialog::PickReason::NeverSaved);
    QVERIFY(!button(d, "cancel")->isEnabled()); // the picker is answered first

    QVERIFY(d.picked("/d/Untitled.ods"));
    QCOMPARE(saves.size(), 1);
    QCOMPARE(saves.at(0).at(1).toString(), QStringLiteral("/d/Untitled.ods"));
    QVERIFY(d.saveDone());

    // 6.1: the read-only file asks for a new place too.
    QCOMPARE(picks.size(), 2);
    QCOMPARE(picks.at(1).at(2).value<UnsavedListDialog::PickReason>(), UnsavedListDialog::PickReason::ReadOnly);
    QVERIFY(d.picked("/d/Budget copy.ods"));
    QVERIFY(d.saveDone());

    QCOMPARE(d.outcome(), UnsavedListDialog::Outcome::Proceeded);
    QCOMPARE(d.result(), int(QDialog::Accepted));
    QCOMPARE(d.savedPaths().value("ro"), QStringLiteral("/d/Budget copy.ods"));
}

void TestDialog::rowSaveAndDiscard()
{
    UnsavedListDialog d(threeDocs());
    QStringList saved;
    d.setSaver([&](const QString &id, const QString &, QString *) { saved << id; return true; });
    QSignalSpy closed(&d, &UnsavedListDialog::documentClosed);
    d.show();

    part<QCheckBox>(d, "budget", "tick")->click(); // unticked; must not change on others' answers
    part<QPushButton>(d, "report", "save")->click();   // 4.1
    QCOMPARE(saved, QStringList({"report"}));
    QCOMPARE(rowIds(d), QStringList({"untitled", "budget"}));
    QCOMPARE(closed.size(), 1);
    QCOMPARE(closed.at(0).at(0).toString(), QStringLiteral("report"));

    part<QPushButton>(d, "untitled", "discard")->click(); // 4.2
    QCOMPARE(rowIds(d), QStringList({"budget"}));
    QVERIFY(!part<QCheckBox>(d, "budget", "tick")->isChecked()); // 4.4
    QCOMPARE(title(d), QStringLiteral("Save changes to 1 document?"));
    QVERIFY(d.isVisible());
    QCOMPARE(d.lostIds(), QList<QString>({"untitled"}));
}

void TestDialog::lastRowCarriesOn()
{
    UnsavedListDialog d({{"a", "Bracket", "/d/Bracket.sc", true, false},
                         {"b", "Lid", "/d/Lid.sc", true, false}});
    d.setSaver([](const QString &, const QString &, QString *) { return true; });
    QSignalSpy finished(&d, &QDialog::finished);
    d.show();
    part<QPushButton>(d, "a", "discard")->click();
    QVERIFY(d.isVisible());
    part<QPushButton>(d, "b", "save")->click(); // 4.3: the last row left; the action goes ahead
    QCOMPARE(finished.size(), 1);
    QCOMPARE(d.result(), int(QDialog::Accepted));
    QCOMPARE(d.outcome(), UnsavedListDialog::Outcome::Proceeded);
    QVERIFY(!d.isVisible());
    QCOMPARE(d.savedIds(), QList<QString>({"b"}));
    QCOMPARE(d.lostIds(), QList<QString>({"a"}));
}

void TestDialog::escapeCancels()
{
    UnsavedListDialog d(threeDocs());
    d.show();
    QVERIFY(QTest::qWaitForWindowExposed(&d));
    QTest::keyClick(&d, Qt::Key_Escape); // 5.1
    QCOMPARE(d.result(), int(QDialog::Rejected));
    QCOMPARE(d.outcome(), UnsavedListDialog::Outcome::Stopped);
    QVERIFY(!d.isVisible());
    QCOMPARE(d.stillOpenIds().size(), 4); // 3.3: everything stays open
    QVERIFY(d.lostIds().isEmpty());
}

void TestDialog::closeButtonCancels()
{
    UnsavedListDialog d(threeDocs());
    d.show();
    d.close(); // the window's close button (2.3)
    QCOMPARE(d.outcome(), UnsavedListDialog::Outcome::Stopped);
    QVERIFY(!d.isVisible());
}

void TestDialog::enterIsSaveSelected()
{
    UnsavedListDialog d(threeDocs());
    QSignalSpy saves(&d, &UnsavedListDialog::saveRequested);
    d.show();
    QVERIFY(QTest::qWaitForWindowExposed(&d));
    QCheckBox *tick = part<QCheckBox>(d, "report", "tick");
    tick->setFocus();
    QTest::keyClick(tick, Qt::Key_Return); // 5.1
    QCOMPARE(saves.size(), 1);
    QCOMPARE(saves.at(0).at(0).toString(), QStringLiteral("report"));
}

void TestDialog::accessibleNames()
{
    UnsavedListDialog d(threeDocs());
    d.show();
    // 5.3: the tick box and the row's buttons name the document.
    QCOMPARE(part<QCheckBox>(d, "report", "tick")->accessibleName(), QStringLiteral("Save Report.docx"));
    QCOMPARE(part<QPushButton>(d, "report", "save")->accessibleName(), QStringLiteral("Save Report.docx"));
    QCOMPARE(part<QPushButton>(d, "report", "discard")->accessibleName(), QStringLiteral("Discard Report.docx"));

    // Each row is one group, named after its document.
    QAccessibleInterface *group = QAccessible::queryAccessibleInterface(row(d, "report"));
    QVERIFY(group);
    QCOMPARE(group->role(), QAccessible::Grouping);
    QCOMPARE(group->text(QAccessible::Name), QStringLiteral("Report.docx"));

    QAccessibleInterface *tick = QAccessible::queryAccessibleInterface(part<QCheckBox>(d, "report", "tick"));
    QCOMPARE(tick->text(QAccessible::Name), QStringLiteral("Save Report.docx"));

    // 5.4: the count is in the window's accessible name.
    QCOMPARE(d.accessibleName(), QStringLiteral("Save changes to 3 documents?"));
}

void TestDialog::buttonsWaitWhileSaving()
{
    UnsavedListDialog d(threeDocs());
    QSignalSpy saves(&d, &UnsavedListDialog::saveRequested); // answered later, like a slow save
    QSignalSpy picks(&d, &UnsavedListDialog::pickRequested); // so no file dialog opens
    d.show();
    button(d, "saveSelected")->click();
    QCOMPARE(saves.size(), 1);

    // 6.2: the row shows it is saving, and everything but Cancel waits.
    QCOMPARE(note(d, "report"), QStringLiteral("Saving…"));
    QVERIFY(!button(d, "saveSelected")->isEnabled());
    QVERIFY(!button(d, "dontSave")->isEnabled());
    QVERIFY(button(d, "cancel")->isEnabled());
    for (const QString &id : QStringList{"report", "untitled", "budget"}) {
        QVERIFY(!part<QCheckBox>(d, id, "tick")->isEnabled());
        QVERIFY(!part<QPushButton>(d, id, "save")->isEnabled());
        QVERIFY(!part<QPushButton>(d, id, "discard")->isEnabled());
        QVERIFY(!part<QToolButton>(d, id, "name")->isEnabled());
    }
    QVERIFY(!d.dontSave()); // and a script is held to the same rule (8.1)
    shoot(d, QStringLiteral("nflb-saving.png"));

    QVERIFY(d.saveDone());
    QCOMPARE(picks.size(), 1); // the never-saved one asks where next
    QVERIFY(!button(d, "cancel")->isEnabled());
}

void TestDialog::cancelDuringSaveAbandonsIt()
{
    UnsavedListDialog d({{"a", "Bracket", "/d/Bracket.sc", true, false},
                         {"b", "Lid", "/d/Lid.sc", true, false},
                         {"c", "Hinge", "/d/Hinge.sc", true, false}});
    QSignalSpy saves(&d, &UnsavedListDialog::saveRequested);
    QSignalSpy abandoned(&d, &UnsavedListDialog::saveAbandoned);
    d.show();
    button(d, "saveSelected")->click();
    QVERIFY(d.saveDone()); // a saved; b is now saving
    QCOMPARE(saves.size(), 2);
    button(d, "cancel")->click();

    QCOMPARE(abandoned.size(), 1);
    QCOMPARE(abandoned.at(0).at(0).toString(), QStringLiteral("b")); // the app is told (6.2)
    QCOMPARE(saves.size(), 2);                                       // c never started
    QCOMPARE(d.outcome(), UnsavedListDialog::Outcome::Stopped);
    QCOMPARE(d.savedIds(), QList<QString>({"a"}));                   // what saved stays saved
    QVERIFY(d.closedIds().isEmpty());                                // nothing closes (3.3)
    QVERIFY(!d.saveDone());                                          // a late answer is ignored
}

void TestDialog::singlePrompt()
{
    UnsavedListDialog d({{"r", "Report.docx", "/home/sam/Report.docx", true, false},
                         {"n", "Notes.txt", "/home/sam/Notes.txt", false, false}});
    QString whyNot = QStringLiteral("You don't have permission to write to Documents");
    bool fail = true;
    d.setSaver([&](const QString &, const QString &, QString *why) {
        if (fail)
            *why = whyNot;
        return !fail;
    });
    d.show();
    QVERIFY(QTest::qWaitForWindowExposed(&d));

    // 1.2: the familiar question, no list.
    QCOMPARE(title(d), QStringLiteral("Save changes to “Report.docx”?"));
    QVERIFY(d.findChild<QScrollArea *>("scroll")->isHidden());
    QCOMPARE(button(d, "saveSelected")->text(), QStringLiteral("Save"));
    shoot(d, QStringLiteral("nflb-single.png"));

    button(d, "saveSelected")->click();
    QVERIFY(d.isVisible()); // 3.1.1: it stays, and says why (3.4)
    QVERIFY(d.findChild<QLabel *>("singleNote")->text().contains(whyNot));

    fail = false;
    button(d, "saveSelected")->click();
    QCOMPARE(d.outcome(), UnsavedListDialog::Outcome::Proceeded);
    QCOMPARE(d.result(), int(QDialog::Accepted));
}

void TestDialog::singlePromptCanBeTurnedOff()
{
    UnsavedListDialog d({{"r", "Report.docx", "/home/sam/Report.docx", true, false}});
    d.setSinglePromptEnabled(false);
    d.show();
    QVERIFY(!d.findChild<QScrollArea *>("scroll")->isHidden());
    QCOMPARE(rowIds(d), QStringList({"r"}));
    QCOMPARE(title(d), QStringLiteral("Save changes to 1 document?"));
}

void TestDialog::nothingUnsavedShowsNothing()
{
    UnsavedListDialog d({{"n", "Notes.txt", "/home/sam/Notes.txt", false, false}});
    QVERIFY(!d.needsAsking());
    QCOMPARE(d.exec(), int(QDialog::Accepted)); // returns at once, shows nothing (1.4)
    QCOMPARE(d.outcome(), UnsavedListDialog::Outcome::Proceeded);
}

void TestDialog::documentChangedAddsARow()
{
    UnsavedListDialog d(threeDocs());
    d.show();
    QVERIFY(d.documentChanged("notes")); // 6.5: it gains changes and joins, ticked
    QCOMPARE(rowIds(d), QStringList({"report", "notes", "untitled", "budget"}));
    QVERIFY(part<QCheckBox>(d, "notes", "tick")->isChecked());
    QCOMPARE(title(d), QStringLiteral("Save changes to 4 documents?"));
}

void TestDialog::longListFitsOnScreen()
{
    QList<Doc> docs;
    for (int i = 1; i <= 80; ++i)
        docs.append({QString::number(i), QStringLiteral("Drawing %1").arg(i), QStringLiteral("/d/Drawing %1.dwg").arg(i), true, false});
    UnsavedListDialog d(docs);
    d.show();
    QVERIFY(QTest::qWaitForWindowExposed(&d));
    // 2.5: the window fits the screen, the list scrolls, the buttons stay in view.
    const QRect avail = d.screen()->availableGeometry();
    QVERIFY2(d.frameGeometry().height() <= avail.height(),
             qPrintable(QStringLiteral("%1 > %2").arg(d.frameGeometry().height()).arg(avail.height())));
    QVERIFY(d.findChild<QScrollArea *>("scroll")->verticalScrollBar()->maximum() > 0);
    QPushButton *save = button(d, "saveSelected");
    QVERIFY(save->isVisible());
    QVERIFY(d.rect().contains(save->mapTo(&d, save->rect().bottomRight())));
}

void TestDialog::nameAsksToShowTheDocument()
{
    UnsavedListDialog d(threeDocs());
    QSignalSpy show(&d, &UnsavedListDialog::showDocumentRequested);
    d.show();
    part<QToolButton>(d, "untitled", "name")->click(); // 2.2
    QCOMPARE(show.size(), 1);
    QCOMPARE(show.at(0).at(0).toString(), QStringLiteral("untitled"));
    QVERIFY(d.isVisible());
}

QTEST_MAIN(TestDialog)
#include "tst_dialog.moc"
