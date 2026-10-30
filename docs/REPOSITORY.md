# K85 OS · StickS3 사용자 빌드

수정 펌웨어 소스·호스트 시험·GitHub Actions 빌드를 함께 관리하는 저장소다.

기준은 MIT 라이선스의 [kion85/k85OS v8.0](https://github.com/kion85/k85OS/tree/v8.0), SHA `116affa940b2faac05ae1440b345c50c15f94f02`다. 원저작권과 LICENSE 및 기존 빌드 전용 저장소의 이력을 보존했다. upstream 원문 설명은 `docs/UPSTREAM-README.md`에 보존한다.

## 변경 내용

- 일반 영문 키보드 기호 32개를 포함한 ASCII 95개 입력, 단일/여러행 공통 키보드.
- 명시 OK만 확정. 취소/뒤로/A+B는 출력 불변. UTF-8 삭제, 최대 바이트 검증, 비밀 입력 숨김.
- Bruce/Legacy 탐색과 A/B 역할 교환, 입력 시험, 미리보기 타임아웃 원복.
- Wi-Fi/MQTT 비밀의 GCM 저장과 기존 형식 읽기. 설정 세대·checksum·임시/백업 복구 및 실패 검사. 자동 포맷/NVS 삭제 차단.
- 4방향 화면 미리보기, IMU 중립·회전·축 교환/반전·감도·데드존·dt 기반 커서.

## 빌드와 시험

Actions의 **Build K85 OS custom**을 실행한다. 이 저장소의 실행 대상 커밋을 체크아웃하며 upstream v8.0을 다시 받지 않는다. main 푸시와 수동 실행을 지원한다.

호스트 시험과 ESP-IDF v5.3.1 펌웨어 빌드가 각각 실행된다. 호스트 시험은 Ubuntu에서 `bash tests/host/run.sh`로도 실행할 수 있다(필요 패키지 g++, libcjson-dev, libmbedtls-dev). Actions와 의존성 버전은 고정한다. 실제 생성된 lock은 빌드 산출물로 보존하고 추적한다.

성공 산출물에는 통합 이미지·앱 단독 파일·부팅 파일·SHA256·소스 SHA/도구 명세·설치 안내가 포함된다. **호스트 시험과 펌웨어 빌드가 모두 통과한 커밋**을 사용한다.

## 설치와 제한

루트의 `docs/INSTALL.md`와 `docs/VALIDATION.md`를 먼저 확인한다. 실기기 설치·연결·회전·게임·음향·IR·복구 시험은 아직 수행하지 않았다. 다운로드 가능한 이미지가 있어도 실물 검증 완료를 뜻하지 않는다.

기기 백업·실제 비밀번호·개인 파일은 이 공개 저장소에 넣지 않는다. upstream OTA 서명 검증을 유지하며, 사용자 빌드를 upstream 정식 서명 빌드로 가장하지 않는다.
