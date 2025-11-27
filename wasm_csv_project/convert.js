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

// Global variable to store converted JSON data
let convertedJsonData = null;
let uploadedFileName = '';

const btnDownloadJson = document.getElementById("btn-download-json");
if (btnDownloadJson) {
  btnDownloadJson.addEventListener("click", () => {
    if (!convertedJsonData) {
      alert('변환된 JSON 데이터가 없습니다.');
      return;
    }

    const blob = new Blob([JSON.stringify(convertedJsonData, null, 2)], {
      type: "application/json",
    });
    const url = URL.createObjectURL(blob);
    const a = document.createElement("a");
    a.href = url;
    // Use original filename with .json extension
    const jsonFilename = uploadedFileName.replace(/\.(csv|CSV)$/, '') + '.json';
    a.download = jsonFilename;
    document.body.appendChild(a);
    a.click();
    a.remove();
    URL.revokeObjectURL(url);
  });
}

const beautifiedEl = document.getElementById('beautified');

function renderJson(obj) {
  if (!beautifiedEl) return;
  try {
    beautifiedEl.textContent = JSON.stringify(obj, null, 2);
  } catch (e) {
    beautifiedEl.textContent = String(obj);
  }
}

// IndexedDB helper
function openDb(dbName = 'wasm_csv_db', storeName = 'files') {
  return new Promise((resolve, reject) => {
    const req = indexedDB.open(dbName, 1);
    req.onupgradeneeded = (e) => {
      const db = e.target.result;
      if (!db.objectStoreNames.contains(storeName)) {
        db.createObjectStore(storeName);
      }
    };
    req.onsuccess = (e) => resolve({ db: e.target.result, storeName });
    req.onerror = (e) => reject(e.target.error);
  });
}

function getFile(key) {
  return openDb().then(({ db, storeName }) => new Promise((resolve, reject) => {
    const tx = db.transaction(storeName, 'readonly');
    const store = tx.objectStore(storeName);
    const req = store.get(key);
    req.onsuccess = () => resolve(req.result);
    req.onerror = (e) => reject(e.target.error);
  }));
}

// Load and convert CSV file from IndexedDB
async function loadAndConvertCsv() {
  try {
    // Get filename from sessionStorage
    uploadedFileName = sessionStorage.getItem('uploadedFileName') || 'data';

    // Load file from IndexedDB
    const file = await getFile('uploaded-file');
    if (!file) {
      throw new Error('업로드된 파일을 찾을 수 없습니다.');
    }

    // Read file as ArrayBuffer and detect encoding
    const arrayBuffer = await file.arrayBuffer();
    const uint8Array = new Uint8Array(arrayBuffer);

    // Auto-detect encoding (UTF-8, UTF-16, EUC-KR, CP949)
    let text;

    // Check for BOM (Byte Order Mark)
    if (uint8Array.length >= 3 &&
        uint8Array[0] === 0xEF &&
        uint8Array[1] === 0xBB &&
        uint8Array[2] === 0xBF) {
      // UTF-8 with BOM
      const decoder = new TextDecoder('utf-8');
      text = decoder.decode(uint8Array.slice(3));
    } else if (uint8Array.length >= 2 &&
               uint8Array[0] === 0xFF &&
               uint8Array[1] === 0xFE) {
      // UTF-16 LE
      const decoder = new TextDecoder('utf-16le');
      text = decoder.decode(uint8Array.slice(2));
    } else if (uint8Array.length >= 2 &&
               uint8Array[0] === 0xFE &&
               uint8Array[1] === 0xFF) {
      // UTF-16 BE
      const decoder = new TextDecoder('utf-16be');
      text = decoder.decode(uint8Array.slice(2));
    } else {
      // Try UTF-8 first (most common)
      try {
        const decoder = new TextDecoder('utf-8', { fatal: true });
        text = decoder.decode(uint8Array);
      } catch (e) {
        // If UTF-8 fails, try common Korean encodings
        const encodings = ['euc-kr', 'windows-949', 'x-windows-949', 'ks_c_5601-1987'];
        let decoded = false;

        for (const encoding of encodings) {
          try {
            const decoder = new TextDecoder(encoding);
            text = decoder.decode(uint8Array);
            // Check if decoding produced valid Korean characters
            if (text.includes('�') === false || text.length > 0) {
              decoded = true;
              break;
            }
          } catch (e) {
            continue;
          }
        }

        // Final fallback
        if (!decoded) {
          const decoder = new TextDecoder();
          text = decoder.decode(uint8Array);
        }
      }
    }

    // Wait for WASM module to be ready
    if (!Module || !Module.convertToJsonAuto) {
      await new Promise((resolve) => {
        Module = Module || {};
        Module.onRuntimeInitialized = resolve;
      });
    }

    // Convert CSV to JSON using WASM
    const jsonString = Module.convertToJsonAuto(text, uploadedFileName);
    convertedJsonData = JSON.parse(jsonString);

    // Render JSON
    renderJson(convertedJsonData);
    window.convertedData = convertedJsonData; // expose for debugging

  } catch (err) {
    console.error('CSV 변환 실패:', err);
    if (beautifiedEl) {
      beautifiedEl.textContent = 'CSV를 JSON으로 변환할 수 없습니다: ' + err.message;
    }
  }
}

// Start loading and converting when page loads
loadAndConvertCsv();