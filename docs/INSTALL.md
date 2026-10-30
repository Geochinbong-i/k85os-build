# 설치·보존·복구 안내

이 파일은 사용자 빌드의 설치 계획이다. 자동으로 기기에 쓰지 않는다. 실기기 확인은 아직 수행하지 않았다.

## 파일 구분

- `k85OS-custom-full.bin`: 빌드가 만든 flash_args의 부트로더·파티션·앱·OTA 초기 정보를 합친 파일. 이 **통합 파일만** 시작 주소 0x0에 쓴다.
- `k85OS-custom-app.bin`: 앱 단독 파일. 시작 주소는 같은 산출물의 flasher_args.json/build-manifest.json의 app_offset을 사용한다. 0x0에 쓰면 안 된다.
- `SHA256SUMS.txt`와 `build-manifest.json`: 파일 해시와 실제 소스 SHA·IDF·파티션 조건.

**데이터 보존 설치에는 통합 BIN을 기본으로 쓰지 않는다.** 통합 BIN은 파일 사이의 빈 주소도 0xFF로 채워 하나의 연속 구간으로 쓴다. 현재 배치에서 그 구간에는 NVS(0x9000~0xEFFF)가 포함된다. 통합 BIN을 0x0에 쓰면 LittleFS 파일이 남더라도 NVS의 암호화 키 재료가 지워져 기존 Wi-Fi/MQTT 비밀을 해독하지 못할 수 있다. 현재 기기의 전체 백업 및 실행 중인 앱 슬롯·파티션을 먼저 확인하고, 동일한 K85 배치에서는 해당 슬롯의 앱만 교체하는 방식을 우선한다. 이 산출물의 app_offset은 기본 ota_0 주소이므로 현재 기기의 부팅 슬롯과 동일한지 별도로 확인한다.

근거(2026-10-31 확인): [Espressif esptool 공식 문서의 merge-bin/섹터 지우기 설명](https://docs.espressif.com/projects/esptool/en/latest/esp32s3/esptool/basic-commands.html). 사용자 보유 원본 통합 이미지의 해당 NVS 구간 24,576바이트도 모두 0xFF임을 확인했다.

## 쓰기 전 확인

1. 정확한 USB 포트와 ESP32-S3 기기를 확인한다. 전원·USB 다운로드 모드·복구 진입은 제조사 절차로 확인한다.
2. 현재 기기 플래시 전체 8MB를 새 이름으로 읽고 SHA256·파일 크기를 확인한다. 읽기 명령은 보유 esptool 버전의 `--help`에 맞춘다. esptool 5 계열 예시는 `python -m esptool --chip esp32s3 --port COM번호 read-flash 0x0 0x800000 before-k85-custom.bin`이다.
3. 사용자 보유 `sticks3-backup-rom.bin`은 Bruce를 올렸던 상태의 백업이다. K85 OS 설정·데이터 백업과 구별한다. 전체 플래시에는 개인정보가 있을 수 있어 공개 저장소에 올리지 않는다.
4. 기존/새 파티션 표를 비교한다. 동일한 K85 v8.0 배치이면 데이터 보존 앱 설치를 우선 검토한다. Bruce에서 처음 넘어오는 경우 LittleFS 내용과 배치가 다를 수 있으므로 호환을 가정하지 않는다.
5. 이 빌드는 마운트 실패 시 자동 포맷하지 않는다. 빈/다른 파일 시스템이면 보존 진단 화면에서 멈출 수 있다. 처음 전환에 포맷이 필요하면 삭제 대상·백업·복구를 별도로 승인하고 준비한다. 전체 erase를 기본 명령으로 제시하지 않는다.

## 설치와 검증

해시·파일 종류·덮어쓰는 주소 범위를 확인한 뒤 사용자 승인 범위의 쓰기만 실행한다. **통합 설치의 NVS 손실 범위를 승인한 경우에만** esptool 5 예시 `python -m esptool --chip esp32s3 --port COM번호 write-flash 0x0 k85OS-custom-full.bin`을 사용한다. 앱 설치는 manifest의 app_offset과 현재 기기의 실제 실행 슬롯을 대조한 주소를 사용한다. 명령 성공은 물리 기능 확인과 다르다.

가상 시험 암호 `Sugar!2026` 등을 쓰는 시험 AP로 연결→저장→전원 재연결을 확인한다. 버튼·4방향·IMU·설정 보존·게임·음향·IR은 VALIDATION.md의 실물 항목대로 확인한다. 실제 암호를 UART·로그에 남기지 않는다.

## 되돌리기와 OTA

USB 다운로드 모드에 들어가 쓰기 전 새로 읽은 전체 백업을 그 백업의 시작 주소 0x0에 복원하고 기동을 확인한다. 기존 Bruce 백업을 복원하면 Bruce 상태로 돌아간다. 이 절차는 아직 실물에서 시험하지 않았다.

upstream OTA 공개 신뢰 키와 검증 경로를 제거하지 않았다. upstream 서명 개인키가 없으므로 이 사용자 빌드를 upstream 정식 OTA로 가장하지 않는다. 사용자 빌드의 USB 설치와 upstream 서명 OTA를 구별한다. eFuse·Secure Boot·Flash Encryption은 변경하지 않는다.
