// 네이티브 판 (jbdev.exe · jdh build html) 이 쓰는 파일을 웹 판 (editor/jbdev.html) 에서 뽑아 냄 — editor/build.py 다음에 실행
//   node editor/gen_native.mjs   (Playwright 필요 · PW=…/playwright/index.mjs 로 위치 지정 가능)
// 만드는 것 (editor/native/ · 저장소에 넣음):
//   docs/<언어>/<이름>.jbl   내장 라이브러리 설명서 (jawbreaker.jbl · DisPlayer.jbl …)
//   ac-<언어>.txt            자동완성 목록 — 줄마다  라이브러리 \t 이름 \t 종류 \t 설명 \t 넣을 글자 \t 도움말  ($0 = 커서 · \n 은 "\\n")
//   game-<언어>.html          내보낸 HTML 의 틀 — /*__JB_PROJECT__*/ 자리에 프로젝트 JSON · __JB_TITLE__ 자리에 이름
//   apk-tpl.json              APK 의 Android 실행기 (classes.dex · resources.arsc · 기본 아이콘 · base64) — jdh build apk
const {chromium}=await import(process.env.PW||'playwright');
import path from 'path';import {fileURLToPath} from 'url';import fs from 'fs';
const ROOT=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const OUT=ROOT+'/editor/native';
const LANGS=['ko','en','ja','zh-CN','zh-TW'];
const b=await chromium.launch({...(process.env.CHROME?{executablePath:process.env.CHROME}:{}),args:['--no-sandbox']});
const pg=await (await b.newContext({viewport:{width:1100,height:800}})).newPage();
const errs=[];pg.on('pageerror',e=>errs.push(e.message));
await pg.goto('file://'+ROOT+'/editor/jbdev.html');await pg.waitForTimeout(500);
fs.mkdirSync(OUT+'/docs',{recursive:true});
for(const L of LANGS){
  const r=await pg.evaluate(L=>{
    LANG=L;try{I18N_EXACT=null;}catch(e){}
    const esc=s=>String(s).replace(/\\/g,'\\\\').replace(/\t/g,'\\t').replace(/\r?\n/g,'\\n');
    const docs={};for(const n of [BUILTIN,DIS_DOC,COL_DOC,CAN_DOC,MAG_DOC,SND_DOC,CAM_DOC])docs[n]=getContent(n);
    const libs=['jawbreaker',DIS_ID,COL_ID,CAN_ID,MAG_ID,SND_ID,CAM_ID];const ac=[];
    for(const lib of libs)for(const it of acMembers(lib,'',null,null))ac.push([lib,it.label,it.kind,tr(it.detail||''),it.insert,tr(it.doc||'')].map(esc).join('\t'));
    for(const [k,ins,d] of STMT_SNIPS)ac.push(['@stmt',k,'snippet','',ins,tr(d)].map(esc).join('\t'));
    for(const lib of libs)ac.push(['@lib',lib,'lib','','',''].map(esc).join('\t'));
    // 내보낸 HTML 의 틀
    const keep=files;files={};let html;
    try{html=gameHtml({name:'__JB_TITLE__'},'main.jb');}finally{files=keep;}
    const i=html.indexOf('const PROJECT='),j=html.indexOf(';\nJBMEM.set(',i);
    if(i<0||j<0)throw new Error('PROJECT 자리를 찾지 못함');
    html=html.slice(0,i)+'const PROJECT=/*__JB_PROJECT__*/'+html.slice(j);
    return {docs,ac:ac.join('\n')+'\n',html};
  },L);
  fs.mkdirSync(OUT+'/docs/'+L,{recursive:true});
  for(const [n,t] of Object.entries(r.docs))fs.writeFileSync(OUT+'/docs/'+L+'/'+n,t);
  fs.writeFileSync(OUT+'/ac-'+L+'.txt',r.ac);
  fs.writeFileSync(OUT+'/game-'+L+'.html',r.html);
  console.log(L,'docs',Object.keys(r.docs).length,'ac',r.ac.split('\n').length-1,'html',r.html.length);
}
// APK 의 Android 실행기 틀 (jdh build apk) — 언어와 상관없이 하나
{const t=await pg.evaluate(()=>{const e=document.getElementById('jbapk-tpl');return e?e.textContent.trim():'';});
 if(!t)throw new Error('jbapk-tpl 이 없음');JSON.parse(t);fs.writeFileSync(OUT+'/apk-tpl.json',t+'\n');console.log('apk-tpl',t.length);}
await b.close();
if(errs.length){console.error(errs.join('\n'));process.exit(1);}
