#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

use serde::Serialize;
use sha2::{Digest, Sha256};
use std::fs::{self, File};
use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};
use std::time::{SystemTime, UNIX_EPOCH};
use tauri::{AppHandle, Emitter, Manager};
use tauri_plugin_dialog::DialogExt;

const EXE_SHA256: &str = "7d372ac439788de5288cacf200d46d4352d036aa74ae5715f654aaeab56a1e4e";
const KEEP_SESSIONS: usize = 10;

#[derive(Serialize)]
struct State {
    game_dir: Option<String>,
    language: String,
    error: Option<String>,
}

fn home(app: &AppHandle) -> PathBuf {
    app.path().data_dir().expect("no data dir").join("ElonaPlus Native")
}

fn load(app: &AppHandle) -> (Option<PathBuf>, String) {
    let text = fs::read_to_string(home(app).join("launcher.cfg")).unwrap_or_default();
    let mut dir = None;
    let mut lang = "ko".to_string();
    for line in text.lines() {
        match line.split_once('=') {
            Some(("game_dir", v)) if !v.is_empty() => dir = Some(PathBuf::from(v)),
            Some(("language", v)) if v == "ko" || v == "original" => lang = v.to_string(),
            _ => {}
        }
    }
    (dir, lang)
}

fn save(app: &AppHandle, dir: &Path, lang: &str) {
    let home = home(app);
    let _ = fs::create_dir_all(&home);
    let _ = fs::write(home.join("launcher.cfg"), format!("game_dir={}\nlanguage={lang}\n", dir.display()));
}

fn check(dir: &Path) -> Result<(), String> {
    let exe = fs::read(dir.join("elonaplus.exe")).map_err(|_| "이 폴더에 elonaplus.exe가 없어요.".to_string())?;
    if format!("{:x}", Sha256::digest(&exe)) != EXE_SHA256 {
        return Err("ElonaPlus 2.335가 아니에요. 2.335만 지원해요.".into());
    }
    Ok(())
}

fn state(dir: Option<PathBuf>, language: String) -> State {
    let error = dir.as_deref().and_then(|d| check(d).err());
    State { game_dir: dir.map(|d| d.display().to_string()), language, error }
}

#[tauri::command]
fn get_state(app: AppHandle) -> State {
    let (dir, lang) = load(&app);
    state(dir, lang)
}

#[tauri::command]
async fn pick_folder(app: AppHandle) -> State {
    let (dir, lang) = load(&app);
    let Some(picked) = app.dialog().file().blocking_pick_folder().and_then(|p| p.into_path().ok()) else {
        return state(dir, lang);
    };
    if check(&picked).is_ok() {
        save(&app, &picked, &lang);
    }
    state(Some(picked), lang)
}

fn new_session(sessions: &Path) -> std::io::Result<PathBuf> {
    fs::create_dir_all(sessions)?;
    let mut old: Vec<_> = fs::read_dir(sessions)?
        .flatten()
        .map(|e| e.path())
        .filter(|p| p.file_name().is_some_and(|n| n.to_string_lossy().starts_with("play-")))
        .collect();
    old.sort();
    for p in old.iter().rev().skip(KEEP_SESSIONS - 1) {
        let _ = fs::remove_dir_all(p);
    }
    let stamp = SystemTime::now().duration_since(UNIX_EPOCH).map(|d| d.as_secs()).unwrap_or(0);
    let session = sessions.join(format!("play-{stamp}"));
    fs::create_dir_all(session.join("cwd"))?;
    Ok(session)
}

#[tauri::command]
fn play(app: AppHandle, language: String) -> Result<(), String> {
    let (dir, _) = load(&app);
    let dir = dir.ok_or("게임 폴더를 먼저 골라 주세요.")?;
    check(&dir)?;
    let ko = language == "ko";
    save(&app, &dir, if ko { "ko" } else { "original" });

    let home = home(&app);
    let user = home.join("user");
    fs::create_dir_all(&user).map_err(|e| e.to_string())?;
    if user.starts_with(&dir) {
        return Err("세이브 폴더가 게임 폴더 안에 있으면 안 돼요.".into());
    }
    let lock = File::create(user.join(".native-player.lock")).map_err(|e| e.to_string())?;
    lock.try_lock().map_err(|_| "이미 게임이 실행 중이에요.".to_string())?;

    let session = new_session(&home.join("sessions")).map_err(|e| e.to_string())?;
    let data = app.path().resource_dir().map_err(|e| e.to_string())?.join("data");
    let game = data.join("game");
    let host = std::env::current_exe()
        .map_err(|e| e.to_string())?
        .with_file_name(if cfg!(windows) { "elona_gui_host.exe" } else { "elona_gui_host" });

    let mut cmd = Command::new(host);
    cmd.arg("--game").arg(game.join(if ko { "start-ko.ax" } else { "start.ax" }))
        .arg("--game-root").arg(&dir)
        .arg("--user-root").arg(&user)
        .arg("--packed-dir").arg(&game)
        .arg("--work-dir").arg(session.join("cwd"))
        .arg("--report").arg(session.join("report.json"))
        .arg("--soundfont").arg(data.join("GeneralUser-GS.sf2"))
        .arg("--auxiliary-close-hide");
    if ko {
        cmd.arg("--translation").arg(data.join("localization/runtime.tsv"))
            .arg("--translation-fallback").arg(data.join("localization/draft-display.tsv"));
    }
    let log = |name: &str| File::create(session.join(name)).map(Stdio::from).map_err(|e| e.to_string());
    cmd.stdin(Stdio::null()).stdout(log("stdout.log")?).stderr(log("stderr.log")?);
    #[cfg(windows)]
    {
        use std::os::windows::process::CommandExt;
        cmd.creation_flags(0x0800_0000); // CREATE_NO_WINDOW
    }
    let mut child = cmd.spawn().map_err(|e| format!("게임을 실행하지 못했어요: {e}"))?;

    let window = app.get_webview_window("main");
    if let Some(w) = &window {
        let _ = w.hide();
    }
    std::thread::spawn(move || {
        let status = child.wait();
        drop(lock);
        if matches!(status, Ok(s) if s.success()) {
            app.exit(0);
            return;
        }
        if let Some(w) = window {
            let _ = w.show();
            let _ = w.set_focus();
        }
        let _ = app.emit("game-exit", format!("게임이 오류로 종료됐어요. 로그: {}", session.display()));
    });
    Ok(())
}

fn main() {
    tauri::Builder::default()
        .plugin(tauri_plugin_dialog::init())
        .invoke_handler(tauri::generate_handler![get_state, pick_folder, play])
        .run(tauri::generate_context!())
        .expect("failed to start launcher");
}
