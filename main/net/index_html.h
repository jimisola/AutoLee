#pragma once

// Verbatim from src/web_server.cpp's INDEX_HTML - framework-agnostic HTML/CSS/JS,
// only the C++ wrapper changed (PROGMEM dropped, not needed on ESP32-C6).
static const char INDEX_HTML[] = R"rawliteral(
<!DOCTYPE html><html lang="en"><head>
<meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>AutoLee</title>
<style>
:root{--bg:#111;--box:#1b1b1b;--card:#222;--accent:#1F6FEB;--green:#00FF00;--red:#FF4444;--text:#eee;--muted:#888;--dim:#555;--border:#333}
*{margin:0;padding:0;box-sizing:border-box}
body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;background:var(--bg);color:var(--text);min-height:100vh;display:flex;justify-content:center;padding:16px 12px}
.wrap{width:100%;max-width:420px}
h1{font-size:1.5em;text-align:center;margin-bottom:2px}
.sub{color:var(--dim);font-size:.8em;text-align:center;margin-bottom:16px}
.sec{background:var(--box);border-radius:12px;padding:16px;margin-bottom:10px}
.sec h2{font-size:.85em;color:var(--muted);text-transform:uppercase;letter-spacing:.06em;margin-bottom:10px}
.badge{display:inline-block;padding:3px 10px;border-radius:10px;font-size:.75em;font-weight:600}
.badge.ok{background:#0d3320;color:var(--green)}.badge.warn{background:#3A2B12;color:#FFD37C}.badge.run{background:#331111;color:var(--red)}.badge.stall{background:#441111;color:#FF8844}
.counter{font-size:3.2em;font-weight:700;color:var(--green);text-align:center;margin:6px 0;font-variant-numeric:tabular-nums;line-height:1.1}
.btn{display:inline-block;padding:10px 20px;border:none;border-radius:8px;font-size:.9em;font-weight:600;cursor:pointer;color:#fff;text-align:center;transition:opacity .15s}
.btn:hover{opacity:.85}.btn:active{opacity:.7}.btn:disabled{opacity:.35;cursor:not-allowed}
.btn-run{background:var(--green);color:#000;font-size:1.1em;width:100%;padding:14px;border-radius:10px}.btn-run.active{background:var(--red);color:#fff}
.btn-blue{background:var(--accent)}.btn-dark{background:#333}.btn-red{background:#B42318}
.btn-sm{padding:6px 10px;font-size:.8em;border-radius:6px}
.row{display:flex;gap:6px;align-items:center;flex-wrap:wrap}.row.ctr{justify-content:center}
.sr{display:flex;justify-content:space-between;padding:3px 0;font-size:.85em}.sr .l{color:var(--muted)}.sr .v{font-weight:600;font-variant-numeric:tabular-nums}
.slider-row{display:flex;align-items:center;gap:10px;margin:6px 0}
.slider-row input[type=range]{flex:1;height:4px;-webkit-appearance:none;background:var(--border);border-radius:2px;outline:none}
.slider-row input[type=range]::-webkit-slider-thumb{-webkit-appearance:none;width:20px;height:20px;border-radius:50%;background:var(--accent);cursor:pointer}
.slider-row span{min-width:50px;text-align:right;color:#fff;font-size:.85em;font-weight:600;font-variant-numeric:tabular-nums}
.ea{display:flex;gap:3px;align-items:center;justify-content:center;margin:6px 0}
.hint{font-size:.75em;color:var(--dim);margin:2px 0 6px}
hr{border:none;border-top:1px solid var(--border);margin:10px 0}
details{margin-top:10px}
details summary{color:var(--accent);font-size:.85em;cursor:pointer;padding:4px 0;user-select:none}
details summary:hover{opacity:.8}
details[open] summary{margin-bottom:8px}
.jam-alert{display:none;background:#2a1111;border:1px solid #442222;border-radius:10px;padding:12px;margin-top:10px;text-align:center}
.pw-alert{display:none;background:#3A2B12;border:1px solid #5c431d;border-radius:10px;padding:12px;margin-bottom:10px;text-align:center}
input[type=text],input[type=password]{width:100%;padding:10px;margin-bottom:6px;background:var(--card);border:1px solid var(--border);border-radius:8px;color:#fff;font-size:.9em}
.upload{border:2px dashed #444;border-radius:8px;padding:16px;text-align:center;color:var(--muted);cursor:pointer;font-size:.85em}
.upload:hover{border-color:var(--accent)}.upload.on{border-color:var(--green);color:var(--green)}
.pbar{width:100%;height:5px;background:#333;border-radius:3px;margin-top:6px;overflow:hidden;display:none}
.pbar .fill{height:100%;background:var(--accent);width:0%;transition:width .3s;border-radius:3px}
#otaS{font-size:.8em;margin-top:4px;min-height:1em}
.log{background:#000;border-radius:8px;padding:8px;font-family:'Courier New',monospace;font-size:.7em;color:#0f0;height:400px;overflow-y:auto;white-space:pre-wrap;word-break:break-all}
.page{display:none}.page.active{display:block}
.nav-footer{margin-top:16px;padding:14px 0;text-align:center;border-top:1px solid var(--border)}
.nav-footer a{color:var(--accent);text-decoration:none;font-size:.85em;font-weight:600;margin:0 10px;cursor:pointer}
.nav-footer a:hover{opacity:.7}
.nav-footer a.active{color:var(--green)}
.back-link{display:inline-block;color:var(--accent);font-size:.85em;font-weight:600;cursor:pointer;margin-bottom:12px;text-decoration:none}
.back-link:hover{opacity:.7}
</style></head><body>
<div class="wrap">

<h1>AutoLee</h1>
<div class="sub">by K.L Design · <span id="ver"></span></div>

<!-- Shown on every page (not just Main) while the web password is still the
     factory default: the firmware refuses every control action with a 403
     until it's changed - see the "defaultPassword" state field. -->
<div class="pw-alert" id="pwAlert">
<div style="color:#FFD37C;font-weight:700;margin-bottom:4px">&#9888; DEFAULT PASSWORD IN USE</div>
<div style="color:#aaa;font-size:.8em;margin-bottom:8px">Controls are locked (run, calibrate, OTA, etc.) until you set a real web password.</div>
<button class="btn btn-blue btn-sm" onclick="showPage('pageWifi')">Set Password</button>
</div>

<!-- ==================== MAIN PAGE ==================== -->
<div id="pageMain" class="page active">

<!-- STATUS + COUNTER + RUN -->
<div class="sec">
<div class="row ctr" style="gap:6px;margin-bottom:6px">
<span id="cb" class="badge warn">NOT CALIBRATED</span>
<span id="sb" class="badge ok">IDLE</span></div>
<div class="counter" id="ctr">0</div>
<button class="btn btn-run" id="br" onclick="toggleRun()">RUN</button>
<div class="jam-alert" id="jamAlert">
<div style="color:#FF4444;font-weight:700;margin-bottom:4px" id="jamTitle">&#9888; JAM DETECTED</div>
<div style="color:#aaa;font-size:.8em;margin-bottom:8px" id="jamMsg">Motor stalled and backed off.</div>
<button class="btn btn-blue" onclick="doAct('return_home')" id="bh">Return Home</button>
</div>
<div class="row ctr" style="margin-top:10px;gap:8px">
<button class="btn btn-dark" onclick="doAct('calibrate')" id="bc">Calibrate</button>
<button class="btn btn-red" onclick="doAct('reset_counter')">Reset Counter</button></div>
<hr>
<h2 style="margin-top:8px">Batch Run</h2>
<div class="sr"><span class="l">Target</span><span class="v" id="btv">OFF</span></div>
<div id="btStatus" class="hint"></div>
<div class="ea">
<button class="btn btn-dark btn-sm" onclick="setBatch(-100)">-100</button>
<button class="btn btn-dark btn-sm" onclick="setBatch(-10)">-10</button>
<button class="btn btn-dark btn-sm" onclick="setBatch(-1)">-1</button>
<button class="btn btn-blue btn-sm" onclick="setBatch(1)">+1</button>
<button class="btn btn-blue btn-sm" onclick="setBatch(10)">+10</button>
<button class="btn btn-blue btn-sm" onclick="setBatch(100)">+100</button></div>
<div class="row ctr" style="margin-top:8px;gap:6px">
<button class="btn btn-blue btn-sm" onclick="doBatch('start')" id="bbStart">Start Batch</button>
<button class="btn btn-dark btn-sm" onclick="doBatch('clear')">Clear</button></div>
</div>

<!-- SPEED PROFILE -->
<div class="sec">
<h2>Speed Profile</h2>
<div class="row ctr" id="profileRow" style="gap:6px;margin-bottom:8px"></div>
<div class="sr"><span class="l">Active</span><span class="v" id="sv">-</span></div>
</div>

<!-- NAV FOOTER -->
<div class="nav-footer">
<a onclick="showPage('pageConfig')">Configuration</a>
<a onclick="showPage('pageLog')">Log</a>
<a onclick="showPage('pageFW')">Firmware</a>
<a onclick="showPage('pageWifi')">WiFi</a>
</div>

</div><!-- /pageMain -->

<!-- ==================== CONFIGURATION PAGE ==================== -->
<div id="pageConfig" class="page">
<a class="back-link" onclick="showPage('pageMain')">&#8592; Back</a>

<!-- MOTOR CURRENT -->
<div class="sec">
<h2>Motor Current</h2>
<div class="slider-row">
<input type="range" id="mcSlider" min="1000" max="4500" step="100" value="2500" oninput="setCurrent(this.value)">
<span id="mcv">2500</span>
</div>
<div class="hint">Run current in mA (1000–4500). Higher = more torque, more heat.</div>
<div id="mcWarn" style="display:none;background:#3A2B12;border-radius:6px;padding:6px 10px;margin-top:6px;font-size:.8em;color:#FFD37C">&#9888; Above 4000 mA exceeds motor rating. Ensure adequate cooling.</div>
</div>

<!-- ENDPOINT TUNING -->
<div class="sec">
<h2>Endpoint Tuning</h2>
<div class="sr"><span class="l">Position</span><span class="v" id="cp">-</span></div>
<div class="sr"><span class="l">Effective UP</span><span class="v" id="eu">-</span></div>
<div class="sr"><span class="l">Effective DOWN</span><span class="v" id="ed">-</span></div>
<details><summary>Endpoint offsets</summary>
<div class="sr"><span class="l">RAW UP</span><span class="v" id="ru">-</span></div>
<div class="sr"><span class="l">RAW DOWN</span><span class="v" id="rd">-</span></div>
<div class="sr"><span class="l">RAW TRAVEL</span><span class="v" id="rt">-</span></div>
<hr>
<div class="hint">UP offset</div>
<div class="ea">
<button class="btn btn-dark btn-sm" onclick="adj('up',-100)">-100</button>
<button class="btn btn-dark btn-sm" onclick="adj('up',-10)">-10</button>
<button class="btn btn-dark btn-sm" onclick="adj('up',-1)">-1</button>
<button class="btn btn-blue btn-sm" onclick="adj('up',1)">+1</button>
<button class="btn btn-blue btn-sm" onclick="adj('up',10)">+10</button>
<button class="btn btn-blue btn-sm" onclick="adj('up',100)">+100</button></div>
<div class="hint" style="margin-top:8px">DOWN offset</div>
<div class="ea">
<button class="btn btn-dark btn-sm" onclick="adj('down',-100)">-100</button>
<button class="btn btn-dark btn-sm" onclick="adj('down',-10)">-10</button>
<button class="btn btn-dark btn-sm" onclick="adj('down',-1)">-1</button>
<button class="btn btn-blue btn-sm" onclick="adj('down',1)">+1</button>
<button class="btn btn-blue btn-sm" onclick="adj('down',10)">+10</button>
<button class="btn btn-blue btn-sm" onclick="adj('down',100)">+100</button></div>
</details>
</div>

<!-- STALL GUARD + WORK ZONE -->
<div class="sec">
<h2>Stall Guard (per profile)</h2>
<div id="sgProfiles"></div>
<hr>
<div class="sr"><span class="l">Work Zone (steps)</span><span class="v" id="wzv">5500</span></div>
<div class="hint">Skip SG near DOWN endpoint (primer push area)</div>
<div class="ea">
<button class="btn btn-dark btn-sm" onclick="setWz(-500)">-500</button>
<button class="btn btn-dark btn-sm" onclick="setWz(-100)">-100</button>
<button class="btn btn-blue btn-sm" onclick="setWz(100)">+100</button>
<button class="btn btn-blue btn-sm" onclick="setWz(500)">+500</button></div>
</div>

<!-- RESET CALIBRATION -->
<div class="sec">
<h2>Reset Calibration</h2>
<div class="hint" style="margin-bottom:8px">Discards the stored endpoint calibration and all tuning above (offsets, StallGuard trips, motor current, work zone, speed profile, counter) and restores the factory defaults. WiFi and the web password are not affected. Only works while the press is idle; recalibrate before running.</div>
<button class="btn btn-red btn-sm" onclick="resetSettings()" style="width:100%">Reset Calibration &amp; Tuning</button>
</div>

<!-- NAV FOOTER -->
<div class="nav-footer">
<a onclick="showPage('pageMain')">Main</a>
<a onclick="showPage('pageLog')">Log</a>
<a onclick="showPage('pageFW')">Firmware</a>
<a onclick="showPage('pageWifi')">WiFi</a>
</div>

</div><!-- /pageConfig -->

<!-- ==================== LOG PAGE ==================== -->
<div id="pageLog" class="page">
<a class="back-link" onclick="showPage('pageMain')">&#8592; Back</a>

<div class="sec">
<h2>Log</h2>
<div class="log" id="logBox"></div>
<button class="btn btn-dark btn-sm" onclick="document.getElementById('logBox').textContent='';fetch('/api/v1/log_clear',{method:'POST'})" style="margin-top:8px;width:100%">Clear Log</button>
</div>

<!-- NAV FOOTER -->
<div class="nav-footer">
<a onclick="showPage('pageMain')">Main</a>
<a onclick="showPage('pageConfig')">Configuration</a>
<a onclick="showPage('pageFW')">Firmware</a>
<a onclick="showPage('pageWifi')">WiFi</a>
</div>

</div><!-- /pageLog -->

<!-- ==================== FIRMWARE PAGE ==================== -->
<div id="pageFW" class="page">
<a class="back-link" onclick="showPage('pageMain')">&#8592; Back</a>

<div class="sec">
<h2>Firmware Update (OTA)</h2>
<div class="upload" id="ua" onclick="document.getElementById('fw').click()">
Tap to select .bin<br><span style="font-size:.8em">or drag &amp; drop</span></div>
<input type="file" id="fw" accept=".bin" style="display:none" onchange="upFW(this.files[0])">
<div class="pbar" id="pb"><div class="fill" id="pf"></div></div>
<div id="otaS"></div>
<a class="btn btn-dark btn-sm" href="/api/v1/coredump" style="display:block;margin-top:10px;text-align:center;text-decoration:none">Download Core Dump</a>
</div>

<!-- NAV FOOTER -->
<div class="nav-footer">
<a onclick="showPage('pageMain')">Main</a>
<a onclick="showPage('pageConfig')">Configuration</a>
<a onclick="showPage('pageLog')">Log</a>
<a onclick="showPage('pageWifi')">WiFi</a>
</div>

</div><!-- /pageFW -->

<!-- ==================== WIFI PAGE ==================== -->
<div id="pageWifi" class="page">
<a class="back-link" onclick="showPage('pageMain')">&#8592; Back</a>

<div class="sec">
<h2>Connection</h2>
<div class="sr"><span class="l">Status</span><span class="v" id="wfStatus">—</span></div>
<div class="sr"><span class="l">SSID</span><span class="v" id="wfSSID">—</span></div>
<div class="sr"><span class="l">IP Address</span><span class="v" id="wfIP">—</span></div>
</div>

<div class="sec">
<h2>WiFi Settings</h2>
<div class="hint" style="margin-bottom:8px">Change WiFi credentials (reboot required)</div>
<input type="text" id="ns" placeholder="SSID">
<input type="password" id="np" placeholder="Password">
<div class="row" style="gap:6px">
<button class="btn btn-blue btn-sm" onclick="saveWifi()" style="flex:1">Save &amp; Reboot</button>
<button class="btn btn-red btn-sm" onclick="resetWifi()" style="flex:1">Reset WiFi</button></div>
</div>

<div class="sec">
<h2>Web Password</h2>
<div class="hint" style="margin-bottom:8px">Required for every control action and firmware upload (user: <b>autolee</b>). The factory default is public &mdash; until you change it, the firmware refuses to run, calibrate or accept a firmware upload at all.</div>
<input type="password" id="wpNew" placeholder="New password">
<button class="btn btn-blue btn-sm" onclick="saveWebPassword()" style="width:100%">Change Password</button>
<div id="wpMsg" class="hint" style="margin-top:6px"></div>
</div>

<!-- NAV FOOTER -->
<div class="nav-footer">
<a onclick="showPage('pageMain')">Main</a>
<a onclick="showPage('pageConfig')">Configuration</a>
<a onclick="showPage('pageLog')">Log</a>
<a onclick="showPage('pageFW')">Firmware</a>
</div>

</div><!-- /pageWifi -->

</div>

<script>
let es;
function sse(){es=new EventSource('/api/v1/events');es.onmessage=e=>{try{upd(JSON.parse(e.data))}catch(x){}};
es.addEventListener('log',e=>{try{const d=JSON.parse(e.data);const lb=document.getElementById('logBox');lb.textContent+=d.log.join('\n')+'\n';lb.scrollTop=lb.scrollHeight}catch(x){}});
es.onerror=()=>{es.close();setTimeout(sse,3000)}}
sse();

function showPage(id){
  document.querySelectorAll('.page').forEach(p=>p.classList.remove('active'));
  document.getElementById(id).classList.add('active');
  window.scrollTo(0,0);
}


let profBuilt=false;
function buildProfileBtns(profiles,activeIdx){
  const row=document.getElementById('profileRow');
  if(!row)return;
  if(profBuilt){
    // Just update active highlight
    profiles.forEach((p,i)=>{
      const b=document.getElementById('profBtn'+i);
      if(b) b.className='btn '+(i===activeIdx?'btn-blue':'btn-dark')+' btn-sm';
    });
    return;
  }
  row.innerHTML='';
  profiles.forEach((p,i)=>{
    const b=document.createElement('button');
    b.id='profBtn'+i;
    b.className='btn '+(i===activeIdx?'btn-blue':'btn-dark')+' btn-sm';
    b.style.cssText='flex:1;padding:10px 4px';
    b.innerHTML=p.name+'<br><span style="font-size:.75em;opacity:.7">'+Math.round(p.hz/1000)+'kHz</span>';
    b.onclick=()=>setProfile(i);
    row.appendChild(b);
  });
  profBuilt=true;
}

let sgBuilt=false;
function buildSgControls(profiles,activeIdx){
  const c=document.getElementById('sgProfiles');
  if(!c)return;
  // Skip full rebuild if already built and an input is focused
  if(sgBuilt){
    // Just update the display values and highlights
    profiles.forEach((p,i)=>{
      const isActive=i===activeIdx;
      const lbl=document.getElementById('sgLbl'+i);
      const inp=document.getElementById('sgIn'+i);
      const row=document.getElementById('sgRow'+i);
      if(lbl) lbl.innerHTML=p.name+' ('+Math.round(p.hz/1000)+'kHz) <span style="color:'+(isActive?'var(--green)':'var(--muted)')+'">SG='+p.sg+'</span>';
      if(row) row.style.background=isActive?'#1a2a3a':'#161616';
      // Only update input if it's not focused (user might be typing)
      if(inp && document.activeElement!==inp) inp.value=p.sg;
    });
    return;
  }
  // First build
  c.innerHTML='';
  profiles.forEach((p,i)=>{
    const isActive=i===activeIdx;
    const div=document.createElement('div');
    div.id='sgRow'+i;
    div.style.cssText='margin-bottom:8px;padding:6px 8px;border-radius:8px;background:'+(isActive?'#1a2a3a':'#161616');
    div.innerHTML='<div class="sr" id="sgLbl'+i+'" style="margin-bottom:4px">'+p.name+' ('+Math.round(p.hz/1000)+'kHz) <span style="color:'+(isActive?'var(--green)':'var(--muted)')+'">SG='+p.sg+'</span></div>'
      +'<div style="display:flex;align-items:center;gap:8px">'
      +'<input type="text" inputmode="numeric" pattern="[0-9]*" id="sgIn'+i+'" value="'+p.sg+'" style="width:80px;padding:6px 8px;background:#222;border:1px solid #444;border-radius:6px;color:#fff;font-size:.9em;text-align:center" placeholder="0-500">'
      +'<button class="btn btn-blue btn-sm" id="sgBtn'+i+'">Set</button>'
      +'<span style="color:var(--dim);font-size:.7em">0 – 500</span></div>';
    c.appendChild(div);
    // Attach event listeners properly (not via inline onclick)
    document.getElementById('sgBtn'+i).addEventListener('click',function(){setSg(i)});
    document.getElementById('sgIn'+i).addEventListener('keydown',function(e){if(e.key==='Enter'){e.preventDefault();setSg(i)}});
    document.getElementById('sgIn'+i).addEventListener('blur',function(){setSg(i)});
  });
  sgBuilt=true;
}

function upd(d){
  // d.version is esp_app_desc_t.version (git describe) - bare, no "v" (release
  // tags are bare semver, e.g. "2.0.0", see CONTRIBUTING.md; untagged dev
  // builds report a bare commit hash). "v" is a display-only prefix, added
  // here rather than baked into the tag/version itself.
  if(d.version)document.getElementById('ver').textContent='v'+d.version;
  document.getElementById('pwAlert').style.display=d.defaultPassword?'block':'none';
  document.getElementById('ctr').textContent=d.counter;
  document.getElementById('sv').textContent=d.profileName+' \u2014 '+d.speed+'Hz (SG='+d.sgTrip+')';
    document.getElementById('cp').textContent=d.position;
    document.getElementById('wzv').textContent=d.workZone;
  if(d.wifiStatus){document.getElementById('wfStatus').textContent=d.wifiStatus;document.getElementById('wfSSID').textContent=d.wifiSSID;document.getElementById('wfIP').textContent=d.wifiIP}
  if(d.currentMa){document.getElementById('mcv').textContent=d.currentMa;document.getElementById('mcSlider').value=d.currentMa;document.getElementById('mcWarn').style.display=d.currentMa>4000?'block':'none'}
  if(d.profiles){buildProfileBtns(d.profiles,d.profileIdx);buildSgControls(d.profiles,d.profileIdx)}
  document.getElementById('btv').textContent=d.batchTarget>0?d.batchTarget:'OFF';
  const bts=document.getElementById('btStatus');
  const bbs=document.getElementById('bbStart');
  if(d.batchActive){bts.textContent='Running: '+d.batchCount+'/'+d.batchTarget;bts.style.color='var(--green)';bbs.disabled=true;bbs.textContent='Running...'}
  else if(d.batchTarget>0&&d.batchCount>=d.batchTarget){bts.textContent='Batch complete!';bts.style.color='var(--green)';bbs.disabled=false;bbs.textContent='Start Batch'}
  else{bts.textContent=d.batchTarget>0?'Ready':'Set a target count';bts.style.color='var(--muted)';bbs.disabled=d.batchTarget<=0;bbs.textContent='Start Batch'}
  const cb=document.getElementById('cb');
  cb.textContent=d.calibrated?'CALIBRATED':'NOT CALIBRATED';
  cb.className='badge '+(d.calibrated?'ok':'warn');
  const sb=document.getElementById('sb');
  sb.textContent=d.state;
  sb.className='badge '+(d.state==='RUNNING'?'run':d.state==='CALIBRATING'?'warn':d.state==='STALLED'||d.state==='HOMING'?'stall':'ok');
  const ja=document.getElementById('jamAlert'),jt=document.getElementById('jamTitle'),jm=document.getElementById('jamMsg'),bh=document.getElementById('bh');
  if(d.state==='STALLED'){jt.textContent='\u26A0 JAM DETECTED';jm.textContent='Motor stalled and backed off.';ja.style.display='block';bh.disabled=false;bh.textContent='Return Home'}
  else if(d.state==='HOMING'){ja.style.display='block';bh.disabled=true;bh.textContent='Returning...'}
  else if(d.positionStale&&d.calibrated){jt.textContent='\u26A0 POSITION UNCONFIRMED';jm.textContent='Calibration restored after a reboot - return home to re-reference the axis before running.';ja.style.display='block';bh.disabled=false;bh.textContent='Return Home'}
  else{ja.style.display='none'}
  const br=document.getElementById('br');
  if(d.state==='RUNNING'){br.textContent='STOP';br.classList.add('active')}
  else{br.textContent='RUN';br.classList.remove('active')}
  const bc=document.getElementById('bc');
  bc.disabled=d.state==='CALIBRATING';
  bc.textContent=d.state==='CALIBRATING'?'Calibrating...':'Calibrate';
  if(d.calibrated){
    document.getElementById('ru').textContent=d.rawUp;
    document.getElementById('rd').textContent=d.rawDown;
    document.getElementById('rt').textContent=d.rawDown-d.rawUp;
    document.getElementById('eu').textContent=d.endpointUp+' (off '+(d.upOffset>=0?'+':'')+d.upOffset+')';
    document.getElementById('ed').textContent=d.endpointDown+' (off '+(d.downOffset>=0?'+':'')+d.downOffset+')';
  }else{['ru','rd','rt','eu','ed'].forEach(i=>document.getElementById(i).textContent='-')}
}

function toggleRun(){fetch('/api/v1/toggle_run',{method:'POST'})}
function setSg(p){const v=parseInt(document.getElementById('sgIn'+p).value)||0;fetch('/api/v1/sg_trip?profile='+p+'&value='+v,{method:'POST'})}
function setWz(d){fetch('/api/v1/work_zone?delta='+d,{method:'POST'})}
function setBatch(d){fetch('/api/v1/batch?delta='+d,{method:'POST'})}
function doBatch(a){fetch('/api/v1/batch?action='+a,{method:'POST'})}
function setProfile(i){fetch('/api/v1/profile?idx='+i,{method:'POST'})}
function setCurrent(v){document.getElementById('mcv').textContent=v;document.getElementById('mcWarn').style.display=v>4000?'block':'none';fetch('/api/v1/current?ma='+v,{method:'POST'})}
function adj(w,d){fetch('/api/v1/endpoint?which='+w+'&delta='+d,{method:'POST'})}
function doAct(a){fetch('/api/v1/action?do='+a,{method:'POST'})}
function resetSettings(){
  if(!confirm('Discard the stored calibration and all tuning, and restore factory defaults?\n\nThe press will be UNCALIBRATED and must be recalibrated before it can run. WiFi and the web password are not affected.'))return;
  doAct('reset_settings')}
function saveWifi(){
  const s=document.getElementById('ns').value,p=document.getElementById('np').value;
  if(!s){alert('SSID required');return}
  fetch('/api/v1/wifi?ssid='+encodeURIComponent(s)+'&pass='+encodeURIComponent(p),{method:'POST'})
  .then(()=>{alert('Saved! Rebooting...');setTimeout(()=>location.reload(),5000)})}
function resetWifi(){
  if(!confirm('Clear saved WiFi credentials and reboot into setup mode?'))return;
  fetch('/api/v1/wifi_reset',{method:'POST'})
  .then(()=>{alert('WiFi cleared! Rebooting into setup mode...');setTimeout(()=>location.reload(),5000)})}
function saveWebPassword(){
  const p=document.getElementById('wpNew').value,m=document.getElementById('wpMsg');
  if(!p){m.textContent='Enter a new password.';m.style.color='#FF4444';return}
  fetch('/api/v1/web_password?pass='+encodeURIComponent(p),{method:'POST'})
  .then(r=>{if(r.ok){document.getElementById('wpNew').value='';m.style.color='#00FF00';
    m.textContent='Password changed. The browser will ask for it on your next action.'}
    else{m.style.color='#FF4444';r.text().then(t=>m.textContent='Failed: '+t)}})
  .catch(()=>{m.style.color='#FF4444';m.textContent='Request failed.'})}

function upFW(f){
  if(!f)return;
  const ua=document.getElementById('ua'),pb=document.getElementById('pb'),pf=document.getElementById('pf'),st=document.getElementById('otaS');
  ua.classList.add('on');ua.textContent=f.name;pb.style.display='block';pf.style.width='0%';
  st.textContent='Uploading...';st.style.color='#888';
  const x=new XMLHttpRequest();x.open('POST','/api/v1/ota');
  x.upload.onprogress=e=>{if(e.lengthComputable){const p=Math.round(e.loaded/e.total*100);pf.style.width=p+'%';st.textContent='Uploading... '+p+'%'}};
  x.onload=()=>{if(x.status===200){pf.style.width='100%';st.textContent='Success! Rebooting...';st.style.color='#00FF00';setTimeout(()=>location.reload(),5000)}
  else{st.textContent='Error: '+x.responseText;st.style.color='#FF4444'}};
  x.onerror=()=>{st.textContent='Upload failed';st.style.color='#FF4444'};
  const fd=new FormData();fd.append('firmware',f);x.send(fd)}

const uae=document.getElementById('ua');
uae.addEventListener('dragover',e=>{e.preventDefault();uae.classList.add('on')});
uae.addEventListener('dragleave',()=>uae.classList.remove('on'));
uae.addEventListener('drop',e=>{e.preventDefault();upFW(e.dataTransfer.files[0])});
fetch('/api/v1/state').then(r=>r.json()).then(upd).catch(()=>{});
</script></body></html>
)rawliteral";
