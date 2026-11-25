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
console.log(errs.length?errs.join('\n'):'no page errors');
await b.close();
