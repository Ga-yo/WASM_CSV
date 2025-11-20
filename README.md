# WASM_CSV

프로그래밍2 팀 프로젝트 - WebAssembly 기반 CSV 데이터 분석 및 시각화 플랫폼

## 프로젝트 개요

CSV 파일을 업로드하여 WebAssembly(WASM)를 통해 데이터를 분석하고 시각화하는 웹 애플리케이션입니다.

### 주요 목표
1. **CSV → JSON 변환**: WASM을 활용한 고속 데이터 파싱
2. **데이터 분석**: 평균, 최대/최소값 추출, 정렬 등 통계 기능
3. **데이터 시각화**: Chart.js를 활용한 차트 생성

### 현재 진행 상황
- [x] CSV 파일 업로드 UI
- [x] WASM 기반 CSV to JSON 변환
- [x] 자동 인코딩 감지 (UTF-8, EUC-KR)
- [x] 데이터 타입 자동 추론
- [x] 기본 통계 계산 (평균, 최대/최소, 표준편차)
- [ ] 데이터 정렬 기능
- [ ] 필터링 기능
- [ ] Chart.js 차트 생성
- [ ] 차트 커스터마이징

## 프로젝트 구조

```
WASM_CSV/
├── README.md
├── .gitignore
├── data_sample/                    # 테스트용 CSV 샘플 파일
│   ├── 중간기말성적.csv
│   ├── 출석프로젝트성적.csv
│   ├── 경찰청_보이스피싱 월별 현황.csv
│   └── ...
└── wasm_csv_project/
    ├── index.html                  # 메인 웹 페이지
    ├── csv_converter.cpp           # 표준 버전 C++ 소스
    ├── csv_converter_optimized.cpp # 대용량 최적화 버전 C++ 소스
    ├── build.sh                    # WASM 빌드 스크립트
    ├── json_format_sample.json     # JSON 출력 포맷 샘플
    ├── main.cpp                    # (미사용) 기본 Hello World
    └── 수정된html_files/           # CSS/JS 라이브러리
```

## 기술 스택

- **Frontend**: HTML, TailwindCSS, JavaScript
- **Backend (WASM)**: C++17, Emscripten
- **시각화**: Chart.js (예정)
- **라이브러리**: Pretendard 폰트, Font Awesome

## 핵심 기능

### 1. CSV 파싱 및 JSON 변환 (완료)
- 다양한 구분자 자동 감지 (쉼표, 탭, 세미콜론)
- 데이터 타입 자동 추론 (integer, float, boolean, date, string)
- 통계 정보 계산 (min, max, mean, std_dev, null_count 등)

### 2. 인코딩 자동 감지 (완료)
- UTF-8, EUC-KR, CP949 자동 감지
- 한글 CSV 파일 지원

### 3. 대용량 파일 처리 (완료)
- 스트리밍 방식으로 메모리 효율적 처리
- 10MB 이상 파일은 메타데이터만 추출 옵션

### 4. 데이터 분석 기능 (예정)
- 데이터 정렬 (오름차순/내림차순)
- 필터링 (조건별 데이터 추출)
- 그룹화 및 집계

### 5. 데이터 시각화 (예정)
- Chart.js를 활용한 차트 생성
  - 막대 차트 (Bar Chart)
  - 선 차트 (Line Chart)
  - 파이 차트 (Pie Chart)
  - 산점도 (Scatter Plot)
- 차트 커스터마이징 (색상, 라벨 등)

## 파일별 설명

### `csv_converter.cpp` (표준 버전)

일반적인 크기의 CSV 파일을 처리하는 기본 버전입니다.

**주요 함수:**
- `convertToJson(csvContent, filename)` - CSV를 JSON으로 변환

**특징:**
- 간단한 구조
- 빠른 컴파일
- 10MB 이하 파일에 적합

### `csv_converter_optimized.cpp` (최적화 버전)

대용량 CSV 파일을 위한 최적화 버전입니다.

**주요 함수:**
- `convertToJson(csvContent, filename)` - 전체 데이터 포함 변환
- `convertToJsonMetadataOnly(csvContent, filename)` - 메타데이터/통계만 추출
- `convertToJsonAuto(csvContent, filename)` - 파일 크기에 따라 자동 선택

**최적화 기법:**
- **Welford 알고리즘**: 한 번의 패스로 평균/분산/표준편차 계산
- **룩업 테이블**: 숫자 판별 최적화
- **메모리 예약**: `reserve()`로 재할당 방지
- **샘플링 기반 타입 감지**: 1000행만 샘플링하여 타입 결정
- **유니크 값 추적 제한**: 메모리 절약을 위해 10,000개로 제한

### `index.html`

메인 웹 페이지로 다음 기능을 포함합니다:

**JavaScript 함수:**
- `initializeApp()` - 이벤트 리스너 초기화
- `processFile(file)` - 파일 처리 및 WASM 호출
- `readFileContent(file)` - 파일 읽기 및 인코딩 감지
- `detectEncoding(uint8Array)` - 인코딩 자동 감지 (UTF-8 vs EUC-KR)
- `formatBytes(bytes)` - 파일 크기 포맷팅

**UI 컴포넌트:**
- 드래그앤드롭 업로드 영역
- 결과 모달 (JSON 뷰어)
- 다운로드 버튼
- 로딩 인디케이터

### `json_format_sample.json`

JSON 출력 포맷의 예시입니다. 다음 섹션으로 구성됩니다:

```json
{
  "metadata": { ... },      // 파일 메타데이터
  "schema": { ... },        // 컬럼 스키마 정의
  "statistics": { ... },    // 컬럼별 통계
  "data": [ ... ],          // 실제 데이터
  "errors": [ ... ]         // 파싱 오류
}
```

### `build.sh`

WASM 빌드 스크립트입니다.

**사용법:**
```bash
./build.sh              # 표준 버전 빌드
./build.sh optimized    # 최적화 버전 빌드
./build.sh both         # 양쪽 모두 빌드
```

**빌드 플래그 설명:**
- `-O2` / `-O3`: 최적화 레벨
- `-flto`: 링크 타임 최적화
- `--closure 1`: JavaScript 코드 압축
- `-s ALLOW_MEMORY_GROWTH=1`: 동적 메모리 확장 허용
- `-s MAXIMUM_MEMORY=2GB`: 최대 메모리 제한

## 개발 환경 설정

### 1. Emscripten 설치

```bash
# Emscripten SDK 클론
git clone https://github.com/emscripten-core/emsdk.git
cd emsdk

# 최신 버전 설치 및 활성화
./emsdk install latest
./emsdk activate latest

# 환경 변수 설정 (매 세션마다 필요)
source ./emsdk_env.sh
```

### 2. 프로젝트 빌드

```bash
cd wasm_csv_project
./build.sh optimized
```

### 3. 로컬 서버 실행

```bash
python3 -m http.server 8080
```

브라우저에서 http://localhost:8080/index.html 접속

## 코드 수정 가이드

### 새로운 데이터 타입 추가

1. `DataType` enum에 새 타입 추가
2. `TypeChecker` 클래스에 감지 함수 추가
3. `detectColumnType()`에서 감지 로직 추가
4. `ColumnStats`에 필요한 통계 필드 추가
5. JSON 출력 부분에 새 타입 처리 추가

### 통계 항목 추가

1. `ColumnStats` 구조체에 필드 추가
2. 데이터 처리 루프에서 값 계산
3. `generateJSON()` 또는 `generateMetadataOnlyJSON()`에서 출력

### 인코딩 추가

`detectEncoding()` 함수 (index.html)에서:
1. 새 인코딩의 바이트 패턴 분석 로직 추가
2. 점수 계산 방식 추가
3. 최종 판정 로직 수정

## WASM의 장점

| 항목 | JavaScript | WASM |
|------|------------|------|
| 속도 | 기준 | 10-100배 빠름 |
| 메모리 | GC 의존 | 직접 제어 |
| 타입 | 동적 | 정적 (최적화 용이) |
| 대용량 처리 | 제한적 | 스트리밍 가능 |

## 성능 예상치

| 파일 크기 | JavaScript | WASM (최적화) |
|-----------|------------|---------------|
| 1MB | ~500ms | ~50ms |
| 10MB | ~5s | ~300ms |
| 100MB | 불가능 | ~3s |

## 주의사항

1. **인코딩**: data_sample의 CSV 파일들은 EUC-KR 인코딩입니다. 자동 감지가 되지만, 새 파일 추가 시 UTF-8 권장
2. **메모리**: 매우 큰 파일(1GB+)은 브라우저 메모리 제한에 주의
3. **빌드 파일**: `.wasm`, `csv_converter.js` 등은 `.gitignore`에 포함되어 있으므로 각자 빌드 필요

## 트러블슈팅

### WASM 로드 실패
- 로컬 파일 시스템에서 직접 열면 CORS 오류 발생
- 반드시 HTTP 서버를 통해 접근 (`python3 -m http.server`)

### 한글 깨짐
- 인코딩 감지 실패 시 콘솔에서 `Detected encoding` 확인
- `detectEncoding()` 함수의 점수 임계값 조정 필요할 수 있음

### 빌드 오류
- `emcc` 명령어가 없으면 `source ./emsdk_env.sh` 실행
- C++17 문법 오류 시 `-std=c++17` 플래그 확인

## 개발 로드맵

### Phase 1: CSV to JSON 변환 (완료)
- [x] CSV 파일 업로드 및 파싱
- [x] WASM 기반 고속 변환
- [x] 인코딩 자동 감지
- [x] 기본 통계 계산

### Phase 2: 데이터 분석 기능 (진행 예정)
- [ ] 데이터 정렬 기능 구현
- [ ] 필터링 기능 구현
- [ ] 검색 기능 추가
- [ ] 데이터 테이블 뷰어

### Phase 3: 시각화 (진행 예정)
- [ ] Chart.js 통합
- [ ] 차트 타입 선택 UI
- [ ] 축/라벨 설정
- [ ] 차트 내보내기 (PNG, SVG)

### Phase 4: 고급 기능 (선택)
- [ ] Web Worker를 이용한 백그라운드 처리
- [ ] 여러 파일 일괄 처리
- [ ] 데이터 변환/가공 기능
- [ ] 대시보드 레이아웃

