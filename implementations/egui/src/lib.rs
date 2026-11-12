//! No File Left Behind for egui.
//!
//! When an application closes several documents at once (quitting, Close All, closing a window),
//! it asks about the unsaved ones once, in one list, and closes nothing if a save fails. The rules
//! are in the repository's `SPEC.md`; rule numbers in this crate refer to it.
//!
//! - [`model`]: the rules as a plain state machine, [`Closing`], with no egui in it. It never
//!   touches a file: it asks the application to pick a place or save, and waits for the answer.
//! - [`window`] (feature `egui`, on by default): [`UnsavedList`], the window that draws a
//!   [`Closing`] as an egui modal and passes clicks in.

pub mod model;
#[cfg(feature = "egui")]
pub mod window;

pub use model::{
    Closing, Doc, Note, Outcome, PickReason, Prompt, Request, Row, RowState, Snapshot,
};
#[cfg(feature = "egui")]
pub use window::{ListResponse, Strings, UnsavedList};
