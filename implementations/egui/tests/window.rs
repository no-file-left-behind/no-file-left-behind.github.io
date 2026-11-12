//! The window, driven through its accessibility tree as a screen reader or a test script would.

use egui::Key;
use egui::accesskit::{Role, Toggled};
use egui_kittest::Harness;
use egui_kittest::kittest::{NodeT as _, Queryable as _};
use no_file_left_behind::{Closing, Doc, Outcome, Request, RowState, UnsavedList};

struct App {
    closing: Closing<u64>,
    shown: Option<u64>,
}

fn designs() -> Vec<Doc<u64>> {
    vec![
        Doc::new(1, "Bracket", Some("~/Designs/Bracket.sc".into())),
        Doc::new(2, "Lid", Some("~/Designs/Lid.sc".into())),
        Doc::new(3, "Untitled", None),
        Doc::new(4, "Gearbox", Some("~/Designs/Gearbox.sc".into())).dirty(false),
    ]
}

fn harness(docs: Vec<Doc<u64>>) -> Harness<'static, App> {
    let app = App {
        closing: Closing::new(docs),
        shown: None,
    };
    Harness::builder().with_size([640.0, 480.0]).build_ui_state(
        |ui, app: &mut App| {
            let r = UnsavedList::new("design", "designs").show(ui.ctx(), &mut app.closing);
            if r.show_document.is_some() {
                app.shown = r.show_document;
            }
        },
        app,
    )
}

fn disabled(h: &Harness<'_, App>, label: &str) -> bool {
    h.get_by_label(label).accesskit_node().is_disabled()
}

#[test]
fn rows_are_present_and_named_after_their_documents() {
    let h = harness(designs());
    // 2.1 and 5.4: the dialog is named by its title, with the count.
    let dialog = h.get_by_role(Role::AlertDialog);
    assert_eq!(
        dialog.accesskit_node().label().as_deref(),
        Some("Save changes to 3 designs?")
    );
    // 2.6
    h.get_by_label("Ticked designs are saved; the rest close without saving.");
    // 2.2 and 5.3: a named group per row, whose tick box and buttons name the document.
    for name in ["Bracket", "Lid", "Untitled"] {
        h.get_by_role_and_label(Role::Group, name);
        h.get_by_role_and_label(Role::Link, name);
        let save = format!("Save {name}");
        let tick = h.get_by_role_and_label(Role::CheckBox, &save);
        assert_eq!(
            tick.accesskit_node().toggled(),
            Some(Toggled::True),
            "2.4: starts ticked"
        );
        h.get_by_role_and_label(Role::Button, &format!("Save {name}"));
        h.get_by_role_and_label(Role::Button, &format!("Discard {name}"));
    }
    // 1.4: the clean document has no row.
    assert!(h.query_by_role_and_label(Role::Group, "Gearbox").is_none());
    h.get_by_label("~/Designs/Bracket.sc");
    h.get_by_label("Not saved yet");
    // 2.3
    h.get_by_role_and_label(Role::Button, "Save Selected");
    h.get_by_role_and_label(Role::Button, "Don't Save");
    h.get_by_role_and_label(Role::Button, "Cancel");
}

#[test]
fn clicking_a_name_asks_to_show_that_document() {
    let mut h = harness(designs());
    h.get_by_role_and_label(Role::Link, "Lid").click();
    h.run();
    assert_eq!(h.state().shown, Some(2));
}

#[test]
fn a_failed_save_keeps_the_window_and_says_why() {
    let docs = designs().into_iter().filter(|d| d.path.is_some()).collect();
    let mut h = harness(docs);
    h.get_by_label("Save Selected").click();
    h.run_steps(2); // a save shows a spinner, which keeps repainting
    assert!(matches!(
        h.state().closing.request(),
        Some(Request::Save { id: 1, .. })
    ));
    h.state_mut().closing.save_done();
    h.run_steps(2);
    h.state_mut().closing.save_failed("The disk is full.");
    h.run();
    // 3.1.1: nothing closes and the list stays; 3.4: the row says why.
    assert_eq!(h.state().closing.outcome(), None);
    h.get_by_role(Role::AlertDialog);
    h.get_by_label("Not saved: The disk is full.");
    h.get_by_label("Saved");
    assert_eq!(
        h.get_by_role(Role::AlertDialog)
            .accesskit_node()
            .label()
            .as_deref(),
        Some("Save changes to 1 design?")
    );
    // The saved row has nothing left to answer.
    assert!(disabled(&h, "Discard Bracket"));
    assert!(!disabled(&h, "Discard Lid"));
}

#[test]
fn every_button_but_cancel_waits_while_saving() {
    let mut h = harness(designs());
    h.get_by_label("Save Selected").click();
    // The row grows by a line, and the window re-centres a frame later.
    h.run_steps(4);
    assert_eq!(h.state().closing.rows()[0].state, RowState::Saving);
    // 6.2: the row shows it is saving.
    h.get_by_label("Saving…");
    for label in [
        "Save Selected",
        "Don't Save",
        "Discard Bracket",
        "Discard Lid",
        "Discard Untitled",
    ] {
        assert!(disabled(&h, label), "{label} should wait");
    }
    for tick in h.get_all_by_role(Role::CheckBox) {
        assert!(tick.accesskit_node().is_disabled());
    }
    assert!(!disabled(&h, "Cancel"));
    h.get_by_label("Cancel").click();
    h.run_steps(2);
    assert_eq!(h.state().closing.outcome(), Some(Outcome::Stopped));
    assert_eq!(h.state().closing.abandoned(), &[1]);
}

#[test]
fn escape_cancels() {
    let mut h = harness(designs());
    h.key_press(Key::Escape);
    h.run();
    assert_eq!(h.state().closing.outcome(), Some(Outcome::Stopped));
    assert!(
        h.query_by_role(Role::AlertDialog).is_none(),
        "the window has gone"
    );
}

#[test]
fn enter_is_save_selected_even_from_a_tick_box() {
    let mut h = harness(designs());
    h.get_by_role_and_label(Role::CheckBox, "Save Lid").focus();
    h.run();
    h.key_press(Key::Enter);
    h.run_steps(2);
    let closing = &h.state().closing;
    assert!(matches!(
        closing.request(),
        Some(Request::Save { id: 1, .. })
    ));
    assert!(closing.rows()[1].ticked, "Enter didn't toggle the tick box");
}

#[test]
fn enter_on_a_focused_button_presses_that_button() {
    let mut h = harness(designs());
    h.get_by_label("Discard Lid").focus();
    h.run();
    h.key_press(Key::Enter);
    h.run();
    let closing = &h.state().closing;
    assert!(!closing.is_open(&2));
    assert_eq!(closing.request(), None);
}

#[test]
fn unticking_and_row_buttons_feed_the_model() {
    let mut h = harness(designs());
    h.get_by_role_and_label(Role::CheckBox, "Save Lid").click();
    h.run();
    assert!(!h.state().closing.rows()[1].ticked);
    h.get_by_role_and_label(Role::Button, "Discard Untitled")
        .click();
    h.run();
    assert!(!h.state().closing.is_open(&3));
    assert!(h.query_by_role_and_label(Role::Group, "Untitled").is_none());
}

#[test]
fn one_unsaved_document_gets_the_single_prompt() {
    let docs = vec![
        Doc::new(1, "Bracket", Some("~/Designs/Bracket.sc".into())).read_only(true),
        Doc::new(2, "Lid", Some("~/Designs/Lid.sc".into())).dirty(false),
    ];
    let mut h = harness(docs);
    let dialog = h.get_by_role(Role::AlertDialog);
    assert_eq!(
        dialog.accesskit_node().label().as_deref(),
        Some("Save changes to “Bracket”?")
    );
    // 6.1: the reason is there before anything is pressed.
    h.get_by_label("The original file is read-only, so saving asks for a new place.");
    assert!(h.query_by_role(Role::CheckBox).is_none());
    h.get_by_role_and_label(Role::Button, "Save").click();
    h.run();
    assert!(matches!(
        h.state().closing.request(),
        Some(Request::Pick { id: 1, .. })
    ));
}

#[test]
fn the_single_prompt_can_be_left_to_the_application() {
    let mut closing = Closing::new([Doc::new(1, "Bracket", None)]);
    let mut h = Harness::new_ui(|ui| {
        let r = UnsavedList::new("design", "designs")
            .single_prompt(false)
            .show(ui.ctx(), &mut closing);
        assert!(!r.shown);
    });
    h.run();
    assert!(h.query_by_role(Role::AlertDialog).is_none());
}

#[test]
fn a_long_list_scrolls_and_keeps_its_buttons_on_screen() {
    let docs = (0..40)
        .map(|i| Doc::new(i, format!("Part {i}"), None))
        .collect();
    let mut h = harness(docs);
    h.set_size(egui::vec2(640.0, 360.0));
    h.run();
    let screen = egui::Rect::from_min_size(egui::Pos2::ZERO, egui::vec2(640.0, 360.0));
    for label in ["Save Selected", "Don't Save", "Cancel"] {
        let rect = h.get_by_label(label).rect();
        assert!(
            screen.contains_rect(rect),
            "{label} at {rect:?} is off the screen"
        );
    }
    // The rows past the window's height are there, scrolled out of view.
    h.get_by_role_and_label(Role::Group, "Part 39");
}

/// Renders the window to a PNG for a look, when `NFLB_RENDER_DIR` names a folder. Needs a GPU or
/// a software renderer that wgpu can find.
#[test]
fn render_for_a_look() {
    let Some(dir) = std::env::var_os("NFLB_RENDER_DIR") else {
        return;
    };
    let dir = std::path::PathBuf::from(dir);
    let mut docs = designs();
    docs[1].read_only = true;
    let mut h = harness(docs);
    h.get_by_label("Save Selected").click();
    h.run_steps(2);
    h.state_mut()
        .closing
        .save_failed("The disk is full. Free some space and try again.");
    h.run_steps(4);
    let image = h.render().expect("render");
    image.save(dir.join("list.png")).expect("save list.png");

    let mut h = harness(designs());
    h.get_by_label("Save Selected").click();
    h.run_steps(4);
    let image = h.render().expect("render");
    image.save(dir.join("saving.png")).expect("save saving.png");

    let docs = vec![
        Doc::new(1, "Bracket", Some("~/Designs/Bracket.sc".into())),
        Doc::new(2, "Report", Some("/srv/shared/Report.sc".into())).read_only(true),
        Doc::new(3, "Untitled", None),
    ];
    let mut h = Harness::builder()
        .with_size([640.0, 400.0])
        .with_theme(egui::Theme::Light)
        .build_ui_state(
            |ui, closing: &mut Closing<u64>| {
                UnsavedList::new("design", "designs").show(ui.ctx(), closing);
            },
            Closing::new(docs),
        );
    h.run();
    let image = h.render().expect("render");
    image.save(dir.join("light.png")).expect("save light.png");

    let mut h = harness(vec![Doc::new(1, "Bracket", None)]);
    h.run();
    let image = h.render().expect("render");
    image.save(dir.join("single.png")).expect("save single.png");
}

#[test]
fn tab_goes_through_each_row_then_the_buttons_and_stays_inside() {
    let mut h = harness(designs());
    let order = [
        (Role::CheckBox, "Save Bracket"),
        (Role::Link, "Bracket"),
        (Role::Button, "Save Bracket"),
        (Role::Button, "Discard Bracket"),
        (Role::CheckBox, "Save Lid"),
    ];
    h.get_by_role_and_label(order[0].0, order[0].1).focus();
    h.run();
    for (role, label) in &order[1..] {
        h.key_press(Key::Tab);
        h.run();
        assert!(
            h.get_by_role_and_label(*role, label).is_focused(),
            "Tab should reach {label}"
        );
    }
    // From the last button, Tab comes back to the first row rather than leaving the window.
    h.get_by_label("Don't Save").focus();
    h.run();
    for label in ["Cancel", "Save Selected"] {
        h.key_press(Key::Tab);
        h.run();
        assert!(
            h.get_by_label(label).is_focused(),
            "Tab should reach {label}"
        );
    }
    h.key_press(Key::Tab);
    h.run();
    assert!(
        h.get_by_role_and_label(Role::CheckBox, "Save Bracket")
            .is_focused()
    );
}

#[test]
fn cancel_gives_focus_back() {
    let mut closing: Option<Closing<u64>> = None;
    let mut h = Harness::new_ui_state(
        |ui, closing: &mut Option<Closing<u64>>| {
            if ui.button("Quit").clicked() {
                *closing = Some(Closing::new(designs()));
            }
            if let Some(c) = closing {
                UnsavedList::new("design", "designs").show(ui.ctx(), c);
            }
        },
        closing.take(),
    );
    h.get_by_label("Quit").focus();
    h.run();
    h.key_press(Key::Enter);
    h.run();
    assert!(
        h.get_by_label("Save Selected").is_focused(),
        "the default button starts with focus"
    );
    h.key_press(Key::Escape);
    h.run();
    assert_eq!(
        h.state().as_ref().and_then(Closing::outcome),
        Some(Outcome::Stopped)
    );
    assert!(
        h.get_by_label("Quit").is_focused(),
        "5.2: focus returns to where it was"
    );

    // The same through the Cancel button, from the keyboard.
    h.key_press(Key::Enter);
    h.run();
    h.get_by_label("Cancel").focus();
    h.run();
    h.key_press(Key::Enter);
    h.run();
    assert_eq!(
        h.state().as_ref().and_then(Closing::outcome),
        Some(Outcome::Stopped)
    );
    assert!(
        h.get_by_label("Quit").is_focused(),
        "5.2: focus returns to where it was"
    );
}
