#!/usr/bin/env python3
# 한 소스 (editor/src/jb.html) → Lite 와 Dev. 두 벌
#   /*DEV-START*/ … /*DEV-END*/    Dev. 에만 들어가는 코드 (사용자 라이브러리) — Lite 에서는 잘라 냄
#   /*LITE-START*/ … /*LITE-END*/  Lite 에만 들어가는 코드 (봉인 안내) — Dev. 에서는 잘라 냄
#   결과: editor/out/JawBreaker-Lite.html · editor/jbdev.html (설치 파일에 들어가는 Dev. 에디터)
import re, os
here = os.path.dirname(os.path.abspath(__file__))
s = open(os.path.join(here, 'src', 'jb.html'), encoding='utf-8').read()
lite = re.sub(r'/\*DEV-START\*/[\s\S]*?/\*DEV-END\*/', '', s).replace('/*LITE-START*/', '').replace('/*LITE-END*/', '')
dev = re.sub(r'/\*LITE-START\*/[\s\S]*?/\*LITE-END\*/', '', s).replace('JawBreaker Lite', 'JawBreaker Dev.')
assert 'JBEXT' not in lite and 'jbxScan' not in lite and 'DEV-START' not in lite, 'Lite 에 Dev. 코드가 남았습니다'
os.makedirs(os.path.join(here, 'out'), exist_ok=True)
open(os.path.join(here, 'out', 'JawBreaker-Lite.html'), 'w', encoding='utf-8').write(lite)
open(os.path.join(here, 'jbdev.html'), 'w', encoding='utf-8').write(dev)
print('lite', len(lite), 'dev', len(dev))
