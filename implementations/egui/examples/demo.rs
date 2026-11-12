//! A pretend app with a few documents. Change them, set the switches, then quit or close all.
//! Make a save fail, or cancel a "Save as", and nothing closes.
//!
//!     cargo run --example demo
//!
//! Saves run on a thread, so the list stays live while a slow one runs (6.2). The save picker is a
//! small egui window here; a real app would open its platform's file dialog in the same place.

use std::path::PathBuf;
use std::sync::mpsc::{Receiver, channel};
use std::thread;
use std::time::Duration;

use eframe::egui;
use egui::{Key, Modal, Modifiers, RichText, ViewportCommand};
use no_file_left_behind::{Closing, Doc, Outcome, PickReason, Request, UnsavedList};

fn main() -> eframe::Result {
    let options = eframe::NativeOptions {
        viewport: egui::ViewportBuilder::default().with_inner_size([720.0, 520.0]),
        ..Default::default()
    };
    eframe::run_native(
        "No File Left Behind",
        options,
        Box::new(|_| Ok(Box::new(DemoApp::new()))),
    )
}

struct DemoDoc {
    id: u64,
    name: String,
    path: Option<PathBuf>,
    dirty: bool,
    read_only: bool,
    /// The next save fails, as if the disk were full.
    fails: bool,
    /// The next save takes three seconds, as on a slow network drive.
    slow: bool,
}

#[derive(Clone, Copy, PartialEq, Eq)]
enum Action {
    Quit,
    CloseAll,
}

/// A save running on a thread.
struct Job {
    /// Which closing it belongs to, so a late answer from an abandoned one is recognised.
    closing: u64,
    id: u64,
    path: PathBuf,
    done: Receiver<Result<(), String>>,
}

/// The pretend save picker. It names the document it asks about (3.1).
struct Picker {
    title: String,
    file_name: String,
    focused: bool,
}

struct DemoApp {
    docs: Vec<DemoDoc>,
    next_id: u64,
    front: u64,
    closing: Option<(Closing<u64>, Action)>,
    closing_number: u64,
    picker: Option<Picker>,
    jobs: Vec<Job>,
    log: Vec<String>,
    may_close: bool,
}

impl DemoApp {
    fn new() -> Self {
        let doc = |id, name: &str, path: Option<&str>, dirty| DemoDoc {
            id,
            name: name.into(),
            path: path.map(PathBuf::from),
            dirty,
            read_only: false,
            fails: false,
            slow: false,
        };
        let mut docs = vec![
            doc(1, "Bracket", Some("~/Designs/Bracket.sc"), true),
            doc(2, "Lid", Some("~/Designs/Lid.sc"), true),
            doc(3, "Untitled", None, true),
            doc(4, "Gearbox", Some("~/Designs/Gearbox.sc"), false),
        ];
        docs[0].slow = true;
        Self {
            docs,
            next_id: 5,
            front: 1,
            closing: None,
            closing_number: 0,
            picker: None,
            jobs: Vec::new(),
            log: Vec::new(),
            may_close: false,
        }
    }

    fn doc_mut(&mut self, id: u64) -> Option<&mut DemoDoc> {
        self.docs.iter_mut().find(|d| d.id == id)
    }

    /// Starts closing every open document (1.3: here the action closes them all).
    fn start(&mut self, action: Action) {
        if self.closing.is_some() {
            return;
        }
        let docs = self.docs.iter().map(|d| Doc {
            id: d.id,
            name: d.name.clone(),
            path: d.path.clone(),
            dirty: d.dirty,
            read_only: d.read_only,
        });
        self.closing_number += 1;
        self.closing = Some((Closing::new(docs), action));
    }

    fn new_document(&mut self) {
        // 6.3: never-saved documents with the same name are told apart by order.
        let mut name = "Untitled".to_owned();
        let mut n = 1;
        while self.docs.iter().any(|d| d.name == name) {
            n += 1;
            name = format!("Untitled {n}");
        }
        self.docs.push(DemoDoc {
            id: self.next_id,
            name,
            path: None,
            dirty: true,
            read_only: false,
            fails: false,
            slow: false,
        });
        self.front = self.next_id;
        self.next_id += 1;
    }

    /// Starts whatever the list is asking for, once.
    fn answer_requests(&mut self, ctx: &egui::Context) {
        let Some((closing, _)) = &mut self.closing else {
            return;
        };
        match closing.poll_request() {
            Some(Request::Pick { id, why }) => {
                let Some(doc) = self.docs.iter().find(|d| d.id == id) else {
                    return;
                };
                let title = match why {
                    PickReason::NeverSaved => format!("Save “{}” as…", doc.name),
                    PickReason::ReadOnly => format!("“{}” is read-only. Save a copy as…", doc.name),
                };
                self.picker = Some(Picker {
                    title,
                    file_name: format!("{}.sc", doc.name),
                    focused: false,
                });
            }
            Some(Request::Save { id, path }) => {
                let Some(doc) = self.docs.iter().find(|d| d.id == id) else {
                    return;
                };
                // A real app would write to a temporary file next to `path` here, and replace the
                // original only once that succeeded (6.2).
                let (fails, slow) = (doc.fails, doc.slow);
                let (tx, rx) = channel();
                let ctx = ctx.clone();
                thread::spawn(move || {
                    thread::sleep(Duration::from_millis(if slow { 3000 } else { 300 }));
                    let result = if fails {
                        Err("The disk is full. Free some space and try again.".to_owned())
                    } else {
                        Ok(())
                    };
                    let _ = tx.send(result);
                    ctx.request_repaint();
                });
                self.jobs.push(Job {
                    closing: self.closing_number,
                    id,
                    path,
                    done: rx,
                });
            }
            None => {}
        }
    }

    /// Takes the results of finished saves and answers the list.
    fn finish_jobs(&mut self) {
        let mut finished = Vec::new();
        self.jobs.retain(|job| match job.done.try_recv() {
            Ok(result) => {
                finished.push((job.closing, job.id, job.path.clone(), result));
                false
            }
            Err(_) => true,
        });
        for (number, id, path, result) in finished {
            let waiting = number == self.closing_number
                && self.closing.as_ref().is_some_and(
                    |(c, _)| matches!(c.request(), Some(Request::Save { id: s, .. }) if *s == id),
                );
            if !waiting {
                // Cancel abandoned it (6.2): a real app deletes its temporary file here, so the
                // original is untouched.
                self.log.push(format!(
                    "A save was abandoned; {} is as it was.",
                    path.display()
                ));
                continue;
            }
            let (closing, _) = self.closing.as_mut().expect("waiting implies a closing");
            match result {
                Ok(()) => {
                    // A real app moves its temporary file over the original here.
                    closing.save_done();
                    let name = if let Some(doc) = self.doc_mut(id) {
                        doc.path = Some(path.clone());
                        doc.dirty = false;
                        doc.read_only = false;
                        doc.name.clone()
                    } else {
                        String::new()
                    };
                    self.log.push(format!("Saved {name} to {}", path.display()));
                }
                Err(reason) => {
                    closing.save_failed(reason.clone());
                    self.log.push(format!("A save failed: {reason}"));
                }
            }
        }
    }

    fn show_picker(&mut self, ctx: &egui::Context) {
        let Some(picker) = &mut self.picker else {
            return;
        };
        let mut answer = None;
        Modal::new(egui::Id::unique("demo save picker")).show(ctx, |ui| {
            ui.set_width(360.0);
            ui.heading(&picker.title);
            ui.label("File name");
            let field = ui.text_edit_singleline(&mut picker.file_name);
            if !picker.focused {
                field.request_focus();
                picker.focused = true;
            }
            if field.lost_focus() && ui.input(|i| i.key_pressed(Key::Enter)) {
                answer = Some(true);
            }
            ui.separator();
            ui.horizontal(|ui| {
                if ui.button("Cancel").clicked() {
                    answer = Some(false);
                }
                if ui.button("Save").clicked() {
                    answer = Some(true);
                }
            });
        });
        if answer.is_none() && ctx.input_mut(|i| i.consume_key(Modifiers::NONE, Key::Escape)) {
            answer = Some(false);
        }
        let Some(save) = answer else { return };
        let picker = self.picker.take().expect("shown above");
        let Some((closing, _)) = &mut self.closing else {
            return;
        };
        if save && !picker.file_name.trim().is_empty() {
            closing.picked(PathBuf::from("~/Designs").join(picker.file_name.trim()));
        } else {
            closing.pick_cancelled();
            self.log.push("“Save as” cancelled.".into());
        }
    }

    /// Closes the documents the list has closed, and ends the action when it has finished.
    fn follow_closing(&mut self, ctx: &egui::Context) {
        let Some((closing, action)) = &self.closing else {
            return;
        };
        let action = *action;
        // A row's own Save or Discard closes its document straight away (4.1, 4.2).
        let closed: Vec<u64> = closing.closed().copied().collect();
        for id in closed {
            if let Some(i) = self.docs.iter().position(|d| d.id == id) {
                let doc = self.docs.remove(i);
                let how = if closing.is_lost(&id) {
                    " without saving"
                } else {
                    ""
                };
                self.log.push(format!("Closed {}{how}", doc.name));
            }
        }
        match closing.outcome() {
            None => {}
            Some(Outcome::Stopped) => {
                self.log.push("Cancelled: nothing more closed.".into());
                self.closing = None;
            }
            Some(Outcome::Proceeded) => {
                self.closing = None;
                if action == Action::Quit {
                    self.may_close = true;
                    ctx.send_viewport_cmd(ViewportCommand::Close);
                }
            }
        }
        if !self.docs.iter().any(|d| d.id == self.front)
            && let Some(first) = self.docs.first()
        {
            self.front = first.id;
        }
    }

    fn documents(&mut self, ui: &mut egui::Ui) {
        let front = self.front;
        let mut make_front = None;
        for doc in &mut self.docs {
            egui::Frame::group(ui.style()).show(ui, |ui| {
                ui.set_width(ui.available_width());
                ui.horizontal(|ui| {
                    let name = if doc.dirty {
                        format!("{} •", doc.name)
                    } else {
                        doc.name.clone()
                    };
                    if ui
                        .selectable_label(doc.id == front, RichText::new(name).strong())
                        .clicked()
                    {
                        make_front = Some(doc.id);
                    }
                    let place = doc
                        .path
                        .as_ref()
                        .map_or("Not saved yet".to_owned(), |p| p.display().to_string());
                    ui.label(RichText::new(place).weak());
                });
                ui.horizontal_wrapped(|ui| {
                    ui.checkbox(&mut doc.dirty, "Unsaved changes");
                    ui.add_enabled(
                        doc.path.is_some(),
                        egui::Checkbox::new(&mut doc.read_only, "Read-only file"),
                    );
                    ui.checkbox(&mut doc.fails, "Save will fail");
                    ui.checkbox(&mut doc.slow, "Slow save");
                });
            });
        }
        if let Some(id) = make_front {
            self.front = id;
        }
    }
}

impl eframe::App for DemoApp {
    fn ui(&mut self, ui: &mut egui::Ui, _frame: &mut eframe::Frame) {
        let ctx = ui.ctx().clone();

        // The window's close button quits through the list too.
        if ctx.input(|i| i.viewport().close_requested()) && !self.may_close {
            ctx.send_viewport_cmd(ViewportCommand::CancelClose);
            self.start(Action::Quit);
        }

        egui::Panel::top("bar").show(ui, |ui| {
            ui.horizontal(|ui| {
                let any = !self.docs.is_empty();
                if ui.add_enabled(any, egui::Button::new("Quit")).clicked() {
                    self.start(Action::Quit);
                }
                if ui
                    .add_enabled(any, egui::Button::new("Close All"))
                    .clicked()
                {
                    self.start(Action::CloseAll);
                }
                if ui.button("New").clicked() {
                    self.new_document();
                }
                if ui.button("Start over").clicked() && self.closing.is_none() {
                    *self = Self::new();
                }
            });
        });
        egui::CentralPanel::default().show(ui, |ui| {
            egui::ScrollArea::vertical().show(ui, |ui| {
                if self.docs.is_empty() {
                    ui.label("No documents are open. Press Start over to run it again.");
                }
                self.documents(ui);
                ui.add_space(8.0);
                for line in self.log.iter().rev().take(8) {
                    ui.label(RichText::new(line).weak());
                }
            });
        });

        // The list of unsaved work. It draws the closing and passes clicks in; the app does the
        // rest below.
        if let Some((closing, _)) = &mut self.closing {
            let response = UnsavedList::new("design", "designs").show(&ctx, closing);
            if let Some(id) = response.show_document {
                self.front = id; // 2.2: bring that document to the front
            }
        }
        self.finish_jobs();
        self.answer_requests(&ctx);
        self.show_picker(&ctx);
        self.answer_requests(&ctx); // a picked place is saved straight away
        self.follow_closing(&ctx);
    }
}
