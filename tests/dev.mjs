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
// ---- 그리기가 놓이는 곳 (jdh_draw_layer: 자체 · 오브젝트 레이어 · UI) — tests/libs/Layers · 실행기(EXE) 와 같은 화면이어야 함 ----
await pg.evaluate(a=>{files['ext/Layers.jbx']=bytesToDataUrl('Layers.jbx',new Uint8Array(a));},[...fs.readFileSync(ROOT+'/tests/fixtures/dev-Layers.jbx')]);
A=await runAsk(fs.readFileSync(ROOT+'/tests/fixtures/layers-main.jb','utf8'),'신뢰하고 쓰기',1500);L=A.L;
ok(L.includes('layers painted'),'layers: program ran');
await pg.locator('#display canvas').screenshot({path:ROOT+'/tests/out/layers-web.png'});
// 색마다 넓이와 가운데: 가려지는 순서가 맞으면 넓이의 비가 정해짐 (파랑 300² − 200² · 회색 200² − 150² · 빨강 150² − 100² · 흰색 100² − 10² · 초록 10² · 노랑 40²)
const lay=await pg.evaluate(()=>{const cv=document.querySelector('#display canvas'),c=cv.getContext('2d'),W=cv.width,d=c.getImageData(0,0,W,cv.height).data;
  const P={blue:[0,0,255],grey:[128,128,128],red:[255,0,0],white:[255,255,255],green:[0,255,0],yellow:[255,255,0]},o={};
  for(const k in P)o[k]={n:0,x:0,minx:1e9,maxx:-1};
  for(let i=0;i<d.length;i+=4)for(const k in P){const q=P[k];if(Math.abs(d[i]-q[0])<24&&Math.abs(d[i+1]-q[1])<24&&Math.abs(d[i+2]-q[2])<24){const x=(i/4)%W,e=o[k];e.n++;e.x+=x;if(x<e.minx)e.minx=x;if(x>e.maxx)e.maxx=x;}}
  for(const k in o)o[k].x/=o[k].n||1;return o;});
{const b=lay.blue.n,r=k=>lay[k].n/b,near=(v,w)=>Math.abs(v-w)<w*0.12+0.002,u=(lay.blue.maxx-lay.blue.minx+1)/300;
 console.log('  layers: grey '+r('grey').toFixed(3)+' · red '+r('red').toFixed(3)+' · white '+r('white').toFixed(3)+' · green '+r('green').toFixed(4)+' · yellow '+r('yellow').toFixed(3)+' · yellow x '+((lay.yellow.x-lay.blue.x)/u).toFixed(1));
 ok(b>1000&&near(r('grey'),17500/50000)&&near(r('red'),12500/50000)&&near(r('white'),9900/50000),'layers: object layer numbers order the drawing between objects (below layer 0 · between 0 and 5)');
 ok(near(r('green'),100/50000),'layers: own layer is above objects');
 ok(near(r('yellow'),1600/50000)&&Math.abs((lay.yellow.x-lay.blue.x)/u-(-150))<6,'layers: UI layer is fixed to the screen (camera moved by 50)');}
L=await run('Use jawbreaker call jaw;\nUse Layers call ly;\nrun{\n    ly.bad();\n    jaw.wait(300);\n}\n',600);
ok(L.some(x=>x.includes('jdh_draw_layer 의 층은 JDH_LAYER_OWN · JDH_LAYER_OBJECT · JDH_LAYER_UI 중 하나입니다 (받은 값: 7)')),'layers: unknown layer → error');
await pg.evaluate(()=>{delete files['ext/Layers.jbx'];});
// ---- 네트워크 (HTTP · WebSocket) — tests/libs/Net · tests/net_server.py · 실행기(EXE) 와 같은 줄이어야 함 ----
{const {spawn}=await import('child_process');
 const srv=spawn('python3',[ROOT+'/tests/net_server.py','8770','8771'],{stdio:'ignore'});await pg.waitForTimeout(1200);
 try{
  await pg.evaluate(a=>{files['ext/Net.jbx']=bytesToDataUrl('Net.jbx',new Uint8Array(a));},[...fs.readFileSync(ROOT+'/tests/fixtures/dev-Net.jbx')]);
  const NET=fs.readFileSync(ROOT+'/tests/fixtures/net-main.jb','utf8');
  A=await runAsk(NET,'신뢰 · 네트워크 허락',14500);L=A.L;console.log(L.map(x=>'    | '+x).join('\n'));
  ok(/네트워크/.test(A.msg)&&/밖으로 보낼 수 있습니다/.test(A.msg),'trust window: network permission and its warning');
  const NEXPECT=['get status=200 size=9 body=hello net','post status=200 body=tag7:한글 body','missing status=404 body=nope','big status=200 size=200000 sum=995134','refused status=0','badurl started=0 status=0',
    'ws state=1 msgs=welcome','ws n=3 msgs=welcome|echo:one|echo:둘','ws closed by server state=2','ws refused state=-1'];
  for(const e of NEXPECT)ok(L.includes(e),'net: '+e);
  // 내보낸 HTML: 에디터에서 허락한 네트워크를 그대로
  {const h2=await pg.evaluate(()=>gameHtml(readProject(files),'main.jb'));fs.writeFileSync(ROOT+'/tests/out/net.html',h2);
   const g2=await ctx.newPage();const gl2=[];g2.on('console',m=>gl2.push(m.text()));
   await g2.goto('file://'+ROOT+'/tests/out/net.html');await g2.waitForTimeout(15500);
   const miss=NEXPECT.filter(e=>!gl2.includes(e));ok(!miss.length,'net: exported HTML gives the same lines (network grant carried)'+(miss.length?' — missing '+miss.join(' | '):''));await g2.close();}
  // 허락하지 않으면: 시작하지 못함 (0) · 접속하지 않음
  await pg.evaluate(async()=>{const h=await jbxHash(files['ext/Net.jbx']);jbxTrustSet(h,{id:'Net',version:'0.1.0',files:0,net:0});});
  L=await run('Use jawbreaker call jaw;\nUse Net call net;\nrun{\n    net.get("http://127.0.0.1:8770/hello");\n    jaw.wait(600);\n    print("nogrant started=%net.started% status=%net.status%");\n    net.wsopen("ws://127.0.0.1:8771/");\n    jaw.wait(600);\n    print("nogrant ws=%net.ws%");\n}\n',1800);
  ok(L.includes('nogrant started=0 status=0')&&L.includes('nogrant ws=-1')&&L.some(x=>/네트워크를 허락하지 않아/.test(x)),'net: without the grant nothing connects');
 }finally{srv.kill();await pg.evaluate(()=>{delete files['ext/Net.jbx'];});}
}
await pg.evaluate(()=>{delete files['ext/Probe.jbx'];});
console.log(errs.length?errs.join('\n'):'no page errors');
await b.close();
