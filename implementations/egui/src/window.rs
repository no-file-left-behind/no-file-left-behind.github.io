//! The window: the list of unsaved work as an egui modal, drawn from a [`Closing`] each frame.
//!
//! The window only draws and passes clicks in. It never saves anything: the application reads
//! [`Closing::poll_request`] every frame, shows its own save picker or starts its own save (on
//! another thread if it likes), and answers when it is done. Until then the window shows the row
//! as waiting and keeps every button but Cancel disabled (6.2).

use std::fmt::Debug;
use std::hash::Hash;
use std::sync::Arc;

use egui::accesskit::Role;
use egui::{
    Align, Checkbox, Context, Id, Key, Layout, Link, Modal, Modifiers, RichText, Spinner,
    TextStyle, TextWrapMode, Ui, UiBuilder, Vec2,
};

use crate::model::{Closing, Note, Outcome, Prompt, Request, Row, RowState};

/// A text that depends on a number, such as the title's count (2.1).
pub type CountText = Arc<dyn Fn(usize) -> String + Send + Sync>;
/// A text that names a document, such as "Save Report.docx" (5.3).
pub type NamedText = Arc<dyn Fn(&str) -> String + Send + Sync>;

/// Every word the window shows or announces, so an application can translate them all.
///
/// [`Strings::english`] fills them in; change any field after.
#[derive(Clone)]
pub struct Strings {
    /// The list's title, given the count (2.1): "Save changes to 3 documents?".
    pub title: CountText,
    /// The single prompt's title (1.2): "Save changes to “Report”?".
    pub single_title: NamedText,
    /// What the ticks mean (2.6).
    pub help: String,
    /// Where a never-saved document lives (2.2).
    pub not_saved_yet: String,
    /// Shown on hovering a row's name, which brings that document to the front (2.2).
    pub show_document: String,
    pub save_selected: String,
    pub dont_save: String,
    pub cancel: String,
    /// A row's Save, and Save in the single prompt.
    pub save: String,
    pub discard: String,
    /// The accessible name of a row's tick box and Save (5.3): "Save Report".
    pub save_named: NamedText,
    /// The accessible name of a row's Discard (5.3): "Discard Report".
    pub discard_named: NamedText,
    /// A row being written (6.2).
    pub saving: String,
    /// A row whose save picker is open.
    pub choosing: String,
    /// A row that saved while the list stays open (3.1.1).
    pub saved: String,
    /// A row whose save picker was cancelled.
    pub cancelled: String,
    /// A row whose save failed, given the application's reason (3.4).
    pub failed: NamedText,
    /// Said up front for a file that can't be written where it is (6.1).
    pub read_only: String,
}

impl Strings {
    /// English, with the application's own word for a document (2.1): `english("design", "designs")`.
    pub fn english(one: &str, many: &str) -> Self {
        let (one, many) = (one.to_owned(), many.to_owned());
        let help_noun = many.clone();
        Self {
            title: Arc::new(move |n| {
                format!("Save changes to {n} {}?", if n == 1 { &one } else { &many })
            }),
            single_title: Arc::new(|name| format!("Save changes to “{name}”?")),
            help: format!("Ticked {help_noun} are saved; the rest close without saving."),
            not_saved_yet: "Not saved yet".into(),
            show_document: "Show this document".into(),
            save_selected: "Save Selected".into(),
            dont_save: "Don't Save".into(),
            cancel: "Cancel".into(),
            save: "Save".into(),
            discard: "Discard".into(),
            save_named: Arc::new(|name| format!("Save {name}")),
            discard_named: Arc::new(|name| format!("Discard {name}")),
            saving: "Saving…".into(),
            choosing: "Choosing where to save…".into(),
            saved: "Saved".into(),
            cancelled: "Not saved: you cancelled choosing where.".into(),
            failed: Arc::new(|reason| format!("Not saved: {reason}")),
            read_only: "The original file is read-only, so saving asks for a new place.".into(),
        }
    }
}

impl Default for Strings {
    fn default() -> Self {
        Self::english("document", "documents")
    }
}

/// What happened in the window this frame.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ListResponse<K> {
    /// The user clicked a row's name: bring this document to the front (2.2).
    pub show_document: Option<K>,
    /// A click was accepted by the model this frame.
    pub changed: bool,
    /// The window was drawn this frame.
    pub shown: bool,
}

impl<K> Default for ListResponse<K> {
    fn default() -> Self {
        Self {
            show_document: None,
            changed: false,
            shown: false,
        }
    }
}

/// The list of unsaved work, and the single prompt for one document.
///
/// Build it each frame (it is cheap) and call [`UnsavedList::show`]:
///
/// ```no_run
/// # use no_file_left_behind::{Closing, UnsavedList};
/// # fn frame(ctx: &egui::Context, closing: &mut Closing<u64>) {
/// let response = UnsavedList::new("design", "designs").show(ctx, closing);
/// # }
/// ```
#[derive(Clone)]
pub struct UnsavedList {
    id: Id,
    strings: Strings,
    single_prompt: bool,
    width: f32,
}

/// What the window remembers between frames while it is open.
#[derive(Clone, Default)]
struct Memory {
    /// Where keyboard focus was before the window opened, to give it back on Cancel (5.2).
    return_focus: Option<Id>,
    /// Buttons drawn last frame: Enter is theirs when one has focus (5.1).
    buttons: Vec<Id>,
}

/// What one frame of drawing collects: the clicks to pass in, and the buttons drawn.
struct Out<K> {
    actions: Vec<Action<K>>,
    buttons: Vec<Id>,
}

enum Action<K> {
    Tick(K, bool),
    Show(K),
    RowSave(K),
    RowDiscard(K),
    SaveSelected,
    DontSave,
    Cancel,
}

impl UnsavedList {
    /// A window that calls a document by the application's own word (2.1), in English.
    pub fn new(one: &str, many: &str) -> Self {
        Self::with_strings(Strings::english(one, many))
    }

    /// A window with every word supplied, e.g. translated.
    pub fn with_strings(strings: Strings) -> Self {
        Self {
            id: Id::unique("no_file_left_behind"),
            strings,
            single_prompt: true,
            width: 560.0,
        }
    }

    /// Distinguishes this window from another one shown at the same time.
    pub fn id_salt(mut self, salt: impl Hash + Debug) -> Self {
        self.id = Id::unique(("no_file_left_behind", salt));
        self
    }

    /// Whether to draw the single prompt for one unsaved document (1.2). Turn it off when the
    /// application has its own: it then shows that while [`Closing::prompt`] is
    /// [`Prompt::Single`], and answers it with [`Closing::save_selected`], [`Closing::dont_save`]
    /// and [`Closing::cancel`].
    pub fn single_prompt(mut self, on: bool) -> Self {
        self.single_prompt = on;
        self
    }

    /// The window's width when the screen has room. It never grows past the screen (2.5).
    pub fn width(mut self, width: f32) -> Self {
        self.width = width;
        self
    }

    /// Draws the window for `closing`, if it is asking anything, and passes the user's clicks in.
    pub fn show<K>(&self, ctx: &Context, closing: &mut Closing<K>) -> ListResponse<K>
    where
        K: Clone + PartialEq + Hash + Debug,
    {
        let memory_id = self.id.with("memory");
        let mut response = ListResponse::default();
        if !self.draws(closing) {
            self.forget(ctx, closing.outcome());
            return response;
        }

        let opening = ctx.data(|d| d.get_temp::<Memory>(memory_id)).is_none();
        let mut memory = ctx
            .data(|d| d.get_temp::<Memory>(memory_id))
            .unwrap_or_default();
        if opening {
            memory.return_focus = ctx.memory(|m| m.focused());
        } else {
            // Keys first. The key press that opened the window (Enter on a Quit button) must not
            // also answer it, hence not on the opening frame.
            let keys = self.keys(ctx, closing, &memory);
            apply(keys, closing, &mut response);
            if !self.draws(closing) {
                self.forget(ctx, closing.outcome());
                return response;
            }
        }

        let prompt = closing.prompt();
        let rows = closing.rows();
        let request = closing.request().cloned();
        let title = match prompt {
            Prompt::Single => (self.strings.single_title)(&rows[0].name),
            _ => (self.strings.title)(closing.count()),
        };

        // 2.5: the window fits on the screen with any number of rows.
        let screen = ctx.content_rect();
        let frame = egui::Frame::popup(&ctx.global_style());
        let chrome = frame.total_margin().sum() + Vec2::splat(32.0);
        // A question about one document needs less room than a list.
        let width = match prompt {
            Prompt::Single => self.width.min(420.0),
            _ => self.width,
        };
        let inner_width = width.min(screen.width() - chrome.x).max(120.0);
        let inner_height = (screen.height() - chrome.y).max(120.0);

        // 5.4: the dialog is named by its title, so opening it announces the count.
        let area = Modal::default_area(self.id)
            .accessible_name(title.clone())
            .role(Role::AlertDialog);
        let mut out = Out {
            actions: Vec::new(),
            buttons: Vec::new(),
        };
        Modal::new(self.id).area(area).frame(frame).show(ctx, |ui| {
            ui.set_width(inner_width);
            ui.set_max_height(inner_height);
            ui.heading(&title);
            let body = Body {
                strings: &self.strings,
                id: self.id,
                request: request.as_ref(),
                idle: closing.is_idle(),
                can_cancel: closing.can_cancel(),
                focus_primary: opening,
            };
            match prompt {
                Prompt::Single => body.single(ui, &rows[0], &mut out),
                _ => body.list(ui, &rows, inner_height, &mut out),
            }
        });
        response.shown = true;
        apply(out.actions, closing, &mut response);

        memory.buttons = out.buttons;
        ctx.data_mut(|d| d.insert_temp(memory_id, memory));
        if closing.outcome().is_some() {
            self.forget(ctx, closing.outcome());
        }
        response
    }

    /// Whether this window has anything to show for `closing`.
    fn draws<K: Clone + PartialEq>(&self, closing: &Closing<K>) -> bool {
        match closing.prompt() {
            Prompt::None => false,
            Prompt::Single => self.single_prompt,
            Prompt::List => true,
        }
    }

    /// Enter and Escape (5.1), taken before any widget sees them.
    fn keys<K: Clone + PartialEq>(
        &self,
        ctx: &Context,
        closing: &Closing<K>,
        memory: &Memory,
    ) -> Vec<Action<K>> {
        let mut actions = Vec::new();
        // An application's own modal on top (its save picker) gets the keys, not this window.
        let layer = Modal::default_area(self.id).layer();
        let on_top = ctx
            .memory(|m| m.top_modal_layer())
            .is_none_or(|top| top == layer);
        if !on_top {
            return actions;
        }
        // Escape is Cancel. While the save picker is open Cancel waits, so Escape is left to it.
        if closing.can_cancel() && ctx.input_mut(|i| i.consume_key(Modifiers::NONE, Key::Escape)) {
            actions.push(Action::Cancel);
        }
        // Enter is Save Selected, unless a button has focus: then it presses that button, as
        // egui's buttons do. A focused tick box doesn't take it, so Enter still saves.
        let focused = ctx.memory(|m| m.focused());
        let on_button = focused.is_some_and(|f| memory.buttons.contains(&f));
        if closing.is_idle()
            && !on_button
            && ctx.input_mut(|i| i.consume_key(Modifiers::NONE, Key::Enter))
        {
            actions.push(Action::SaveSelected);
        }
        actions
    }

    /// The window has closed: drop its memory, and on Cancel give focus back (5.2).
    fn forget(&self, ctx: &Context, outcome: Option<Outcome>) {
        let memory_id = self.id.with("memory");
        let Some(memory) = ctx.data_mut(|d| d.remove_temp::<Memory>(memory_id)) else {
            return;
        };
        if outcome == Some(Outcome::Stopped)
            && let Some(id) = memory.return_focus
        {
            ctx.memory_mut(|m| m.request_focus(id));
            // When the window was drawn this frame, egui still treats it as the top modal next
            // frame and drops focus from the widgets behind it, so ask again once it has gone.
            let pass = ctx.cumulative_pass_nr();
            ctx.data_mut(|d| d.insert_temp(Id::unique(RETURN_FOCUS), (id, pass)));
            ctx.add_plugin(ReturnFocus);
        }
    }
}

const RETURN_FOCUS: &str = "no_file_left_behind return focus";

/// Gives focus back a pass after the window closed (5.2). Added once per context.
struct ReturnFocus;

impl egui::plugin::Plugin for ReturnFocus {
    fn debug_name(&self) -> &'static str {
        "No File Left Behind: return focus"
    }

    fn on_end_pass(&mut self, ui: &mut Ui) {
        let ctx = ui.ctx().clone();
        let key = Id::unique(RETURN_FOCUS);
        let Some((id, pass)) = ctx.data(|d| d.get_temp::<(Id, u64)>(key)) else {
            return;
        };
        if ctx.cumulative_pass_nr() > pass {
            ctx.data_mut(|d| d.remove::<(Id, u64)>(key));
            ctx.memory_mut(|m| m.request_focus(id));
        }
    }
}

/// Passes the user's clicks in, in the order they came.
fn apply<K: Clone + PartialEq>(
    actions: Vec<Action<K>>,
    closing: &mut Closing<K>,
    response: &mut ListResponse<K>,
) {
    for action in actions {
        response.changed |= match action {
            Action::Tick(id, on) => closing.tick(&id, on),
            Action::Show(id) => {
                response.show_document = Some(id);
                false
            }
            Action::RowSave(id) => closing.row_save(&id),
            Action::RowDiscard(id) => closing.row_discard(&id),
            Action::SaveSelected => closing.save_selected(),
            Action::DontSave => closing.dont_save(),
            Action::Cancel => closing.cancel(),
        };
    }
}

/// What drawing one frame of the window needs.
struct Body<'a, K> {
    strings: &'a Strings,
    id: Id,
    request: Option<&'a Request<K>>,
    idle: bool,
    can_cancel: bool,
    focus_primary: bool,
}

impl<K: Clone + PartialEq + Hash + Debug> Body<'_, K> {
    fn list(&self, ui: &mut Ui, rows: &[Row<K>], inner_height: f32, out: &mut Out<K>) {
        // 2.6: what a tick means, in words, next to the rows.
        ui.label(RichText::new(&self.strings.help).weak());
        ui.add_space(4.0);

        // 2.5: the rows scroll, and the footer keeps the room it took last frame.
        let footer_id = self.id.with("footer height");
        let footer = ui
            .data(|d| d.get_temp::<f32>(footer_id))
            .unwrap_or_else(|| ui.spacing().interact_size.y * 2.0);
        let used = ui.min_rect().height();
        let rows_height = (inner_height - used - footer).max(ui.spacing().interact_size.y * 2.0);
        egui::ScrollArea::vertical()
            .max_height(rows_height)
            .auto_shrink([false, true])
            .show(ui, |ui| {
                for row in rows {
                    self.row(ui, row, out);
                }
            });

        let top = ui.min_rect().bottom();
        ui.separator();
        self.footer(ui, &self.strings.save_selected, out);
        let height = ui.min_rect().bottom() - top;
        if (height - footer).abs() > 0.5 {
            ui.data_mut(|d| d.insert_temp(footer_id, height));
            ui.ctx()
                .request_discard("No File Left Behind: footer height");
        }
    }

    fn row(&self, ui: &mut Ui, row: &Row<K>, out: &mut Out<K>) {
        let answerable = self.idle && row.state == RowState::Unsaved;
        let builder = UiBuilder::new()
            .id_salt(("row", &row.id))
            .accessibility_role(Role::Group) // 5.3: one group per row, named after its document
            .accessibility_label(row.name.clone());
        ui.scope_builder(builder, |ui| {
            let save_named = (self.strings.save_named)(&row.name);
            split(
                ui,
                self.id.with(("row buttons", &row.id)),
                out,
                |ui, out| {
                    // Tab order follows the reading order: tick, name, Save, Discard (5.2).
                    let mut ticked = row.ticked;
                    let tick = ui
                        .add_enabled(answerable, Checkbox::without_text(&mut ticked))
                        .accessible_name(save_named.clone());
                    if tick.changed() {
                        out.actions.push(Action::Tick(row.id.clone(), ticked));
                    }
                    ui.vertical(|ui| {
                        let name = ui
                            .add(Link::new(RichText::new(&row.name).strong()))
                            .on_hover_text(&self.strings.show_document);
                        out.buttons.push(name.id);
                        if name.clicked() {
                            out.actions.push(Action::Show(row.id.clone()));
                        }
                        self.location_and_status(ui, row);
                    });
                },
                |ui, out| {
                    let save = ui
                        .add_enabled(answerable, egui::Button::new(&self.strings.save).small())
                        .accessible_name(save_named.clone());
                    let discard = ui
                        .add_enabled(answerable, egui::Button::new(&self.strings.discard).small())
                        .accessible_name((self.strings.discard_named)(&row.name));
                    out.buttons.extend([save.id, discard.id]);
                    if save.clicked() {
                        out.actions.push(Action::RowSave(row.id.clone()));
                    }
                    if discard.clicked() {
                        out.actions.push(Action::RowDiscard(row.id.clone()));
                    }
                },
            );
        });
        ui.add_space(4.0);
    }

    /// Where it lives (2.2), then what is happening to it or went wrong (3.4, 6.1, 6.2).
    fn location_and_status(&self, ui: &mut Ui, row: &Row<K>) {
        let location = match &row.location {
            Some(path) => path.display().to_string(),
            None => self.strings.not_saved_yet.clone(),
        };
        ui.label(RichText::new(location).weak());

        let visuals = ui.visuals().clone();
        let picking = matches!(self.request, Some(Request::Pick { id, .. }) if *id == row.id);
        if row.state == RowState::Saving {
            ui.horizontal(|ui| {
                ui.add(Spinner::new().size(ui.text_style_height(&TextStyle::Body)));
                ui.label(&self.strings.saving);
            });
        } else if picking {
            ui.label(&self.strings.choosing);
        } else if let Some(note) = &row.note {
            let text = match note {
                Note::Failed(reason) => (self.strings.failed)(reason),
                Note::Cancelled => self.strings.cancelled.clone(),
            };
            ui.label(RichText::new(text).color(visuals.error_fg_color));
        } else if row.state == RowState::Saved {
            ui.label(&self.strings.saved);
        } else if row.read_only {
            // 6.1: say why before the user presses anything.
            ui.label(RichText::new(&self.strings.read_only).color(visuals.warn_fg_color));
        }
    }

    fn single(&self, ui: &mut Ui, row: &Row<K>, out: &mut Out<K>) {
        self.location_and_status(ui, row);
        ui.add_space(4.0);
        ui.separator();
        self.footer(ui, &self.strings.save, out);
    }

    /// Don't Save on the left, less prominent; Cancel and the default button on the right (2.3,
    /// 3.2). Added in reading order so Tab follows it (5.2).
    fn footer(&self, ui: &mut Ui, primary: &str, out: &mut Out<K>) {
        split(
            ui,
            self.id.with("footer buttons"),
            out,
            |ui, out| {
                let dont = ui.add_enabled(self.idle, egui::Button::new(&self.strings.dont_save));
                out.buttons.push(dont.id);
                if dont.clicked() {
                    out.actions.push(Action::DontSave);
                }
            },
            |ui, out| {
                let cancel =
                    ui.add_enabled(self.can_cancel, egui::Button::new(&self.strings.cancel));
                // The default button: filled with the style's selection colour, as egui marks
                // what is chosen.
                let visuals = ui.visuals();
                let label = RichText::new(primary)
                    .strong()
                    .color(visuals.selection.stroke.color);
                let fill = visuals.selection.bg_fill;
                let save = ui.add_enabled(self.idle, egui::Button::new(label).fill(fill));
                out.buttons.extend([cancel.id, save.id]);
                if self.focus_primary {
                    save.request_focus();
                }
                if cancel.clicked() {
                    out.actions.push(Action::Cancel);
                }
                if save.clicked() {
                    out.actions.push(Action::SaveSelected);
                }
            },
        );
    }
}

/// Lays `left` out in the width that `right` leaves, and `right` flush against the right edge,
/// adding the widgets in reading order so Tab follows it (5.2). egui's right-to-left layouts would
/// reverse the order, so `right`'s width is measured and remembered from the previous frame.
fn split<S>(
    ui: &mut Ui,
    id: Id,
    state: &mut S,
    left: impl FnOnce(&mut Ui, &mut S),
    right: impl FnOnce(&mut Ui, &mut S),
) {
    let remembered = ui.data(|d| d.get_temp::<f32>(id));
    let right_width = remembered.unwrap_or(0.0);
    ui.horizontal_top(|ui| {
        let gap = ui.spacing().item_spacing.x;
        let left_width = (ui.available_width() - right_width - gap).max(0.0);
        ui.allocate_ui_with_layout(
            Vec2::new(left_width, 0.0),
            Layout::left_to_right(Align::Min),
            |ui| {
                ui.set_min_width(left_width);
                left(ui, state);
            },
        );
        let measured = ui
            .scope(|ui| {
                ui.style_mut().wrap_mode = Some(TextWrapMode::Extend);
                right(ui, state);
            })
            .response
            .rect
            .width();
        if remembered.is_none_or(|w| (w - measured).abs() > 0.5) {
            ui.data_mut(|d| d.insert_temp(id, measured));
            ui.ctx()
                .request_discard("No File Left Behind: button widths");
        }
    });
}
