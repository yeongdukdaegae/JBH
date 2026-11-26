// 실행: node tests/…  (Playwright 가 다른 곳에 있으면 PW=…/playwright/index.mjs · 크롬 위치는 CHROME=…)
const {chromium}=await import(process.env.PW||'playwright');
import path from 'path';import {fileURLToPath} from 'url';
const ROOT=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
import fs from 'fs';
const b=await chromium.launch({...(process.env.CHROME?{executablePath:process.env.CHROME}:{}),args:['--no-sandbox']});
const ok=(c,m)=>console.log((c?'PASS ':'FAIL ')+m);
const ctx=await b.newContext({viewport:{width:1100,height:800},locale:'ko-KR'});
const pg=await ctx.newPage();const errs=[];
pg.on('pageerror',e=>errs.push('PAGEERR '+e.message));pg.on('console',m=>{if(m.type()==='error')errs.push('CONSOLE '+m.text());});
await pg.goto('file://'+ROOT+'/editor/jbdev.html');await pg.waitForTimeout(400);
ok(await pg.title()==='JawBreaker Dev.','title: '+await pg.title());
await pg.evaluate(()=>{localStorage.setItem('secret-key','SIGNING');});
const zip=fs.readFileSync(ROOT+'/tests/fixtures/dev-proj.zip');
await pg.evaluate(async a=>{await handleImport([new File([new Uint8Array(a)],'proj.zip')],'project');},[...zip]);
await pg.waitForTimeout(500);
if(await pg.evaluate(()=>!document.getElementById('modal').hidden)){await pg.click('#m-btns button.primary, #m-btns button:last-child');await pg.waitForTimeout(300);}
console.log('  files:',await pg.evaluate(()=>Object.keys(files).join(', ')));
const logs=()=>pg.evaluate(()=>[...document.querySelectorAll('#log .ln .tx')].map(x=>x.textContent));
async function run(src,wait=1200){
  if(src)await pg.evaluate(t=>{files['main.jb']=t;openFile('main.jb');},src);
  await pg.evaluate(()=>{document.querySelector('#log').replaceChildren();});
  const r=pg.evaluate(()=>doRun());await pg.waitForTimeout(400);
  if(await pg.evaluate(()=>!document.getElementById('modal').hidden)){await pg.click('#m-btns button:nth-child(2)');}
  await r.catch(()=>{});await pg.waitForTimeout(wait);return logs();
}
// 신뢰 확인 창: 신뢰하지 않은 라이브러리는 실행 전에 물음 — 취소하면 실행하지 않음
const runAsk=async(src,pick,wait=1500)=>{   // pick: 신뢰 확인 창에서 누를 버튼 글자 (null = 창이 안 떠야 함)
  if(src)await pg.evaluate(t=>{files['main.jb']=t;openFile('main.jb');},src);
  await pg.evaluate(()=>{document.querySelector('#log').replaceChildren();});
  const r=pg.evaluate(()=>doRun());await pg.waitForTimeout(500);
  const tt=()=>pg.evaluate(()=>document.getElementById('modal').hidden?null:document.getElementById('m-title').textContent);
  let title=await tt();
  if(title==='저장하시겠습니까?'){await pg.locator('#m-btns button',{hasText:'저장하지 않고 실행'}).click();await pg.waitForTimeout(400);title=await tt();}
  const msg=await pg.evaluate(()=>document.getElementById('m-msg').textContent);
  if(title&&pick)await pg.locator('#m-btns button',{hasText:pick}).first().click();
  await r.catch(()=>{});await pg.waitForTimeout(wait);return {title,msg,L:await logs()};
};
let A=await runAsk(null,'취소',300);
ok(A.title==='라이브러리를 신뢰할까요?'&&/Counter 0\.2\.0/.test(A.msg)&&/쓰는 엔진 기능 없음/.test(A.msg)&&/격리 없이/.test(A.msg),'trust window before the first run: '+A.title);
ok(A.L.some(x=>/신뢰하지 않아 실행하지 않았습니다/.test(x))&&!A.L.some(x=>/total=/.test(x)),'cancel → the program does not run');
A=await runAsk(null,'신뢰하고 쓰기',1500);
let L=A.L;console.log(L.map(x=>'    | '+x).join('\n'));
ok(L.some(x=>x.includes('total=7')),'trust → runs');
A=await runAsk(null,null,300);ok(A.title===null,'trusted library: not asked again');
ok(L.some(x=>x.includes('total=7 name=hello')),'commands reach the library, values come back');
ok(L.some(x=>x.includes('t=8 p=12.5')),'values usable in expressions (int · float)');
ok(L.some(x=>x.includes('after reset=0')),'reset');ok(L.some(x=>/frames>0=[1-9]/.test(x)),'frame hook runs');
const pr=L.find(x=>x.includes("probe "))||"";ok(/read=blocked write=blocked malloc=ok env=none/.test(pr),"C library: no files, no environment, memory works");



const H='Use jawbreaker call jaw;\nUse Counter call cnt;\nrun{\n';
L=await run(H+'    cnt.nope();\n}\n');console.log('  bad command:',L.slice(-1)[0]);
L=await run(H+'    cnt.add("x");\n}\n');console.log('  bad type:',L.slice(-1)[0]);
L=await run(H+'    cnt.add(1, 2);\n}\n');console.log('  bad count:',L.slice(-1)[0]);
L=await run(H+'    print(cnt.zzz);\n}\n');console.log('  bad value:',L.slice(-1)[0]);
L=await run(H+'    var int call a = cnt.add(1);\n}\n');console.log('  call in expr:',L.slice(-1)[0]);
L=await run(H+'    cnt.boom();\n    loop{ jaw.wait(50); }\n}\n',1200);console.log('  lib throws:',L.slice(-1)[0]);
ok(L.some(x=>/main\.jb:4/.test(x)&&/Counter/.test(x)),'library error stops the run with the calling line');
L=await run(H+'    cnt.hang();\n    loop{ jaw.wait(50); }\n}\n',6500);console.log('  lib hangs:',L.slice(-1)[0]);
ok(L.some(x=>/응답하지 않아/.test(x)),'hung library is killed, editor stays alive');
ok(await pg.evaluate(()=>document.querySelectorAll('iframe[sandbox]').length)===0,'no sandbox frames left after stop');
// C conversion refuses
// export html
await pg.evaluate(t=>{files['main.jb']=t;},fs.readFileSync(ROOT+'/tests/fixtures/dev-main.jb','utf8'));
const html=await pg.evaluate(()=>gameHtml(readProject(files),'main.jb'));
fs.mkdirSync(ROOT+'/tests/out',{recursive:true});fs.writeFileSync(ROOT+'/tests/out/game.html',html);ok(html.includes('JBEXT')&&html.includes('ext/Counter.jbx'),'exported HTML carries the runner and the library ('+html.length+' chars)');
const g=await ctx.newPage();const gl=[];g.on('console',m=>gl.push(m.text()));g.on('pageerror',e=>gl.push('PAGEERR '+e.message));
await g.goto('file://'+ROOT+'/tests/out/game.html');await g.waitForTimeout(2000);
console.log('  game console:',gl.join(' || ').slice(0,400));
ok(gl.some(x=>x.includes('total=7 name=hello')),'exported game runs the library');

// ---- 엔진 기능 (그리기 · 오브젝트 · 파일) — tests/libs/Probe · 실행기(EXE) 와 같은 결과여야 함 (CI 의 EXE 시험과 같은 main.jb) ----
await pg.evaluate(a=>{files['ext/Probe.jbx']=bytesToDataUrl('Probe.jbx',new Uint8Array(a[0]));files['data.txt']=a[1];},[[...fs.readFileSync(ROOT+'/tests/fixtures/dev-Probe.jbx')],fs.readFileSync(ROOT+'/tests/fixtures/probe-data.txt','utf8')]);
const PROBE=fs.readFileSync(ROOT+'/tests/fixtures/probe-main.jb','utf8');
A=await runAsk(PROBE,'신뢰 · 파일 읽기 허락',1500);L=A.L;
ok(/화면에 그리기/.test(A.msg)&&/오브젝트 읽기/.test(A.msg)&&/오브젝트 바꾸기/.test(A.msg)&&/파일 읽기/.test(A.msg),'trust window lists the permissions');console.log(L.map(x=>'    | '+x).join('\n'));
const EXPECT=['look exists=1 px=10 py=20 sw=80 color=#5c68ff text=-','nope exists=0 color=-','lbl text=hello','after pos=[100, -50] color=#ff8800 text=bye scale=[40, 40]','file size=18 head=line one/line two/','ext size=-1','follow tail=[200, -50] frames>0='];
for(const e of EXPECT)ok(L.some(x=>x.startsWith(e)),'probe: '+e);
// 그리기 층: 화면의 몇 점의 색 (빨간 사각형 · 초록 원 · 파란 선)
await pg.locator('#display canvas').screenshot({path:ROOT+'/tests/out/probe-web.png'});console.log('  screenshot: tests/out/probe-web.png');
const colors=await pg.evaluate(()=>{const cv=document.querySelector('#display canvas'),c=cv.getContext('2d'),d=c.getImageData(0,0,cv.width,cv.height).data;const n={r:0,g:0,b:0};
  for(let i=0;i<d.length;i+=4){if(d[i]>200&&d[i+1]<40&&d[i+2]<40)n.r++;if(d[i]<40&&d[i+1]>200&&d[i+2]<40)n.g++;if(d[i]<40&&d[i+1]<40&&d[i+2]>200)n.b++;}return n;});
ok(colors.r>500&&colors.g>500&&colors.b>100,'draw layer is painted (red '+colors.r+' · green '+colors.g+' · blue '+colors.b+')');
// 내보낸 HTML: 에디터에서 허락한 files.read 를 그대로 · 같은 결과
{const h2=await pg.evaluate(()=>gameHtml(readProject(files),'main.jb'));fs.writeFileSync(ROOT+'/tests/out/probe.html',h2);
 const g2=await ctx.newPage();const gl2=[];g2.on('console',m=>gl2.push(m.text()));g2.on('pageerror',e=>gl2.push('PAGEERR '+e.message));
 await g2.goto('file://'+ROOT+'/tests/out/probe.html');await g2.waitForTimeout(2500);
 const miss=EXPECT.filter(e=>!gl2.some(x=>x.startsWith(e)));console.log('  exported probe:',miss.length?'missing '+miss.join(' | ')+' || '+gl2.join(' || ').slice(0,300):'all lines');
 ok(!miss.length,'exported HTML gives the same results (files.read grant carried)');await g2.close();}
// 허락하지 않으면 파일이 없는 것처럼
await pg.evaluate(async()=>{const h=await jbxHash(files['ext/Probe.jbx']);jbxTrustSet(h,{id:'Probe',version:'0.1.0',files:0});});
L=await run('Use jawbreaker call jaw;\nUse Probe call p;\nrun{\n    p.readf("data.txt");\n    jaw.wait(200);\n    print("nogrant size=%p.fsize%");\n}\n',800);
ok(L.some(x=>x.includes('nogrant size=-1')),'files.read without permission from the user: no files');
// 없는 오브젝트에 쓰기 → 그 줄에서 멈춤
L=await run('Use jawbreaker call jaw;\nUse DisPlayer call dis;\nUse Probe call p;\nrun{\n    dis.setupObject();\n    p.move("ghost", [1, 2]);\n    jaw.wait(300);\n}\n',800);console.log('  write missing:',L.slice(-1)[0]);
ok(L.some(x=>/main\.jb:6/.test(x)&&/ghost/.test(x)),'writing a missing object stops at the calling line');
// 권한 없이 엔진 기능을 쓰는 라이브러리 (jdh 가 굽지 않는 것을 손으로 만든 것) → 불러오지 않음
await pg.evaluate(a=>{delete files['ext/Probe.jbx'];files['ext/NoPerm.jbx']=bytesToDataUrl('NoPerm.jbx',new Uint8Array(a));},[...fs.readFileSync(ROOT+'/tests/fixtures/dev-NoPerm.jbx')]);
L=(await runAsk('Use jawbreaker call jaw;\nUse NoPerm call q;\nrun{\n    q.peek("x");\n    jaw.wait(300);\n}\n','신뢰하고 쓰기',800)).L;console.log('  no permission:',L.slice(-1)[0]);
ok(L.some(x=>/objects\.read/.test(x)&&/NoPerm/.test(x)),'host function without the declared permission is refused');
await pg.evaluate(()=>{delete files['ext/NoPerm.jbx'];delete files['data.txt'];});

// ---- 불러오기 · 설명서 · 자동완성 ----
await pg.evaluate(()=>{localStorage.removeItem('jawbreaker.jbx.trust');delete files['ext/Probe.jbx'];document.querySelector('#log').replaceChildren();});
ok(await pg.evaluate(()=>!!document.getElementById('m-importlib')),'menu has "라이브러리 불러오기 (.jbx)"');
await pg.setInputFiles('#jbxin',{name:'Probe-0.1.0.jbx',mimeType:'application/octet-stream',buffer:fs.readFileSync(ROOT+'/tests/fixtures/dev-Probe.jbx')});
await pg.waitForTimeout(500);
let T=await pg.evaluate(()=>document.getElementById('modal').hidden?null:document.getElementById('m-title').textContent);
ok(T==='라이브러리를 신뢰할까요?','import → trust window');
await pg.locator('#m-btns button',{hasText:'취소'}).first().click();await pg.waitForTimeout(300);
ok(await pg.evaluate(()=>!files['ext/Probe.jbx']),'cancel → not added');
await pg.setInputFiles('#jbxin',{name:'Probe-0.1.0.jbx',mimeType:'application/octet-stream',buffer:fs.readFileSync(ROOT+'/tests/fixtures/dev-Probe.jbx')});
await pg.waitForTimeout(500);await pg.locator('#m-btns button',{hasText:'파일 읽기는 막음'}).first().click();await pg.waitForTimeout(300);
ok(await pg.evaluate(()=>!!files['ext/Probe.jbx']),'trust → added as ext/Probe.jbx (file name from the id)');
console.log('  log:',(await logs()).slice(-1)[0]);
// 프로젝트에 파일로 넣어도 (불러오기 · 끌어 놓기) 같은 창
await pg.evaluate(()=>{delete files['ext/Probe.jbx'];});
const imp=pg.evaluate(async a=>{await handleImport([new File([new Uint8Array(a)],'Probe.jbx')],'project');},[...fs.readFileSync(ROOT+'/tests/fixtures/dev-Probe.jbx')]);
await pg.waitForTimeout(500);
T=await pg.evaluate(()=>document.getElementById('modal').hidden?null:document.getElementById('m-title').textContent);
ok(T===null,'same file already trusted → not asked again');await imp;
ok(await pg.evaluate(()=>!!files['ext/Probe.jbx']),'import as a file → ext/Probe.jbx');
// 설명서: ext/Probe.jbx 를 열면 읽기 전용 설명
const doc=await pg.evaluate(()=>{openFile('ext/Probe.jbx');return {v:ta.value,ro:ta.readOnly,img:!document.getElementById('imgview').hidden};});
console.log(doc.v.split('\n').slice(0,6).map(x=>'    | '+x).join('\n'));
ok(doc.ro&&!doc.img&&/Probe 0\.1\.0/.test(doc.v)&&/별칭\.move\(string, mat12\);/.test(doc.v)&&/별칭\.px   float/.test(doc.v)&&/objects\.write/.test(doc.v),'opening ext/Probe.jbx shows its documentation (read only)');
// 자동완성
const ac=await pg.evaluate(()=>{
  const at=src=>{files['main.jb']=src;openFile('main.jb');ta.value=src;ta.selectionStart=ta.selectionEnd=src.length;const r=acCompute();return r?r.items.map(i=>i.label+'|'+(i.detail||'')):[];};
  return {mem:at('Use Probe call p;\nrun{\n    p.'),use:at('Use '),str:at('Use Probe call p;\nrun{\n    print("%p.')};});
ok(ac.mem.includes('move|(string, mat12)')&&ac.mem.includes('px|float'),'autocomplete: alias. → commands and values');
ok(ac.use.some(x=>x.startsWith('Probe|')),'autocomplete: Use → user library');
ok(ac.str.some(x=>x.startsWith('p.fsize|')),'autocomplete: %alias.value% in strings');
// ---- 리스트 · 행렬 인자 — tests/libs/Lists · 실행기(EXE) 와 같은 줄 · 같은 오류여야 함 (CI 의 EXE 시험과 같은 main.jb) ----
await pg.evaluate(a=>{files['ext/Lists.jbx']=bytesToDataUrl('Lists.jbx',new Uint8Array(a));},[...fs.readFileSync(ROOT+'/tests/fixtures/dev-Lists.jbx')]);
A=await runAsk(fs.readFileSync(ROOT+'/tests/fixtures/lists-main.jb','utf8'),'신뢰하고 쓰기',1500);L=A.L;console.log(L.map(x=>'    | '+x).join('\n'));
const LEXPECT=['isum n=3 sum=5 total=5','isum n=0 sum=0','fsum n=3 sum=4 n=3','join n=3:a|bc|한글','bits 10101','pair 7 seven','mix t 1.5 -2 3 9','mat 1 2 3 4 5 6','anymat 3x2 sum=21 last=6','rowmat rows=3 (1,2) (3,4) (5,6)','isum n=20000 sum=200010000'];
ok(LEXPECT.every(x=>L.includes(x)),'lists · matrices: same lines as the EXE');
const LERR=[['L.pair([7]);',"'pair' 의 1번째 값은 항목이 2개인 리스트여야 합니다 (받은 것: 1개)"],
 ['var list call xs; xs = [7]; L.pair(xs);',"'pair' 의 1번째 값은 항목이 2개인 리스트여야 합니다 (받은 것: 1개)"],
 ['L.isum([1, "a"]);',"'isum' 의 1번째 값의 2번째 항목은 int 형식이어야 합니다 (받은 값: \"a\")"],
 ['var list call xs; xs = ["x", 2]; L.join(xs);',"'join' 의 1번째 값의 2번째 항목은 string 형식이어야 합니다 (받은 값: 2)"],
 ['L.isum(3);',"'isum' 의 1번째 값은 리스트여야 합니다 (받은 형식: int)"],
 ['L.mat([1, 2 | 3, 4]);',"'mat' 의 1번째 값은 matrix[2, 3] 행렬이어야 합니다 (받은 값: matrix[2, 2])"],
 ['L.rowmat([1, 2, 3]);',"'rowmat' 의 1번째 값은 matrix[..., 2] 행렬이어야 합니다 (받은 값: matrix[1, 3])"],
 ['var compound call c; c = [1, [2, 3]]; L.isum(c);',"'isum' 의 1번째 값의 2번째 항목은 int 형식이어야 합니다 (받은 값: matrix[1, 2])"]];
for(const [body,want] of LERR){
  L=await run('Use jawbreaker call jaw;\nUse Lists call L;\n\nrun{\n    '+body+'\n}\n',300);
  const last=L.filter(x=>/✖|오류/.test(x)).slice(-1)[0]||L.slice(-1)[0]||'';
  ok(last.includes('main.jb:5')&&last.includes(want),'lists error: '+want+(last.includes(want)?'':' — got: '+last));
}
await pg.evaluate(()=>{delete files['ext/Lists.jbx'];});
await pg.evaluate(()=>{delete files['ext/Probe.jbx'];});
console.log(errs.length?errs.join('\n'):'no page errors');
await b.close();
