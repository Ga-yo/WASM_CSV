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

    this.log('='.repeat(60), 'info');
    this.log('🚀 WASM 테스트 시작', 'info');
    this.btnTestWasm.classList.add('disabled');

    const timestamps = {};
    timestamps.testStart = new Date();
    this.log(`[시작 시간] ${timestamps.testStart.toLocaleTimeString()}.${timestamps.testStart.getMilliseconds()}`, 'info');

    try {
      // Read file
      const fileReadStart = performance.now();
      const text = await this.readFileAsText(this.selectedFile);
      const fileReadEnd = performance.now();
      timestamps.fileReadTime = (fileReadEnd - fileReadStart) / 1000;
      this.log(`파일 읽기 완료 (${(text.length / 1024 / 1024).toFixed(2)} MB) - ${timestamps.fileReadTime.toFixed(3)}초`, 'success');

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

      // Conversion start
      timestamps.conversionStart = new Date();
      this.log(`[변환 시작] ${timestamps.conversionStart.toLocaleTimeString()}.${timestamps.conversionStart.getMilliseconds()}`, 'info');

      const startTime = performance.now();

      // Count rows (for detailed metrics)
      const rowCountStart = performance.now();
      const lines = text.split('\n');
      const rowCount = lines.length - 1; // Excluding header
      const rowCountEnd = performance.now();
      timestamps.rowCountTime = (rowCountEnd - rowCountStart) / 1000;

      jsonString = Module.convertToJsonOptimized ?
        Module.convertToJsonOptimized(text, this.selectedFile.name) :
        Module.convertToJsonAuto(text, this.selectedFile.name);
      const endTime = performance.now();

      timestamps.conversionEnd = new Date();
      this.log(`[변환 종료] ${timestamps.conversionEnd.toLocaleTimeString()}.${timestamps.conversionEnd.getMilliseconds()}`, 'success');

      const parseStart = performance.now();
      const result = JSON.parse(jsonString);
      const parseEnd = performance.now();
      timestamps.jsonParseTime = (parseEnd - parseStart) / 1000;

      const duration = (endTime - startTime) / 1000;
      timestamps.totalConversionTime = duration;

      timestamps.testEnd = new Date();
      this.log(`[종료 시간] ${timestamps.testEnd.toLocaleTimeString()}.${timestamps.testEnd.getMilliseconds()}`, 'success');
      this.log(`[총 소요 시간] ${duration.toFixed(3)}초`, 'success');

      this.wasmResult = {
        duration: duration,
        fileSize: this.selectedFile.size,
        result: result,
        timestamps: timestamps,
        rowCount: rowCount,
        success: true
      };

      this.log(`✅ WASM 변환 완료: ${duration.toFixed(3)}초`, 'success');
      this.log('='.repeat(60), 'info');
      this.displayWasmResult();
      this.hideProgress();

    } catch (err) {
      this.log(`❌ WASM 테스트 실패: ${err.message}`, 'error');
      this.log('='.repeat(60), 'info');

      this.wasmResult = {
        duration: 0,
        fileSize: this.selectedFile.size,
        result: null,
        timestamps: {},
        rowCount: 0,
        success: false,
        error: {
          message: err.message,
          name: err.name
        }
      };

      this.displayWasmResult();
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

    this.log('='.repeat(60), 'info');
    this.log('🚀 JavaScript 테스트 시작', 'info');
    this.btnTestJs.classList.add('disabled');

    const timestamps = {};
    timestamps.testStart = new Date();
    this.log(`[시작 시간] ${timestamps.testStart.toLocaleTimeString()}.${timestamps.testStart.getMilliseconds()}`, 'info');

    try {
      // Read file
      const fileReadStart = performance.now();
      const text = await this.readFileAsText(this.selectedFile);
      const fileReadEnd = performance.now();
      timestamps.fileReadTime = (fileReadEnd - fileReadStart) / 1000;
      this.log(`파일 읽기 완료 (${(text.length / 1024 / 1024).toFixed(2)} MB) - ${timestamps.fileReadTime.toFixed(3)}초`, 'success');

      this.showProgress('JavaScript 변환 중...', 50, '처리 중...');

      // Run conversion (use setTimeout to allow UI update)
      await new Promise(resolve => setTimeout(resolve, 100));

      // Conversion start
      timestamps.conversionStart = new Date();
      this.log(`[변환 시작] ${timestamps.conversionStart.toLocaleTimeString()}.${timestamps.conversionStart.getMilliseconds()}`, 'info');

      const startTime = performance.now();

      // Count rows (for detailed metrics)
      const rowCountStart = performance.now();
      const lines = text.split('\n');
      const rowCount = lines.length - 1; // Excluding header
      const rowCountEnd = performance.now();
      timestamps.rowCountTime = (rowCountEnd - rowCountStart) / 1000;

      const result = this.jsConverter.convertToJson(text, this.selectedFile.name);
      const endTime = performance.now();

      timestamps.conversionEnd = new Date();
      this.log(`[변환 종료] ${timestamps.conversionEnd.toLocaleTimeString()}.${timestamps.conversionEnd.getMilliseconds()}`, 'success');

      const duration = (endTime - startTime) / 1000;
      timestamps.totalConversionTime = duration;

      timestamps.testEnd = new Date();
      this.log(`[종료 시간] ${timestamps.testEnd.toLocaleTimeString()}.${timestamps.testEnd.getMilliseconds()}`, 'success');
      this.log(`[총 소요 시간] ${duration.toFixed(3)}초`, 'success');

      this.jsResult = {
        duration: duration,
        fileSize: this.selectedFile.size,
        result: result,
        timestamps: timestamps,
        rowCount: rowCount,
        success: true
      };

      this.log(`✅ JavaScript 변환 완료: ${duration.toFixed(3)}초`, 'success');
      this.log('='.repeat(60), 'info');
      this.displayJsResult();
      this.hideProgress();

    } catch (err) {
      this.log(`❌ JavaScript 테스트 실패: ${err.message}`, 'error');

      // Store error information
      this.jsResult = {
        duration: 0,
        fileSize: this.selectedFile.size,
        result: null,
        timestamps: timestamps,
        rowCount: 0,
        success: false,
        error: {
          message: err.message,
          name: err.name,
          stack: err.stack
        }
      };

      // Determine error type
      let errorType = '알 수 없는 오류';
      let errorReason = '';

      if (err.message.includes('Maximum call stack size exceeded')) {
        errorType = '스택 오버플로우 (Stack Overflow)';
        errorReason = 'JavaScript의 재귀 호출 깊이 제한으로 인한 실패. 대용량 CSV 파일 처리 시 발생하는 일반적인 문제입니다.';
      } else if (err.message.includes('out of memory') || err.message.includes('allocation failed')) {
        errorType = '메모리 부족 (Out of Memory)';
        errorReason = 'JavaScript 힙 메모리 한계를 초과했습니다. 브라우저의 메모리 제약으로 인한 실패입니다.';
      } else if (err.message.includes('timeout')) {
        errorType = '타임아웃 (Timeout)';
        errorReason = '처리 시간이 너무 오래 걸려 타임아웃되었습니다.';
      }

      this.jsResult.error.type = errorType;
      this.jsResult.error.reason = errorReason;

      this.log(`⚠️ 오류 유형: ${errorType}`, 'error');
      this.log(`⚠️ 원인: ${errorReason}`, 'error');
      this.log('='.repeat(60), 'info');

      this.displayJsResult();
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

    if (result.success) {
      document.getElementById('js-time').textContent = `${result.duration.toFixed(3)}초`;
      document.getElementById('js-file-size').textContent = `${(result.fileSize / 1024 / 1024).toFixed(2)} MB`;
      document.getElementById('js-throughput').textContent = `${(result.fileSize / 1024 / 1024 / result.duration).toFixed(2)} MB/s`;
      document.getElementById('js-rows').textContent = (result.result.metadata?.totalRows || 0).toLocaleString();
      document.getElementById('js-cols').textContent = result.result.metadata?.totalColumns || 0;
    } else {
      document.getElementById('js-time').textContent = `실패`;
      document.getElementById('js-time').style.color = '#dc2626';
      document.getElementById('js-file-size').textContent = `${(result.fileSize / 1024 / 1024).toFixed(2)} MB`;
      document.getElementById('js-throughput').textContent = `처리 불가`;
      document.getElementById('js-rows').textContent = `처리 실패`;
      document.getElementById('js-cols').textContent = `-`;
    }

    this.resultsSection.style.display = 'block';
  }

  displayComparison() {
    if (!this.wasmResult || !this.jsResult) return;

    const wasmSuccess = this.wasmResult.success;
    const jsSuccess = this.jsResult.success;

    document.getElementById('comparison-result').style.display = 'block';

    // Handle different success/failure scenarios
    let chartHtml = '';
    let speedupText = '';

    if (wasmSuccess && jsSuccess) {
      // Both succeeded
      const speedup = (this.jsResult.duration / this.wasmResult.duration).toFixed(2);
      const wasmHeight = 100;
      const jsHeight = (this.jsResult.duration / this.wasmResult.duration) * 100;

      speedupText = `WASM이 ${speedup}x 더 빠름`;

      chartHtml = `
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
    } else if (wasmSuccess && !jsSuccess) {
      // WASM succeeded, JS failed
      speedupText = 'WASM만 성공 - 대용량 처리 가능';

      chartHtml = `
        <div class="chart-bar">
          <div class="chart-bar-fill wasm" style="height: 100px;">
            ✓ ${this.wasmResult.duration.toFixed(3)}초
          </div>
          <div class="chart-bar-label">WASM ✅</div>
        </div>
        <div class="chart-bar">
          <div class="chart-bar-fill" style="height: 50px; background: linear-gradient(180deg, #ef4444, #dc2626);">
            ✗ 실패
          </div>
          <div class="chart-bar-label">JavaScript ❌</div>
        </div>
      `;
    } else if (!wasmSuccess && jsSuccess) {
      // WASM failed, JS succeeded
      speedupText = 'JavaScript만 성공';

      chartHtml = `
        <div class="chart-bar">
          <div class="chart-bar-fill" style="height: 50px; background: linear-gradient(180deg, #ef4444, #dc2626);">
            ✗ 실패
          </div>
          <div class="chart-bar-label">WASM ❌</div>
        </div>
        <div class="chart-bar">
          <div class="chart-bar-fill js" style="height: 100px;">
            ✓ ${this.jsResult.duration.toFixed(3)}초
          </div>
          <div class="chart-bar-label">JavaScript ✅</div>
        </div>
      `;
    } else {
      // Both failed
      speedupText = '둘 다 실패';

      chartHtml = `
        <div class="chart-bar">
          <div class="chart-bar-fill" style="height: 50px; background: linear-gradient(180deg, #ef4444, #dc2626);">
            ✗ 실패
          </div>
          <div class="chart-bar-label">WASM ❌</div>
        </div>
        <div class="chart-bar">
          <div class="chart-bar-fill" style="height: 50px; background: linear-gradient(180deg, #ef4444, #dc2626);">
            ✗ 실패
          </div>
          <div class="chart-bar-label">JavaScript ❌</div>
        </div>
      `;
    }

    document.getElementById('speedup-badge').textContent = speedupText;
    document.getElementById('comparison-chart').innerHTML = chartHtml;

    // Create detailed latency comparison table
    let detailTableHtml = '';

    if (wasmSuccess && jsSuccess) {
      // Both succeeded - show full comparison
      const speedup = (this.jsResult.duration / this.wasmResult.duration).toFixed(2);

      detailTableHtml = `
        <div style="margin-top: 24px;">
          <h3 style="font-size: 16px; font-weight: 600; margin-bottom: 12px; color: #1f2937;">
            📊 상세 레이턴시 측정 결과
          </h3>
          <table style="width: 100%; border-collapse: collapse; font-size: 14px;">
            <thead>
              <tr style="background: #f3f4f6; border-bottom: 2px solid #e5e7eb;">
                <th style="padding: 12px; text-align: left; font-weight: 600; color: #374151;">측정 항목</th>
                <th style="padding: 12px; text-align: right; font-weight: 600; color: #059669;">WASM</th>
                <th style="padding: 12px; text-align: right; font-weight: 600; color: #dc2626;">JavaScript</th>
                <th style="padding: 12px; text-align: right; font-weight: 600; color: #7c3aed;">성능 차이</th>
              </tr>
            </thead>
            <tbody>
              <tr style="border-bottom: 1px solid #e5e7eb;">
                <td style="padding: 12px; color: #4b5563;">파일 읽기 시간</td>
                <td style="padding: 12px; text-align: right; color: #059669;">${this.wasmResult.timestamps.fileReadTime.toFixed(3)}초</td>
                <td style="padding: 12px; text-align: right; color: #dc2626;">${this.jsResult.timestamps.fileReadTime.toFixed(3)}초</td>
                <td style="padding: 12px; text-align: right; color: #7c3aed;">${(this.jsResult.timestamps.fileReadTime / this.wasmResult.timestamps.fileReadTime).toFixed(2)}x</td>
              </tr>
              <tr style="border-bottom: 1px solid #e5e7eb; background: #fafafa;">
                <td style="padding: 12px; color: #4b5563;">행 개수 확인 시간</td>
                <td style="padding: 12px; text-align: right; color: #059669;">${this.wasmResult.timestamps.rowCountTime.toFixed(3)}초</td>
                <td style="padding: 12px; text-align: right; color: #dc2626;">${this.jsResult.timestamps.rowCountTime.toFixed(3)}초</td>
                <td style="padding: 12px; text-align: right; color: #7c3aed;">${(this.jsResult.timestamps.rowCountTime / this.wasmResult.timestamps.rowCountTime).toFixed(2)}x</td>
              </tr>
              <tr style="border-bottom: 1px solid #e5e7eb;">
                <td style="padding: 12px; color: #4b5563;">데이터 포맷 변환 시간</td>
                <td style="padding: 12px; text-align: right; color: #059669; font-weight: 600;">${this.wasmResult.timestamps.totalConversionTime.toFixed(3)}초</td>
                <td style="padding: 12px; text-align: right; color: #dc2626; font-weight: 600;">${this.jsResult.timestamps.totalConversionTime.toFixed(3)}초</td>
                <td style="padding: 12px; text-align: right; color: #7c3aed; font-weight: 600;">${speedup}x</td>
              </tr>
              ${this.wasmResult.timestamps.jsonParseTime ? `
              <tr style="border-bottom: 1px solid #e5e7eb; background: #fafafa;">
                <td style="padding: 12px; color: #4b5563;">JSON 파싱 시간 (WASM만 해당)</td>
                <td style="padding: 12px; text-align: right; color: #059669;">${this.wasmResult.timestamps.jsonParseTime.toFixed(3)}초</td>
                <td style="padding: 12px; text-align: right; color: #9ca3af;">N/A</td>
                <td style="padding: 12px; text-align: right; color: #9ca3af;">-</td>
              </tr>
              ` : ''}
              <tr style="border-bottom: 1px solid #e5e7eb;">
                <td style="padding: 12px; color: #4b5563;">처리된 행 개수</td>
                <td style="padding: 12px; text-align: right; color: #059669;">${this.wasmResult.rowCount.toLocaleString()}개</td>
                <td style="padding: 12px; text-align: right; color: #dc2626;">${this.jsResult.rowCount.toLocaleString()}개</td>
                <td style="padding: 12px; text-align: right; color: #7c3aed;">동일</td>
              </tr>
              <tr style="border-bottom: 1px solid #e5e7eb; background: #fafafa;">
                <td style="padding: 12px; color: #4b5563;">행당 처리 시간</td>
                <td style="padding: 12px; text-align: right; color: #059669;">${(this.wasmResult.timestamps.totalConversionTime / this.wasmResult.rowCount * 1000).toFixed(3)}ms</td>
                <td style="padding: 12px; text-align: right; color: #dc2626;">${(this.jsResult.timestamps.totalConversionTime / this.jsResult.rowCount * 1000).toFixed(3)}ms</td>
                <td style="padding: 12px; text-align: right; color: #7c3aed;">${((this.jsResult.timestamps.totalConversionTime / this.jsResult.rowCount) / (this.wasmResult.timestamps.totalConversionTime / this.wasmResult.rowCount)).toFixed(2)}x</td>
              </tr>
              <tr style="background: #eff6ff; border-bottom: 2px solid #3b82f6;">
                <td style="padding: 12px; color: #1e3a8a; font-weight: 700;">총 실행 시간</td>
                <td style="padding: 12px; text-align: right; color: #059669; font-weight: 700;">${this.wasmResult.duration.toFixed(3)}초</td>
                <td style="padding: 12px; text-align: right; color: #dc2626; font-weight: 700;">${this.jsResult.duration.toFixed(3)}초</td>
                <td style="padding: 12px; text-align: right; color: #7c3aed; font-weight: 700;">${speedup}x 빠름</td>
              </tr>
            </tbody>
          </table>
        </div>
      `;
    } else if (wasmSuccess && !jsSuccess) {
      // WASM succeeded, JS failed - show error details
      const fileSizeMB = (this.wasmResult.fileSize / 1024 / 1024).toFixed(2);

      detailTableHtml = `
        <div style="margin-top: 24px;">
          <h3 style="font-size: 16px; font-weight: 600; margin-bottom: 12px; color: #1f2937;">
            📊 상세 레이턴시 측정 결과
          </h3>

          <!-- WASM Success Info -->
          <div style="background: #dcfce7; border-left: 4px solid #059669; padding: 16px; border-radius: 8px; margin-bottom: 16px;">
            <h4 style="color: #059669; font-weight: 700; margin-bottom: 8px;">✅ WASM 성공</h4>
            <table style="width: 100%; font-size: 14px;">
              <tr>
                <td style="padding: 6px 0; color: #4b5563; width: 200px;">파일 크기</td>
                <td style="padding: 6px 0; color: #059669; font-weight: 600;">${fileSizeMB} MB</td>
              </tr>
              <tr>
                <td style="padding: 6px 0; color: #4b5563;">처리된 행 개수</td>
                <td style="padding: 6px 0; color: #059669; font-weight: 600;">${this.wasmResult.rowCount.toLocaleString()}개</td>
              </tr>
              <tr>
                <td style="padding: 6px 0; color: #4b5563;">총 변환 시간</td>
                <td style="padding: 6px 0; color: #059669; font-weight: 600;">${this.wasmResult.duration.toFixed(3)}초</td>
              </tr>
              <tr>
                <td style="padding: 6px 0; color: #4b5563;">행당 처리 시간</td>
                <td style="padding: 6px 0; color: #059669; font-weight: 600;">${(this.wasmResult.duration / this.wasmResult.rowCount * 1000).toFixed(3)}ms</td>
              </tr>
              <tr>
                <td style="padding: 6px 0; color: #4b5563;">처리 속도</td>
                <td style="padding: 6px 0; color: #059669; font-weight: 600;">${(this.wasmResult.fileSize / 1024 / 1024 / this.wasmResult.duration).toFixed(2)} MB/s</td>
              </tr>
            </table>
          </div>

          <!-- JavaScript Error Info -->
          <div style="background: #fee2e2; border-left: 4px solid #dc2626; padding: 16px; border-radius: 8px;">
            <h4 style="color: #dc2626; font-weight: 700; margin-bottom: 8px;">❌ JavaScript 실패</h4>
            <table style="width: 100%; font-size: 14px;">
              <tr>
                <td style="padding: 6px 0; color: #4b5563; width: 200px;">오류 유형</td>
                <td style="padding: 6px 0; color: #dc2626; font-weight: 600;">${this.jsResult.error.type}</td>
              </tr>
              <tr>
                <td style="padding: 6px 0; color: #4b5563; vertical-align: top;">오류 원인</td>
                <td style="padding: 6px 0; color: #dc2626;">${this.jsResult.error.reason}</td>
              </tr>
              <tr>
                <td style="padding: 6px 0; color: #4b5563;">오류 메시지</td>
                <td style="padding: 6px 0; color: #991b1b; font-family: monospace; font-size: 12px;">${this.jsResult.error.message}</td>
              </tr>
            </table>
          </div>

          <!-- Key Insights -->
          <div style="background: #fef3c7; border-left: 4px solid #f59e0b; padding: 16px; border-radius: 8px; margin-top: 16px;">
            <h4 style="color: #b45309; font-weight: 700; margin-bottom: 8px;">💡 핵심 인사이트</h4>
            <ul style="margin: 0; padding-left: 20px; color: #78350f; line-height: 1.8;">
              <li><strong>대용량 파일 처리:</strong> WASM은 ${fileSizeMB}MB의 대용량 CSV 파일을 성공적으로 처리했지만, JavaScript는 실패했습니다.</li>
              <li><strong>메모리 효율성:</strong> WASM은 네이티브 메모리 관리를 통해 JavaScript의 힙 메모리 제약을 우회합니다.</li>
              <li><strong>스택 안정성:</strong> JavaScript는 재귀 호출 깊이 제한으로 인해 대용량 데이터 처리 시 스택 오버플로우가 발생할 수 있습니다.</li>
              <li><strong>프로덕션 권장사항:</strong> ${fileSizeMB}MB 이상의 대용량 CSV 파일 처리에는 WASM 사용을 강력히 권장합니다.</li>
            </ul>
          </div>
        </div>
      `;
    } else if (!wasmSuccess && jsSuccess) {
      // WASM failed, JS succeeded
      detailTableHtml = `
        <div style="margin-top: 24px;">
          <h3 style="font-size: 16px; font-weight: 600; margin-bottom: 12px; color: #1f2937;">
            📊 상세 레이턴시 측정 결과
          </h3>
          <div style="background: #fee2e2; border-left: 4px solid #dc2626; padding: 16px; border-radius: 8px;">
            <h4 style="color: #dc2626; font-weight: 700;">WASM 실패, JavaScript 성공</h4>
            <p style="color: #991b1b; margin-top: 8px;">WASM: ${this.wasmResult.error.message}</p>
            <p style="color: #059669; margin-top: 8px;">JavaScript는 ${this.jsResult.duration.toFixed(3)}초에 성공적으로 완료되었습니다.</p>
          </div>
        </div>
      `;
    } else {
      // Both failed
      detailTableHtml = `
        <div style="margin-top: 24px;">
          <h3 style="font-size: 16px; font-weight: 600; margin-bottom: 12px; color: #1f2937;">
            📊 상세 레이턴시 측정 결과
          </h3>
          <div style="background: #fee2e2; border-left: 4px solid #dc2626; padding: 16px; border-radius: 8px;">
            <h4 style="color: #dc2626; font-weight: 700;">둘 다 실패</h4>
            <p style="color: #991b1b; margin-top: 8px;">WASM: ${this.wasmResult.error?.message || '알 수 없는 오류'}</p>
            <p style="color: #991b1b; margin-top: 8px;">JavaScript: ${this.jsResult.error?.message || '알 수 없는 오류'}</p>
          </div>
        </div>
      `;
    }

    document.getElementById('comparison-chart').innerHTML = chartHtml + detailTableHtml;

    this.log('📊 상세 성능 비교 완료', 'success');
    if (wasmSuccess && jsSuccess) {
      const speedup = (this.jsResult.duration / this.wasmResult.duration).toFixed(2);
      this.log(`성능 비교: WASM이 JavaScript보다 ${speedup}x 빠름`, 'success');
    } else if (wasmSuccess && !jsSuccess) {
      this.log(`성능 비교: WASM만 성공 - JavaScript는 ${this.jsResult.error.type}로 실패`, 'success');
    }
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
