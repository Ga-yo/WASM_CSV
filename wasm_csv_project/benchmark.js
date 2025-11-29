// ============================================================================
// Pure JavaScript CSV to JSON Converter (for benchmarking)
// ============================================================================

// Declare Module as global variable for WASM
var Module = Module || {};

class PureJSCSVConverter {
  constructor() {
    this.csvData = null;
    this.fileName = '';
  }

  // Parse CSV with quoted fields support
  parseCSVLine(line) {
    const result = [];
    let current = '';
    let inQuotes = false;

    for (let i = 0; i < line.length; i++) {
      const char = line[i];
      const nextChar = line[i + 1];

      if (char === '"') {
        if (inQuotes && nextChar === '"') {
          current += '"';
          i++;
        } else {
          inQuotes = !inQuotes;
        }
      } else if (char === ',' && !inQuotes) {
        result.push(current.trim());
        current = '';
      } else {
        current += char;
      }
    }

    result.push(current.trim());
    return result;
  }

  // Detect data type
  detectType(values) {
    const sample = values.filter(v => v && v.trim() !== '').slice(0, 100);
    if (sample.length === 0) return 'string';

    let intCount = 0;
    let floatCount = 0;
    let boolCount = 0;

    for (const val of sample) {
      const trimmed = val.trim();

      // Boolean check
      if (trimmed === 'true' || trimmed === 'false' ||
          trimmed === 'TRUE' || trimmed === 'FALSE') {
        boolCount++;
        continue;
      }

      // Number check
      const num = Number(trimmed);
      if (!isNaN(num) && trimmed !== '') {
        if (Number.isInteger(num)) {
          intCount++;
        } else {
          floatCount++;
        }
      }
    }

    const total = sample.length;
    if (intCount / total > 0.8) return 'integer';
    if ((intCount + floatCount) / total > 0.8) return 'float';
    if (boolCount / total > 0.8) return 'boolean';
    return 'string';
  }

  // Calculate statistics
  calculateStats(values, type) {
    const stats = {
      count: values.length,
      unique: new Set(values.filter(v => v && v.trim() !== '')).size,
      nullCount: values.filter(v => !v || v.trim() === '').length,
    };

    if (type === 'integer' || type === 'float') {
      const numbers = values
        .map(v => Number(v))
        .filter(n => !isNaN(n));

      if (numbers.length > 0) {
        stats.min = Math.min(...numbers);
        stats.max = Math.max(...numbers);
        stats.avg = numbers.reduce((a, b) => a + b, 0) / numbers.length;
      }
    }

    return stats;
  }

  // Convert value based on type
  convertValue(value, type) {
    if (!value || value.trim() === '') return null;

    const trimmed = value.trim();

    switch (type) {
      case 'integer':
        const int = parseInt(trimmed);
        return isNaN(int) ? trimmed : int;
      case 'float':
        const float = parseFloat(trimmed);
        return isNaN(float) ? trimmed : float;
      case 'boolean':
        if (trimmed === 'true' || trimmed === 'TRUE') return true;
        if (trimmed === 'false' || trimmed === 'FALSE') return false;
        return trimmed;
      default:
        return trimmed;
    }
  }

  // Main conversion function
  convertToJson(csvText, filename) {
    const startTime = performance.now();

    // Normalize line endings and split
    const normalizedText = csvText.replace(/\r\n/g, '\n').replace(/\r/g, '\n');
    const lines = normalizedText.split('\n').filter(line => line.trim() !== '');

    if (lines.length === 0) {
      return {
        error: 'Empty CSV file',
        metadata: { filename, processingTime: 0 }
      };
    }

    // Parse header
    const headers = this.parseCSVLine(lines[0]);
    const numColumns = headers.length;
    const numRows = lines.length - 1;

    // Parse all rows
    const rows = [];
    for (let i = 1; i < lines.length; i++) {
      const row = this.parseCSVLine(lines[i]);
      // Normalize row length
      while (row.length < numColumns) row.push('');
      if (row.length > numColumns) row.length = numColumns;
      rows.push(row);
    }

    // Detect column types
    const columnData = Array(numColumns).fill(null).map(() => []);
    for (const row of rows) {
      for (let i = 0; i < numColumns; i++) {
        columnData[i].push(row[i]);
      }
    }

    const columnTypes = columnData.map(data => this.detectType(data));
    const columnStats = columnData.map((data, i) =>
      this.calculateStats(data, columnTypes[i])
    );

    // Build JSON structure
    const jsonData = rows.map(row => {
      const obj = {};
      for (let i = 0; i < numColumns; i++) {
        const header = headers[i];
        const value = this.convertValue(row[i], columnTypes[i]);
        obj[header] = value;
      }
      return obj;
    });

    const endTime = performance.now();
    const processingTime = (endTime - startTime) / 1000; // seconds

    // Build result
    const result = {
      metadata: {
        filename: filename,
        totalRows: numRows,
        totalColumns: numColumns,
        processingTime: processingTime,
        columns: headers.map((header, i) => ({
          name: header,
          type: columnTypes[i],
          stats: {
            count: columnStats[i].count,
            unique: columnStats[i].unique,
            nullCount: columnStats[i].nullCount,
            min: columnStats[i].min,
            max: columnStats[i].max,
            avg: columnStats[i].avg
          }
        }))
      },
      data: jsonData
    };

    return result;
  }
}

// ============================================================================
// Benchmark UI Controller
// ============================================================================

class BenchmarkController {
  constructor() {
    this.fileInput = document.getElementById('file-input');
    this.fileInfo = document.getElementById('file-info');
    this.btnTestWasm = document.getElementById('btn-test-wasm');
    this.btnTestJs = document.getElementById('btn-test-js');
    this.btnTestBoth = document.getElementById('btn-test-both');
    this.btnClear = document.getElementById('btn-clear');
    this.logContainer = document.getElementById('log-container');
    this.progressSection = document.getElementById('progress-section');
    this.resultsSection = document.getElementById('results-section');

    this.selectedFile = null;
    this.wasmResult = null;
    this.jsResult = null;

    this.jsConverter = new PureJSCSVConverter();

    this.setupEventListeners();
  }

  setupEventListeners() {
    this.fileInput.addEventListener('change', (e) => this.handleFileSelect(e));
    this.btnTestWasm.addEventListener('click', () => this.runWasmTest());
    this.btnTestJs.addEventListener('click', () => this.runJsTest());
    this.btnTestBoth.addEventListener('click', () => this.runBothTests());
    this.btnClear.addEventListener('click', () => this.clearResults());
  }

  handleFileSelect(event) {
    const file = event.target.files[0];
    if (!file) return;

    this.selectedFile = file;
    const sizeMB = (file.size / 1024 / 1024).toFixed(2);
    this.fileInfo.textContent = `${file.name} (${sizeMB} MB)`;
    this.log(`파일 선택됨: ${file.name} (${sizeMB} MB)`, 'info');
  }

  log(message, type = 'info') {
    const entry = document.createElement('div');
    entry.className = `log-entry ${type}`;
    const timestamp = new Date().toLocaleTimeString();
    entry.textContent = `[${timestamp}] ${message}`;
    this.logContainer.appendChild(entry);
    this.logContainer.scrollTop = this.logContainer.scrollHeight;
  }

  showProgress(title, percentage, status) {
    this.progressSection.style.display = 'block';
    document.getElementById('progress-title').textContent = title;
    document.getElementById('progress-percentage').textContent = `${percentage}%`;
    document.getElementById('progress-fill').style.width = `${percentage}%`;
    document.getElementById('progress-status').textContent = status;
  }

  hideProgress() {
    this.progressSection.style.display = 'none';
  }

  async readFileAsText(file) {
    return new Promise((resolve, reject) => {
      const reader = new FileReader();
      reader.onprogress = (e) => {
        if (e.lengthComputable) {
          const percentage = Math.round((e.loaded / e.total) * 100);
          this.showProgress('파일 읽는 중...', percentage, `${(e.loaded / 1024 / 1024).toFixed(2)} MB / ${(e.total / 1024 / 1024).toFixed(2)} MB`);
        }
      };
      reader.onload = (e) => resolve(e.target.result);
      reader.onerror = (e) => reject(e);
      reader.readAsText(file, 'UTF-8');
    });
  }

  async runWasmTest() {
    if (!this.selectedFile) {
      alert('CSV 파일을 먼저 선택해주세요.');
      return;
    }

    this.log('WASM 테스트 시작...', 'info');
    this.btnTestWasm.classList.add('disabled');

    try {
      // Read file
      const text = await this.readFileAsText(this.selectedFile);
      this.log(`파일 읽기 완료 (${(text.length / 1024 / 1024).toFixed(2)} MB)`, 'success');

      // Wait for WASM module
      this.log('WASM 모듈 대기 중...', 'info');

      if (typeof Module === 'undefined' || !Module.convertToJsonAuto) {
        this.log('WASM 모듈 초기화 중...', 'info');
        await new Promise((resolve, reject) => {
          const timeout = setTimeout(() => {
            reject(new Error('WASM 모듈 로드 타임아웃 (10초)'));
          }, 10000);

          // Save original callback if it exists
          const originalCallback = Module && Module.onRuntimeInitialized;

          const initCallback = () => {
            clearTimeout(timeout);
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
      }

      this.log('WASM 모듈 준비 완료', 'success');

      this.showProgress('WASM 변환 중...', 50, '처리 중...');

      // Run conversion - try optimized version first
      let jsonString;
      if (Module.convertToJsonOptimized) {
        this.log('Using OPTIMIZED WASM converter', 'success');
      } else if (Module.convertToJsonAuto) {
        this.log('Using standard WASM converter', 'info');
      } else {
        throw new Error('WASM module not ready');
      }

      const startTime = performance.now();
      jsonString = Module.convertToJsonOptimized ?
        Module.convertToJsonOptimized(text, this.selectedFile.name) :
        Module.convertToJsonAuto(text, this.selectedFile.name);
      const endTime = performance.now();
      const result = JSON.parse(jsonString);

      const duration = (endTime - startTime) / 1000;
      this.wasmResult = {
        duration: duration,
        fileSize: this.selectedFile.size,
        result: result
      };

      this.log(`WASM 변환 완료: ${duration.toFixed(3)}초`, 'success');
      this.displayWasmResult();
      this.hideProgress();

    } catch (err) {
      this.log(`WASM 테스트 실패: ${err.message}`, 'error');
      this.hideProgress();
    } finally {
      this.btnTestWasm.classList.remove('disabled');
    }
  }

  async runJsTest() {
    if (!this.selectedFile) {
      alert('CSV 파일을 먼저 선택해주세요.');
      return;
    }

    this.log('JavaScript 테스트 시작...', 'info');
    this.btnTestJs.classList.add('disabled');

    try {
      // Read file
      const text = await this.readFileAsText(this.selectedFile);
      this.log(`파일 읽기 완료 (${(text.length / 1024 / 1024).toFixed(2)} MB)`, 'success');

      this.showProgress('JavaScript 변환 중...', 50, '처리 중...');

      // Run conversion (use setTimeout to allow UI update)
      await new Promise(resolve => setTimeout(resolve, 100));

      const startTime = performance.now();
      const result = this.jsConverter.convertToJson(text, this.selectedFile.name);
      const endTime = performance.now();

      const duration = (endTime - startTime) / 1000;
      this.jsResult = {
        duration: duration,
        fileSize: this.selectedFile.size,
        result: result
      };

      this.log(`JavaScript 변환 완료: ${duration.toFixed(3)}초`, 'success');
      this.displayJsResult();
      this.hideProgress();

    } catch (err) {
      this.log(`JavaScript 테스트 실패: ${err.message}`, 'error');
      this.hideProgress();
    } finally {
      this.btnTestJs.classList.remove('disabled');
    }
  }

  async runBothTests() {
    this.log('='.repeat(50), 'info');
    this.log('비교 테스트 시작', 'info');
    await this.runWasmTest();
    await new Promise(resolve => setTimeout(resolve, 500));
    await this.runJsTest();
    this.displayComparison();
    this.log('='.repeat(50), 'info');
  }

  displayWasmResult() {
    const result = this.wasmResult;
    document.getElementById('wasm-result').style.display = 'block';
    document.getElementById('wasm-time').textContent = `${result.duration.toFixed(3)}초`;
    document.getElementById('wasm-file-size').textContent = `${(result.fileSize / 1024 / 1024).toFixed(2)} MB`;
    document.getElementById('wasm-throughput').textContent = `${(result.fileSize / 1024 / 1024 / result.duration).toFixed(2)} MB/s`;
    document.getElementById('wasm-rows').textContent = (result.result.metadata?.totalRows || 0).toLocaleString();
    document.getElementById('wasm-cols').textContent = result.result.metadata?.totalColumns || 0;
    this.resultsSection.style.display = 'block';
  }

  displayJsResult() {
    const result = this.jsResult;
    document.getElementById('js-result').style.display = 'block';
    document.getElementById('js-time').textContent = `${result.duration.toFixed(3)}초`;
    document.getElementById('js-file-size').textContent = `${(result.fileSize / 1024 / 1024).toFixed(2)} MB`;
    document.getElementById('js-throughput').textContent = `${(result.fileSize / 1024 / 1024 / result.duration).toFixed(2)} MB/s`;
    document.getElementById('js-rows').textContent = (result.result.metadata?.totalRows || 0).toLocaleString();
    document.getElementById('js-cols').textContent = result.result.metadata?.totalColumns || 0;
    this.resultsSection.style.display = 'block';
  }

  displayComparison() {
    if (!this.wasmResult || !this.jsResult) return;

    const speedup = (this.jsResult.duration / this.wasmResult.duration).toFixed(2);
    const wasmHeight = 100;
    const jsHeight = (this.jsResult.duration / this.wasmResult.duration) * 100;

    document.getElementById('comparison-result').style.display = 'block';
    document.getElementById('speedup-badge').textContent = `WASM이 ${speedup}x 더 빠름`;

    const chartHtml = `
      <div class="chart-bar">
        <div class="chart-bar-fill wasm" style="height: ${wasmHeight}px;">
          ${this.wasmResult.duration.toFixed(3)}초
        </div>
        <div class="chart-bar-label">WASM</div>
      </div>
      <div class="chart-bar">
        <div class="chart-bar-fill js" style="height: ${jsHeight}px;">
          ${this.jsResult.duration.toFixed(3)}초
        </div>
        <div class="chart-bar-label">JavaScript</div>
      </div>
    `;

    document.getElementById('comparison-chart').innerHTML = chartHtml;

    this.log(`성능 비교: WASM이 JavaScript보다 ${speedup}x 빠름`, 'success');
  }

  clearResults() {
    this.wasmResult = null;
    this.jsResult = null;
    document.getElementById('wasm-result').style.display = 'none';
    document.getElementById('js-result').style.display = 'none';
    document.getElementById('comparison-result').style.display = 'none';
    this.resultsSection.style.display = 'none';
    this.logContainer.innerHTML = '<div class="log-entry info">결과 초기화됨</div>';
    this.log('새로운 테스트를 시작할 수 있습니다.', 'info');
  }
}

// Initialize when DOM is ready
document.addEventListener('DOMContentLoaded', () => {
  const controller = new BenchmarkController();

  // Check WASM module status on page load
  const checkWasmModule = () => {
    if (typeof Module !== 'undefined' && Module.convertToJsonAuto) {
      controller.log('✓ WASM 모듈 로드 완료', 'success');
      if (Module.convertToJsonOptimized) {
        controller.log('✓ 최적화 함수 사용 가능', 'success');
      }
    } else {
      controller.log('⏳ WASM 모듈 로딩 중...', 'info');
      if (typeof Module === 'undefined') {
        controller.log('⚠️ Module 객체 없음 - csv_converter.js 로드 확인 필요', 'error');
      }
    }
  };

  // Check immediately
  checkWasmModule();

  // Check again after module initialization
  // Save original callback if it exists
  const originalCallback = Module && Module.onRuntimeInitialized;

  const initCallback = () => {
    if (originalCallback && typeof originalCallback === 'function') {
      originalCallback();
    }
    checkWasmModule();
  };

  if (typeof Module !== 'undefined') {
    Module.onRuntimeInitialized = initCallback;
  } else {
    window.Module = {
      onRuntimeInitialized: initCallback
    };
  }
});
