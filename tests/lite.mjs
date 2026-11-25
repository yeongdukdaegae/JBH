// 실행: node tests/…  (Playwright 가 다른 곳에 있으면 PW=…/playwright/index.mjs · 크롬 위치는 CHROME=…)
const {chromium}=await import(process.env.PW||'playwright');
import path from 'path';import {fileURLToPath} from 'url';
const ROOT=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
import fs from 'fs';import crypto from 'crypto';
const b=await chromium.launch({...(process.env.CHROME?{executablePath:process.env.CHROME}:{}),args:['--no-sandbox']});
const ok=(c,m)=>console.log((c?'PASS ':'FAIL ')+m);
const ctx=await b.newContext({viewport:{width:1100,height:800},locale:'ko-KR'});
const pg=await ctx.newPage();const errs=[];
pg.on('pageerror',e=>errs.push('PAGEERR '+e.message));pg.on('console',m=>{if(m.type()==='error')errs.push('CONSOLE '+m.text());});
await pg.goto('file://'+ROOT+'/editor/out/JawBreaker-Lite.html');await pg.waitForTimeout(400);
ok(await pg.title()==='JawBreaker Lite','tab title: '+await pg.title());
ok((await pg.textContent('#start h1')).trim()==='JawBreaker Lite','start title');
ok((await pg.textContent('header .brand span')).trim()==='JawBreaker Lite','header title');
fs.mkdirSync(ROOT+'/tests/out',{recursive:true});
// import the zip through the same code path as the file input
const zip=fs.readFileSync(ROOT+'/tests/fixtures/lite-proj.zip');
await pg.evaluate(async a=>{const f=new File([new Uint8Array(a)],'p.zip');await handleImport([f],'project');},[...zip]);
await pg.waitForTimeout(500);
if(await pg.evaluate(()=>!document.getElementById('modal').hidden)){console.log('  dialog:',(await pg.innerText('.mbox')).replace(/\n+/g,' | ').slice(0,200));await pg.click('#m-btns button.primary, #m-btns button:last-child');await pg.waitForTimeout(300);}
console.log('  files:',await pg.evaluate(()=>Object.keys(files).join(', ')));
ok(await pg.evaluate(()=>Object.hasOwn(files,'ext/Particle.jbx')),'ext/Particle.jbx kept on import');
const sha=u=>crypto.createHash('sha256').update(Buffer.from(u)).digest('hex');
const orig=sha(fs.readFileSync(ROOT+'/tests/fixtures/lite-Particle.jbx'));
ok(sha(await pg.evaluate(()=>[...dataUrlToBytes(files['ext/Particle.jbx'])]))===orig,'bytes unchanged in project');
// saved zip keeps it byte for byte
const out=await pg.evaluate(async()=>{const bl=await zipBlob(files,dirs,'LiteTest');return [...new Uint8Array(await bl.arrayBuffer())];});
fs.writeFileSync(ROOT+'/tests/out/lite-out.zip',Buffer.from(out));
// run → sealed message
let r=pg.evaluate(()=>doRun());await pg.waitForTimeout(400);
if(await pg.evaluate(()=>!document.getElementById('modal').hidden)){await pg.click('#m-btns button:nth-child(2)');}
await r;await pg.waitForTimeout(300);
const log=(await pg.evaluate(()=>[...document.querySelectorAll('#log .ln .tx')].map(x=>x.textContent))).slice(-2).join(' || ');
console.log('  log:',log);ok(/JawBreaker Dev\. 에서만/.test(log),'sealed message on Use');
// open the file in the editor
await pg.evaluate(()=>openFile('ext/Particle.jbx'));await pg.waitForTimeout(400);
// path rules
console.log('  rules:',await pg.evaluate(()=>[pathRuleError('a.jbx',false),pathRuleError('ext/a.jbx',false),pathRuleError('ext/a.jb',false),pathRuleError('lib/a.jbl',false),pathRuleError('img/a.png',false)].join(' | ')));
// english message
await pg.evaluate(()=>setLang('en'));r=pg.evaluate(()=>doRun());await pg.waitForTimeout(400);
if(await pg.evaluate(()=>!document.getElementById('modal').hidden)){await pg.click('#m-btns button:nth-child(2)');}
await r;await pg.waitForTimeout(300);
console.log('  log(en):',(await pg.evaluate(()=>[...document.querySelectorAll('#log .ln .tx')].map(x=>x.textContent))).slice(-1)[0]);
// a normal project still runs
await pg.evaluate(()=>{setLang('ko');files['main.jb']='Use jawbreaker call jaw;\nrun{\n    print("ok-run");\n}\n';openFile('main.jb');});
r=pg.evaluate(()=>doRun());await pg.waitForTimeout(400);
if(await pg.evaluate(()=>!document.getElementById('modal').hidden)){await pg.click('#m-btns button:nth-child(2)');}
await r;await pg.waitForTimeout(500);
ok((await pg.evaluate(()=>[...document.querySelectorAll('#log .ln .tx')].map(x=>x.textContent))).some(x=>x.includes('ok-run')),'project without the library runs');
console.log(errs.length?errs.join('\n'):'no page errors');
await b.close();
