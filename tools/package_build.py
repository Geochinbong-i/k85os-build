"""실제 flash_args/메타데이터로 설치 파일과 재현 명세를 만든다(기기에 쓰지 않음)."""
import csv
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / 'build'
OUT = ROOT / 'dist'
OUT.mkdir(exist_ok=True)
def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

flash = json.loads((BUILD/'flasher_args.json').read_text())
assert flash.get('flash_files') and flash.get('app', {}).get('file'), 'Missing actual flash metadata'
assert json.loads((BUILD/'project_description.json').read_text())['target'] == 'esp32s3'
source = subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip()
app = BUILD / flash['app']['file']
assert app.is_file()
subprocess.run([sys.executable,'-m','esptool','--chip','esp32s3','merge_bin',
                '-o',str(OUT/'k85OS-custom-full.bin'),'@flash_args'],cwd=BUILD,check=True)
shutil.copy2(app,OUT/'k85OS-custom-app.bin')
for offset,name in flash['flash_files'].items():
    path = BUILD / name
    assert path.is_file(), path
    shutil.copy2(path,OUT/f'{offset}-{path.name}')
for path in [BUILD/'flash_args',BUILD/'flasher_args.json',BUILD/'project_description.json',
             ROOT/'dependencies.lock',ROOT/'sdkconfig',ROOT/'partitions.csv',ROOT/'docs/INSTALL.md',
             ROOT/'docs/VALIDATION.md']:
    assert path.is_file(), path
    shutil.copy2(path,OUT/path.name)

# 슬롯 크기는 소스 파티션 표로 검사한다. 메타데이터의 app offset도 대조한다.
rows = [r for r in csv.reader((ROOT/'partitions.csv').read_text().splitlines()) if r and not r[0].lstrip().startswith('#')]
slot = next(r for r in rows if r[0].strip()=='ota_0')
assert int(slot[3].strip(),0)==int(flash['app']['offset'],0)
assert app.stat().st_size<=int(slot[4].strip(),0), 'App exceeds unchanged OTA slot'
assert (OUT/'k85OS-custom-full.bin').stat().st_size<=8*1024*1024
manifest = {
    'source_sha':source,'upstream_sha':'116affa940b2faac05ae1440b345c50c15f94f02',
    'idf':subprocess.check_output(['idf.py','--version'],text=True).strip(),
    'target':'esp32s3','flash_bytes':8*1024*1024,
    'app_offset':flash['app']['offset'],'app_size':app.stat().st_size,'slot_bytes':int(slot[4].strip(),0),
    'tests':'see matching GitHub Actions host-checks job',
    'device_test':'NOT RUN; no flashing performed',
    'files':{p.name:sha(p) for p in sorted(OUT.iterdir()) if p.is_file() and p.name not in ('SHA256SUMS.txt','build-manifest.json')}
}
(OUT/'build-manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n')
(OUT/'SHA256SUMS.txt').write_text(''.join(f'{sha(p)}  {p.name}\n' for p in sorted(OUT.iterdir()) if p.is_file() and p.name!='SHA256SUMS.txt'))
print(json.dumps({'source_sha':source,'app_bytes':app.stat().st_size,'artifacts':len(list(OUT.iterdir()))}))
