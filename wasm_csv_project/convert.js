const sectionIds = ["stats-section", "visualize-section", "json-section"];

function showSection(id) {
  sectionIds.forEach((sid) => {
    const el = document.getElementById(sid);
    if (el) el.classList.add("hidden");
  });
  const target = document.getElementById(id);
  target.classList.remove("hidden");
}

const btnStats = document.getElementById("btn-show-stats");
const btnData = document.getElementById("btn-show-data");
const btnJsonMain = document.getElementById("btn-download-json-main");

if (btnStats)
  btnStats.addEventListener("click", () => showSection("stats-section"));
if (btnData)
  btnData.addEventListener("click", () => showSection("visualize-section"));
if (btnJsonMain)
  btnJsonMain.addEventListener("click", () => showSection("json-section"));

const btnDownloadJson = document.getElementById("btn-download-json");
if (btnDownloadJson) {
  btnDownloadJson.addEventListener("click", () => {
    const sample = { message: "JSON export placeholder" };
    const blob = new Blob([JSON.stringify(sample, null, 2)], {
      type: "application/json",
    });
    const url = URL.createObjectURL(blob);
    const a = document.createElement("a");
    a.href = url;
    a.download = "data.json";
    document.body.appendChild(a);
    a.click();
    a.remove();
    URL.revokeObjectURL(url);
  });
}

// Try to load a sample JSON file (시군구별 교통사고 통계.json) and render it
const sampleJsonRelPath = '../data_sample_to_json/시군구별 교통사고 통계.json';
const beautifiedEl = document.getElementById('beautified');

function renderJson(obj) {
  if (!beautifiedEl) return;
  try {
    beautifiedEl.textContent = JSON.stringify(obj, null, 2);
  } catch (e) {
    beautifiedEl.textContent = String(obj);
  }
}

fetch(encodeURI(sampleJsonRelPath))
  .then((res) => {
    if (!res.ok) throw new Error('HTTP ' + res.status);
    return res.json();
  })
  .then((json) => {
    window.sampleData = json; // expose for debugging
    renderJson(json);
  })
  .catch((err) => {
    console.warn('샘플 JSON 로드 실패:', err);
    if (beautifiedEl) beautifiedEl.textContent = '샘플 JSON을 로드할 수 없습니다: ' + err;
  });