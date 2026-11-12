#pragma once

// No File Left Behind: the rules with no window attached, ported from
// scenarios/reference/model.mjs. Rule numbers refer to SPEC.md.
//
// The model holds the documents one closing action (quit, close window, Close All) is closing and
// answers one question at a time: what should the window show now, and what is the application
// being asked to do? It never touches a file. When it needs one it sets a request (pick a place,
// or save to a path), and the application answers with picked() / pickCancelled() or
// saveDone() / saveFailed().
//
// Every input returns true when it was accepted and false when it was ignored (its button would
// be disabled or waiting). An ignored input changes nothing.

#include <QList>
#include <QMap>
#include <QString>

namespace nflb {

class ClosingModel
{
public:
    // One document the action closes, in the order the user sees them (1.3, 2.2).
    struct Document
    {
        QString id;
        QString name;
        QString path;          // empty if it has never been saved
        bool dirty = true;     // has unsaved changes
        bool readOnly = false; // its file can't be written where it is (6.1)
    };

    enum class Prompt { List, Single, None };             // None: the window is gone
    enum class Outcome { Pending, Proceeded, Stopped };   // Pending while asking
    enum class RowState { Unsaved, Saving, Saved };
    enum class NoteKind { None, Failed, Cancelled };
    enum class RequestType { None, Pick, Save };
    enum class PickReason { NeverSaved, ReadOnly };

    struct Note
    {
        NoteKind kind = NoteKind::None;
        QString text; // the failure reason; null for a cancelled picker
    };

    struct Request
    {
        RequestType type = RequestType::None;
        QString id;
        PickReason why = PickReason::NeverSaved; // for Pick
        QString path;                            // for Save
    };

    struct Row
    {
        QString id;
        QString name;
        QString where; // the path, or null for "Not saved yet"
        bool readOnly = false;
        bool ticked = false;
        RowState state = RowState::Unsaved;
        Note note;
    };

    struct Snapshot
    {
        Prompt prompt = Prompt::None;
        int count = 0;                // unsaved documents still to answer: the title's number (2.1)
        QList<Row> rows;              // one per listed document, in order
        Request request;
        Outcome outcome = Outcome::Pending;
        QList<QString> open;          // ids still open
        QList<QString> closed;        // ids closed
        QMap<QString, QString> saved; // id to path, for every document whose changes were saved
        QList<QString> lost;          // ids closed with unsaved changes, by the user's choice
        QList<QString> abandoned;     // ids whose save was cancelled part-way (6.2)
    };

    explicit ClosingModel(const QList<Document> &docs);

    Snapshot snapshot() const;

    // The window's buttons.
    bool tick(const QString &id, bool on);
    bool saveSelected(); // Save Selected (3.1), or Save in the single prompt
    bool dontSave();     // Don't Save (3.2)
    bool cancel();       // Cancel, Escape, or the window's close button (3.3, 2.3, 5.1)

    // A row's own buttons (section 4).
    bool rowSave(const QString &id);
    bool rowDiscard(const QString &id);

    // The application's answers.
    bool picked(const QString &path);
    bool pickCancelled();
    bool saveDone();
    bool saveFailed(const QString &reason);

    // A document in the set changed while the window was open (6.5).
    bool docChanged(const QString &id);

    // Conveniences for a window: not part of the scenario contract.
    QString name(const QString &id) const;
    QString path(const QString &id) const;
    bool isFinished() const { return m_outcome != Outcome::Pending; }

private:
    enum class State { Clean, Unsaved, Saved }; // Saved: saved by this window, still open
    enum class RunMode { Selected, Row, Single };

    struct Doc
    {
        QString id;
        QString name;
        QString path;
        bool readOnly = false;
        State state = State::Clean;
        bool open = true;
        bool ticked = false;
        Note note;
        QString savedTo;
        bool lost = false;
    };

    struct Run
    {
        QList<QString> queue;
        RunMode mode = RunMode::Selected;
        bool failed = false;
        QString target; // the row being saved, for RunMode::Row
    };

    QList<const Doc *> listed() const;
    int unsavedCount() const;
    Doc *doc(const QString &id);
    const Doc *doc(const QString &id) const;
    bool idle() const;
    bool start(const QList<QString> &queue, RunMode mode);
    void next();
    void finishRun();
    void lastRowGone();
    void proceed();

    QList<Doc> m_docs;
    Request m_request;
    Run m_run;
    Outcome m_outcome = Outcome::Pending;
    QList<QString> m_abandoned;
    bool m_single = false;
};

} // namespace nflb
