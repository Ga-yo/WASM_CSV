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
let currentSortColumn = null;
let currentSortDirection = null; // 'asc' or 'desc'
let originalDataOrder = null; // Store original order for reset
let activeFilter = null; // 'max', 'min', 'avg', or 'stats'

// Chart related variables
let myChart = null;
const chartTypeSelect = document.getElementById('chart-type');
const chartLabelColSelect = document.getElementById('chart-label-col');
const chartDataColSelect = document.getElementById('chart-data-col');
const chartCanvas = document.getElementById('myChart');


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

// Function to update the uploaded file UI
function updateFileInfoUI(file) {
  const fileNameEl = document.getElementById('uploaded-file-name');
  const fileSizeEl = document.getElementById('uploaded-file-size');

  if (fileNameEl && fileSizeEl && file) {
    fileNameEl.textContent = file.name;
    fileSizeEl.textContent = `${(file.size / 1024).toFixed(2)} KB`;
    uploadedFileName = file.name; // Store filename
  }
}

// IndexedDB helper to delete a file
function deleteFile(key) {
  return openDb().then(({ db, storeName }) => new Promise((resolve, reject) => {
    const tx = db.transaction(storeName, 'readwrite');
    const store = tx.objectStore(storeName);
    const req = store.delete(key);
    req.onsuccess = () => {
      sessionStorage.removeItem('uploadedFileName');
      resolve();
    };
    req.onerror = (e) => reject(e.target.error);
  }));
}

// Event listener for delete button
const btnDeleteFile = document.getElementById('btn-delete-file');
if (btnDeleteFile) {
  btnDeleteFile.addEventListener('click', async () => {
    if (confirm('파일을 삭제하시겠습니까? 분석 페이지에서 파일이 제거됩니다.')) {
      try {
        await deleteFile('uploaded-file');
        alert('파일이 삭제되었습니다.');
        window.location.href = 'upload.html'; // Redirect to upload page
      } catch (err) {
        console.error('파일 삭제 실패:', err);
        alert('파일 삭제 중 오류가 발생했습니다.');
      }
    }
  });
}

// Load and convert CSV file from IndexedDB
async function loadAndConvertCsv() {
  try {
    const file = await getFile('uploaded-file');
    if (!file) {
      throw new Error('업로드된 파일을 찾을 수 없습니다.');
    }

    // Update UI with file info
    updateFileInfoUI(file);

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
            if (text.includes('') === false || text.length > 0) {
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
    if (typeof Module === 'undefined' || !Module.convertToJsonAuto) {
      console.log('Waiting for WASM module to initialize...');

      await new Promise((resolve, reject) => {
        const timeout = setTimeout(() => {
          reject(new Error('WASM 모듈 로딩 시간 초과 (10초)'));
        }, 10000);

        // Save original callback if it exists
        const originalCallback = Module && Module.onRuntimeInitialized;

        const initCallback = () => {
          clearTimeout(timeout);
          console.log('WASM module ready');
          if (originalCallback && typeof originalCallback === 'function') {
            originalCallback();
          }
          resolve();
        };

        if (typeof Module === 'undefined') {
          window.Module = {
            onRuntimeInitialized: initCallback
          };
        } else {
          Module.onRuntimeInitialized = initCallback;
        }
      });
    } else {
      console.log('WASM module already loaded');
    }

    // Convert CSV to JSON using WASM
    const jsonString = Module.convertToJsonAuto(text, uploadedFileName);
    convertedJsonData = JSON.parse(jsonString);

    // Render JSON
    renderJson(convertedJsonData);
    window.convertedData = convertedJsonData; // expose for debugging

    // Initialize chart section
    initializeChartSection();

    // Initialize stats section
    initializeStatsSection();

  } catch (err) {
    console.error('CSV 변환 실패:', err);
    if (beautifiedEl) {
      beautifiedEl.textContent = 'CSV를 JSON으로 변환할 수 없습니다: ' + err.message;
    }
    // If file is not found, redirect to upload page
    if (err.message.includes('찾을 수 없습니다')) {
      alert('분석할 파일이 없습니다. 먼저 파일을 업로드해주세요.');
      window.location.href = 'upload.html';
    }
  }
}

function initializeStatsSection() {
  // Check if data exists
  if (!convertedJsonData || !convertedJsonData.data || convertedJsonData.data.length === 0) {
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
  const btnDownloadExcel = document.getElementById('btn-download-excel');
  if (btnDownloadExcel) {
    btnDownloadExcel.addEventListener('click', downloadAsExcel);
  }

  // Add event listener for reset button
  const btnResetTable = document.getElementById('btn-reset-table');
  if (btnResetTable) {
    btnResetTable.addEventListener('click', resetTableSort);
  }

  // Add event listeners for filter buttons
  const btnFilterMax = document.getElementById('btn-filter-max');
  const btnFilterMin = document.getElementById('btn-filter-min');
  const btnFilterAvg = document.getElementById('btn-filter-avg');
  const btnFilterStats = document.getElementById('btn-filter-stats');

  if (btnFilterMax) {
    btnFilterMax.addEventListener('click', () => toggleFilter('max'));
  }
  if (btnFilterMin) {
    btnFilterMin.addEventListener('click', () => toggleFilter('min'));
  }
  if (btnFilterAvg) {
    btnFilterAvg.addEventListener('click', () => toggleFilter('avg'));
  }
  if (btnFilterStats) {
    btnFilterStats.addEventListener('click', () => toggleFilter('stats'));
  }

  // Add event listener for close stats result
  const btnCloseStats = document.getElementById('btn-close-stats');
  if (btnCloseStats) {
    btnCloseStats.addEventListener('click', hideStatsResult);
  }
}

function renderDataTable() {
  const tableHead = document.getElementById('stats-table-head');
  const tableBody = document.getElementById('stats-table-body');
  const totalRowsEl = document.getElementById('total-rows');

  if (!tableHead || !tableBody) return;

  tableHead.innerHTML = '';
  tableBody.innerHTML = '';

  const data = convertedJsonData.data;
  if (!data || data.length === 0) return;

  // Update total rows count
  if (totalRowsEl) {
    totalRowsEl.textContent = data.length.toLocaleString();
  }

  // Get column names from first data row
  const columns = Object.keys(data[0]);

  // Create table header (가로줄 = 컬럼명)
  const headerRow = document.createElement('tr');
  headerRow.className = 'border-b-2 border-neutral-300';

  columns.forEach(colName => {
    const th = document.createElement('th');
    th.className = 'p-2 text-left font-semibold text-neutral-900 bg-neutral-50 border border-neutral-200 cursor-pointer hover:bg-neutral-100';
    th.style.userSelect = 'none';

    // Create header content with sort icon
    const headerContent = document.createElement('div');
    headerContent.className = 'flex items-center gap-2';

    const headerText = document.createElement('span');
    headerText.textContent = colName;

    const sortIcon = document.createElement('i');
    sortIcon.className = 'fa-solid text-neutral-400';

    // Set sort icon based on current sort state
    if (currentSortColumn === colName) {
      if (currentSortDirection === 'asc') {
        sortIcon.className = 'fa-solid fa-sort-up text-blue-600';
      } else if (currentSortDirection === 'desc') {
        sortIcon.className = 'fa-solid fa-sort-down text-blue-600';
      }
    } else {
      sortIcon.className = 'fa-solid fa-sort text-neutral-400';
    }

    headerContent.appendChild(headerText);
    headerContent.appendChild(sortIcon);
    th.appendChild(headerContent);

    // Add click event for sorting or filter application
    th.addEventListener('click', () => {
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
    const row = document.createElement('tr');
    row.className = 'border-b border-neutral-100 hover:bg-neutral-50';

    columns.forEach(colName => {
      const td = document.createElement('td');
      td.className = 'p-2 text-neutral-700 border border-neutral-200';

      const value = data[i][colName];
      // Handle null, undefined, or empty values
      td.textContent = (value === null || value === undefined || value === '') ? '-' : value;

      row.appendChild(td);
    });

    tableBody.appendChild(row);
  }

  // Show message if data is truncated
  if (data.length > displayLimit) {
    const messageRow = document.createElement('tr');
    const messageCell = document.createElement('td');
    messageCell.colSpan = columns.length;
    messageCell.className = 'p-4 text-center text-neutral-500 italic border border-neutral-200';
    messageCell.textContent = `처음 ${displayLimit}개 행만 표시됩니다. (전체: ${data.length.toLocaleString()}개 행)`;
    messageRow.appendChild(messageCell);
    tableBody.appendChild(messageRow);
  }
}

function sortTableByColumn(columnName) {
  if (!convertedJsonData || !convertedJsonData.data || convertedJsonData.data.length === 0) {
    return;
  }

  // Determine sort direction
  if (currentSortColumn === columnName) {
    // Toggle between asc -> desc -> no sort (reset)
    if (currentSortDirection === 'asc') {
      currentSortDirection = 'desc';
    } else if (currentSortDirection === 'desc') {
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
    currentSortDirection = 'asc';
  }

  // Sort the data
  convertedJsonData.data.sort((a, b) => {
    let valA = a[columnName];
    let valB = b[columnName];

    // Handle null/undefined/empty values
    if (valA === null || valA === undefined || valA === '') valA = '';
    if (valB === null || valB === undefined || valB === '') valB = '';

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

    return currentSortDirection === 'asc' ? comparison : -comparison;
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
  const btnFilterMax = document.getElementById('btn-filter-max');
  const btnFilterMin = document.getElementById('btn-filter-min');
  const btnFilterAvg = document.getElementById('btn-filter-avg');
  const btnFilterStats = document.getElementById('btn-filter-stats');

  if (btnFilterMax) {
    btnFilterMax.classList.toggle('active', activeFilter === 'max');
  }
  if (btnFilterMin) {
    btnFilterMin.classList.toggle('active', activeFilter === 'min');
  }
  if (btnFilterAvg) {
    btnFilterAvg.classList.toggle('active', activeFilter === 'avg');
  }
  if (btnFilterStats) {
    btnFilterStats.classList.toggle('active', activeFilter === 'stats');
  }
}

function showFilterInstruction() {
  const instruction = document.getElementById('filter-instruction');
  if (instruction) {
    instruction.classList.remove('hidden');
  }
}

function hideFilterInstruction() {
  const instruction = document.getElementById('filter-instruction');
  if (instruction) {
    instruction.classList.add('hidden');
  }
}

function hideStatsResult() {
  const statsResult = document.getElementById('stats-result');
  if (statsResult) {
    statsResult.classList.add('hidden');
  }
}

function showStatsResult(text) {
  const statsResult = document.getElementById('stats-result');
  const statsResultText = document.getElementById('stats-result-text');

  if (statsResult && statsResultText) {
    statsResultText.textContent = text;
    statsResult.classList.remove('hidden');
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
    if (value !== null && value !== undefined && value !== '') {
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
    alert('숫자 타입 데이터만 가능합니다');
    return;
  }

  const data = convertedJsonData.data;
  const values = data.map(row => {
    const val = row[columnName];
    return val !== null && val !== undefined && val !== '' ? parseFloat(val) : null;
  }).filter(v => v !== null && !isNaN(v) && isFinite(v));

  if (values.length === 0) {
    alert('유효한 숫자 데이터가 없습니다');
    return;
  }

  let resultText = '';

  if (activeFilter === 'max') {
    const maxValue = Math.max(...values);
    resultText = `"${columnName}" 열의 최대값: ${maxValue.toLocaleString()}`;
  } else if (activeFilter === 'min') {
    const minValue = Math.min(...values);
    resultText = `"${columnName}" 열의 최소값: ${minValue.toLocaleString()}`;
  } else if (activeFilter === 'avg') {
    const sum = values.reduce((a, b) => a + b, 0);
    const avg = sum / values.length;
    resultText = `"${columnName}" 열의 평균: ${avg.toFixed(2)}`;
  } else if (activeFilter === 'stats') {
    const maxValue = Math.max(...values);
    const minValue = Math.min(...values);
    const sum = values.reduce((a, b) => a + b, 0);
    const avg = sum / values.length;
    const count = values.length;

    resultText = `"${columnName}" 열의 통계 - 개수: ${count.toLocaleString()}, 최소값: ${minValue.toLocaleString()}, 최대값: ${maxValue.toLocaleString()}, 평균: ${avg.toFixed(2)}`;
  }

  showStatsResult(resultText);
}

function downloadAsExcel() {
  if (!convertedJsonData || !convertedJsonData.data || convertedJsonData.data.length === 0) {
    alert('다운로드할 데이터가 없습니다.');
    return;
  }

  try {
    // Check if XLSX library is loaded
    if (typeof XLSX === 'undefined') {
      alert('Excel 라이브러리를 로드하는 중입니다. 잠시 후 다시 시도해주세요.');
      return;
    }

    // Create a new workbook
    const wb = XLSX.utils.book_new();

    // Convert JSON data to worksheet
    // XLSX.utils.json_to_sheet will automatically use object keys as headers
    const ws = XLSX.utils.json_to_sheet(convertedJsonData.data);

    // Set column widths for better readability
    const columns = Object.keys(convertedJsonData.data[0]);
    const wscols = columns.map(() => ({ wch: 15 })); // 15 characters wide
    ws['!cols'] = wscols;

    // Add worksheet to workbook
    XLSX.utils.book_append_sheet(wb, ws, 'Data');

    // Generate filename from original file name
    const originalFilename = uploadedFileName.replace(/\.(csv|CSV)$/, '') || 'data';
    const excelFilename = `${originalFilename}.xlsx`;

    // Write and download the file
    XLSX.writeFile(wb, excelFilename);

    console.log('Excel 파일 다운로드 완료:', excelFilename);

  } catch (err) {
    console.error('Excel 다운로드 실패:', err);
    alert('Excel 파일 다운로드 중 오류가 발생했습니다: ' + err.message);
  }
}

function initializeChartSection() {
  // Check if data exists and is not empty
  if (!convertedJsonData || !convertedJsonData.data || convertedJsonData.data.length === 0) {
    return;
  }

  const headers = Object.keys(convertedJsonData.data[0]);
  let numericHeaders = [];

  // Try to get numeric headers from meta info first
  if (convertedJsonData.meta && convertedJsonData.meta.fields) {
    numericHeaders = convertedJsonData.meta.fields
      .filter(field => field.type === 'number')
      .map(field => field.name);
  } else {
    // Fallback: Infer numeric headers from the first data row
    console.warn('Meta info not found. Inferring numeric columns from data.');
    const firstRow = convertedJsonData.data[0];
    numericHeaders = headers.filter(h => !isNaN(parseFloat(firstRow[h])) && isFinite(firstRow[h]));
  }

  // Populate select options
  chartLabelColSelect.innerHTML = headers.map(h => `<option value="${h}">${h}</option>`).join('');
  chartDataColSelect.innerHTML = numericHeaders.map(h => `<option value="${h}">${h}</option>`).join('');

  // Add event listeners to update chart
  chartTypeSelect.addEventListener('change', renderChart);
  chartLabelColSelect.addEventListener('change', renderChart);
  chartDataColSelect.addEventListener('change', renderChart);

  // Initial chart render
  renderChart();
}

const CHART_COLORS = [
  'rgba(54, 162, 235, 0.8)',
  'rgba(255, 99, 132, 0.8)',
  'rgba(75, 192, 192, 0.8)',
  'rgba(255, 206, 86, 0.8)',
  'rgba(153, 102, 255, 0.8)',
  'rgba(255, 159, 64, 0.8)',
  'rgba(199, 199, 199, 0.8)',
];

const CHART_BORDER_COLORS = CHART_COLORS.map(color => color.replace('0.8', '1'));


function renderChart() {
  if (!convertedJsonData || !chartCanvas) return;

  const type = chartTypeSelect.value;
  const labelCol = chartLabelColSelect.value;
  const dataCol = chartDataColSelect.value;

  if (!labelCol || !dataCol) return;

  const labels = convertedJsonData.data.map(row => row[labelCol]);
  const data = convertedJsonData.data.map(row => row[dataCol]);

  if (myChart) {
    myChart.destroy();
  }

  // --- Chart.js Dataset Configuration ---
  const datasetOptions = {
    label: dataCol,
    data: data,
    borderWidth: 1.5,
  };

  // Apply different styles based on chart type
  if (type === 'bar') {
    datasetOptions.backgroundColor = CHART_COLORS;
    datasetOptions.borderColor = CHART_BORDER_COLORS;
    datasetOptions.borderRadius = 4; // Rounded corners for bars
  } else if (type === 'pie') {
    datasetOptions.backgroundColor = CHART_COLORS;
    datasetOptions.borderColor = '#fff';
  } else if (type === 'line') {
    datasetOptions.backgroundColor = 'rgba(54, 162, 235, 0.2)'; // Fill color under the line
    datasetOptions.borderColor = 'rgba(54, 162, 235, 1)';
    datasetOptions.pointBackgroundColor = 'rgba(54, 162, 235, 1)';
    datasetOptions.pointBorderColor = '#fff';
    datasetOptions.pointHoverRadius = 7;
    datasetOptions.fill = true; // Enable fill
    datasetOptions.tension = 0.1; // Make the line slightly curved
  }

  const ctx = chartCanvas.getContext('2d');
  myChart = new Chart(ctx, {
    type: type,
    data: {
      labels: labels,
      datasets: [datasetOptions]
    },
    options: {
      responsive: true,
      maintainAspectRatio: false,
      plugins: {
        legend: {
          position: 'top',
          labels: {
            font: {
              family: "'Pretendard', sans-serif",
              size: 12,
            }
          }
        },
        title: {
          display: true,
          text: `${labelCol} 별 ${dataCol} 분포`,
          font: {
            family: "'Pretendard', sans-serif",
            size: 16,
            weight: '600'
          },
          padding: { top: 10, bottom: 20 }
        }
      },
      scales: {
        y: {
          grid: {
            color: '#e5e7eb' // Lighter grid lines
          }
        }
      }
    }
  });
}

// Start loading and converting when page loads
loadAndConvertCsv();
