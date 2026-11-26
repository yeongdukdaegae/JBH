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
let L=await run(null,1500);console.log(L.map(x=>'    | '+x).join('\n'));
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
const grantKey=()=>pg.evaluate(()=>{const L=jbxRead(files['ext/Probe.jbx'],'ext/Probe.jbx');return 'jawbreaker.jbx.files:'+(readProject(files).name||'')+':'+L.id+'@'+L.version;});
const GK=await grantKey();
await pg.evaluate(k=>localStorage.setItem(k,'1'),GK);
const PROBE=fs.readFileSync(ROOT+'/tests/fixtures/probe-main.jb','utf8');
L=await run(PROBE,1500);console.log(L.map(x=>'    | '+x).join('\n'));
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
await pg.evaluate(k=>localStorage.setItem(k,'0'),GK);
L=await run('Use jawbreaker call jaw;\nUse Probe call p;\nrun{\n    p.readf("data.txt");\n    jaw.wait(200);\n    print("nogrant size=%p.fsize%");\n}\n',800);
ok(L.some(x=>x.includes('nogrant size=-1')),'files.read without permission from the user: no files');
// 없는 오브젝트에 쓰기 → 그 줄에서 멈춤
L=await run('Use jawbreaker call jaw;\nUse DisPlayer call dis;\nUse Probe call p;\nrun{\n    dis.setupObject();\n    p.move("ghost", [1, 2]);\n    jaw.wait(300);\n}\n',800);console.log('  write missing:',L.slice(-1)[0]);
ok(L.some(x=>/main\.jb:6/.test(x)&&/ghost/.test(x)),'writing a missing object stops at the calling line');
// 권한 없이 엔진 기능을 쓰는 라이브러리 (jdh 가 굽지 않는 것을 손으로 만든 것) → 불러오지 않음
await pg.evaluate(a=>{delete files['ext/Probe.jbx'];files['ext/NoPerm.jbx']=bytesToDataUrl('NoPerm.jbx',new Uint8Array(a));},[...fs.readFileSync(ROOT+'/tests/fixtures/dev-NoPerm.jbx')]);
L=await run('Use jawbreaker call jaw;\nUse NoPerm call q;\nrun{\n    q.peek("x");\n    jaw.wait(300);\n}\n',800);console.log('  no permission:',L.slice(-1)[0]);
ok(L.some(x=>/objects\.read/.test(x)&&/NoPerm/.test(x)),'host function without the declared permission is refused');
await pg.evaluate(()=>{delete files['ext/NoPerm.jbx'];delete files['data.txt'];});
console.log(errs.length?errs.join('\n'):'no page errors');
await b.close();
