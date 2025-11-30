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
  console.log("업로드 파일:", file.name, "크기:", file.size, "bytes");

  try {
    // DBManager.putFile now handles both IndexedDB and sessionStorage
    await dbManager.putFile('uploaded-file', file);
    
    // On success, redirect to the conversion page
    window.location.href = "convert.html";

  } catch (err) {
    console.error('파일 저장 실패:', err);

    // Check for QuotaExceededError, which DBManager will now throw on transaction error
    if (err.name === 'QuotaExceededError' || (err.message && err.message.toLowerCase().includes('quota'))) {
      alert(`파일 저장 실패: 파일이 너무 큽니다(현재 크기: ${(file.size / 1024 / 1024).toFixed(1)}MB). 브라우저의 로컬 저장 용량을 초과했습니다. 더 작은 파일을 사용해주세요.`);
    } else {
      alert(`알 수 없는 오류로 파일 저장에 실패했습니다: ${err.name}\n\n이 문제가 계속되면 다른 브라우저를 사용해보세요.`);
    }
    // Do NOT redirect on failure
  }
}
