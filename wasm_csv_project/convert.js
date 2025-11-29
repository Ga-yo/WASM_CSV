import DBManager from './DBManager.js';

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

// Chart related variables
let myChart = null;
const chartTypeSelect = document.getElementById('chart-type');
const chartLabelColSelect = document.getElementById('chart-label-col');
const chartDataColSelect = document.getElementById('chart-data-col');
const chartCanvas = document.getElementById('myChart');

// Create an instance of the DBManager
const dbManager = new DBManager();


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

// Load and convert CSV file from IndexedDB
async function loadAndConvertCsv() {
  try {
    // Get filename from sessionStorage
    uploadedFileName = sessionStorage.getItem('uploadedFileName') || 'data';

    // Load file from IndexedDB using DBManager
    const file = await dbManager.getFile('uploaded-file');
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

  } catch (err) {
    console.error('CSV 변환 실패:', err);
    if (beautifiedEl) {
      beautifiedEl.textContent = 'CSV를 JSON으로 변환할 수 없습니다: ' + err.message;
    }
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
