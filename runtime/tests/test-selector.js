// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 OliverCherryZ
const fs=require('fs'),vm=require('vm'),path=require('path');
const root=process.argv[2] || path.resolve(__dirname, '../plugins/vsl_articulation_selector');
function source(n){return fs.readFileSync(root+'/'+n,'utf8').replace(/^\.pragma library\s*/,'')}
let ctx={};vm.createContext(ctx);vm.runInContext(source('CatalogData.js'),ctx);
let action={};vm.createContext(action);vm.runInContext(source('SelectionActions.js'),action);
const tests=[];function check(n,v){tests.push({name:n,passed:!!v});if(!v)throw Error(n)}
const profile=JSON.parse(fs.readFileSync(path.resolve(root,'../../expression-maps/active.json'),'utf8'));
check('profile enabled and matched to the smooth bridge',profile.schemaVersion===1&&profile.enabled===true&&profile.resourceIds.includes('Synchron Smooth Bridge'));
const instrumentMap={vi:'violin',vi2:'violin',va:'viola',vc:'violoncello'};
for(const [catalogId,profileId] of Object.entries(instrumentMap)){
 const p=profile.instruments[profileId];
 const routes=p.articulations;
 check(catalogId+' catalog tokens, keyswitches and required leaf counts match runtime',ctx.rows.filter(r=>r.instruments.includes(catalogId)).every(r=>{
  const found=Object.values(routes).filter(a=>a.explicitToken===r.token);
  return found.length===1&&JSON.stringify(found[0].keyswitches)===JSON.stringify(r.perInstrument[catalogId].ks)
   &&found[0].requiredLeafPaths.length===r.perInstrument[catalogId].requiredLeaves;
 }));
 check(catalogId+' default, fallback and technique routes resolve',routes[p.defaultArticulation]&&routes[p.chordFallbackArticulation]&&Object.values(p.techniqueMappings).every(r=>routes[r]));
 check(catalogId+' sample leaf references are aligned',Object.values(routes).every(r=>r.requiredLeafPaths.length===r.requiredPatchEntries.length&&(!r.requiredPatchEntriesByPresetInstrument||Object.values(r.requiredPatchEntriesByPresetInstrument).every(v=>v.length===r.requiredLeafPaths.length))));
}
check('549 exact canonical tokens',ctx.rows.length===549&&new Set(ctx.rows.map(r=>r.token)).size===549);
check('historical full Chinese aliases unchanged',ctx.rows.every(r=>profile.notationAliases[r.label]===r.token));
check('all new score captions English/Italian and at most36 characters',ctx.rows.every(r=>r.caption.length<=36&&!/[\u3400-\u9fff]/.test(r.caption)));
check('no hash or canonical token visible',ctx.rows.every(r=>!r.caption.includes('ssv.')&&!r.caption.includes('vsl:')));
check('original ricochet variants preserved',Array.from({length:4},(_,i)=>'ricochet a'+(i+1)).every(c=>ctx.rows.some(r=>r.caption===c)));
check('recorded dynamic-legato captions do not imply live hairpins',ctx.rows.filter(r=>r.en.includes('p < f legato')||r.en.includes('f > p legato')).every(r=>r.caption.includes('sample')));
check('measured-tremolo recorded tempo is labelled sample',ctx.rows.filter(r=>r.en.startsWith('Tremolo / Measured / ')).every(r=>r.caption.includes('sample')&&r.caption.includes('bpm')));
check('blend captions never claim an automatic CC curve',ctx.rows.filter(r=>r.en.includes(' XF ')||r.en.includes('XF all')).every(r=>!r.caption.includes('auto CC')&&!r.caption.includes('CC ramp')));
check('short names and full English details searchable',ctx.filter({instrument:'vi',q:'spicc. agile'}).length>0&&ctx.filter({instrument:'vi',q:'Soft attack'}).length>0);
check('historical Chinese searchable',ctx.filter({instrument:'vi',q:'柔和'}).length>0);
check('no short-caption alias registration',ctx.rows.every(r=>profile.notationAliases[r.caption]===undefined));
check('violin options exclude cello effects',!ctx.options('category','vi').some(o=>o.value==='Effects'));
check('cello options include effects',ctx.options('category','vc').some(o=>o.value==='Effects'));
check('visible filter options English', ['category','attack','release','vibrato','length','mode'].every(k=>ctx.options(k,'vc').every(o=>!/[\u3400-\u9fff]/.test(o.text))));
const T={NOTE:1,CHORD:2,REST:3,STAFF_TEXT:4};
const violinStaff={part:{instrumentId:'violin'}};
const chord={type:2,noteType:0,track:5,fraction:{ticks:480},staff:violinStaff},note={type:1,parent:chord};
const other={type:2,noteType:0,track:4,fraction:{ticks:960}};
check('note resolves to chord',action.resolveTarget([note],T,0).target===chord);
check('multiple notes same chord accepted',action.resolveTarget([note,{type:1,parent:chord}],T,0).target===chord);
check('multiple positions rejected',!!action.resolveTarget([chord,other],T,0).error);
check('empty selection rejected',!!action.resolveTarget([],T,0).error);
check('grace chord rejected',!!action.resolveTarget([{type:2,noteType:1}],T,0).error);
function harness(target){
 const log=[],before=target?{text:target.text,vslArticulationToken:target.vslArticulationToken,vslCaptionSnapshot:target.vslCaptionSnapshot}:null;
 const cursor={track:null,rewindToFraction(v){log.push(['rewind',v])},add(v){log.push(['add',v])}};
 const score={startCmd(v){log.push(['start',v])},endCmd(v){log.push(['end',v])},newCursor(){log.push(['cursor']);return cursor}};
 const api={score,knownRoute:ctx.knownRoute,createStaffText(){log.push(['create']);return {type:4,text:'',vslArticulationToken:'',vslCaptionSnapshot:''}}};
 return {log,cursor,score,api,before};
}
let route=ctx.rows.find(r=>r.caption==='spicc. agile'),h=harness();
let made=action.apply(h.api,chord,4,route);
check('insertion preserves track and fraction',h.cursor.track===5&&h.log.find(x=>x[0]==='rewind')[1]===chord.fraction);
check('insertion single command boundary',h.log.filter(x=>x[0]==='start').length===1&&h.log.filter(x=>x[0]==='end').length===1&&!h.log.find(x=>x[0]==='end')[1]);
check('one visible StaffText stores all three fields',h.log.filter(x=>x[0]==='add').length===1&&made.text===route.caption&&made.vslArticulationToken===route.token&&made.vslCaptionSnapshot===route.caption);
check('metadata preselects inserted route',action.matchingRow(ctx.rows,made).id===route.id);
let text={type:4,text:'pizz.',track:5,fraction:{ticks:480},staff:violinStaff,vslArticulationToken:'old',vslCaptionSnapshot:'pizz.'};h=harness(text);
let changed=action.apply(h.api,text,4,route);
check('replacement updates original three fields',changed===text&&text.text===route.caption&&text.vslArticulationToken===route.token&&text.vslCaptionSnapshot===route.caption);
check('replacement one command no cursor or new element',h.log.length===2&&h.log[0][0]==='start'&&h.log[1][0]==='end');
let failed=false;h=harness();h.api.createStaffText=()=>{throw Error('mock insertion failure')};
try{action.apply(h.api,chord,4,route)}catch(_){failed=true}
check('failed insertion requests rollback once',failed&&h.log.filter(x=>x[0]==='end').length===1&&h.log.find(x=>x[0]==='end')[1]===true);
failed=false;h=harness();h.api.createStaffText=()=>({type:4,text:''});
try{action.apply(h.api,chord,4,route)}catch(_){failed=true}
check('older host without native metadata fails closed',failed&&h.log.find(x=>x[0]==='end')[1]===true&&!h.log.some(x=>x[0]==='add'));
failed=false;h=harness();let forged={...route,token:'vsl:short/ssv.b0000000000000000'};
try{action.apply(h.api,chord,4,forged)}catch(_){failed=true}
check('forged token rejected before transaction',failed&&h.log.length===0);
failed=false;h=harness();
try{action.apply(h.api,chord,4,{...route,caption:'forged caption'})}catch(_){failed=true}
check('unowned caption rejected before transaction',failed&&h.log.length===0);
const celloOnly=ctx.rows.filter(r=>r.instruments.length===1&&r.instruments[0]==='vc');
check('exactly ten cello-only catalog routes',celloOnly.length===10);
let rejected=0;
for(const r of celloOnly)for(const id of ['violin','viola']){
 let z=harness();let bad=false;
 try{action.apply(z.api,{...chord,staff:{part:{instrumentId:id}}},4,r)}catch(_){bad=true}
 if(bad&&z.log.length===0)rejected++;
}
check('all cello-only routes rejected on actual violin and viola before command regardless browse filter',rejected===20);
let accepted=0;
for(const r of celloOnly){let z=harness();let t=action.apply(z.api,{...chord,staff:{part:{instrumentId:'violoncello'}}},4,r);if(t.vslArticulationToken===r.token)accepted++}
check('all ten cello-only routes accepted on actual cello',accepted===10);
check('common route works for each supported actual instrument',['violin','viola','violoncello'].every(id=>{let z=harness();return action.apply(z.api,{...chord,staff:{part:{instrumentId:id}}},4,route).vslArticulationToken===route.token}));
const oldText={...text};h=harness(text);failed=false;
try{action.apply(h.api,text,4,celloOnly[0])}catch(_){failed=true}
check('wrong-instrument replacement preserves all fields and opens no command',failed&&h.log.length===0&&JSON.stringify(text)===JSON.stringify(oldText));
let unknownRejected=0;
for(const target of [{...chord,staff:null},{...chord,staff:{part:null}},{...chord,staff:{part:{instrumentId:'flute'}}}]){
 let z=harness();try{action.apply(z.api,target,4,route)}catch(_){if(z.log.length===0)unknownRejected++}
}
check('missing or unsupported actual instrument fails closed',unknownRejected===3);
let tickArg=null;
const changedInstrument={...chord,staff:{part:{instrumentId:'violin',instrumentIdAtTick(t){tickArg=t;return 'violoncello'}}}};
h=harness();const celloMade=action.apply(h.api,changedInstrument,4,celloOnly[0]);
check('instrument at actual target tick overrides first instrument',tickArg===chord.fraction&&celloMade.vslArticulationToken===celloOnly[0].token);
h=harness();failed=false;
try{action.apply(h.api,{...chord,staff:{part:{instrumentId:'violoncello',instrumentIdAtTick(){return 'violin'}}}},4,celloOnly[0])}catch(_){failed=true}
check('later violin instrument cannot inherit initial cello permission',failed&&h.log.length===0);
h=harness();failed=false;
try{action.apply(h.api,{...chord,staff:{part:{instrumentId:'violoncello',instrumentIdAtTick(){throw Error('unavailable tick')}}}},4,celloOnly[0])}catch(_){failed=true}
check('tick query failure never falls back to potentially wrong first instrument',failed&&h.log.length===0);
h=harness();failed=false;
try{action.apply(h.api,chord,4,{...celloOnly[0],instruments:['vi','vi2','va','vc']})}catch(_){failed=true}
check('forged availability list cannot bypass canonical instrument guard',failed&&h.log.length===0);
h=harness();const scalarOnly=action.apply(h.api,chord,4,{id:route.id,caption:route.caption,token:route.token});
check('only scalar model fields are needed for trusted catalog lookup',scalarOnly.vslArticulationToken===route.token);
h=harness();const wrappedRoute={...route,instruments:{0:'vi',1:'vi2',2:'va',3:'vc',length:4}};
const wrappedMade=action.apply(h.api,chord,4,wrappedRoute);
check('non-array QML-style nested sequence is never mistaken for catalog corruption',wrappedMade.vslArticulationToken===route.token);
let groups={};for(const r of ctx.rows)(groups[r.caption]??=[]).push(r);
let pair=Object.values(groups).find(g=>g.length>1);check('collision fixture exists',!!pair);
for(const r of pair.slice(0,2)){
 let t={type:4,text:action.escapeXml(r.caption),vslArticulationToken:r.token,vslCaptionSnapshot:r.caption};
 check('same caption exact metadata resolves '+r.id,action.matchingRow(ctx.rows,t).id===r.id);
}
check('ambiguous bare caption never guessed',action.matchingRow(ctx.rows,{text:pair[0].caption,vslArticulationToken:'',vslCaptionSnapshot:''})===null);
check('manual visible edit invalidates old metadata preselection',action.matchingRow(ctx.rows,{...made,text:'arco'})===null);
check('legacy full-path text remains selectable',action.matchingRow(ctx.rows,{text:route.label}).id===route.id);
check('formatting does not change selector plain-caption match',action.matchingRow(ctx.rows,{...made,text:'<b>'+made.text+'</b>'}).id===route.id);
let all=0;
for(const r of ctx.rows){let z=harness();let target=r.instruments.includes('vi')?chord:{...chord,staff:{part:{instrumentId:'violoncello'}}};let t=action.apply(z.api,target,4,r);if(action.plainCaption(t.text)!==r.caption||t.vslArticulationToken!==r.token||t.vslCaptionSnapshot!==r.caption)throw Error(r.id);all++}
check('all549 caption-token-snapshot writes roundtrip',all===549);
check('XML p<f display preserved',ctx.rows.filter(r=>r.caption.includes('<')).every(r=>action.plainCaption(action.escapeXml(r.caption))===r.caption));
const qml=source('VSLArticulationSelector.qml');
check('QML shows short preview plus full English details',qml.includes('modelData.caption')&&qml.includes('selectedRoute.en')&&!qml.includes('modelData.zh'));
check('QML keeps canonical lookup and metadata apply',qml.includes('knownRoute: Catalog.knownRoute')&&qml.includes('Element.STAFF_TEXT, selectedRoute)'));
check('QML doubleclick provides direct invocation',qml.includes('onDoubleClicked:'));
check('QML owns white/hover/selected list backgrounds under dark control themes',qml.includes('background: Rectangle')&&qml.includes('rowDelegate.highlighted ? "#dcebdc" : rowDelegate.hovered ? "#eef1ed" : "#ffffff"'));
process.stdout.write(JSON.stringify({tests,roundtrippedRoutes:all,captionCount:new Set(ctx.rows.map(r=>r.caption)).size}));
