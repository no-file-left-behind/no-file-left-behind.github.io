#pragma once

// No File Left Behind: the list of unsaved work, as a Qt Widgets dialog. Rule numbers refer to
// SPEC.md. The rules live in ClosingModel; this class draws its snapshot and passes clicks in.
//
// The dialog never touches a file. It asks the application to save or to pick a place, in one of
// two ways:
//
//  - Signals (fits a slow save and a native file dialog): connect saveRequested() and
//    pickRequested(), do the work, and answer with saveDone() / saveFailed() and picked() /
//    pickCancelled(), now or later.
//  - Functions (the short way): setSaver() with a function that saves and returns at once. If
//    nothing is connected to pickRequested() and no setPicker() function is given, the dialog asks
//    for a place with QFileDialog itself.

#include "closingmodel.h"

#include <QDialog>
#include <QHash>
#include <QPointer>
#include <QSet>
#include <functional>

class QAbstractButton;
class QDialogButtonBox;
class QLabel;
class QPushButton;
class QScrollArea;
class QVBoxLayout;

namespace nflb {

class UnsavedRow;

class UnsavedListDialog : public QDialog
{
    Q_OBJECT

public:
    using Document = ClosingModel::Document;
    using PickReason = ClosingModel::PickReason;
    using Outcome = ClosingModel::Outcome;

    // Saves document `id` to `path`. Returns true when it saved; otherwise sets *whyNot to a reason
    // the user can act on (3.4).
    using Saver = std::function<bool(const QString &id, const QString &path, QString *whyNot)>;
    // Asks where to save document `id`. Returns the chosen path, or an empty string if the user
    // cancelled.
    using Picker = std::function<QString(const QString &id, const QString &name, PickReason why)>;

    // `docs`: every document the action closes, in the order the user sees them (1.3, 2.2),
    // including ones with no unsaved changes; those get no row (1.4) unless they change (6.5).
    explicit UnsavedListDialog(const QList<Document> &docs, QWidget *parent = nullptr);
    ~UnsavedListDialog() override;

    // The application's word for a document (2.1), e.g. tr("design"), tr("designs").
    void setNoun(const QString &singular, const QString &plural);
    // With one unsaved document, show the familiar single prompt (1.2). On by default; turn it off
    // to show a one-row list instead.
    void setSinglePromptEnabled(bool enabled);
    void setSaver(Saver saver);
    void setPicker(Picker picker);
    // Used by the built-in QFileDialog picker, e.g. tr("Designs (*.sc)") and "sc".
    void setFileFilter(const QString &filter, const QString &defaultSuffix = QString());

    // False when nothing is unsaved: exec() then returns Accepted at once without showing anything.
    bool needsAsking() const;

    int exec() override;
    void open() override;

    // After the dialog finishes (and at any time before).
    Outcome outcome() const;
    QList<QString> savedIds() const;
    QMap<QString, QString> savedPaths() const; // id to the path it was saved to
    QList<QString> lostIds() const;            // closed with unsaved changes, by the user's choice
    QList<QString> stillOpenIds() const;
    QList<QString> closedIds() const;
    QList<QString> abandonedIds() const;       // saves cancelled part-way (6.2)

    const ClosingModel &model() const { return m_model; }

signals:
    // Write document `id` to `path`. Write to a temporary file and replace the original only on
    // success, so an abandoned or failed save leaves no half-written file (6.2).
    void saveRequested(const QString &id, const QString &path);
    // Show a save picker that names the document (3.1); `why` says whether it was never saved or
    // its file is read-only (6.1).
    void pickRequested(const QString &id, const QString &name, nflb::ClosingModel::PickReason why);
    // The user pressed Cancel during this save (6.2): stop it, and keep the original file intact.
    void saveAbandoned(const QString &id);
    // The user clicked a document's name: bring it to the front (2.2).
    void showDocumentRequested(const QString &id);
    // A document has closed (saved, discarded, or closed by the action going ahead). The app can
    // close it now or wait for the outcome.
    void documentClosed(const QString &id);

public slots:
    // The application's answers.
    bool saveDone();
    bool saveFailed(const QString &reason);
    bool picked(const QString &path);
    bool pickCancelled();
    // A document in the set changed while the dialog was open (6.5).
    bool documentChanged(const QString &id);

    // The same inputs a click gives, for scripts and tests (8.1). They follow the same rules.
    bool setTicked(const QString &id, bool on);
    bool saveSelected();
    bool dontSave();
    bool cancel();
    bool rowSave(const QString &id);
    bool rowDiscard(const QString &id);

    void reject() override; // Escape and the window's close button are Cancel (2.3, 5.1)

private:
    friend class UnsavedRow;

    void buildUi();
    void after(bool accepted);
    void pump();
    void issue(const ClosingModel::Request &request);
    void refresh();
    void refreshList(const ClosingModel::Snapshot &s);
    void refreshSingle(const ClosingModel::Snapshot &s);
    void fitToScreen();
    void finishIfDone();
    void announceClosed(const ClosingModel::Snapshot &s);
    QString pickWithFileDialog(const QString &id, const QString &name, PickReason why);
    QString noteText(const ClosingModel::Row &row) const;
    void onButton(QAbstractButton *button);

    ClosingModel m_model;
    Saver m_saver;
    Picker m_picker;
    QString m_singular;
    QString m_plural;
    QString m_filter;
    QString m_suffix;
    bool m_singleEnabled = true;

    bool m_pumping = false;
    bool m_again = false;
    ClosingModel::Request m_issuedRequest; // the request last handed to the application
    bool m_finished = false;
    QSet<QString> m_announcedClosed;
    bool m_wasBusy = false;
    QPointer<QWidget> m_focusBeforeBusy;

    QLabel *m_icon = nullptr;
    QLabel *m_title = nullptr;
    QLabel *m_help = nullptr;
    QLabel *m_singleWhere = nullptr;
    QLabel *m_singleNote = nullptr;
    QScrollArea *m_scroll = nullptr;
    QWidget *m_rowsHost = nullptr;
    QVBoxLayout *m_rowsLayout = nullptr;
    QDialogButtonBox *m_buttons = nullptr;
    QPushButton *m_save = nullptr;
    QPushButton *m_dontSave = nullptr;
    QPushButton *m_cancel = nullptr;
    QHash<QString, UnsavedRow *> m_rows;
};

} // namespace nflb
