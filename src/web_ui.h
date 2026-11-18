// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 SvenPausH
// Dieses Programm ist freie Software: Sie können es unter den Bedingungen der GNU General Public License,
// Version 3, wie von der Free Software Foundation veröffentlicht, weitergeben und/oder ändern.
// Es wird ohne jede Gewährleistung bereitgestellt. Siehe Datei LICENSE.
#pragma once
#include <pgmspace.h>

// Gemeinsamer Kopf mit Stylesheet (hell/dunkel automatisch)
static const char HTML_HEAD[] PROGMEM = R"HTML(<!doctype html><html lang="de"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>Pool-Dosierung</title>
<style>
:root{--bg:#f4f6f8;--card:#fff;--tx:#1b2430;--mut:#6b7684;--ac:#0a7ea4;--ok:#2e9d57;--warn:#d98e04;--bad:#c93c3c;--bd:#dde3ea}
@media(prefers-color-scheme:dark){:root{--bg:#10151b;--card:#1a222b;--tx:#e6ebf0;--mut:#93a0ae;--ac:#4cc2e8;--ok:#4cc27a;--warn:#f0b43c;--bad:#f06a6a;--bd:#2b3643}}
*{box-sizing:border-box}body{margin:0;font:16px/1.4 system-ui,sans-serif;background:var(--bg);color:var(--tx)}
header{display:flex;justify-content:space-between;align-items:center;padding:12px 16px;background:var(--card);border-bottom:1px solid var(--bd)}
h1{font-size:1.1rem;margin:0}h2{font-size:1rem;margin:26px 0 8px;color:var(--ac)}a{color:var(--ac);text-decoration:none}
main{max-width:900px;margin:0 auto;padding:16px}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(170px,1fr));gap:12px}
.card{background:var(--card);border:1px solid var(--bd);border-radius:12px;padding:14px}
.card .l{color:var(--mut);font-size:.85rem}.card .v{font-size:1.9rem;font-weight:600}
small,.hint{color:var(--mut);font-size:.8rem}
.bar{height:10px;border-radius:5px;background:var(--bd);overflow:hidden;margin:6px 0}.bar i{display:block;height:100%;background:var(--ok)}
.pill{display:inline-block;padding:2px 10px;border-radius:99px;font-size:.8rem;background:var(--bd);color:var(--tx)}
.pill.on{background:var(--ok);color:#fff}.pill.off{background:var(--bad);color:#fff}
.row{display:flex;justify-content:space-between;align-items:center;gap:12px;padding:8px 0;border-bottom:1px solid var(--bd)}
.row>span:first-child{flex:1}
.row input[type=number],.row input[type=text],.row input[type=password]{width:50%;max-width:260px;padding:6px;border:1px solid var(--bd);border-radius:6px;background:var(--bg);color:var(--tx)}
.row input[type=checkbox]{width:22px;height:22px}
button{padding:8px 14px;border:0;border-radius:8px;background:var(--ac);color:#fff;font-size:.95rem;cursor:pointer}
button.sec{background:var(--bd);color:var(--tx)}button.dng{background:var(--bad)}
.save{position:sticky;bottom:0;padding:12px 0;background:var(--bg)}
</style></head><body>)HTML";

// Hauptseite: Messwerte in Echtzeit
static const char HTML_MAIN[] PROGMEM = R"HTML(
<header><h1>Pool-Dosierung</h1><span><span id="conn" class="pill">…</span> &nbsp; <a href="/settings">Einstellungen</a></span></header>
<main>
<div id="banner" class="card" style="display:none;margin-bottom:12px;border-color:var(--warn)">Testmodus: Die Pumpen werden nicht geschaltet. Aktivieren unter Einstellungen → Dosierlogik.</div>
<div class="grid">
<div class="card"><div class="l">Temperatur</div><div class="v"><span id="t">–</span> °C</div></div>
<div class="card"><div class="l">pH-Wert</div><div class="v" id="ph">–</div><small>Regelwert <span id="phf">–</span></small></div>
<div class="card"><div class="l">Redox</div><div class="v"><span id="orp">–</span> mV</div><small>Regelwert <span id="orpf">–</span></small></div>
<div class="card"><div class="l">Durchfluss</div><div class="v" id="flow">–</div><small id="delay"></small></div>
</div>
<h2>Dosierpumpen</h2>
<div class="grid" id="pumps"></div>
<h2>Status</h2>
<div class="card"><small id="info"></small></div>
</main>
<script>
const $=i=>document.getElementById(i);
async function getStatus(){const c=new AbortController();const t=setTimeout(()=>c.abort(),4000);try{return await (await fetch('/api/status',{cache:'no-store',signal:c.signal})).json()}finally{clearTimeout(t)}}
const f=(v,d)=>v==null?'–':Number(v).toFixed(d);
const dur=s=>{s=Math.max(0,Math.round(s));const h=Math.floor(s/3600),m=Math.floor(s%3600/60);return h>0?h+' h '+m+' min':m+' min '+(s%60)+' s'};
const mm=s=>{s=Math.max(0,Math.round(s));return Math.floor(s/60)+':'+String(s%60).padStart(2,'0')};
const nm=['Pumpe 1 · pH-Minus','Pumpe 2 · Chlor / Redox'];
$('pumps').innerHTML=nm.map((n,i)=>`<div class="card"><div class="l">${n} <span id="r${i}" class="pill">aus</span></div>
<div id="s${i}" style="margin:6px 0"></div><div class="bar"><i id="b${i}" style="width:0"></i></div><small id="k${i}"></small></div>`).join('');
async function tick(){
 try{
  const d=await getStatus();
  $('conn').textContent='online';$('conn').className='pill on';
  $('t').textContent=f(d.temp,1);$('ph').textContent=f(d.ph,2);$('phf').textContent=f(d.phF,2);
  $('orp').textContent=f(d.orp,0);$('orpf').textContent=f(d.orpF,0);
  $('flow').textContent=d.flow?'läuft':'steht';
  $('delay').textContent=d.flow?(d.delayLeft>0?'Dosierfreigabe in '+mm(d.delayLeft):'Dosierfreigabe erteilt'):'';
  $('banner').style.display=d.dosing?'none':'block';
  d.p.forEach((p,i)=>{
   $('r'+i).textContent=p.on?'AN':'aus';$('r'+i).className='pill'+(p.on?' on':'');
   $('s'+i).textContent=p.state;
   $('b'+i).style.width=Math.max(0,Math.min(100,p.tankPct))+'%';
   $('b'+i).style.background=p.tankPct<15?'var(--bad)':p.tankPct<30?'var(--warn)':'var(--ok)';
   $('k'+i).innerHTML=`Behälter: ${f(p.tankPct,0)} % (${f(p.tankMl/1000,2)} l)<br>Laufzeit gesamt: ${dur(p.runSec)}<br>Heute: ${dur(p.dayRunSec)}, ${p.dayDoses} Dosierungen<br>${f(p.mlPulse,1)} ml pro Impuls`;
  });
  $('info').textContent=(d.mode=='ap'?'Setup-Modus (AP)':'WLAN '+d.rssi+' dBm')+' · IP '+d.ip+' · MQTT: '+d.mqtt+' · InfluxDB: '+d.influx+' · Messdaten vor '+f(d.dataAge,0)+' s · Laufzeit '+dur(d.uptime)+' · Uhrzeit '+(d.time||'nicht synchronisiert')+(d.ntp?' (NTP '+d.ntp+')':'')+' · Heap '+Math.round(d.heap/1024)+' kB';
 }catch(e){$('conn').textContent='offline';$('conn').className='pill off'}
}
(async function loop(){await tick();setTimeout(loop,1000)})();
</script></body></html>)HTML";

// Einstellungen: Anfang (das Formular selbst erzeugt der Code aus der Parametertabelle)
static const char HTML_SET_TOP[] PROGMEM = R"HTML(
<header><h1>Einstellungen</h1><a href="/">← Messwerte</a></header>
<main>
<div id="saved" class="card" style="display:none;margin-bottom:12px;border-color:var(--ok)">Gespeichert.</div>
<form method="post" action="/save">)HTML";

// Einstellungen: Ende mit Wartung (Zähler, Behälter, Test)
static const char HTML_SET_BOTTOM[] PROGMEM = R"HTML(
<div class="save"><button type="submit">Speichern</button></div>
</form>
<h2>Wartung</h2>
<div class="grid" id="maint"></div>
<div class="row"><span>Alle Pumpen sofort stoppen</span><button type="button" class="dng" onclick="act('stop')">Stopp</button></div>
<div class="row"><span>Neustart</span><button type="button" class="sec" onclick="act('reboot')">Neustart</button></div>
<div class="row"><span>WLAN-Daten löschen (danach Setup-Modus)</span><button type="button" class="dng" onclick="act('wifireset')">Löschen</button></div>
</main>
<script>
const $=i=>document.getElementById(i);
async function getStatus(){const c=new AbortController();const t=setTimeout(()=>c.abort(),4000);try{return await (await fetch('/api/status',{cache:'no-store',signal:c.signal})).json()}finally{clearTimeout(t)}}
const dur=s=>{s=Math.max(0,Math.round(s));const h=Math.floor(s/3600),m=Math.floor(s%3600/60);return h>0?h+' h '+m+' min':m+' min '+(s%60)+' s'};
if(location.search.includes('saved'))$('saved').style.display='block';
const nm=['Pumpe 1 · pH-Minus','Pumpe 2 · Chlor / Redox'];
$('maint').innerHTML=nm.map((n,i)=>`<div class="card"><div class="l">${n}</div><div id="m${i}" style="margin:6px 0"></div>
<div class="row"><button type="button" onclick="act('refill',${i})">Behälter aufgefüllt</button></div>
<div class="row"><button type="button" class="sec" onclick="act('resetrun',${i})">Laufzeit-Zähler zurücksetzen</button></div>
<div class="row"><button type="button" class="sec" onclick="act('resetday',${i})">Tageszähler zurücksetzen</button></div>
<div class="row"><span>Test / Schlauch füllen</span><span><input id="pv${i}" type="number" value="10" min="1" max="120" style="width:70px"> s <button type="button" onclick="act('prime',${i},$('pv${i}').value)">Start</button></span></div></div>`).join('');
async function act(a,i=0,v=0){
 if((a=='wifireset'||a=='reboot')&&!confirm('Wirklich?'))return;
 await fetch('/action',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:`a=${a}&i=${i}&v=${v}`});
 tick();
}
async function tick(){
 try{const d=await getStatus();
  d.p.forEach((p,i)=>{$('m'+i).innerHTML=`${p.state}<br><small>Behälter ${p.tankPct.toFixed(0)} % · Laufzeit gesamt ${dur(p.runSec)} · heute ${dur(p.dayRunSec)} / ${p.dayDoses} Dosierungen</small>`});
 }catch(e){}
}
(async function loop(){await tick();setTimeout(loop,2000)})();
</script></body></html>)HTML";
