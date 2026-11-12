// A tiny editor to try the list of unsaved work. Each tab is a document; tick "Save will fail" or
// "Save is slow" on a tab, then close it, use Close All, or quit.
//
// It answers the dialog through signals, the way a real app with slow saves and a native file
// dialog would. The files live in a temporary folder that is removed when the app quits.

#include "unsavedlistdialog.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QMainWindow>
#include <QMenuBar>
#include <QPlainTextEdit>
#include <QPointer>
#include <QSaveFile>
#include <QStatusBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QVBoxLayout>

using nflb::UnsavedListDialog;

struct Document
{
    QString id;
    QString name;
    QString path; // empty: never saved
    QWidget *page = nullptr;
    QPlainTextEdit *editor = nullptr;
    QCheckBox *fails = nullptr;
    QCheckBox *slow = nullptr;
};

class Editor : public QMainWindow
{
    Q_OBJECT

public:
    Editor()
    {
        m_tabs = new QTabWidget(this);
        m_tabs->setTabsClosable(true);
        m_tabs->setDocumentMode(true);
        setCentralWidget(m_tabs);
        connect(m_tabs, &QTabWidget::tabCloseRequested, this, [this](int index) {
            closeDocuments({documentAt(index)});
        });

        QMenu *file = menuBar()->addMenu(tr("&File"));
        file->addAction(tr("&New"), QKeySequence::New, this, [this] { addDocument(nextUntitled(), QString(), QString(), true); });
        file->addAction(tr("Close &All"), this, [this] { closeDocuments(m_docs); });
        QAction *list = file->addAction(tr("Use the List for One Document Too"));
        list->setCheckable(true);
        connect(list, &QAction::toggled, this, [this](bool on) { m_listForOne = on; });
        file->addSeparator();
        file->addAction(tr("&Quit"), QKeySequence::Quit, this, &QWidget::close);

        // A few documents: two with files (one read-only), one never saved, one with no changes.
        const QString dir = m_dir.path();
        write(dir + "/Bracket.txt", "A bracket.\n");
        write(dir + "/Lid.txt", "A lid.\n");
        write(dir + "/Gearbox.txt", "A gearbox.\n");
        QFile::setPermissions(dir + "/Lid.txt", QFile::ReadOwner | QFile::ReadGroup | QFile::ReadOther);
        addDocument(tr("Bracket"), dir + "/Bracket.txt", "A bracket, now with holes.\n", true);
        addDocument(tr("Lid"), dir + "/Lid.txt", "A lid, now with a hinge.\n", true);
        addDocument(tr("Untitled"), QString(), "Notes for later.\n", true);
        addDocument(tr("Gearbox"), dir + "/Gearbox.txt", QString(), false);

        resize(720, 420);
        statusBar()->showMessage(tr("Files are in %1").arg(QDir::toNativeSeparators(dir)));
    }

protected:
    void closeEvent(QCloseEvent *event) override
    {
        if (closeDocuments(m_docs))
            event->accept();
        else
            event->ignore(); // Cancel, or a save that didn't happen: the app stays open (3.1.1, 3.3)
    }

private:
    static void write(const QString &path, const QByteArray &text)
    {
        QFile f(path);
        if (f.open(QIODevice::WriteOnly))
            f.write(text);
    }

    QString nextUntitled() const
    {
        // 6.3: never-saved documents with the same name are told apart by order.
        QString name = tr("Untitled");
        for (int n = 2;; ++n) {
            bool taken = false;
            for (const Document *d : m_docs)
                taken = taken || d->name == name;
            if (!taken)
                return name;
            name = tr("Untitled %1").arg(n);
        }
    }

    void addDocument(const QString &name, const QString &path, const QString &text, bool changed)
    {
        auto *d = new Document;
        d->id = QString::number(++m_lastId);
        d->name = name;
        d->path = path;
        d->page = new QWidget;
        d->editor = new QPlainTextEdit(d->page);
        QFile f(path);
        if (!path.isEmpty() && f.open(QIODevice::ReadOnly))
            d->editor->setPlainText(QString::fromUtf8(f.readAll()));
        if (changed)
            d->editor->setPlainText(text);
        d->editor->document()->setModified(changed);
        d->fails = new QCheckBox(tr("Save will fail"), d->page);
        d->slow = new QCheckBox(tr("Save is slow (3 seconds)"), d->page);

        auto *switches = new QHBoxLayout;
        switches->addWidget(d->fails);
        switches->addWidget(d->slow);
        switches->addStretch(1);
        auto *layout = new QVBoxLayout(d->page);
        layout->addWidget(d->editor);
        layout->addLayout(switches);

        m_docs.append(d);
        m_tabs->addTab(d->page, QString());
        connect(d->editor->document(), &QTextDocument::modificationChanged, this, [this, d] {
            updateTab(d);
            // 6.5: a change while the list is open gets a row, or needs saving again.
            if (m_dialog && d->editor->document()->isModified())
                m_dialog->documentChanged(d->id);
        });
        updateTab(d);
        m_tabs->setCurrentWidget(d->page);
    }

    void updateTab(Document *d)
    {
        const int i = m_tabs->indexOf(d->page);
        const bool changed = d->editor->document()->isModified();
        m_tabs->setTabText(i, changed ? d->name + QStringLiteral(" •") : d->name);
        m_tabs->setTabToolTip(i, d->path.isEmpty() ? tr("Not saved yet") : QDir::toNativeSeparators(d->path));
    }

    Document *documentAt(int index) const
    {
        for (Document *d : m_docs) {
            if (m_tabs->indexOf(d->page) == index)
                return d;
        }
        return nullptr;
    }

    Document *find(const QString &id) const
    {
        for (Document *d : m_docs) {
            if (d->id == id)
                return d;
        }
        return nullptr;
    }

    // Closes `docs`, asking about unsaved ones first. Returns true when the action may go ahead.
    bool closeDocuments(const QList<Document *> &docs)
    {
        if (docs.isEmpty())
            return true;
        QList<UnsavedListDialog::Document> list; // 1.3: only the documents this action closes
        for (const Document *d : docs) {
            const bool readOnly = !d->path.isEmpty() && !QFileInfo(d->path).isWritable();
            list.append({d->id, d->name, d->path, d->editor->document()->isModified(), readOnly});
        }

        UnsavedListDialog dialog(list, this);
        dialog.setSinglePromptEnabled(!m_listForOne);
        m_dialog = &dialog;
        connect(&dialog, &UnsavedListDialog::saveRequested, this, &Editor::save);
        connect(&dialog, &UnsavedListDialog::saveAbandoned, this, &Editor::abandon);
        connect(&dialog, &UnsavedListDialog::pickRequested, this, &Editor::pick);
        connect(&dialog, &UnsavedListDialog::showDocumentRequested, this, [this](const QString &id) {
            if (Document *d = find(id))
                m_tabs->setCurrentWidget(d->page);
        });
        // A row answered on its own closes at once (4.1, 4.2), even if the user then cancels.
        connect(&dialog, &UnsavedListDialog::documentClosed, this, [this](const QString &id) {
            if (Document *d = find(id))
                remove(d);
        });

        const bool goAhead = dialog.exec() == QDialog::Accepted;
        m_dialog = nullptr;
        const QStringList lost = dialog.lostIds();
        statusBar()->showMessage(goAhead ? tr("Closed. %n document(s) closed without saving.", nullptr, int(lost.size()))
                                         : tr("Cancelled: what is still open stays open."));
        return goAhead;
    }

    void remove(Document *d)
    {
        m_docs.removeOne(d);
        m_tabs->removeTab(m_tabs->indexOf(d->page));
        d->page->deleteLater();
        delete d;
    }

    void pick(const QString &id, const QString &name, UnsavedListDialog::PickReason why)
    {
        // A window-modal file dialog: the list waits for its answer (3.1), and the picker names
        // the document (3.1).
        Document *d = find(id);
        auto *picker = new QFileDialog(m_dialog,
                                       why == UnsavedListDialog::PickReason::ReadOnly
                                           ? tr("Save a Copy of “%1”").arg(name)
                                           : tr("Save “%1” As").arg(name),
                                       m_dir.path(), tr("Text (*.txt)"));
        picker->setAcceptMode(QFileDialog::AcceptSave);
        picker->setDefaultSuffix("txt");
        picker->selectFile(d && !d->path.isEmpty() ? QFileInfo(d->path).completeBaseName() + tr(" copy") : name);
        picker->setAttribute(Qt::WA_DeleteOnClose);
        QPointer<UnsavedListDialog> dialog = m_dialog;
        connect(picker, &QFileDialog::fileSelected, this, [dialog](const QString &path) {
            if (dialog)
                dialog->picked(path);
        });
        connect(picker, &QFileDialog::rejected, this, [dialog] {
            if (dialog)
                dialog->pickCancelled();
        });
        picker->open();
    }

    void save(const QString &id, const QString &path)
    {
        Document *d = find(id);
        if (!d)
            return;
        if (d->slow->isChecked()) {
            // A slow save: the list shows "Saving…" and only Cancel stays available (6.2).
            m_pending = new QTimer(this);
            m_pending->setSingleShot(true);
            connect(m_pending, &QTimer::timeout, this, [this, id, path] {
                m_pending->deleteLater();
                m_pending = nullptr;
                finishSave(id, path);
            });
            m_pending->start(3000);
            return;
        }
        finishSave(id, path);
    }

    void abandon(const QString &)
    {
        // Nothing was written yet: the file is only replaced when a save completes (6.2).
        if (m_pending) {
            m_pending->stop();
            m_pending->deleteLater();
            m_pending = nullptr;
        }
    }

    void finishSave(const QString &id, const QString &path)
    {
        Document *d = find(id);
        if (!d || !m_dialog)
            return;
        if (d->fails->isChecked()) {
            m_dialog->saveFailed(tr("The disk is full: free some space and try again.")); // 3.4
            return;
        }
        // QSaveFile writes to a temporary file and replaces the original only on success (6.2).
        QSaveFile out(path);
        if (!out.open(QIODevice::WriteOnly)) {
            m_dialog->saveFailed(tr("You don't have permission to write to %1.")
                                     .arg(QDir::toNativeSeparators(QFileInfo(path).absolutePath())));
            return;
        }
        out.write(d->editor->toPlainText().toUtf8());
        if (!out.commit()) {
            m_dialog->saveFailed(out.errorString());
            return;
        }
        d->path = path;
        d->editor->document()->setModified(false);
        updateTab(d);
        m_dialog->saveDone();
    }

    QTemporaryDir m_dir;
    QTabWidget *m_tabs = nullptr;
    QList<Document *> m_docs;
    QPointer<UnsavedListDialog> m_dialog;
    QTimer *m_pending = nullptr;
    int m_lastId = 0;
    bool m_listForOne = false;
};

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    Editor editor;
    editor.show();
    return app.exec();
}

#include "main.moc"
