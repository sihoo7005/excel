# CellBook

C++로 만든 윈도우용 스프레드시트 앱입니다. HTML이나 브라우저 화면을 사용하지 않습니다. Qt 네이티브 위젯과 자체 수식 계산 엔진을 사용합니다. Microsoft Excel과는 별개의 프로그램입니다.

## 다운로드와 실행

[최신 윈도우 다운로드](https://github.com/sihoo7005/excel/releases/tag/latest)

- `CellBook-Setup.exe`: 설치 프로그램. 실행하면 시작 메뉴에 CellBook이 추가됩니다.
- `CellBook-Windows-x64.zip`: 압축을 전부 풀고 `CellBook/CellBook.exe`를 실행합니다. DLL 파일을 실행 파일과 함께 유지하세요.
- Windows 10/11, 64비트. Excel, Python, 개발 도구 설치는 필요하지 않습니다.
- `examples/가계부.cellbook`를 열면 수식과 서식이 있는 예시를 볼 수 있습니다.

코드 서명 인증서는 포함되어 있지 않습니다. 설치 파일은 저장소의 공개된 소스로 빌드됩니다.

## 기본 기능

| 기능 | 구현 |
|---|---|
| 셀 편집 | 키보드 입력, 한글 입력, 더블 클릭 또는 F2, 주소창, 수식 입력줄 |
| 계산 | 사칙연산, 거듭제곱, 백분율, 비교, 문자열 연결, 자동 재계산 |
| 참조 | `A1`, `$A$1`, `A$1`, `$A1`, `A1:C10`, `Sheet1!A1`, `'두 번째'!A1` |
| 여러 시트 | 추가, 이름 변경, 삭제, 시트 간 참조 |
| 편집 | 복사, 잘라내기, 붙여넣기, 내용 삭제, 찾기, 전체 바꾸기 |
| 채우기 | 아래로/오른쪽으로 채우기, 상대 참조 자동 이동 |
| 서식 | 글꼴, 크기, 굵게, 기울임, 밑줄, 글자/배경 색, 정렬, 테두리, 줄바꿈 |
| 숫자 서식 | 일반, 소수, 천 단위, 백분율, 원화, 날짜 |
| 행·열 | 크기 조정, 열 자동 맞춤, 삽입·삭제, 참조 갱신 |
| 병합 | 범위 병합/해제, 내용 삭제 전 확인 |
| 데이터 | 열 기준 오름차순/내림차순, 첫 행 제외 옵션, 텍스트 포함 필터 |
| 보기 | 첫 행 고정, 수식 표시, 50-200% 배율, 선택 개수·합계·평균 |
| 차트 | 막대/꺾은선, 차트 저장, PNG 내보내기, XLSX 차트 내보내기 |
| 출력 | 현재 시트/선택 범위 인쇄, 여러 페이지, PDF 저장 |
| 실행 취소 | 최대 50단계, 통합문서 변경 복구, 큰 문서는 메모리 한도에 따라 줄어듦 |
| 파일 | `.cellbook`, `.xlsx`, `.csv`, `.tsv`, 임시 파일 후 원자적 저장 |

## 수식 함수

`SUM`, `AVERAGE`, `MIN`, `MAX`, `COUNT`, `COUNTA`, `PRODUCT`,
`IF`, `IFERROR`, `AND`, `OR`, `NOT`, `ISBLANK`, `ISNUMBER`, `ISERROR`,
`ABS`, `ROUND`, `ROUNDUP`, `ROUNDDOWN`, `INT`, `MOD`, `POWER`, `SQRT`,
`COUNTIF`, `SUMIF`, `LEN`, `LEFT`, `RIGHT`, `MID`,
`CONCAT`, `CONCATENATE`, `UPPER`, `LOWER`, `TRIM`.

```text
=SUM(B2:B10)
=IF(C2>=60,"통과","재시험")
=ROUND(A1/3,2)
=SUMIF(A2:A10,">100",B2:B10)
='두 번째'!A1+$B$2
=IFERROR(A1/B1,0)
```

문자열은 `"문자열"`로 씁니다. `0012`처럼 숫자로 바뀌면 안 되는 값은 `'0012`로 입력합니다. 쉼표 또는 세미콜론으로 함수 인자를 구분할 수 있습니다. 순환 참조는 `#CYCLE!`, 지원하지 않는 함수는 `#NAME?`로 표시합니다.

## 파일 호환과 범위

- `.cellbook`: 데이터, 수식, 기본 서식, 크기, 병합, 차트 정의를 보존하는 자체 형식입니다.
- `.xlsx`: 기본 워크시트 데이터, 수식, 기본 셀 서식, 너비/높이, 병합을 읽고 씁니다. 기존 차트, 이미지, 매크로, 피벗 테이블, 조건부 서식, 이름 정의, 데이터 유효성, 고급 Excel 기능을 보존하지 않습니다. 지원하지 않는 함수는 계산하지 않습니다. 원본 Excel 파일은 새 이름으로 저장합니다.
- `.xlsx` 수식은 수식 자체와 숫자 계산 결과를 저장합니다. 문자열/오류를 반환하는 수식의 캐시 결과는 Excel에서 재계산하기 전까지 정확하지 않을 수 있습니다.
- `.csv`/`.tsv`: 현재 시트의 계산 결과만 내보냅니다. 한글을 위한 UTF-8(Unicode Transformation Format, 8비트) 서명을 포함합니다. 불러올 때 외부 텍스트의 수식을 자동 실행하지 않습니다.
- 기본 화면은 1,000행 × 100열입니다. 주소창이나 파일 열기로 범위를 확장할 수 있습니다. 시트 주소는 최대 1,048,576행 × 16,384열입니다. 저장된 셀은 통합문서당 1,000,000개까지 읽습니다. 범위 편집은 한 번에 100,000개 셀, 수식 범위 확장은 1,000,000개 셀까지입니다.
- 차트는 첫 행을 제목으로, 첫 열을 범주로 사용합니다. 화면 차트는 최대 100개 데이터 행과 12개 계열을 표시합니다.
- 필터는 화면에 적용됩니다. 파일에 저장되지 않습니다. 행·열 삽입/삭제는 병합을 해제합니다. 정렬은 선택 범위의 행과 상대 참조를 이동합니다. 선택 범위 밖에서 개별 행을 참조하는 수식까지 Excel과 동일하게 재배치하지는 않습니다.
- COUNTIF/SUMIF의 조건은 숫자/문자열 비교를 지원합니다. 와일드카드 조건은 지원하지 않습니다. 배열 수식, 전체 열/행 참조, 구조화된 표 참조, 외부 통합문서 참조는 지원하지 않습니다.

## 주요 단축키

`Ctrl+N/O/S`: 새 문서/열기/저장 · `Ctrl+Z/Y`: 실행 취소/다시 실행
`Ctrl+C/X/V`: 복사/잘라내기/붙여넣기 · `Delete`: 내용 삭제
`Ctrl+B/I/U`: 굵게/기울임/밑줄 · `Ctrl+F/H`: 찾기/바꾸기
`Ctrl+D/R`: 아래로/오른쪽으로 채우기 · `Alt+=`: 자동 합계
`Shift+F11`: 새 시트 · `Ctrl+P`: 인쇄

## 직접 빌드

CMake 3.22 이상, C++17 컴파일러, Qt 6.4 이상이 필요합니다. Qt Core/Gui/Widgets/PrintSupport와 Qt Gui의 private 헤더를 설치하세요. Windows 배포 빌드는 Qt 6.8.3 + Microsoft Visual C++ 2022를 사용합니다. QXlsx는 고정 커밋으로 자동 다운로드합니다.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
```

Linux의 화면 없는 환경에서는 `QT_QPA_PLATFORM=offscreen`을 설정하세요. Windows에서는 Qt 경로를 `CMAKE_PREFIX_PATH`에 지정하고 Qt `bin`을 `PATH`에 추가하세요.

Qt 없이 수식 엔진만 테스트:

```sh
cmake -S . -B build-core -DCELLBOOK_CORE_ONLY=ON
cmake --build build-core
ctest --test-dir build-core --output-on-failure
```

GitHub Actions는 Windows와 Linux에서 계산·파일 왕복·화면 기능을 테스트합니다. Windows 실행 파일과 Qt/Microsoft 런타임을 함께 패키징하고, 패키징된 실행 파일을 다시 테스트한 뒤 설치 파일과 압축 파일을 Releases에 올립니다.

## 라이선스

앱 소스: MIT. Qt: 동적 링크 LGPL-3.0(GNU Lesser General Public License, version 3). QXlsx: MIT. 자세한 내용은 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)를 참조하세요.
