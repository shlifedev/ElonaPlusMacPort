const { invoke } = window.__TAURI__.core;
const { listen } = window.__TAURI__.event;
const $ = (q) => document.querySelector(q);
let lang = "ko";

function render(s) {
  lang = s.language;
  $("#dir").textContent = s.game_dir || "선택 안 됨";
  $("#dir").title = s.game_dir || "";
  $("#msg").textContent = s.error || (s.game_dir ? "" : "elonaplus.exe가 있는 2.335 폴더를 골라 주세요.");
  $("#play").disabled = !s.game_dir || !!s.error;
  document.querySelectorAll("[data-lang]").forEach((b) => b.classList.toggle("on", b.dataset.lang === lang));
}

$("#pick").onclick = async () => render(await invoke("pick_folder"));

document.querySelectorAll("[data-lang]").forEach((b) => {
  b.onclick = () => {
    lang = b.dataset.lang;
    document.querySelectorAll("[data-lang]").forEach((x) => x.classList.toggle("on", x === b));
  };
});

$("#play").onclick = async () => {
  $("#play").disabled = true;
  try {
    await invoke("play", { language: lang });
  } catch (e) {
    $("#msg").textContent = e;
    $("#play").disabled = false;
  }
};

listen("game-exit", (e) => {
  $("#msg").textContent = e.payload;
  $("#play").disabled = false;
});

invoke("get_state").then(render);
