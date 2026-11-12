#include "closingmodel.h"

namespace nflb {

ClosingModel::ClosingModel(const QList<Document> &docs)
{
    int unsaved = 0;
    for (const Document &d : docs) {
        Doc doc;
        doc.id = d.id;
        doc.name = d.name;
        doc.path = d.path;
        doc.readOnly = d.readOnly;
        doc.state = d.dirty ? State::Unsaved : State::Clean;
        doc.ticked = d.dirty; // 2.4: every row starts ticked
        if (d.dirty)
            ++unsaved;
        m_docs.append(doc);
    }
    m_single = unsaved == 1; // 1.2: one unsaved document keeps the single prompt
    if (unsaved == 0)
        proceed(); // nothing to ask
}

// ---- what the window shows ----------------------------------------------------------------------

QList<const ClosingModel::Doc *> ClosingModel::listed() const
{
    QList<const Doc *> out;
    for (const Doc &d : m_docs) {
        if (d.open && d.state != State::Clean)
            out.append(&d);
    }
    return out;
}

int ClosingModel::unsavedCount() const
{
    int n = 0;
    for (const Doc *d : listed()) {
        if (d->state == State::Unsaved)
            ++n;
    }
    return n;
}

ClosingModel::Doc *ClosingModel::doc(const QString &id)
{
    for (Doc &d : m_docs) {
        if (d.id == id)
            return &d;
    }
    return nullptr;
}

const ClosingModel::Doc *ClosingModel::doc(const QString &id) const
{
    for (const Doc &d : m_docs) {
        if (d.id == id)
            return &d;
    }
    return nullptr;
}

ClosingModel::Snapshot ClosingModel::snapshot() const
{
    Snapshot s;
    const bool finished = isFinished();
    const QList<const Doc *> rows = finished ? QList<const Doc *>() : listed();

    // 1.1, 1.2
    if (finished)
        s.prompt = Prompt::None;
    else
        s.prompt = m_single && rows.size() == 1 ? Prompt::Single : Prompt::List;
    s.count = finished ? 0 : unsavedCount(); // 2.1

    for (const Doc *d : rows) { // 2.2, 1.4
        Row r;
        r.id = d->id;
        r.name = d->name;
        r.where = d->path.isEmpty() ? QString() : d->path;
        r.readOnly = d->readOnly;
        r.ticked = d->state == State::Unsaved && d->ticked;
        if (m_request.type == RequestType::Save && m_request.id == d->id)
            r.state = RowState::Saving;
        else
            r.state = d->state == State::Saved ? RowState::Saved : RowState::Unsaved;
        r.note = d->note;
        s.rows.append(r);
    }

    s.request = m_request;
    s.outcome = m_outcome;
    for (const Doc &d : m_docs) {
        (d.open ? s.open : s.closed).append(d.id);
        if (!d.savedTo.isEmpty())
            s.saved.insert(d.id, d.savedTo);
        if (d.lost)
            s.lost.append(d.id); // closed with unsaved changes, by the user's choice
    }
    s.abandoned = m_abandoned;
    return s;
}

QString ClosingModel::name(const QString &id) const
{
    const Doc *d = doc(id);
    return d ? d->name : QString();
}

QString ClosingModel::path(const QString &id) const
{
    const Doc *d = doc(id);
    return d ? d->path : QString();
}

// ---- the window's buttons -----------------------------------------------------------------------

bool ClosingModel::idle() const
{
    return m_outcome == Outcome::Pending && m_request.type == RequestType::None;
}

bool ClosingModel::tick(const QString &id, bool on)
{
    Doc *d = doc(id);
    if (!idle() || !d || !d->open || d->state != State::Unsaved)
        return false;
    d->ticked = on; // 4.4: only this row changes
    return true;
}

bool ClosingModel::saveSelected()
{
    if (!idle())
        return false;
    QList<QString> queue; // row order
    for (const Doc *d : listed()) {
        if (d->state == State::Unsaved && d->ticked)
            queue.append(d->id);
    }
    return start(queue, snapshot().prompt == Prompt::Single ? RunMode::Single : RunMode::Selected);
}

bool ClosingModel::dontSave()
{
    if (!idle())
        return false;
    proceed();
    return true;
}

bool ClosingModel::cancel()
{
    if (isFinished())
        return false;
    if (m_request.type == RequestType::Pick)
        return false; // the picker is in front; answer it first
    if (m_request.type == RequestType::Save)
        m_abandoned.append(m_request.id); // 6.2: abandon the save in progress
    m_request = Request();
    m_outcome = Outcome::Stopped;
    return true;
}

// ---- a row's own buttons (section 4) ------------------------------------------------------------

bool ClosingModel::rowSave(const QString &id)
{
    const Doc *d = doc(id);
    if (!idle() || !d || !d->open || d->state != State::Unsaved)
        return false;
    return start({id}, RunMode::Row);
}

bool ClosingModel::rowDiscard(const QString &id)
{
    Doc *d = doc(id);
    if (!idle() || !d || !d->open || d->state != State::Unsaved)
        return false;
    d->open = false; // 4.2
    d->lost = true;
    lastRowGone();
    return true;
}

// ---- the application's answers ------------------------------------------------------------------

bool ClosingModel::picked(const QString &path)
{
    if (m_request.type != RequestType::Pick)
        return false;
    const QString id = m_request.id;
    m_request = Request();
    m_request.type = RequestType::Save;
    m_request.id = id;
    m_request.path = path;
    return true;
}

bool ClosingModel::pickCancelled()
{
    if (m_request.type != RequestType::Pick)
        return false;
    doc(m_request.id)->note = Note{NoteKind::Cancelled, QString()};
    m_run.failed = true; // 3.1.3: a cancelled picker stops the run
    m_run.queue.clear();
    m_request = Request();
    finishRun();
    return true;
}

bool ClosingModel::saveDone()
{
    if (m_request.type != RequestType::Save)
        return false;
    Doc *d = doc(m_request.id);
    d->path = m_request.path;
    d->readOnly = false;
    d->state = State::Saved;
    d->savedTo = d->path;
    d->note = Note();
    m_run.queue.removeFirst();
    m_request = Request();
    next();
    return true;
}

bool ClosingModel::saveFailed(const QString &reason)
{
    if (m_request.type != RequestType::Save)
        return false;
    doc(m_request.id)->note = Note{NoteKind::Failed, reason}; // 3.4: the row says why
    m_run.failed = true;
    m_run.queue.removeFirst();
    m_request = Request();
    next(); // 3.1.3: the others still save
    return true;
}

bool ClosingModel::docChanged(const QString &id)
{
    Doc *d = doc(id);
    if (isFinished() || !d || !d->open)
        return false;
    if (m_request.type != RequestType::None && m_request.id == id)
        return true; // being saved now: the save takes its latest state
    if (d->state == State::Clean)
        d->ticked = true; // a new row joins the list, ticked (2.4)
    d->state = State::Unsaved;
    d->savedTo.clear();
    return true;
}

// ---- how a run of saves goes --------------------------------------------------------------------

bool ClosingModel::start(const QList<QString> &queue, RunMode mode)
{
    for (const QString &id : queue)
        doc(id)->note = Note();
    m_run = Run();
    m_run.queue = queue;
    m_run.mode = mode;
    m_run.target = mode == RunMode::Row ? queue.value(0) : QString();
    next();
    return true;
}

void ClosingModel::next()
{
    if (m_run.queue.isEmpty()) {
        finishRun();
        return;
    }
    const Doc *d = doc(m_run.queue.first());
    m_request = Request();
    m_request.id = d->id;
    // 3.1: ask where only for a document with no file to write to; 6.1: or whose file can't be
    // written.
    if (d->path.isEmpty()) {
        m_request.type = RequestType::Pick;
        m_request.why = PickReason::NeverSaved;
    } else if (d->readOnly) {
        m_request.type = RequestType::Pick;
        m_request.why = PickReason::ReadOnly;
    } else {
        m_request.type = RequestType::Save;
        m_request.path = d->path;
    }
}

void ClosingModel::finishRun()
{
    const Run run = m_run;
    m_run = Run();
    if (run.mode == RunMode::Row) {
        // 4.1: a row that saved closes and leaves the list; one that didn't stays, with its reason.
        Doc *d = doc(run.target);
        if (d->state == State::Saved)
            d->open = false;
        lastRowGone();
        return;
    }
    if (!run.failed)
        proceed(); // 3.1.2. Otherwise 3.1.1: nothing closes and the window stays.
}

// 4.3: when no unsaved row is left, the action carries on.
void ClosingModel::lastRowGone()
{
    if (unsavedCount() == 0)
        proceed();
}

void ClosingModel::proceed()
{
    for (Doc &d : m_docs) {
        if (d.open && d.state == State::Unsaved)
            d.lost = true;
        d.open = false;
    }
    m_request = Request();
    m_run = Run();
    m_outcome = Outcome::Proceeded;
}

} // namespace nflb
