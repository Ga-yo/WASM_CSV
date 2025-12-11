import DBManager from "./DBManager.js";

const spinnerContainer = document.getElementById("spinner-container");
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
let uploadedFileName = "";
let originalCsvContent = null; // Store original CSV for direct Excel conversion
let currentSortColumn = null;
let currentSortDirection = null; // 'asc' or 'desc'
let originalDataOrder = null; // Store original order for reset
let activeFilter = null; // 'max', 'min', 'avg', or 'stats'

// Chart related variables
let myChart = null;
const chartTypeSelect = document.getElementById("chart-type");
const chartLabelColSelect = document.getElementById("chart-label-col");
const chartDataColSelect = document.getElementById("chart-data-col");
const chartCanvas = document.getElementById("myChart");

// Create an instance of the DBManager
const dbManager = new DBManager();

const btnDownloadJson = document.getElementById("btn-download-json");
if (btnDownloadJson) {
  btnDownloadJson.addEventListener("click", () => {
    if (!convertedJsonData) {
      alert("변환된 JSON 데이터가 없습니다.");
      return;
    }

    try {
      // Use streaming approach for large data
      const chunks = [];

      // Start JSON structure
      chunks.push('{\n');

      // Add metadata if exists
      if (convertedJsonData.metadata) {
        chunks.push('  "metadata": ');
        chunks.push(JSON.stringify(convertedJsonData.metadata, null, 2).split('\n').map((line, i) => i === 0 ? line : '  ' + line).join('\n'));
        chunks.push(',\n');
      }

      // Add data array start
      chunks.push('  "data": [\n');

      // Process data in chunks to avoid string length limits
      const dataLength = convertedJsonData.data.length;
      const chunkSize = 1000; // Process 1000 rows at a time

      for (let i = 0; i < dataLength; i += chunkSize) {
        const endIndex = Math.min(i + chunkSize, dataLength);
        const dataChunk = convertedJsonData.data.slice(i, endIndex);

        dataChunk.forEach((row, index) => {
          const isLastInChunk = (i + index === dataLength - 1);
          const rowStr = JSON.stringify(row, null, 2)
            .split('\n')
            .map((line, lineIndex) => lineIndex === 0 ? '    ' + line : '    ' + line)
            .join('\n');

          chunks.push(rowStr);
          if (!isLastInChunk) {
            chunks.push(',\n');
          } else {
            chunks.push('\n');
          }
        });
      }

      // Close data array and JSON structure
      chunks.push('  ]\n');
      chunks.push('}');

      // Create blob from chunks
      const blob = new Blob(chunks, {
        type: "application/json",
      });

      const url = URL.createObjectURL(blob);
      const a = document.createElement("a");
      a.href = url;
      // Use original filename with .json extension
      const jsonFilename = uploadedFileName.replace(/\.(csv|CSV)$/, "") + ".json";
      a.download = jsonFilename;
      document.body.appendChild(a);
      a.click();
      a.remove();
      URL.revokeObjectURL(url);
    } catch (e) {
      console.error("JSON 다운로드 오류:", e);
      alert("JSON 다운로드 중 오류가 발생했습니다: " + e.message);
    }
  });
}

const beautifiedEl = document.getElementById("beautified");

function renderJson(obj) {
  if (!beautifiedEl) return;
  try {
    // Ensure we're working with an object, not a string
    const dataToRender = typeof obj === 'string' ? JSON.parse(obj) : obj;

    // Check if data is too large for rendering
    const dataSize = dataToRender.data ? dataToRender.data.length : 0;
    if (dataSize > 1000) {
      // Show a preview instead of the full data
      const preview = {
        ...dataToRender,
        data: dataToRender.data.slice(0, 100) // Only first 100 rows
      };
      beautifiedEl.textContent = JSON.stringify(preview, null, 2);
      beautifiedEl.textContent += `\n\n... (${dataSize - 100}개 행 생략됨. 전체 데이터는 다운로드 버튼을 사용하세요)`;
    } else {
      beautifiedEl.textContent = JSON.stringify(dataToRender, null, 2);
    }
  } catch (e) {
    console.error("JSON rendering error:", e);
    beautifiedEl.textContent = "JSON 렌더링 오류: " + e.message + "\n\n데이터가 너무 큽니다. 다운로드 버튼을 사용해주세요.";
  }
}

// Function to update the uploaded file UI
function updateFileInfoUI(file) {
  const fileNameEl = document.getElementById("uploaded-file-name");
  const fileSizeEl = document.getElementById("uploaded-file-size");

  if (fileNameEl && fileSizeEl && file) {
    fileNameEl.textContent = file.name;
    fileSizeEl.textContent = `${(file.size / 1024).toFixed(2)} KB`;
    uploadedFileName = file.name; // Store filename
  }
}

// Event listener for delete button
const btnDeleteFile = document.getElementById("btn-delete-file");
if (btnDeleteFile) {
  btnDeleteFile.addEventListener("click", async () => {
    if (
      confirm("파일을 삭제하시겠습니까? 분석 페이지에서 파일이 제거됩니다.")
    ) {
      try {
        await dbManager.deleteFile("uploaded-file");
        alert("파일이 삭제되었습니다.");
        window.location.href = "upload.html"; // Redirect to upload page
      } catch (err) {
        console.error("파일 삭제 실패:", err);
        alert("파일 삭제 중 오류가 발생했습니다.");
      }
    }
  });
}

// Load and convert CSV file from IndexedDB
async function loadAndConvertCsv() {
  console.log("Starting CSV conversion process...");
  spinnerContainer.classList.remove("hidden");
  try {
    console.time("CSV Conversion");
    // Get filename from sessionStorage
    uploadedFileName = sessionStorage.getItem("uploadedFileName") || "data";

    // Load file from IndexedDB using DBManager
    console.log("Fetching file from IndexedDB...");
    const file = await dbManager.getFile("uploaded-file");
    console.log("File fetched successfully from IndexedDB.");

    if (!file) {
      throw new Error("업로드된 파일을 찾을 수 없습니다.");
    }

    // Update UI with file info
    updateFileInfoUI(file);

    // Read file as ArrayBuffer and detect encoding
    const arrayBuffer = await file.arrayBuffer();
    const uint8Array = new Uint8Array(arrayBuffer);

    console.log("Starting encoding detection...");
    // Auto-detect encoding (UTF-8, UTF-16, EUC-KR, CP949)
    let text;

    // Check for BOM (Byte Order Mark)
    if (uint8Array.length >= 3 &&
        uint8Array[0] === 0xEF &&
        uint8Array[1] === 0xBB &&
        uint8Array[2] === 0xBF) {
      // UTF-8 with BOM
      console.log("Detected UTF-8 with BOM.");
      const decoder = new TextDecoder('utf-8');
      text = decoder.decode(uint8Array.slice(3));
    } else if (uint8Array.length >= 2 &&
               uint8Array[0] === 0xFF &&
               uint8Array[1] === 0xFE) {
      // UTF-16 LE
      console.log("Detected UTF-16 LE.");
      const decoder = new TextDecoder('utf-16le');
      text = decoder.decode(uint8Array.slice(2));
    } else if (uint8Array.length >= 2 &&
               uint8Array[0] === 0xFE &&
               uint8Array[1] === 0xFF) {
      // UTF-16 BE
      console.log("Detected UTF-16 BE.");
      const decoder = new TextDecoder('utf-16be');
      text = decoder.decode(uint8Array.slice(2));
    } else {
      // Try UTF-8 first (most common)
      try {
        console.log("Attempting to decode with UTF-8...");
        const decoder = new TextDecoder('utf-8', { fatal: true });
        text = decoder.decode(uint8Array);
        console.log("Successfully decoded with UTF-8.");
      } catch (e) {
        console.log("UTF-8 decoding failed. Trying Korean encodings...");
        // If UTF-8 fails, try common Korean encodings
        const encodings = ['euc-kr', 'cp949', 'windows-949'];
        let decoded = false;

        for (const encoding of encodings) {
          try {
            console.log(`Attempting to decode with ${encoding}...`);
            // fatal: true 옵션으로 엄격하게 디코딩을 시도합니다.
            const decoder = new TextDecoder(encoding, { fatal: true });
            text = decoder.decode(uint8Array);
            decoded = true;
            console.log(`Successfully decoded with ${encoding}`);
            break;
          } catch (e) {
            console.log(`Decoding with ${encoding} failed.`);
            // 디코딩 실패 시 다음 인코딩으로 넘어갑니다.
            continue;
          }
        }

        if (!decoded) {
          throw new Error('파일 인코딩을 감지할 수 없습니다. (UTF-8, EUC-KR, CP949 시도 실패)');
        }
      }
    }
    console.log("Encoding detection and decoding complete.");

    // Wait for WASM module to be ready using the global promise
    if (!Module || !Module.convertToJsonOptimized) {
      console.log('Waiting for WASM module to initialize via promise...');
      await window.wasmReadyPromise;
      console.log('WASM module is ready to use.');
    } else {
      console.log('WASM module was already loaded.');
    }

    // Store original CSV content for direct Excel conversion
    originalCsvContent = text;
    console.log("Original CSV content stored for Excel conversion.");

    // Convert CSV to JSON using WASM
    console.log("Calling WASM function 'convertToJsonOptimized'...");
    const wasmStartTime = performance.now();
    const jsonString = Module.convertToJsonOptimized(text, uploadedFileName);
    const wasmEndTime = performance.now();
    const wasmTime = wasmEndTime - wasmStartTime;
    console.log("WASM function execution finished.");

    const csvSize = new Blob([text]).size;
    const jsonSize = new Blob([jsonString]).size;
    const conversionRate = (csvSize / (1024 * 1024)) / (wasmTime / 1000); // MB/s

    console.log(`[CSV->JSON Conversion Stats]
- CSV Size: ${(csvSize / 1024).toFixed(2)} KB
- JSON Size: ${(jsonSize / 1024).toFixed(2)} KB
- WASM Conversion Time: ${wasmTime.toFixed(2)} ms
- Conversion Rate: ${conversionRate.toFixed(2)} MB/s`);

    console.log("Parsing JSON string...");
    const parsedData = JSON.parse(jsonString);

    // C++ 모듈에서 반환된 오류 확인
    if (parsedData.error) {
      // 오류가 있으면 alert으로 사용자에게 알리고, 업로드 페이지로 리디렉션
      alert(`파일 처리 오류: ${parsedData.error}`);
      window.location.href = "upload.html";
      return; // 추가 처리를 중단
    }
    convertedJsonData = parsedData;

    // Render JSON
    renderJson(convertedJsonData);
    window.convertedData = convertedJsonData; // expose for debugging

    // Initialize chart section
    initializeChartSection();

    // Initialize stats section
    initializeStatsSection();
    console.log("Initialization of UI sections complete.");
  } catch (err) {
    console.error("CSV 변환 실패:", err);
    if (beautifiedEl) {
      beautifiedEl.textContent =
        "CSV를 JSON으로 변환할 수 없습니다: " + err.message;
    }
    // If file is not found, redirect to upload page
    if (err.message.includes("찾을 수 없습니다")) {
      alert("분석할 파일이 없습니다. 먼저 파일을 업로드해주세요.");
      window.location.href = "upload.html";
    }
  } finally {
    spinnerContainer.remove();;
    console.timeEnd("CSV Conversion");
  }
}

function initializeStatsSection() {
  // Check if data exists
  if (
    !convertedJsonData ||
    !convertedJsonData.data ||
    convertedJsonData.data.length === 0
  ) {
    return;
  }

  // Store original data order
  originalDataOrder = [...convertedJsonData.data];

  // Reset sort state
  currentSortColumn = null;
  currentSortDirection = null;
  activeFilter = null;

  // Render data table
  renderDataTable();

  // Add event listener for Excel download button
  const btnDownloadExcel = document.getElementById("btn-download-excel");
  if (btnDownloadExcel) {
    btnDownloadExcel.addEventListener("click", downloadAsExcel);
  }

  // Add event listener for reset button
  const btnResetTable = document.getElementById("btn-reset-table");
  if (btnResetTable) {
    btnResetTable.addEventListener("click", resetTableSort);
  }

  // Add event listeners for filter buttons
  const btnFilterMax = document.getElementById("btn-filter-max");
  const btnFilterMin = document.getElementById("btn-filter-min");
  const btnFilterAvg = document.getElementById("btn-filter-avg");
  const btnFilterStats = document.getElementById("btn-filter-stats");

  if (btnFilterMax) {
    btnFilterMax.addEventListener("click", () => toggleFilter("max"));
  }
  if (btnFilterMin) {
    btnFilterMin.addEventListener("click", () => toggleFilter("min"));
  }
  if (btnFilterAvg) {
    btnFilterAvg.addEventListener("click", () => toggleFilter("avg"));
  }
  if (btnFilterStats) {
    btnFilterStats.addEventListener("click", () => toggleFilter("stats"));
  }

  // Add event listener for close stats result
  const btnCloseStats = document.getElementById("btn-close-stats");
  if (btnCloseStats) {
    btnCloseStats.addEventListener("click", hideStatsResult);
  }
}

function renderDataTable() {
  const tableHead = document.getElementById("stats-table-head");
  const tableBody = document.getElementById("stats-table-body");
  const totalRowsEl = document.getElementById("total-rows");

  if (!tableHead || !tableBody) return;

  tableHead.innerHTML = "";
  tableBody.innerHTML = "";

  const data = convertedJsonData.data;
  if (!data || data.length === 0) return;

  // Update total rows count
  if (totalRowsEl) {
    totalRowsEl.textContent = data.length.toLocaleString();
  }

  // Get column names from first data row
  const columns = Object.keys(data[0]);

  // Create table header (가로줄 = 컬럼명)
  const headerRow = document.createElement("tr");
  headerRow.className = "border-b-2 border-neutral-300";

  columns.forEach((colName) => {
    const th = document.createElement("th");
    th.className =
      "p-2 text-left font-semibold text-neutral-900 bg-neutral-50 border border-neutral-200 cursor-pointer hover:bg-neutral-100";
    th.style.userSelect = "none";

    // Create header content with sort icon
    const headerContent = document.createElement("div");
    headerContent.className = "flex items-center gap-2";

    const headerText = document.createElement("span");
    headerText.textContent = colName;

    const sortIcon = document.createElement("i");
    sortIcon.className = "fa-solid text-neutral-400";

    // Set sort icon based on current sort state
    if (currentSortColumn === colName) {
      if (currentSortDirection === "asc") {
        sortIcon.className = "fa-solid fa-sort-up text-blue-600";
      } else if (currentSortDirection === "desc") {
        sortIcon.className = "fa-solid fa-sort-down text-blue-600";
      }
    } else {
      sortIcon.className = "fa-solid fa-sort text-neutral-400";
    }

    headerContent.appendChild(headerText);
    headerContent.appendChild(sortIcon);
    th.appendChild(headerContent);

    // Add click event for sorting or filter application
    th.addEventListener("click", () => {
      if (activeFilter) {
        applyFilterToColumn(colName);
      } else {
        sortTableByColumn(colName);
      }
    });

    headerRow.appendChild(th);
  });

  tableHead.appendChild(headerRow);

  // Create table body (세로줄 = 데이터 행)
  // Limit to first 100 rows for performance
  const displayLimit = Math.min(100, data.length);

  for (let i = 0; i < displayLimit; i++) {
    const row = document.createElement("tr");
    row.className = "border-b border-neutral-100 hover:bg-neutral-50";

    columns.forEach((colName) => {
      const td = document.createElement("td");
      td.className = "p-2 text-neutral-700 border border-neutral-200";

      const value = data[i][colName];
      // Handle null, undefined, or empty values
      td.textContent =
        value === null || value === undefined || value === "" ? "-" : value;

      row.appendChild(td);
    });

    tableBody.appendChild(row);
  }

  // Show message if data is truncated
  if (data.length > displayLimit) {
    const messageRow = document.createElement("tr");
    const messageCell = document.createElement("td");
    messageCell.colSpan = columns.length;
    messageCell.className =
      "p-4 text-center text-neutral-500 italic border border-neutral-200";
    messageCell.textContent = `처음 ${displayLimit}개 행만 표시됩니다. (전체: ${data.length.toLocaleString()}개 행)`;
    messageRow.appendChild(messageCell);
    tableBody.appendChild(messageRow);
  }
}

function sortTableByColumn(columnName) {
  if (
    !convertedJsonData ||
    !convertedJsonData.data ||
    convertedJsonData.data.length === 0
  ) {
    return;
  }

  // Determine sort direction
  if (currentSortColumn === columnName) {
    // Toggle between asc -> desc -> no sort (reset)
    if (currentSortDirection === "asc") {
      currentSortDirection = "desc";
    } else if (currentSortDirection === "desc") {
      // Reset to original order
      currentSortColumn = null;
      currentSortDirection = null;
      convertedJsonData.data = [...originalDataOrder];
      renderDataTable();
      return;
    }
  } else {
    // New column, start with ascending
    currentSortColumn = columnName;
    currentSortDirection = "asc";
  }

  // Sort the data
  convertedJsonData.data.sort((a, b) => {
    let valA = a[columnName];
    let valB = b[columnName];

    // Handle null/undefined/empty values
    if (valA === null || valA === undefined || valA === "") valA = "";
    if (valB === null || valB === undefined || valB === "") valB = "";

    // Try to parse as number for numeric comparison
    const numA = parseFloat(valA);
    const numB = parseFloat(valB);

    let comparison = 0;

    if (!isNaN(numA) && !isNaN(numB)) {
      // Numeric comparison
      comparison = numA - numB;
    } else {
      // String comparison
      comparison = String(valA).localeCompare(String(valB));
    }

    return currentSortDirection === "asc" ? comparison : -comparison;
  });

  // Re-render table
  renderDataTable();
}

function resetTableSort() {
  if (!originalDataOrder) return;

  // Restore original order
  convertedJsonData.data = [...originalDataOrder];
  currentSortColumn = null;
  currentSortDirection = null;

  // Deactivate filter
  activeFilter = null;
  updateFilterButtons();
  hideStatsResult();
  hideFilterInstruction();

  // Re-render table
  renderDataTable();
}

function toggleFilter(filterType) {
  if (activeFilter === filterType) {
    // Deactivate filter
    activeFilter = null;
    hideFilterInstruction();
  } else {
    // Activate filter
    activeFilter = filterType;
    showFilterInstruction();
  }

  updateFilterButtons();
  hideStatsResult();
}

function updateFilterButtons() {
  const btnFilterMax = document.getElementById("btn-filter-max");
  const btnFilterMin = document.getElementById("btn-filter-min");
  const btnFilterAvg = document.getElementById("btn-filter-avg");
  const btnFilterStats = document.getElementById("btn-filter-stats");

  if (btnFilterMax) {
    btnFilterMax.classList.toggle("active", activeFilter === "max");
  }
  if (btnFilterMin) {
    btnFilterMin.classList.toggle("active", activeFilter === "min");
  }
  if (btnFilterAvg) {
    btnFilterAvg.classList.toggle("active", activeFilter === "avg");
  }
  if (btnFilterStats) {
    btnFilterStats.classList.toggle("active", activeFilter === "stats");
  }
}

function showFilterInstruction() {
  const instruction = document.getElementById("filter-instruction");
  if (instruction) {
    instruction.classList.remove("hidden");
  }
}

function hideFilterInstruction() {
  const instruction = document.getElementById("filter-instruction");
  if (instruction) {
    instruction.classList.add("hidden");
  }
}

function hideStatsResult() {
  const statsResult = document.getElementById("stats-result");
  if (statsResult) {
    statsResult.classList.add("hidden");
  }
}

function showStatsResult(text) {
  const statsResult = document.getElementById("stats-result");
  const statsResultText = document.getElementById("stats-result-text");

  if (statsResult && statsResultText) {
    statsResultText.textContent = text;
    statsResult.classList.remove("hidden");
  }
}

function isNumericColumn(columnName) {
  const data = convertedJsonData.data;
  if (!data || data.length === 0) return false;

  // Check first 100 rows to determine if column is numeric
  const sampleSize = Math.min(100, data.length);
  let numericCount = 0;

  for (let i = 0; i < sampleSize; i++) {
    const value = data[i][columnName];
    if (value !== null && value !== undefined && value !== "") {
      const num = parseFloat(value);
      if (!isNaN(num) && isFinite(num)) {
        numericCount++;
      }
    }
  }

  // Consider numeric if at least 80% of non-empty values are numeric
  return numericCount > 0 && numericCount / sampleSize >= 0.8;
}

function applyFilterToColumn(columnName) {
  if (!activeFilter || !convertedJsonData || !convertedJsonData.data) {
    return;
  }

  // Check if column is numeric
  if (!isNumericColumn(columnName)) {
    alert("숫자 타입 데이터만 가능합니다");
    return;
  }

  const data = convertedJsonData.data;
  const values = data
    .map((row) => {
      const val = row[columnName];
      return val !== null && val !== undefined && val !== ""
        ? parseFloat(val)
        : null;
    })
    .filter((v) => v !== null && !isNaN(v) && isFinite(v));

  if (values.length === 0) {
    alert("유효한 숫자 데이터가 없습니다");
    return;
  }

  let resultText = "";

  if (activeFilter === "max") {
    const maxValue = Math.max(...values);
    resultText = `"${columnName}" 열의 최대값: ${maxValue.toLocaleString()}`;
  } else if (activeFilter === "min") {
    const minValue = Math.min(...values);
    resultText = `"${columnName}" 열의 최소값: ${minValue.toLocaleString()}`;
  } else if (activeFilter === "avg") {
    const sum = values.reduce((a, b) => a + b, 0);
    const avg = sum / values.length;
    resultText = `"${columnName}" 열의 평균: ${avg.toFixed(2)}`;
  } else if (activeFilter === "stats") {
    const maxValue = Math.max(...values);
    const minValue = Math.min(...values);
    const sum = values.reduce((a, b) => a + b, 0);
    const avg = sum / values.length;
    const count = values.length;

    resultText = `"${columnName}" 열의 통계 - 개수: ${count.toLocaleString()}, 최소값: ${minValue.toLocaleString()}, 최대값: ${maxValue.toLocaleString()}, 평균: ${avg.toFixed(
      2
    )}`;
  }

  showStatsResult(resultText);
}

// Helper function to parse file range input (e.g., "1-40", "1,5,10", "1-10,15,20-25")
function parseFileRange(input, maxFiles) {
  // Empty or "all" means all files
  if (!input || input.toLowerCase() === 'all') {
    return Array.from({ length: maxFiles }, (_, i) => i + 1);
  }

  const selectedSet = new Set();

  // Split by comma
  const parts = input.split(',');

  for (const part of parts) {
    const trimmed = part.trim();

    if (!trimmed) continue;

    // Check if it's a range (e.g., "1-40")
    if (trimmed.includes('-')) {
      const rangeParts = trimmed.split('-');
      if (rangeParts.length !== 2) {
        throw new Error(`잘못된 범위 형식: "${trimmed}"`);
      }

      const start = parseInt(rangeParts[0].trim());
      const end = parseInt(rangeParts[1].trim());

      if (isNaN(start) || isNaN(end)) {
        throw new Error(`숫자가 아닌 값: "${trimmed}"`);
      }

      if (start < 1 || end > maxFiles || start > end) {
        throw new Error(`범위 오류: "${trimmed}" (1-${maxFiles} 범위 내에서 입력하세요)`);
      }

      for (let i = start; i <= end; i++) {
        selectedSet.add(i);
      }
    } else {
      // Single number
      const num = parseInt(trimmed);

      if (isNaN(num)) {
        throw new Error(`숫자가 아닌 값: "${trimmed}"`);
      }

      if (num < 1 || num > maxFiles) {
        throw new Error(`범위 오류: ${num} (1-${maxFiles} 범위 내에서 입력하세요)`);
      }

      selectedSet.add(num);
    }
  }

  // Convert Set to sorted array
  return Array.from(selectedSet).sort((a, b) => a - b);
}

async function downloadAsExcel() {
  // Check if original CSV content is available
  if (!originalCsvContent) {
    alert("원본 CSV 데이터를 찾을 수 없습니다. 파일을 다시 업로드해주세요.");
    return;
  }

  try {
    // Check if XLSX library is loaded
    if (typeof XLSX === "undefined") {
      alert("Excel 라이브러리를 로드하는 중입니다. 잠시 후 다시 시도해주세요.");
      return;
    }

    console.log("Excel 변환 시작 - 원본 CSV 직접 사용");

    // Split CSV into lines
    const lines = originalCsvContent.split(/\r?\n/).filter(line => line.trim());
    const totalLines = lines.length;

    if (totalLines === 0) {
      alert("CSV 파일이 비어있습니다.");
      return;
    }

    // Split into multiple Excel files (10,000 rows per file for stability)
    const ROWS_PER_FILE = 10000;
    const headerLine = lines[0];
    const dataLines = lines.slice(1);
    const numFiles = Math.ceil(dataLines.length / ROWS_PER_FILE);

    console.log(`총 ${dataLines.length.toLocaleString()}행을 ${numFiles}개의 Excel 파일로 분할합니다...`);

    // Show download range selection dialog if multiple files
    let selectedFiles = [];
    if (numFiles > 1) {
      const message =
        `총 ${dataLines.length.toLocaleString()}행의 데이터가 ${numFiles}개의 Excel 파일로 분할됩니다.\n` +
        `(각 파일당 10,000행)\n\n` +
        `다운로드할 파일 범위를 입력하세요:\n` +
        `• 전체: 빈칸 또는 "all"\n` +
        `• 범위: "1-40" (1번부터 40번 파일)\n` +
        `• 개별: "1,5,10" (1번, 5번, 10번 파일만)\n` +
        `• 혼합: "1-10,15,20-25" (1~10번, 15번, 20~25번)\n\n` +
        `예) 1-40`;

      const userInput = prompt(message, `1-${numFiles}`);

      // User cancelled
      if (userInput === null) {
        console.log("Excel 다운로드가 취소되었습니다.");
        return;
      }

      // Parse user input
      try {
        selectedFiles = parseFileRange(userInput.trim(), numFiles);

        if (selectedFiles.length === 0) {
          alert("선택된 파일이 없습니다. 다운로드를 취소합니다.");
          return;
        }

        console.log(`선택된 파일: ${selectedFiles.length}개 (${selectedFiles.join(', ')})`);
      } catch (err) {
        alert(`입력 형식 오류: ${err.message}\n\n올바른 형식: "1-40", "1,5,10", "1-10,15,20-25"`);
        return;
      }
    } else {
      // Single file
      selectedFiles = [1];
    }

    // Generate base filename
    const originalFilename = uploadedFileName.replace(/\.(csv|CSV)$/, "") || "data";

    // Excel cell text limit
    const EXCEL_TEXT_LIMIT = 32767;

    // Helper function to truncate long fields in a CSV line
    const truncateCSVLine = (line) => {
      const fields = [];
      let field = '';
      let inQuotes = false;

      for (let i = 0; i < line.length; i++) {
        const char = line[i];

        if (char === '"') {
          if (inQuotes && i + 1 < line.length && line[i + 1] === '"') {
            field += '"';
            i++;
          } else {
            inQuotes = !inQuotes;
          }
        } else if (char === ',' && !inQuotes) {
          // Truncate field if too long
          if (field.length > EXCEL_TEXT_LIMIT) {
            field = field.substring(0, EXCEL_TEXT_LIMIT - 3) + '...';
          }
          fields.push(field);
          field = '';
        } else {
          field += char;
        }
      }

      // Add last field
      if (field.length > EXCEL_TEXT_LIMIT) {
        field = field.substring(0, EXCEL_TEXT_LIMIT - 3) + '...';
      }
      fields.push(field);

      // Reconstruct CSV line with proper quoting
      return fields.map(f => {
        if (f.includes(',') || f.includes('"') || f.includes('\n')) {
          return `"${f.replace(/"/g, '""')}"`;
        }
        return f;
      }).join(',');
    };

    // Process only selected files
    for (let i = 0; i < selectedFiles.length; i++) {
      const fileNum = selectedFiles[i] - 1; // Convert to 0-indexed
      const startRow = fileNum * ROWS_PER_FILE;
      const endRow = Math.min(startRow + ROWS_PER_FILE, dataLines.length);
      const fileLines = dataLines.slice(startRow, endRow);

      console.log(`파일 ${fileNum + 1}/${numFiles} 생성 중... (${i + 1}/${selectedFiles.length} 선택됨) (${startRow + 1} ~ ${endRow} 행)`);

      // Truncate long fields in header and data lines
      const truncatedHeader = truncateCSVLine(headerLine);
      const truncatedLines = fileLines.map(line => truncateCSVLine(line));

      // Build CSV string for this file with header
      let csvString = truncatedHeader + '\n' + truncatedLines.join('\n');

      // Convert CSV string to worksheet using XLSX parser with retry logic
      console.log(`  파일 ${fileNum + 1} CSV 파싱 중...`);
      let ws = null;
      let retryCount = 0;
      const maxRetries = 3;

      while (retryCount < maxRetries && !ws) {
        try {
          const workbook = XLSX.read(csvString, { type: 'string', raw: false });

          // Check if workbook and sheet exist
          if (!workbook || !workbook.Sheets || !workbook.Sheets['Sheet1']) {
            throw new Error('CSV 파싱 실패: 워크시트를 생성할 수 없습니다.');
          }

          ws = workbook.Sheets['Sheet1'];

          // Verify worksheet has data
          if (!ws['!ref']) {
            throw new Error('CSV 파싱 실패: 워크시트가 비어있습니다.');
          }

        } catch (parseError) {
          retryCount++;
          console.warn(`  파일 ${fileNum + 1} 파싱 시도 ${retryCount}/${maxRetries} 실패:`, parseError);

          if (retryCount >= maxRetries) {
            throw new Error(`파일 ${fileNum + 1} 파싱 실패 (${maxRetries}회 재시도): ${parseError.message}`);
          }

          // Wait a bit before retry to allow GC
          await new Promise(resolve => setTimeout(resolve, 100));
        }
      }

      // Set column widths
      const range = XLSX.utils.decode_range(ws['!ref']);
      const numCols = range.e.c + 1;
      ws["!cols"] = Array(numCols).fill({ wch: 15 });

      // Create a new workbook for this file
      const wb = XLSX.utils.book_new();
      XLSX.utils.book_append_sheet(wb, ws, "Data");

      // Clear CSV string to free memory
      csvString = null;

      // Generate filename with part number
      const excelFilename = numFiles > 1
        ? `${originalFilename}_part${String(fileNum + 1).padStart(3, '0')}.xlsx`
        : `${originalFilename}.xlsx`;

      console.log(`  파일 ${fileNum + 1} 쓰기 중... (${excelFilename})`);

      // Write and download the file
      XLSX.writeFile(wb, excelFilename);

      console.log(`  파일 ${fileNum + 1}/${numFiles} 완료: ${excelFilename}`);

      // Allow browser to breathe between files
      if (i < selectedFiles.length - 1) {
        await new Promise(resolve => setTimeout(resolve, 200));
      }
    }

    console.log(`선택된 Excel 파일 다운로드 완료: ${selectedFiles.length}개 파일`);

    if (selectedFiles.length === numFiles) {
      alert(`다운로드 완료!\n총 ${dataLines.length.toLocaleString()}행이 ${numFiles}개의 Excel 파일로 분할되었습니다.\n\n파일명: ${originalFilename}_part001.xlsx ~ part${String(numFiles).padStart(3, '0')}.xlsx`);
    } else {
      const fileList = selectedFiles.length <= 10
        ? selectedFiles.map(n => `part${String(n).padStart(3, '0')}`).join(', ')
        : `part${String(selectedFiles[0]).padStart(3, '0')} ~ part${String(selectedFiles[selectedFiles.length - 1]).padStart(3, '0')} 외 ${selectedFiles.length}개`;

      alert(`다운로드 완료!\n선택한 ${selectedFiles.length}개 파일이 다운로드되었습니다.\n\n파일명: ${originalFilename}_${fileList}.xlsx`);
    }
  } catch (err) {
    console.error("Excel 다운로드 실패:", err);
    alert("Excel 파일 다운로드 중 오류가 발생했습니다: " + err.message);
  }
}

function initializeChartSection() {
  // Check if data exists and is not empty
  if (
    !convertedJsonData ||
    !convertedJsonData.data ||
    convertedJsonData.data.length === 0
  ) {
    return;
  }

  const headers = Object.keys(convertedJsonData.data[0]);
  let numericHeaders = [];

  // Try to get numeric headers from metadata first
  if (convertedJsonData.metadata && convertedJsonData.metadata.columns) {
    numericHeaders = convertedJsonData.metadata.columns
      .filter((col) => col.type === "integer" || col.type === "float")
      .map((col) => col.name);
  } else {
    // Fallback: Infer numeric headers from the first data row
    console.warn("Metadata not found. Inferring numeric columns from data.");
    const firstRow = convertedJsonData.data[0];
    numericHeaders = headers.filter(
      (h) => !isNaN(parseFloat(firstRow[h])) && isFinite(firstRow[h])
    );
  }

  // Populate select options
  chartLabelColSelect.innerHTML = headers
    .map((h) => `<option value="${h}">${h}</option>`)
    .join("");
  chartDataColSelect.innerHTML = numericHeaders
    .map((h) => `<option value="${h}">${h}</option>`)
    .join("");

  // Add event listeners to update chart
  chartTypeSelect.addEventListener("change", renderChart);
  chartLabelColSelect.addEventListener("change", renderChart);
  chartDataColSelect.addEventListener("change", renderChart);

  // Initial chart render
  renderChart();
}

const CHART_COLORS = [
  "rgba(54, 162, 235, 0.8)",
  "rgba(255, 99, 132, 0.8)",
  "rgba(75, 192, 192, 0.8)",
  "rgba(255, 206, 86, 0.8)",
  "rgba(153, 102, 255, 0.8)",
  "rgba(255, 159, 64, 0.8)",
  "rgba(199, 199, 199, 0.8)",
];

const CHART_BORDER_COLORS = CHART_COLORS.map((color) =>
  color.replace("0.8", "1")
);

function renderChart() {
  if (!convertedJsonData || !chartCanvas) return;

  const type = chartTypeSelect.value;
  const labelCol = chartLabelColSelect.value;
  const dataCol = chartDataColSelect.value;

  if (!labelCol || !dataCol) return;

  const labels = convertedJsonData.data.map((row) => row[labelCol]);
  const data = convertedJsonData.data.map((row) => row[dataCol]);
  const styleHeight = chartCanvas.style.height;

  if (myChart) {
    myChart.destroy();
  }

  chartCanvas.style.height = styleHeight;
  // --- Chart.js Dataset Configuration ---
  const datasetOptions = {
    label: dataCol,
    data: data,
    borderWidth: 1.5,
  };

  // Apply different styles based on chart type
  if (type === "bar") {
    datasetOptions.backgroundColor = CHART_COLORS;
    datasetOptions.borderColor = CHART_BORDER_COLORS;
    datasetOptions.borderRadius = 4; // Rounded corners for bars
  } else if (type === "pie") {
    datasetOptions.backgroundColor = CHART_COLORS;
    datasetOptions.borderColor = "#fff";
  } else if (type === "line") {
    datasetOptions.backgroundColor = "rgba(54, 162, 235, 0.2)"; // Fill color under the line
    datasetOptions.borderColor = "rgba(54, 162, 235, 1)";
    datasetOptions.pointBackgroundColor = "rgba(54, 162, 235, 1)";
    datasetOptions.pointBorderColor = "#fff";
    datasetOptions.pointHoverRadius = 7;
    datasetOptions.fill = true; // Enable fill
    datasetOptions.tension = 0.1; // Make the line slightly curved
  }

  const ctx = chartCanvas.getContext("2d");
  myChart = new Chart(ctx, {
    type: type,
    data: {
      labels: labels,
      datasets: [datasetOptions],
    },
    options: {
      responsive: true,
      maintainAspectRatio: false,
      plugins: {
        legend: {
          position: "top",
          labels: {
            font: {
              family: "'Pretendard', sans-serif",
              size: 12,
            },
          },
        },
        title: {
          display: true,
          text: `${labelCol} 별 ${dataCol} 분포`,
          font: {
            family: "'Pretendard', sans-serif",
            size: 16,
            weight: "600",
          },
          padding: { top: 10, bottom: 20 },
        },
        zoom: {
          pan: {
            enabled: true,
            mode: "x",
          },
          zoom: {
            wheel: {
              enabled: true,
            },
            pinch: {
              enabled: true,
            },
            mode: "x",
            drag: {
              enabled: true
            }
          },
        },
      },
      scales: {
        y: {
          grid: {
            color: "#e5e7eb", // Lighter grid lines
          },
        },
      },
    },
  });
}

// Start loading and converting when page loads
loadAndConvertCsv();
