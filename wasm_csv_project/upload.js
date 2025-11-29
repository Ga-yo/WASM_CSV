import DBManager from './DBManager.js';

const fileInput = document.getElementById("file-input");
const dropzone = document.getElementById("dropzone");

// Create an instance of the DBManager
const dbManager = new DBManager();

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

async function handleFileUpload(file) {
  console.log("업로드 파일:", file.name);

  // 1. 파일 메타정보를 sessionStorage에 먼저 저장합니다.
  sessionStorage.setItem('uploadedFileName', file.name);
  sessionStorage.setItem('uploadedFileSize', String(file.size));

  try {
    // 2. 파일 객체를 IndexedDB에 저장합니다.
    await dbManager.putFile('uploaded-file', file);
  } catch (err) {
    console.error('IndexedDB 저장 실패:', err);
    alert('파일을 브라우저 데이터베이스에 저장하는 데 실패했습니다. 페이지를 벗어나면 파일이 사라질 수 있습니다.');
  }
  
  // 3. 변환 페이지로 이동합니다.
  window.location.href = "convert.html";
}
