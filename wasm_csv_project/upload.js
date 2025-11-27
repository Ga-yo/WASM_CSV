const fileInput = document.getElementById("file-input");
const dropzone = document.getElementById("dropzone");

// --- IndexedDB helpers (simple Promise wrappers) ---
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

function putFile(key, file) {
  return openDb().then(({ db, storeName }) => new Promise((resolve, reject) => {
    const tx = db.transaction(storeName, 'readwrite');
    const store = tx.objectStore(storeName);
    const req = store.put(file, key);
    req.onsuccess = () => resolve();
    req.onerror = (e) => reject(e.target.error);
  }));
}

// 파일 선택 이벤트
fileInput.addEventListener("change", (event) => {
  const file = event.target.files[0];
  if (!file) return;
  handleFileUpload(file);
});

// 드래그 앤 드롭 이벤트
dropzone.addEventListener("dragover", (event) => {
  event.preventDefault();
  highlight();
});

dropzone.addEventListener("dragenter", (event) => {
  event.preventDefault();
  highlight();
});

dropzone.addEventListener("dragleave", (event) => {
  event.preventDefault();
  unhighlight();
});

dropzone.addEventListener("drop", (event) => {
  event.preventDefault();
  unhighlight();

  const file = event.dataTransfer.files[0];
  if (!file) return;

  handleFileUpload(file);
});

function highlight() {
  dropzone.classList.add("bg-neutral-100");
}
function unhighlight() {
  dropzone.classList.remove("bg-neutral-100");
}

function handleFileUpload(file) {
  console.log("업로드 파일:", file.name);

  // 저장: 파일 자체를 IndexedDB에 넣고(추천) 메타 정보는 sessionStorage에 보관
  putFile('uploaded-file', file).then(() => {
    try {
      sessionStorage.setItem('uploadedFileName', file.name);
      sessionStorage.setItem('uploadedFileSize', String(file.size));
    } catch (e) {
      console.warn('sessionStorage 저장 실패', e);
    }
    // 변환 페이지로 이동
    window.location.href = "convert.html";
  }).catch((err) => {
    console.error('IndexedDB 저장 실패, 세션에만 저장합니다', err);
    try {
      sessionStorage.setItem('uploadedFileName', file.name);
      sessionStorage.setItem('uploadedFileSize', String(file.size));
    } catch (e) {
      console.warn('sessionStorage 저장 실패', e);
    }
    window.location.href = "convert.html";
  });
}
