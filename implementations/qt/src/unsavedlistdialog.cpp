#include "unsavedlistdialog.h"

#include <QAccessible>
#include <QAccessibleWidget>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMetaMethod>
#include <QPainter>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace nflb {

namespace {

// A one-line label that shortens long text in the middle, so a deep path can't push the dialog
// wider than the screen (2.5). The full text is in its tooltip and accessible name.
class ElidedLabel : public QLabel
{
public:
    using QLabel::QLabel;

    void setFullText(const QString &text)
    {
        m_full = text;
        setToolTip(text);
        setAccessibleName(text);
        updateGeometry();
        update();
    }

    QSize minimumSizeHint() const override { return QSize(0, QLabel::minimumSizeHint().height()); }

    QSize sizeHint() const override
    {
        const QFontMetrics fm(font());
        return QSize(fm.horizontalAdvance(m_full) + 2, fm.height());
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        const QString shown = fontMetrics().elidedText(m_full, Qt::ElideMiddle, contentsRect().width());
        // The label's foreground role carries the muted colour, so draw with it rather than a
        // fixed colour: the app's palette and style decide.
        style()->drawItemText(&p, contentsRect(), Qt::AlignLeft | Qt::AlignVCenter, palette(), isEnabled(),
                              shown, foregroundRole());
    }

private:
    QString m_full;
};

QFont scaled(QFont f, qreal factor, bool bold)
{
    if (f.pointSizeF() > 0)
        f.setPointSizeF(f.pointSizeF() * factor);
    else if (f.pixelSize() > 0)
        f.setPixelSize(qRound(f.pixelSize() * factor));
    f.setBold(bold);
    return f;
}

} // namespace

// One row of the list (2.2). It is its own class so assistive technology can be told it is a
// group named after its document (5.3).
class UnsavedRow : public QFrame
{
    Q_OBJECT

public:
    UnsavedRow(const ClosingModel::Row &row, UnsavedListDialog *dialog)
        : QFrame(dialog), m_id(row.id)
    {
        setObjectName(QStringLiteral("row:") + row.id);
        setAccessibleName(row.name);

        tick = new QCheckBox(this);
        tick->setObjectName(QStringLiteral("tick"));
        tick->setAccessibleName(UnsavedListDialog::tr("Save %1").arg(row.name)); // 5.3

        name = new QToolButton(this);
        name->setObjectName(QStringLiteral("name"));
        name->setAutoRaise(true);
        name->setText(row.name);
        name->setFont(scaled(name->font(), 1.0, true));
        name->setToolTip(UnsavedListDialog::tr("Show this document"));
        name->setAccessibleName(row.name);
        name->setAccessibleDescription(UnsavedListDialog::tr("Show this document"));
        name->setFocusPolicy(Qt::StrongFocus); // 5.2: the name is in the Tab order

        where = new ElidedLabel(this);
        where->setObjectName(QStringLiteral("where"));
        where->setForegroundRole(QPalette::PlaceholderText); // muted, from the palette (2.2)
        where->setFont(scaled(where->font(), 0.9, false));
        where->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);

        noteIcon = new QLabel(this);
        noteIcon->setObjectName(QStringLiteral("noteIcon"));
        note = new QLabel(this);
        note->setObjectName(QStringLiteral("note"));
        note->setWordWrap(true);
        note->setTextInteractionFlags(Qt::TextSelectableByMouse);

        save = new QPushButton(UnsavedListDialog::tr("Save"), this);
        save->setObjectName(QStringLiteral("save"));
        save->setAccessibleName(UnsavedListDialog::tr("Save %1").arg(row.name)); // 5.3
        save->setAutoDefault(false); // 5.1: Enter stays with Save Selected

        discard = new QPushButton(UnsavedListDialog::tr("Discard"), this);
        discard->setObjectName(QStringLiteral("discard"));
        discard->setAccessibleName(UnsavedListDialog::tr("Discard %1").arg(row.name)); // 5.3
        discard->setAutoDefault(false);

        auto *noteLine = new QHBoxLayout;
        noteLine->setContentsMargins(0, 0, 0, 0);
        noteLine->addWidget(noteIcon, 0, Qt::AlignTop);
        noteLine->addWidget(note, 1);

        auto *grid = new QGridLayout(this);
        grid->setContentsMargins(4, 4, 4, 4);
        grid->setVerticalSpacing(1);
        grid->addWidget(tick, 0, 0);
        grid->addWidget(name, 0, 1, Qt::AlignLeft);
        grid->addWidget(save, 0, 2);
        grid->addWidget(discard, 0, 3);
        grid->addWidget(where, 1, 1, 1, 3);
        grid->addLayout(noteLine, 2, 1, 1, 3);
        grid->setColumnStretch(1, 1);

        connect(tick, &QCheckBox::toggled, dialog, [dialog, this](bool on) { dialog->setTicked(m_id, on); });
        connect(name, &QToolButton::clicked, dialog, [dialog, this] { emit dialog->showDocumentRequested(m_id); });
        connect(save, &QPushButton::clicked, dialog, [dialog, this] { dialog->rowSave(m_id); });
        connect(discard, &QPushButton::clicked, dialog, [dialog, this] { dialog->rowDiscard(m_id); });
    }

    void display(const ClosingModel::Row &row, bool busy, const QString &noteText)
    {
        const bool open = row.state == ClosingModel::RowState::Unsaved;
        {
            const QSignalBlocker block(tick);
            tick->setChecked(row.ticked);
        }
        tick->setEnabled(!busy && open);
        name->setEnabled(!busy);
        save->setEnabled(!busy && open);
        discard->setEnabled(!busy && open);
        where->setFullText(row.where.isNull() ? UnsavedListDialog::tr("Not saved yet")
                                              : QDir::toNativeSeparators(row.where));

        note->setText(noteText);
        note->setVisible(!noteText.isEmpty());
        // The failure reason must stand out without a fixed colour (3.4): bold, with the style's
        // warning icon.
        const bool bad = row.note.kind != ClosingModel::NoteKind::None && row.state != ClosingModel::RowState::Saving;
        note->setFont(scaled(font(), 0.9, bad));
        note->setForegroundRole(bad ? QPalette::WindowText : QPalette::PlaceholderText);
        if (bad) {
            const int size = style()->pixelMetric(QStyle::PM_SmallIconSize, nullptr, this);
            noteIcon->setPixmap(style()->standardIcon(QStyle::SP_MessageBoxWarning, nullptr, this).pixmap(size, size));
        }
        noteIcon->setVisible(bad);
    }

    QString id() const { return m_id; }

    QCheckBox *tick;
    QToolButton *name;
    ElidedLabel *where;
    QLabel *noteIcon;
    QLabel *note;
    QPushButton *save;
    QPushButton *discard;

private:
    QString m_id;
};

namespace {

// 5.3: each row is one group for assistive technology, named after its document.
QAccessibleInterface *rowAccessible(const QString &className, QObject *object)
{
    if (className == QLatin1String("nflb::UnsavedRow") && object && object->isWidgetType())
        return new QAccessibleWidget(static_cast<QWidget *>(object), QAccessible::Grouping);
    return nullptr;
}

bool sameRequest(const ClosingModel::Request &a, const ClosingModel::Request &b)
{
    return a.type == b.type && a.id == b.id && a.path == b.path && a.why == b.why;
}

} // namespace

UnsavedListDialog::UnsavedListDialog(const QList<Document> &docs, QWidget *parent)
    : QDialog(parent), m_model(docs), m_singular(tr("document")), m_plural(tr("documents"))
{
    static bool factoryInstalled = false;
    if (!factoryInstalled) {
        QAccessible::installFactory(rowAccessible);
        factoryInstalled = true;
    }
    setModal(true);
    buildUi();
    refresh();
}

UnsavedListDialog::~UnsavedListDialog() = default;

void UnsavedListDialog::buildUi()
{
    m_icon = new QLabel(this);
    const int iconSize = style()->pixelMetric(QStyle::PM_MessageBoxIconSize, nullptr, this);
    m_icon->setPixmap(style()->standardIcon(QStyle::SP_MessageBoxWarning, nullptr, this).pixmap(iconSize, iconSize));

    m_title = new QLabel(this);
    m_title->setObjectName(QStringLiteral("title"));
    m_title->setWordWrap(true);
    m_title->setFont(scaled(font(), 1.15, true));

    m_help = new QLabel(this);
    m_help->setObjectName(QStringLiteral("help"));
    m_help->setWordWrap(true);
    m_help->setForegroundRole(QPalette::PlaceholderText);

    m_singleWhere = new QLabel(this);
    m_singleWhere->setObjectName(QStringLiteral("singleWhere"));
    m_singleWhere->setForegroundRole(QPalette::PlaceholderText);
    m_singleWhere->setWordWrap(true);

    m_singleNote = new QLabel(this);
    m_singleNote->setObjectName(QStringLiteral("singleNote"));
    m_singleNote->setWordWrap(true);
    m_singleNote->setFont(scaled(font(), 1.0, true));

    auto *texts = new QVBoxLayout;
    texts->addWidget(m_title);
    texts->addWidget(m_singleWhere);
    texts->addWidget(m_singleNote);
    texts->addWidget(m_help);

    auto *top = new QHBoxLayout;
    top->addWidget(m_icon, 0, Qt::AlignTop);
    top->addLayout(texts, 1);

    m_rowsHost = new QWidget;
    m_rowsHost->setObjectName(QStringLiteral("rows"));
    m_rowsLayout = new QVBoxLayout(m_rowsHost);
    m_rowsLayout->setContentsMargins(0, 0, 0, 0);
    m_rowsLayout->setSpacing(2);
    m_rowsLayout->addStretch(1);

    // 2.5: the rows scroll inside the window; the buttons below stay put.
    m_scroll = new QScrollArea(this);
    m_scroll->setObjectName(QStringLiteral("scroll"));
    m_scroll->setWidget(m_rowsHost);
    m_scroll->setWidgetResizable(true);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setFrameShape(QFrame::StyledPanel);

    // 2.3: the platform decides the order of the three buttons. Save Selected is the default;
    // Don't Save carries the destructive role and is never the default (3.2).
    m_buttons = new QDialogButtonBox(this);
    m_save = m_buttons->addButton(QDialogButtonBox::Save);
    m_dontSave = m_buttons->addButton(QDialogButtonBox::Discard);
    m_cancel = m_buttons->addButton(QDialogButtonBox::Cancel);
    m_save->setObjectName(QStringLiteral("saveSelected"));
    m_dontSave->setObjectName(QStringLiteral("dontSave"));
    m_cancel->setObjectName(QStringLiteral("cancel"));
    m_dontSave->setText(tr("Don't Save"));
    m_cancel->setText(tr("Cancel"));
    m_save->setDefault(true);
    m_save->setAutoDefault(true);
    m_dontSave->setAutoDefault(false);
    connect(m_buttons, &QDialogButtonBox::clicked, this, &UnsavedListDialog::onButton);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(top);
    layout->addWidget(m_scroll, 1);
    layout->addWidget(m_buttons);

    m_save->setFocus(); // the default button has focus when the window opens (5.1)
}

void UnsavedListDialog::onButton(QAbstractButton *button)
{
    if (button == m_save)
        saveSelected();
    else if (button == m_dontSave)
        dontSave();
    else if (button == m_cancel)
        cancel();
}

// ---- settings -----------------------------------------------------------------------------------

void UnsavedListDialog::setNoun(const QString &singular, const QString &plural)
{
    m_singular = singular;
    m_plural = plural;
    refresh();
}

void UnsavedListDialog::setSinglePromptEnabled(bool enabled)
{
    m_singleEnabled = enabled;
    refresh();
}

void UnsavedListDialog::setSaver(Saver saver)
{
    m_saver = std::move(saver);
}

void UnsavedListDialog::setPicker(Picker picker)
{
    m_picker = std::move(picker);
}

void UnsavedListDialog::setFileFilter(const QString &filter, const QString &defaultSuffix)
{
    m_filter = filter;
    m_suffix = defaultSuffix;
}

bool UnsavedListDialog::needsAsking() const
{
    return !m_model.isFinished();
}

// ---- showing and finishing ----------------------------------------------------------------------

int UnsavedListDialog::exec()
{
    if (m_model.isFinished()) { // nothing unsaved: no window (1.4)
        finishIfDone();
        return result();
    }
    return QDialog::exec();
}

void UnsavedListDialog::open()
{
    if (m_model.isFinished()) {
        QTimer::singleShot(0, this, [this] { finishIfDone(); });
        return;
    }
    QDialog::open();
}

void UnsavedListDialog::finishIfDone()
{
    const ClosingModel::Snapshot s = m_model.snapshot();
    if (s.outcome == Outcome::Pending || m_finished)
        return;
    m_finished = true;
    announceClosed(s);
    done(s.outcome == Outcome::Proceeded ? QDialog::Accepted : QDialog::Rejected);
}

void UnsavedListDialog::announceClosed(const ClosingModel::Snapshot &s)
{
    for (const QString &id : s.closed) {
        if (!m_announcedClosed.contains(id)) {
            m_announcedClosed.insert(id);
            emit documentClosed(id);
        }
    }
}

UnsavedListDialog::Outcome UnsavedListDialog::outcome() const
{
    return m_model.snapshot().outcome;
}

QList<QString> UnsavedListDialog::savedIds() const
{
    return m_model.snapshot().saved.keys();
}

QMap<QString, QString> UnsavedListDialog::savedPaths() const
{
    return m_model.snapshot().saved;
}

QList<QString> UnsavedListDialog::lostIds() const
{
    return m_model.snapshot().lost;
}

QList<QString> UnsavedListDialog::stillOpenIds() const
{
    return m_model.snapshot().open;
}

QList<QString> UnsavedListDialog::closedIds() const
{
    return m_model.snapshot().closed;
}

QList<QString> UnsavedListDialog::abandonedIds() const
{
    return m_model.snapshot().abandoned;
}

// ---- inputs -------------------------------------------------------------------------------------

bool UnsavedListDialog::setTicked(const QString &id, bool on)
{
    const bool ok = m_model.tick(id, on);
    after(ok);
    return ok;
}

bool UnsavedListDialog::saveSelected()
{
    const bool ok = m_model.saveSelected();
    after(ok);
    return ok;
}

bool UnsavedListDialog::dontSave()
{
    const bool ok = m_model.dontSave();
    after(ok);
    return ok;
}

bool UnsavedListDialog::cancel()
{
    const ClosingModel::Request inFlight = m_model.snapshot().request;
    const bool ok = m_model.cancel();
    // 6.2: Cancel during a save abandons it; the app must stop and keep the original intact.
    if (ok && inFlight.type == ClosingModel::RequestType::Save)
        emit saveAbandoned(inFlight.id);
    after(ok);
    return ok;
}

void UnsavedListDialog::reject()
{
    cancel();
}

bool UnsavedListDialog::rowSave(const QString &id)
{
    const bool ok = m_model.rowSave(id);
    after(ok);
    return ok;
}

bool UnsavedListDialog::rowDiscard(const QString &id)
{
    const bool ok = m_model.rowDiscard(id);
    after(ok);
    return ok;
}

bool UnsavedListDialog::saveDone()
{
    const bool ok = m_model.saveDone();
    after(ok);
    return ok;
}

bool UnsavedListDialog::saveFailed(const QString &reason)
{
    const bool ok = m_model.saveFailed(reason.isEmpty() ? tr("The file could not be saved.") : reason);
    after(ok);
    return ok;
}

bool UnsavedListDialog::picked(const QString &path)
{
    if (path.isEmpty()) // an empty answer is no place at all
        return pickCancelled();
    const bool ok = m_model.picked(path);
    after(ok);
    return ok;
}

bool UnsavedListDialog::pickCancelled()
{
    const bool ok = m_model.pickCancelled();
    after(ok);
    return ok;
}

bool UnsavedListDialog::documentChanged(const QString &id)
{
    const bool ok = m_model.docChanged(id);
    after(ok);
    return ok;
}

void UnsavedListDialog::after(bool)
{
    // Even an ignored input redraws, so a check box the model refused goes back to its state.
    pump();
}

// Draws the model and hands any new request to the application. An answer may arrive while a
// request is being handed over (a saver that returns at once, or a slot that answers inside the
// signal), so this loops instead of recursing.
void UnsavedListDialog::pump()
{
    if (m_pumping) {
        m_again = true;
        return;
    }
    m_pumping = true;
    do {
        m_again = false;
        refresh();
        const ClosingModel::Snapshot s = m_model.snapshot();
        announceClosed(s);
        if (s.outcome != Outcome::Pending)
            break;
        if (s.request.type == ClosingModel::RequestType::None) {
            m_issuedRequest = ClosingModel::Request();
            break;
        }
        if (sameRequest(s.request, m_issuedRequest))
            break; // already asked; waiting for the answer
        m_issuedRequest = s.request;
        issue(s.request);
    } while (m_again);
    m_pumping = false;
    finishIfDone();
}

void UnsavedListDialog::issue(const ClosingModel::Request &request)
{
    if (request.type == ClosingModel::RequestType::Save) {
        if (m_saver) {
            if (isVisible())
                repaint(); // show "Saving…" before a save that blocks (6.2)
            QString whyNot;
            if (m_saver(request.id, request.path, &whyNot))
                saveDone();
            else
                saveFailed(whyNot);
            return;
        }
        if (!isSignalConnected(QMetaMethod::fromSignal(&UnsavedListDialog::saveRequested)))
            qWarning("nflb::UnsavedListDialog: no saver set and nothing connected to saveRequested()");
        emit saveRequested(request.id, request.path);
        return;
    }

    const QString name = m_model.name(request.id);
    if (m_picker) {
        picked(m_picker(request.id, name, request.why));
        return;
    }
    if (isSignalConnected(QMetaMethod::fromSignal(&UnsavedListDialog::pickRequested))) {
        emit pickRequested(request.id, name, request.why);
        return;
    }
    picked(pickWithFileDialog(request.id, name, request.why));
}

QString UnsavedListDialog::pickWithFileDialog(const QString &id, const QString &name, PickReason why)
{
    // 3.1: the picker names the document it is asking about; 6.1: and says why it asks.
    const QString title = why == PickReason::ReadOnly ? tr("Save a Copy of “%1”").arg(name)
                                                      : tr("Save “%1” As").arg(name);
    QFileDialog picker(this, title);
    picker.setAcceptMode(QFileDialog::AcceptSave);
    picker.setFileMode(QFileDialog::AnyFile);
    if (!m_filter.isEmpty())
        picker.setNameFilter(m_filter);
    if (!m_suffix.isEmpty())
        picker.setDefaultSuffix(m_suffix);
    const QString path = m_model.path(id);
    if (!path.isEmpty())
        picker.selectFile(path);
    else
        picker.selectFile(m_suffix.isEmpty() ? name : name + QLatin1Char('.') + m_suffix);
    if (picker.exec() != QDialog::Accepted)
        return QString();
    return picker.selectedFiles().value(0);
}

// ---- drawing ------------------------------------------------------------------------------------

QString UnsavedListDialog::noteText(const ClosingModel::Row &row) const
{
    using RS = ClosingModel::RowState;
    using NK = ClosingModel::NoteKind;
    if (row.state == RS::Saving)
        return tr("Saving…"); // 6.2
    if (row.note.kind == NK::Failed)
        return tr("Not saved: %1").arg(row.note.text); // 3.4
    if (row.note.kind == NK::Cancelled)
        return tr("Not saved: you cancelled choosing where.");
    if (row.state == RS::Saved)
        return tr("Saved"); // 3.1.1: documents that did save show as saved
    if (row.readOnly)
        return tr("The original file is read-only, so saving asks for a new place."); // 6.1, up front
    return QString();
}

void UnsavedListDialog::refresh()
{
    const ClosingModel::Snapshot s = m_model.snapshot();
    if (s.prompt == ClosingModel::Prompt::None)
        return; // finishing; keep the last picture

    const bool single = s.prompt == ClosingModel::Prompt::Single && m_singleEnabled;
    const bool busy = s.request.type != ClosingModel::RequestType::None;
    if (busy && !m_wasBusy)
        m_focusBeforeBusy = focusWidget(); // before disabling moves it

    // 6.2: while a save runs every button but Cancel waits; while a picker is open it is the
    // picker that gets answered, so Cancel waits too.
    m_save->setEnabled(!busy);
    m_dontSave->setEnabled(!busy);
    m_cancel->setEnabled(s.request.type != ClosingModel::RequestType::Pick);

    if (single)
        refreshSingle(s);
    else
        refreshList(s);

    // Disabling the buttons during a save pushes focus along to Cancel. Once the run ends, put it
    // back, so Enter is Save Selected again (5.1).
    if (!busy && m_wasBusy) {
        QWidget *back = m_focusBeforeBusy;
        if (back && back->isVisible() && back->isEnabled())
            back->setFocus();
        else if (!focusWidget() || focusWidget() == m_cancel || !focusWidget()->isEnabled())
            m_save->setFocus();
    }
    m_wasBusy = busy;

    // 5.4: the title, with its count, is the window's accessible name, so it is announced when the
    // window opens.
    const QString oldName = accessibleName();
    setWindowTitle(m_title->text());
    setAccessibleName(m_title->text());
    setAccessibleDescription(single ? m_singleWhere->text() : m_help->text());
    if (oldName != accessibleName() && isVisible()) {
        QAccessibleEvent event(this, QAccessible::NameChanged);
        QAccessible::updateAccessibility(&event);
    }
}

void UnsavedListDialog::refreshSingle(const ClosingModel::Snapshot &s)
{
    // 1.2: the familiar single-document prompt, drawn by the same dialog.
    const ClosingModel::Row &row = s.rows.first();
    m_icon->show();
    m_scroll->hide();
    m_title->setText(tr("Save changes to “%1”?").arg(row.name));
    // Keep a short question on one line, as a message box would.
    m_title->setMinimumWidth(qMin(m_title->fontMetrics().horizontalAdvance(m_title->text()) + 2, 480));
    m_singleWhere->setText(row.where.isNull() ? tr("Not saved yet") : QDir::toNativeSeparators(row.where));
    m_singleWhere->show();
    const QString note = noteText(row);
    m_singleNote->setText(note);
    m_singleNote->setVisible(!note.isEmpty());
    m_help->setText(tr("Your changes will be lost if you don't save them."));
    m_help->show();
    m_save->setText(tr("Save"));
}

void UnsavedListDialog::refreshList(const ClosingModel::Snapshot &s)
{
    m_icon->hide();
    m_singleWhere->hide();
    m_singleNote->hide();
    m_scroll->show();
    m_title->setText(tr("Save changes to %1 %2?").arg(s.count).arg(s.count == 1 ? m_singular : m_plural)); // 2.1
    m_title->setMinimumWidth(0);
    m_help->setText(tr("Ticked %1 are saved; the rest close without saving.").arg(m_plural));          // 2.6
    m_help->show();
    m_save->setText(tr("Save Selected"));

    const bool busy = s.request.type != ClosingModel::RequestType::None;
    QWidget *focus = focusWidget();
    bool lostFocus = false;
    int lostAt = 0;

    // Rows that left the list (4.1, 4.2).
    QSet<QString> listed;
    for (const ClosingModel::Row &row : s.rows)
        listed.insert(row.id);
    const QList<QString> ids = m_rows.keys();
    for (const QString &id : ids) {
        if (listed.contains(id))
            continue;
        UnsavedRow *gone = m_rows.take(id);
        if (focus && gone->isAncestorOf(focus)) {
            lostFocus = true;
            lostAt = m_rowsLayout->indexOf(gone);
        }
        m_rowsLayout->removeWidget(gone);
        gone->hide();
        gone->deleteLater();
    }

    // Rows in the model's order (2.2), including one that joined (6.5).
    for (int i = 0; i < s.rows.size(); ++i) {
        const ClosingModel::Row &row = s.rows.at(i);
        UnsavedRow *w = m_rows.value(row.id);
        if (!w) {
            w = new UnsavedRow(row, this);
            m_rows.insert(row.id, w);
        }
        if (m_rowsLayout->indexOf(w) != i) {
            m_rowsLayout->removeWidget(w);
            m_rowsLayout->insertWidget(i, w);
        }
        w->display(row, busy, noteText(row));
        w->setVisible(true);
    }

    // 5.2: Tab goes through each row (tick box, name, Save, Discard), then the window's buttons.
    QWidget *prev = nullptr;
    auto chain = [&prev](QWidget *w) {
        if (prev)
            QWidget::setTabOrder(prev, w);
        prev = w;
    };
    for (const ClosingModel::Row &row : s.rows) {
        UnsavedRow *w = m_rows.value(row.id);
        chain(w->tick);
        chain(w->name);
        chain(w->save);
        chain(w->discard);
    }
    for (QAbstractButton *b : m_buttons->buttons())
        chain(b);

    if (lostFocus) {
        // The row that had focus left: move to the row that took its place, or the default button.
        UnsavedRow *nextRow = s.rows.isEmpty() ? nullptr
                                               : m_rows.value(s.rows.at(qMin(lostAt, int(s.rows.size()) - 1)).id);
        if (nextRow && nextRow->tick->isEnabled())
            nextRow->tick->setFocus();
        else
            m_save->setFocus();
    }

    fitToScreen();
}

void UnsavedListDialog::fitToScreen()
{
    // 2.5: show every row when they fit; past that, the list scrolls and the window stays on the
    // screen with its buttons in view.
    const QScreen *scr = screen();
    const QRect avail = scr ? scr->availableGeometry() : QRect(0, 0, 1024, 768);
    m_rowsLayout->activate();
    const int frame = 2 * m_scroll->frameWidth();
    const int scrollBar = m_scroll->verticalScrollBar()->sizeHint().width();
    const int width = qMin(qMax(m_rowsHost->sizeHint().width() + frame + scrollBar, 420), avail.width() * 3 / 4);
    // Wrapped notes are taller when narrower, so ask for the height at the width the rows get.
    int rowsHeight = m_rowsHost->sizeHint().height();
    if (m_rowsHost->hasHeightForWidth())
        rowsHeight = qMax(rowsHeight, m_rowsHost->heightForWidth(width - frame - scrollBar));
    m_scroll->setMinimumHeight(qMin(rowsHeight + frame, avail.height() / 2));
    m_scroll->setMinimumWidth(width);
    setMaximumHeight(avail.height());
    adjustSize();
}

} // namespace nflb

#include "unsavedlistdialog.moc"
