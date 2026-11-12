//! Runs `scenarios/scenarios.json` against `Closing`, the same way `scenarios/reference/run.mjs`
//! runs it against the reference model.

use no_file_left_behind::model::{
    Closing, Doc, Note, Outcome, PickReason, Prompt, Request, RowState, Snapshot,
};
use serde_json::{Map, Value, json};
use std::path::{Path, PathBuf};

fn path(p: &Path) -> Value {
    Value::String(p.to_string_lossy().into_owned())
}

fn ids(ids: &[String]) -> Value {
    json!(ids)
}

fn request(r: &Request<String>) -> Value {
    match r {
        Request::Pick { id, why } => json!({
            "type": "pick",
            "id": id,
            "why": match why { PickReason::NeverSaved => "never-saved", PickReason::ReadOnly => "read-only" },
        }),
        Request::Save { id, path: p } => json!({ "type": "save", "id": id, "path": path(p) }),
    }
}

/// The snapshot in the shape `scenarios/README.md` describes.
fn to_json(s: &Snapshot<String>) -> Value {
    let rows: Vec<Value> = s
        .rows
        .iter()
        .map(|r| {
            json!({
                "id": r.id,
                "name": r.name,
                "where": r.location.as_deref().map_or(Value::Null, path),
                "readOnly": r.read_only,
                "ticked": r.ticked,
                "state": match r.state {
                    RowState::Unsaved => "unsaved",
                    RowState::Saving => "saving",
                    RowState::Saved => "saved",
                },
                "note": match &r.note {
                    None => Value::Null,
                    Some(Note::Failed(text)) => json!({ "kind": "failed", "text": text }),
                    Some(Note::Cancelled) => json!({ "kind": "cancelled", "text": null }),
                },
            })
        })
        .collect();
    let saved: Map<String, Value> = s
        .saved
        .iter()
        .map(|(id, p)| (id.clone(), path(p)))
        .collect();
    json!({
        "prompt": match s.prompt { Prompt::List => "list", Prompt::Single => "single", Prompt::None => "none" },
        "count": s.count,
        "rows": rows,
        "request": s.request.as_ref().map_or(Value::Null, request),
        "outcome": match s.outcome {
            None => Value::Null,
            Some(Outcome::Proceeded) => json!("proceeded"),
            Some(Outcome::Stopped) => json!("stopped"),
        },
        "open": ids(&s.open),
        "closed": ids(&s.closed),
        "saved": saved,
        "lost": ids(&s.lost),
        "abandoned": ids(&s.abandoned),
    })
}

/// `want` is a partial picture: every field it names must match; arrays must match in length and
/// order, each element matched the same partial way.
fn differs(got: &Value, want: &Value, at: &str) -> Option<String> {
    let mismatch = || Some(format!("{at}: wanted {want}, got {got}"));
    match want {
        Value::Array(w) => {
            let Value::Array(g) = got else {
                return mismatch();
            };
            if g.len() != w.len() {
                return mismatch();
            }
            g.iter()
                .zip(w)
                .enumerate()
                .find_map(|(i, (g, w))| differs(g, w, &format!("{at}[{i}]")))
        }
        Value::Object(w) => {
            if !got.is_object() {
                return mismatch();
            }
            w.iter().find_map(|(k, w)| {
                differs(got.get(k).unwrap_or(&Value::Null), w, &format!("{at}.{k}"))
            })
        }
        // Numbers compare by value, so 1 and 1.0 agree as they do in JavaScript.
        Value::Number(w) => match got {
            Value::Number(g) if g.as_f64() == w.as_f64() => None,
            _ => mismatch(),
        },
        _ => (got != want).then(mismatch).flatten(),
    }
}

fn doc(d: &Value) -> Doc<String> {
    Doc {
        id: d["id"].as_str().expect("doc id").to_owned(),
        name: d["name"].as_str().expect("doc name").to_owned(),
        path: d["path"].as_str().map(PathBuf::from),
        dirty: d["dirty"].as_bool().unwrap_or(false),
        read_only: d["readOnly"].as_bool().unwrap_or(false),
    }
}

fn input(m: &mut Closing<String>, step: &Value) -> bool {
    let id = || step["id"].as_str().expect("step id").to_owned();
    match step["do"].as_str().expect("step do") {
        "tick" => m.tick(&id(), step["on"].as_bool().expect("step on")),
        "saveSelected" => m.save_selected(),
        "dontSave" => m.dont_save(),
        "cancel" => m.cancel(),
        "rowSave" => m.row_save(&id()),
        "rowDiscard" => m.row_discard(&id()),
        "picked" => m.picked(step["path"].as_str().expect("step path")),
        "pickCancelled" => m.pick_cancelled(),
        "saveDone" => m.save_done(),
        "saveFailed" => m.save_failed(step["reason"].as_str().expect("step reason")),
        "docChanged" => m.doc_changed(&id()),
        other => panic!("unknown input {other}"),
    }
}

fn run(scenario: &Value) -> Option<String> {
    let docs = scenario["docs"].as_array().expect("docs");
    let mut m = Closing::new(docs.iter().map(doc));
    for (i, step) in scenario["steps"]
        .as_array()
        .expect("steps")
        .iter()
        .enumerate()
    {
        if let Some(want) = step.get("expect") {
            if let Some(d) = differs(&to_json(&m.snapshot()), want, "snapshot") {
                return Some(format!("step {}: {d}", i + 1));
            }
            continue;
        }
        let accepted = input(&mut m, step);
        let ignored = step["ignored"].as_bool().unwrap_or(false);
        if accepted == ignored {
            let how = if accepted { "accepted" } else { "ignored" };
            return Some(format!("step {}: {} was {how}", i + 1, step["do"]));
        }
    }
    None
}

#[test]
fn every_scenario_passes() {
    let file = Path::new(env!("CARGO_MANIFEST_DIR")).join("../../scenarios/scenarios.json");
    let text = std::fs::read_to_string(&file).expect("read scenarios.json");
    let all: Value = serde_json::from_str(&text).expect("parse scenarios.json");
    let scenarios = all["scenarios"].as_array().expect("scenarios");
    // A guard against a file that loaded as nearly nothing; the file itself may grow.
    assert!(scenarios.len() >= 37, "too few scenarios loaded");
    let failures: Vec<String> = scenarios
        .iter()
        .filter_map(|sc| run(sc).map(|p| format!("FAIL {}: {p}", sc["id"])))
        .collect();
    assert!(failures.is_empty(), "{}", failures.join("\n"));
}

#[test]
fn poll_request_hands_each_request_out_once() {
    let mut m = Closing::new([
        Doc::new(1, "Bracket", Some("Bracket.sc".into())),
        Doc::new(2, "Untitled", None),
    ]);
    assert_eq!(m.poll_request(), None);
    assert!(m.save_selected());
    assert!(matches!(
        m.poll_request(),
        Some(Request::Save { id: 1, .. })
    ));
    assert_eq!(m.poll_request(), None);
    assert!(
        m.request().is_some(),
        "the request stays until it is answered"
    );
    assert!(m.save_done());
    assert!(matches!(
        m.poll_request(),
        Some(Request::Pick {
            id: 2,
            why: PickReason::NeverSaved
        })
    ));
    assert!(m.picked("Untitled.sc"));
    assert!(matches!(
        m.poll_request(),
        Some(Request::Save { id: 2, .. })
    ));
    assert!(m.save_done());
    assert_eq!(m.poll_request(), None);
    assert_eq!(m.outcome(), Some(Outcome::Proceeded));
}
