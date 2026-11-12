//! The rules with no window attached: a port of `scenarios/reference/model.mjs`.
//!
//! [`Closing`] holds the documents one action is closing (a quit, Close All, closing a window) and
//! answers two questions at any moment: what should the window show, and what is the application
//! being asked to do? A window draws [`Closing::snapshot`] (or the accessors) and passes clicks in;
//! the application does the real saving and reports back.
//!
//! The model never touches a file. When it needs one it raises a [`Request`]:
//!
//! - [`Request::Pick`]: show a save picker that names this document, then answer
//!   [`Closing::picked`] or [`Closing::pick_cancelled`];
//! - [`Request::Save`]: write this document to this path, then answer [`Closing::save_done`] or
//!   [`Closing::save_failed`].
//!
//! Every input returns `true` when it was accepted and `false` when it was ignored (a button that
//! is disabled or waiting). Rule numbers refer to `SPEC.md`.

use std::path::{Path, PathBuf};

/// One document the action closes, as the application describes it when the action starts.
///
/// `K` is the application's own key for a document: a tab index, a slot-map key, a `u64`, a
/// `String`. The model is generic over it so the application gets its own key back in every
/// [`Request`] and row, and never has to keep a second table mapping made-up ids to documents.
/// The model only clones and compares keys.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Doc<K> {
    /// The application's key for this document.
    pub id: K,
    /// The name the user knows it by. The application tells same-named documents apart (6.3).
    pub name: String,
    /// Its file, or `None` if it has never been saved.
    pub path: Option<PathBuf>,
    /// Whether it has unsaved changes. Documents without are never listed (1.4).
    pub dirty: bool,
    /// Its file can't be written (read-only, or moved away): saving asks for a new place (6.1).
    pub read_only: bool,
}

impl<K> Doc<K> {
    /// A document with unsaved changes and a file it can be saved to.
    pub fn new(id: K, name: impl Into<String>, path: Option<PathBuf>) -> Self {
        Self {
            id,
            name: name.into(),
            path,
            dirty: true,
            read_only: false,
        }
    }

    /// Sets whether it has unsaved changes.
    pub fn dirty(mut self, dirty: bool) -> Self {
        self.dirty = dirty;
        self
    }

    /// Sets whether its file can't be written (6.1).
    pub fn read_only(mut self, read_only: bool) -> Self {
        self.read_only = read_only;
        self
    }
}

/// Why a save picker is needed.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum PickReason {
    /// The document has never been saved (3.1).
    NeverSaved,
    /// Its file can't be written where it is (6.1).
    ReadOnly,
}

/// What the application is being asked to do.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum Request<K> {
    /// Show a save picker for this document. The picker must name it (3.1).
    Pick { id: K, why: PickReason },
    /// Write this document to this path. Write to a temporary file and replace the original only on
    /// success, so an abandoned or failed save leaves no half-written file (6.2).
    Save { id: K, path: PathBuf },
}

impl<K> Request<K> {
    /// The document the request is about.
    pub fn id(&self) -> &K {
        match self {
            Request::Pick { id, .. } | Request::Save { id, .. } => id,
        }
    }
}

/// How the action ended.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum Outcome {
    /// The quit or close goes ahead: every listed document is closed.
    Proceeded,
    /// The user cancelled: the action stops, and what is still open stays open (3.3).
    Stopped,
}

/// Which prompt the window shows.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum Prompt {
    /// The list of unsaved work (1.1).
    List,
    /// The single-document prompt, for exactly one unsaved document (1.2).
    Single,
    /// No window: the action has finished.
    None,
}

/// Where a row stands.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum RowState {
    /// Still to be answered.
    Unsaved,
    /// Being written right now (6.2).
    Saving,
    /// Saved by this window, and still open because a later save in the same run didn't happen
    /// (3.1.1).
    Saved,
}

/// What a row says about its last attempt (3.4).
#[derive(Clone, Debug, PartialEq, Eq, Hash)]
pub enum Note {
    /// The save failed, with the application's reason in words the user can act on.
    Failed(String),
    /// The user cancelled the save picker.
    Cancelled,
}

/// One row of the list, as the window should draw it.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Row<K> {
    pub id: K,
    pub name: String,
    /// Where it lives: its file, or `None` for "Not saved yet" (2.2).
    pub location: Option<PathBuf>,
    /// Its file can't be written; the row says so up front (6.1).
    pub read_only: bool,
    /// The tick box. Only an unsaved row can be ticked.
    pub ticked: bool,
    pub state: RowState,
    pub note: Option<Note>,
}

/// Everything the window shows and the application needs, at one moment.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Snapshot<K> {
    pub prompt: Prompt,
    /// Unsaved documents still to answer: the number in the title (2.1).
    pub count: usize,
    /// One per listed document, in the order the user sees them (2.2).
    pub rows: Vec<Row<K>>,
    pub request: Option<Request<K>>,
    /// `None` while asking.
    pub outcome: Option<Outcome>,
    /// The action's documents still open, in order.
    pub open: Vec<K>,
    /// The action's documents closed so far, in order.
    pub closed: Vec<K>,
    /// Every document whose changes were saved, with where they went.
    pub saved: Vec<(K, PathBuf)>,
    /// Documents closed with unsaved changes, by the user's choice.
    pub lost: Vec<K>,
    /// Documents whose save was cancelled part-way: no half-written file may be left (6.2).
    pub abandoned: Vec<K>,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum DocState {
    Clean,
    Unsaved,
    Saved,
}

#[derive(Clone, Debug)]
struct Entry<K> {
    id: K,
    name: String,
    path: Option<PathBuf>,
    read_only: bool,
    state: DocState,
    open: bool,
    ticked: bool,
    note: Option<Note>,
    saved_to: Option<PathBuf>,
    lost: bool,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum RunMode {
    Selected,
    Row,
    Single,
}

/// The saves in progress for one click.
#[derive(Clone, Debug)]
struct Run {
    /// Indexes into `docs`, in row order.
    queue: Vec<usize>,
    mode: RunMode,
    failed: bool,
    /// The document being saved changed during its save (6.5).
    changed: bool,
    /// The row a row's Save is for.
    target: Option<usize>,
}

/// One closing action and its list of unsaved work.
#[derive(Clone, Debug)]
pub struct Closing<K> {
    docs: Vec<Entry<K>>,
    request: Option<Request<K>>,
    /// Whether the current request has been handed out by [`Closing::poll_request`].
    request_polled: bool,
    run: Option<Run>,
    outcome: Option<Outcome>,
    abandoned: Vec<K>,
    single: bool,
}

impl<K: Clone + PartialEq> Closing<K> {
    /// Starts a closing action. `docs` are the documents the action closes, in the order the user
    /// sees them (1.3, 2.2): every one, saved or not.
    ///
    /// With no unsaved document the action proceeds at once, and [`Closing::outcome`] says so.
    pub fn new(docs: impl IntoIterator<Item = Doc<K>>) -> Self {
        let docs: Vec<Entry<K>> = docs
            .into_iter()
            .map(|d| Entry {
                id: d.id,
                name: d.name,
                path: d.path,
                read_only: d.read_only,
                state: if d.dirty {
                    DocState::Unsaved
                } else {
                    DocState::Clean
                },
                open: true,
                ticked: d.dirty, // 2.4: every row starts ticked
                note: None,
                saved_to: None,
                lost: false,
            })
            .collect();
        let unsaved = docs.iter().filter(|d| d.state == DocState::Unsaved).count();
        let mut closing = Self {
            docs,
            request: None,
            request_polled: false,
            run: None,
            outcome: None,
            abandoned: Vec::new(),
            single: unsaved == 1, // 1.2: one unsaved document keeps the single prompt
        };
        if unsaved == 0 {
            closing.proceed(); // nothing to ask
        }
        closing
    }

    // ---- what the window shows -------------------------------------------------------------

    fn is_listed(d: &Entry<K>) -> bool {
        d.open && d.state != DocState::Clean
    }

    fn is_unsaved(d: &Entry<K>) -> bool {
        Self::is_listed(d) && d.state == DocState::Unsaved
    }

    fn index(&self, id: &K) -> Option<usize> {
        self.docs.iter().position(|d| &d.id == id)
    }

    fn listed_len(&self) -> usize {
        self.docs.iter().filter(|d| Self::is_listed(d)).count()
    }

    /// Which prompt to show (1.1, 1.2).
    pub fn prompt(&self) -> Prompt {
        if self.outcome.is_some() {
            Prompt::None
        } else if self.single && self.listed_len() == 1 {
            Prompt::Single
        } else {
            Prompt::List
        }
    }

    /// Unsaved documents still to answer: the number in the title (2.1).
    pub fn count(&self) -> usize {
        if self.outcome.is_some() {
            0
        } else {
            self.docs.iter().filter(|d| Self::is_unsaved(d)).count()
        }
    }

    /// The rows to draw, in order (2.2, 1.4). Empty once the action has finished.
    pub fn rows(&self) -> Vec<Row<K>> {
        if self.outcome.is_some() {
            return Vec::new();
        }
        let saving = match &self.request {
            Some(Request::Save { id, .. }) => Some(id),
            _ => None,
        };
        self.docs
            .iter()
            .filter(|d| Self::is_listed(d))
            .map(|d| Row {
                id: d.id.clone(),
                name: d.name.clone(),
                location: d.path.clone(),
                read_only: d.read_only,
                ticked: d.state == DocState::Unsaved && d.ticked,
                state: if saving == Some(&d.id) {
                    RowState::Saving
                } else if d.state == DocState::Saved {
                    RowState::Saved
                } else {
                    RowState::Unsaved
                },
                note: d.note.clone(),
            })
            .collect()
    }

    /// What the application is being asked to do now, if anything. It stays the same until it is
    /// answered, so it can be read every frame.
    pub fn request(&self) -> Option<&Request<K>> {
        self.request.as_ref()
    }

    /// The current request, handed out once: the first call after it is raised returns it, later
    /// calls return `None` until the next one. An application that polls every frame uses this to
    /// start each pick or save exactly once; it still answers with the methods below.
    pub fn poll_request(&mut self) -> Option<Request<K>> {
        if self.request_polled {
            return None;
        }
        self.request_polled = true;
        self.request.clone()
    }

    /// `None` while asking; how the action ended once it has.
    pub fn outcome(&self) -> Option<Outcome> {
        self.outcome
    }

    /// Whether this document is still open.
    pub fn is_open(&self, id: &K) -> bool {
        self.index(id).is_some_and(|i| self.docs[i].open)
    }

    /// The action's documents closed so far, in order. A document closes as soon as its row is
    /// answered (4.1, 4.2), so an application closes these as they appear, not only at the end.
    pub fn closed(&self) -> impl Iterator<Item = &K> {
        self.docs.iter().filter(|d| !d.open).map(|d| &d.id)
    }

    /// Where this document's changes were saved, if they were.
    pub fn saved_to(&self, id: &K) -> Option<&Path> {
        self.index(id)
            .and_then(|i| self.docs[i].saved_to.as_deref())
    }

    /// Whether this document was closed with unsaved changes, by the user's choice.
    pub fn is_lost(&self, id: &K) -> bool {
        self.index(id).is_some_and(|i| self.docs[i].lost)
    }

    /// Documents whose save was cancelled part-way (6.2). The application must not leave a
    /// half-written file for any of them.
    pub fn abandoned(&self) -> &[K] {
        &self.abandoned
    }

    /// Everything at once.
    pub fn snapshot(&self) -> Snapshot<K> {
        Snapshot {
            prompt: self.prompt(),
            count: self.count(),
            rows: self.rows(),
            request: self.request.clone(),
            outcome: self.outcome,
            open: self
                .docs
                .iter()
                .filter(|d| d.open)
                .map(|d| d.id.clone())
                .collect(),
            closed: self.closed().cloned().collect(),
            saved: self
                .docs
                .iter()
                .filter_map(|d| d.saved_to.clone().map(|p| (d.id.clone(), p)))
                .collect(),
            lost: self
                .docs
                .iter()
                .filter(|d| d.lost)
                .map(|d| d.id.clone())
                .collect(),
            abandoned: self.abandoned.clone(),
        }
    }

    // ---- the window's buttons --------------------------------------------------------------

    /// Whether the window's buttons are live: nothing finished, nothing being picked or saved.
    pub fn is_idle(&self) -> bool {
        self.outcome.is_none() && self.request.is_none()
    }

    /// An unsaved row that can be answered now.
    fn answerable(&self, id: &K) -> Option<usize> {
        let i = self.index(id)?;
        let d = &self.docs[i];
        (self.is_idle() && d.open && d.state == DocState::Unsaved).then_some(i)
    }

    /// Ticks or unticks a row.
    pub fn tick(&mut self, id: &K, on: bool) -> bool {
        if self.snapshot().prompt == Prompt::Single {
            return false; // the single prompt has no tick box
        }
        match self.answerable(id) {
            Some(i) => {
                self.docs[i].ticked = on; // 4.4: only this row changes
                true
            }
            None => false,
        }
    }

    /// Save Selected (3.1), or Save in the single prompt.
    pub fn save_selected(&mut self) -> bool {
        if !self.is_idle() {
            return false;
        }
        let queue: Vec<usize> = (0..self.docs.len())
            .filter(|&i| Self::is_unsaved(&self.docs[i]) && self.docs[i].ticked)
            .collect(); // row order
        let mode = if self.prompt() == Prompt::Single {
            RunMode::Single
        } else {
            RunMode::Selected
        };
        self.start(queue, mode);
        true
    }

    /// Don't Save (3.2): everything listed closes without saving.
    pub fn dont_save(&mut self) -> bool {
        if !self.is_idle() {
            return false;
        }
        self.proceed();
        true
    }

    /// Cancel, Escape, or the window's close button (3.3, 2.3, 5.1).
    ///
    /// Ignored while a save picker is open: the picker is in front, and it is the one to answer.
    /// During a save it abandons that save (6.2); see [`Closing::abandoned`].
    pub fn cancel(&mut self) -> bool {
        if self.outcome.is_some() {
            return false;
        }
        match self.request.take() {
            Some(r @ Request::Pick { .. }) => {
                self.request = Some(r);
                return false;
            }
            Some(Request::Save { id, .. }) => self.abandoned.push(id), // 6.2
            None => {}
        }
        self.run = None;
        self.outcome = Some(Outcome::Stopped);
        true
    }

    /// Whether Cancel would be accepted now.
    pub fn can_cancel(&self) -> bool {
        self.outcome.is_none() && !matches!(self.request, Some(Request::Pick { .. }))
    }

    // ---- a row's own buttons (section 4) ---------------------------------------------------

    /// A row's Save (4.1).
    pub fn row_save(&mut self, id: &K) -> bool {
        match self.answerable(id) {
            Some(i) => {
                self.start(vec![i], RunMode::Row);
                true
            }
            None => false,
        }
    }

    /// A row's Discard (4.2).
    pub fn row_discard(&mut self, id: &K) -> bool {
        match self.answerable(id) {
            Some(i) => {
                self.docs[i].open = false;
                self.docs[i].lost = true;
                self.last_row_gone();
                true
            }
            None => false,
        }
    }

    // ---- the application's answers ---------------------------------------------------------

    /// The user chose where to save the document a [`Request::Pick`] asked about.
    pub fn picked(&mut self, path: impl Into<PathBuf>) -> bool {
        match &self.request {
            Some(Request::Pick { id, .. }) => {
                let id = id.clone();
                self.set_request(Some(Request::Save {
                    id,
                    path: path.into(),
                }));
                true
            }
            _ => false,
        }
    }

    /// The user cancelled the save picker.
    pub fn pick_cancelled(&mut self) -> bool {
        let Some(Request::Pick { id, .. }) = &self.request else {
            return false;
        };
        if let Some(i) = self.index(id) {
            self.docs[i].note = Some(Note::Cancelled);
        }
        if let Some(run) = &mut self.run {
            run.failed = true; // 3.1.3: a cancelled picker stops the run
            run.queue.clear();
        }
        self.set_request(None);
        self.finish_run();
        true
    }

    /// The document a [`Request::Save`] asked for was written.
    pub fn save_done(&mut self) -> bool {
        let Some(Request::Save { id, path }) = &self.request else {
            return false;
        };
        // 6.5: changed while it was being saved, so not all of it is saved
        let changed = self
            .run
            .as_mut()
            .is_some_and(|run| std::mem::take(&mut run.changed));
        if let Some(i) = self.index(id) {
            let d = &mut self.docs[i];
            d.path = Some(path.clone());
            d.read_only = false;
            d.state = if changed {
                DocState::Unsaved
            } else {
                DocState::Saved
            };
            d.saved_to = Some(path.clone());
            d.note = None;
        }
        self.after_save();
        true
    }

    /// The document a [`Request::Save`] asked for could not be written. `reason` is shown on its row,
    /// in words the user can act on (3.4).
    pub fn save_failed(&mut self, reason: impl Into<String>) -> bool {
        let Some(Request::Save { id, .. }) = &self.request else {
            return false;
        };
        if let Some(i) = self.index(id) {
            self.docs[i].note = Some(Note::Failed(reason.into())); // 3.4: the row says why
        }
        if let Some(run) = &mut self.run {
            run.failed = true;
            run.changed = false;
        }
        self.after_save(); // 3.1.3: the others still save
        true
    }

    /// A document in the set changed while the window was open (6.5).
    ///
    /// If it is the one being saved now, the save may not include the change, so the document
    /// still needs saving when that save is done.
    pub fn doc_changed(&mut self, id: &K) -> bool {
        let Some(i) = self.index(id) else {
            return false;
        };
        if self.outcome.is_some() || !self.docs[i].open {
            return false;
        }
        if matches!(&self.request, Some(Request::Save { id: saving, .. }) if saving == id) {
            if let Some(run) = &mut self.run {
                run.changed = true; // told on save_done
            }
            return true;
        }
        let d = &mut self.docs[i];
        if d.state == DocState::Clean {
            d.ticked = true; // a new row joins the list, ticked (2.4)
        }
        d.state = DocState::Unsaved;
        d.saved_to = None;
        true
    }

    // ---- how a run of saves goes -----------------------------------------------------------

    fn set_request(&mut self, request: Option<Request<K>>) {
        self.request_polled = request.is_none();
        self.request = request;
    }

    fn start(&mut self, queue: Vec<usize>, mode: RunMode) {
        for &i in &queue {
            self.docs[i].note = None;
        }
        let target = (mode == RunMode::Row).then(|| queue[0]);
        self.run = Some(Run {
            queue,
            mode,
            failed: false,
            changed: false,
            target,
        });
        self.next();
    }

    fn after_save(&mut self) {
        if let Some(run) = &mut self.run
            && !run.queue.is_empty()
        {
            run.queue.remove(0);
        }
        self.set_request(None);
        self.next();
    }

    fn next(&mut self) {
        let Some(&i) = self.run.as_ref().and_then(|r| r.queue.first()) else {
            return self.finish_run();
        };
        let d = &self.docs[i];
        // 3.1: ask where only for a document with no file to write to; 6.1: or whose file can't be
        // written.
        let request = match &d.path {
            None => Request::Pick {
                id: d.id.clone(),
                why: PickReason::NeverSaved,
            },
            Some(_) if d.read_only => Request::Pick {
                id: d.id.clone(),
                why: PickReason::ReadOnly,
            },
            Some(path) => Request::Save {
                id: d.id.clone(),
                path: path.clone(),
            },
        };
        self.set_request(Some(request));
    }

    fn finish_run(&mut self) {
        let Some(run) = self.run.take() else { return };
        if run.mode == RunMode::Row {
            // 4.1: a row that saved closes and leaves the list; one that didn't stays, with its
            // reason.
            if let Some(i) = run.target
                && self.docs[i].state == DocState::Saved
            {
                self.docs[i].open = false;
            }
            return self.last_row_gone();
        }
        // 3.1.2: everything ticked has saved, so the action goes ahead. Otherwise 3.1.1: nothing
        // closes and the window stays. A document that changed during the run is unsaved again and
        // ticked, so it holds the window open too: it must not close unasked (6.5).
        let ticked_left = self.docs.iter().any(|d| Self::is_unsaved(d) && d.ticked);
        if !run.failed && !ticked_left {
            self.proceed();
        }
    }

    /// 4.3: when no unsaved row is left, the action carries on.
    fn last_row_gone(&mut self) {
        if !self.docs.iter().any(Self::is_unsaved) {
            self.proceed();
        }
    }

    fn proceed(&mut self) {
        for d in &mut self.docs {
            if d.open && d.state == DocState::Unsaved {
                d.lost = true;
            }
            d.open = false;
        }
        self.set_request(None);
        self.run = None;
        self.outcome = Some(Outcome::Proceeded);
    }
}
