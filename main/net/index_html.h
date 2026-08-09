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
.conn-alert{display:none;background:#2a1111;border:1px solid #442222;border-radius:10px;padding:12px;margin-bottom:10px;text-align:center}
/* Applied to .wrap while the event stream is not live. Dims every value that
   comes from the device so a frozen page cannot be mistaken for a live one -
   the banner alone is easy to scroll past, the greyed-out numbers are not. */
.stale .counter,.stale .badge,.stale .sr .v,.stale #logBox{opacity:.35}
input[type=text],input[type=password],select{width:100%;padding:10px;margin-bottom:6px;background:var(--card);border:1px solid var(--border);border-radius:8px;color:#fff;font-size:.9em}
.upload{border:2px dashed #444;border-radius:8px;padding:16px;text-align:center;color:var(--muted);cursor:pointer;font-size:.85em}
.upload:hover{border-color:var(--accent)}.upload.on{border-color:var(--green);color:var(--green)}
.pbar{width:100%;height:5px;background:#333;border-radius:3px;margin-top:6px;overflow:hidden;display:none}
.pbar .fill{height:100%;background:var(--accent);width:0%;transition:width .3s;border-radius:3px}
#otaS{font-size:.8em;margin-top:4px;min-height:1em}
.log{background:#000;border-radius:8px;padding:8px;font-family:'Courier New',monospace;font-size:.7em;color:#0f0;height:400px;overflow-y:auto}
.log .ll{white-space:pre-wrap;word-break:break-all}
.log .ll-D{color:var(--muted)}.log .ll-W{color:#FFD37C}.log .ll-E{color:#FF6666}
.page{display:none}.page.active{display:block}
.nav-footer{margin-top:16px;padding:14px 0;text-align:center;border-top:1px solid var(--border)}
.nav-footer .nav-row{margin-bottom:8px}.nav-footer .nav-row:last-child{margin-bottom:0}
/* Real <button>s styled as links, so the nav is in the tab order and answers Enter/Space. */
.nav-footer button{background:none;border:none;padding:0;font-family:inherit;color:var(--accent);font-size:.85em;font-weight:600;margin:0 10px;cursor:pointer}
.nav-footer button:hover{opacity:.7}
.nav-footer button.active{color:var(--green)}
.back-link{display:inline-block;background:none;border:none;padding:0;font-family:inherit;color:var(--accent);font-size:.85em;font-weight:600;cursor:pointer;margin-bottom:12px}
.back-link:hover{opacity:.7}
/* Refusal toast. Fixed to the viewport, not the page: the control that was
   refused may be anywhere on a long page, and the message has to be seen. */
#toast{position:fixed;left:50%;bottom:20px;transform:translateX(-50%);width:max-content;max-width:min(92vw,420px);background:#42160f;border:1px solid #7a2b1d;color:#ffd9d2;padding:10px 14px;border-radius:10px;font-size:.85em;font-weight:600;text-align:center;opacity:0;pointer-events:none;transition:opacity .2s;z-index:20}
#toast.on{opacity:1}
</style></head><body>
<div class="wrap">

<div id="toast" role="status" aria-live="polite"></div>

<h1>AutoLee</h1>
<div class="sub">by K.L Design · <span id="ver"></span></div>

<!-- Shown on every page (not just Main) while the device is on a real network
     with the web password still at the factory default: the firmware refuses
     every control action with a 403 until it's changed - see the
     "defaultPassword" state field, which stays false in standalone AP mode
     where no password is required at all. -->
<div class="pw-alert" id="pwAlert">
<div style="color:#FFD37C;font-weight:700;margin-bottom:4px">&#9888; DEFAULT PASSWORD IN USE</div>
<div style="color:#aaa;font-size:.8em;margin-bottom:8px">Controls are locked (run, calibrate, OTA, etc.) until you set a real web password.</div>
<button class="btn btn-blue btn-sm" onclick="showPage('pageWifi')">Set Password</button>
</div>

<!-- Shown whenever the active profile's StallGuard trip is 0, which switches
     runtime jam detection off completely. The setting is persisted, so without
     this the machine can come back from a reboot with no stall protection and
     nothing on screen saying so. -->
<div class="pw-alert" id="sgOffAlert" style="display:none">
<div style="color:#FFD37C;font-weight:700;margin-bottom:4px">&#9888; JAM DETECTION DISABLED</div>
<div style="color:#aaa;font-size:.8em">This profile's StallGuard trip is 0, so runs will not stop on a jam. Raise SG on the Config page to re-enable it.</div>
</div>

<!-- Shown whenever the SSE stream is not delivering. Everything on this page
     except the Diag block is rendered from that stream, so without this the
     dashboard goes on displaying the last values it happened to receive - a
     press that has since jammed still reads IDLE, and the counter sits still.
     See the liveness watchdog in the script for why onerror alone is not
     enough to detect this. -->
<div class="conn-alert" id="connAlert">
<div style="color:#FF4444;font-weight:700;margin-bottom:4px">&#9888; CONNECTION LOST</div>
<div style="color:#aaa;font-size:.8em">These values are frozen at the last update and are <b>not live</b>. Reconnecting&hellip;</div>
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
<button class="btn btn-blue" onclick="jamAction()" id="bh">Return Home</button>
</div>
<div class="row ctr" style="margin-top:10px;gap:8px">
<button class="btn btn-dark" id="bc" onclick="doAct('/api/v1/motion/calibrate')">Calibrate</button>
<button class="btn btn-red" onclick="doAct('/api/v1/motion/reset_counter')">Reset Counter</button></div>
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

<!-- NAV FOOTER - filled by buildNavFooters() -->
<div class="nav-footer"></div>

</div><!-- /pageMain -->

<!-- ==================== CONFIGURATION PAGE ==================== -->
<div id="pageConfig" class="page">
<button class="back-link" onclick="showPage('pageMain')">&#8592; Back</button>

<!-- MOTOR CURRENT -->
<div class="sec">
<h2>Motor Current</h2>
<div class="slider-row">
<!-- oninput updates the label only; onchange (drag release, or one arrow
     keypress) is what commits. See showCurrent()/setCurrent(). -->
<input type="range" id="mcSlider" min="1000" max="4500" step="100" value="2500" oninput="showCurrent(this.value)" onchange="setCurrent(this.value)">
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

<!-- NAV FOOTER - filled by buildNavFooters() -->
<div class="nav-footer"></div>

</div><!-- /pageConfig -->

<!-- ==================== LOG PAGE ==================== -->
<div id="pageLog" class="page">
<button class="back-link" onclick="showPage('pageMain')">&#8592; Back</button>

<div class="sec">
<h2>Log</h2>
<div class="row ctr" id="logFilter" style="gap:6px;margin-bottom:8px">
<button class="btn btn-blue btn-sm" data-lvl="" onclick="setLogFilter('')">All</button>
<button class="btn btn-dark btn-sm" data-lvl="D" onclick="setLogFilter('D')">Debug</button>
<button class="btn btn-dark btn-sm" data-lvl="I" onclick="setLogFilter('I')">Info</button>
<button class="btn btn-dark btn-sm" data-lvl="W" onclick="setLogFilter('W')">Warn</button>
<button class="btn btn-dark btn-sm" data-lvl="E" onclick="setLogFilter('E')">Error</button>
</div>
<div class="log" id="logBox"></div>
<button class="btn btn-dark btn-sm" onclick="clearLog()" style="margin-top:8px;width:100%">Clear Log</button>
</div>

<div class="sec">
<h2>Server Verbosity</h2>
<div class="hint" style="margin-bottom:8px">Minimum level actually recorded - both here and on serial. Debug adds high-frequency StallGuard/calibration-search traces; only turn it on while troubleshooting.</div>
<div class="row ctr" id="logLevelCtl" style="gap:6px">
<button class="btn btn-dark btn-sm" data-lvl="debug" onclick="setServerLogLevel('debug')">Debug</button>
<button class="btn btn-dark btn-sm" data-lvl="info" onclick="setServerLogLevel('info')">Info</button>
<button class="btn btn-dark btn-sm" data-lvl="warn" onclick="setServerLogLevel('warn')">Warn</button>
<button class="btn btn-dark btn-sm" data-lvl="error" onclick="setServerLogLevel('error')">Error</button>
</div>
</div>

<!-- NAV FOOTER - filled by buildNavFooters() -->
<div class="nav-footer"></div>

</div><!-- /pageLog -->

<!-- ==================== FIRMWARE PAGE ==================== -->
<div id="pageFW" class="page">
<button class="back-link" onclick="showPage('pageMain')">&#8592; Back</button>

<div class="sec">
<h2>Firmware Update (OTA)</h2>
<div class="upload" id="ua" onclick="document.getElementById('fw').click()">
Tap to select .bin<br><span style="font-size:.8em">or drag &amp; drop</span></div>
<input type="file" id="fw" accept=".bin" style="display:none" onchange="upFW(this.files[0])">
<div class="pbar" id="pb"><div class="fill" id="pf"></div></div>
<div id="otaS"></div>
<a class="btn btn-dark btn-sm" href="/api/v1/diagnostics/coredump" style="display:block;margin-top:10px;text-align:center;text-decoration:none">Download Core Dump</a>
</div>

<!-- NAV FOOTER - filled by buildNavFooters() -->
<div class="nav-footer"></div>

</div><!-- /pageFW -->

<!-- ==================== WIFI PAGE ==================== -->
<div id="pageWifi" class="page">
<button class="back-link" onclick="showPage('pageMain')">&#8592; Back</button>

<div class="sec">
<h2>Connection</h2>
<div class="sr"><span class="l">Status</span><span class="v" id="wfStatus">—</span></div>
<div class="sr"><span class="l">SSID</span><span class="v" id="wfSSID">—</span></div>
<div class="sr"><span class="l">IP Address</span><span class="v" id="wfIP">—</span></div>
</div>

<div class="sec">
<h2>WiFi Settings</h2>
<div class="hint" style="margin-bottom:8px">Change WiFi credentials (applied live, no reboot)</div>
<div class="row" style="gap:6px;margin-bottom:6px">
<select id="nsList" style="flex:1" onchange="if(this.value)document.getElementById('ns').value=this.value"></select>
<button class="btn btn-dark btn-sm" onclick="loadWifiScan()" title="Rescan">&#8635;</button></div>
<input type="text" id="ns" placeholder="SSID (pick above or type)">
<div class="row" style="gap:6px;margin-bottom:6px">
<input type="password" id="np" autocomplete="off" placeholder="Password" style="flex:1;margin:0">
<button class="btn btn-dark btn-sm" onclick="togPw('np',this)" aria-label="Show password" title="Show/hide">&#128065;</button></div>
<div class="row" style="gap:6px">
<button class="btn btn-blue btn-sm" onclick="saveWifi()" style="flex:1">Save &amp; Connect</button>
<button class="btn btn-red btn-sm" onclick="resetWifi()" style="flex:1">Reset WiFi</button></div>
</div>

<div class="sec">
<h2>Web Password</h2>
<div class="hint" style="margin-bottom:8px">Required for every control action and firmware upload once AutoLee has joined a WiFi network for the first time (user: <b>autolee</b>; the factory-default password is <b>autolee</b> until you change it). Until then &mdash; including after Skip on the setup screen &mdash; no password is asked for at all: the AP key on the screen already means you are standing at the press. That factory default is public knowledge &mdash; so once you go on a network, until you change it, the firmware refuses to run, calibrate or accept a firmware upload at all. <b>Locked out?</b> Reset the password from the press itself: <b>Config &rarr; Reset Pwd</b> (two taps), which restores the factory default so you can set a new one here.</div>
<div class="row" style="gap:6px;margin-bottom:6px">
<input type="password" id="wpNew" autocomplete="new-password" placeholder="New password (at least 8 characters)" style="flex:1;margin:0">
<button class="btn btn-dark btn-sm" onclick="togPw('wpNew',this)" aria-label="Show password" title="Show/hide">&#128065;</button></div>
<div class="row" style="gap:6px;margin-bottom:6px">
<input type="password" id="wpNew2" autocomplete="new-password" placeholder="Repeat new password" style="flex:1;margin:0">
<button class="btn btn-dark btn-sm" onclick="togPw('wpNew2',this)" aria-label="Show password" title="Show/hide">&#128065;</button></div>
<button class="btn btn-blue btn-sm" onclick="saveWebPassword()" style="width:100%">Change Password</button>
<div id="wpMsg" class="hint" style="margin-top:6px"></div>
</div>

<!-- NAV FOOTER - filled by buildNavFooters() -->
<div class="nav-footer"></div>

</div><!-- /pageWifi -->

<!-- ==================== DIAGNOSTICS PAGE ==================== -->
<div id="pageDiag" class="page">
<button class="back-link" onclick="showPage('pageMain')">&#8592; Back</button>

<div class="sec">
<h2>System</h2>
<div class="sr"><span class="l">Version</span><span class="v" id="dgVer">-</span></div>
<div class="sr"><span class="l">IDF Version</span><span class="v" id="dgIdf">-</span></div>
<div class="sr"><span class="l">Built</span><span class="v" id="dgBuilt">-</span></div>
<div class="sr"><span class="l">ELF SHA256</span><span class="v" id="dgSha">-</span></div>
<div class="sr"><span class="l">Running Partition</span><span class="v" id="dgPart">-</span></div>
<div class="sr"><span class="l">Uptime</span><span class="v" id="dgUptime">-</span></div>
<div class="sr"><span class="l">Last Reset Reason</span><span class="v" id="dgReset">-</span></div>
<div class="sr"><span class="l">Coredump Waiting</span><span class="v" id="dgCoredump">-</span></div>
</div>

<div class="sec">
<h2>Resources</h2>
<div class="sr"><span class="l">Free Heap</span><span class="v" id="dgHeap">-</span></div>
<div class="sr"><span class="l">Min Free Heap</span><span class="v" id="dgMinHeap">-</span></div>
<div class="sr"><span class="l">Largest Free Block</span><span class="v" id="dgLargestBlock">-</span></div>
<div class="sr"><span class="l">Flash Size</span><span class="v" id="dgFlash">-</span></div>
<div class="sr"><span class="l">CPU Freq</span><span class="v" id="dgCpu">-</span></div>
<div class="sr"><span class="l">WiFi RSSI</span><span class="v" id="dgRssi">-</span></div>
<div class="sr"><span class="l">Pump Task Stack HWM</span><span class="v" id="dgStack">-</span></div>
<div class="sr"><span class="l">Settings Version</span><span class="v" id="dgSettingsVer">-</span></div>
</div>

<div class="sec">
<h2>Lifetime Health</h2>
<!-- "Total Cycles", not "Cycle Count": this is health.cycleCount, the lifetime
     stroke count, and it is deliberately NOT the piece counter on the Main page
     (that one caps at 9999 and Reset Counter zeroes it). Two different numbers
     both labelled "count" is what made the old reading look broken. -->
<div class="sr"><span class="l">Total Cycles (lifetime)</span><span class="v" id="dgCycles">-</span></div>
<div class="sr"><span class="l">Stall Count</span><span class="v" id="dgStalls">-</span></div>
<div class="sr"><span class="l">Avg Cycle Time</span><span class="v" id="dgAvgCycle">-</span></div>
<div class="sr"><span class="l">Longest Cycle</span><span class="v" id="dgLongestCycle">-</span></div>
<div class="sr"><span class="l">Calibration Count</span><span class="v" id="dgCalCount">-</span></div>
<div class="sr"><span class="l">OTA Count</span><span class="v" id="dgOtaCount">-</span></div>
<div class="sr"><span class="l">Reset Count</span><span class="v" id="dgResetCount">-</span></div>
</div>

<button class="btn btn-dark btn-sm" onclick="loadDiag()" style="width:100%">Refresh</button>

<!-- NAV FOOTER - filled by buildNavFooters() -->
<div class="nav-footer"></div>

</div><!-- /pageDiag -->

</div>

<script>
// ---- SSE liveness ---------------------------------------------------------
// broadcastState() sends a full state message only when the state actually
// changed, plus a "heartbeat" event every SSE_HEARTBEAT_MS (8 s, config.h) when
// it did not - so on an idle machine silence is normal and both events have to
// count as signs of life.
//
// es.onerror is not sufficient on its own: it fires on a socket that resets, but
// a connection that stops delivering without resetting (WiFi roam, NAT/proxy
// idle timeout, the device rebooting behind a stuck TCP session) never raises
// it. That is the case that mattered - the page kept rendering the last state it
// had received, indistinguishable from live, so a press that had since jammed
// still read IDLE.
const SSE_STALE_MS=20000;  // > 2x SSE_HEARTBEAT_MS: one dropped heartbeat is
                           // not a dropout, two in a row is.
let es,lastSignalMs=Date.now(),connLive=true;
function renderConn(){
  document.getElementById('connAlert').style.display=connLive?'none':'block';
  document.querySelector('.wrap').classList.toggle('stale',!connLive);
}
function markSignal(){lastSignalMs=Date.now();if(!connLive){connLive=true;renderConn()}}
function markDead(){if(connLive){connLive=false;renderConn()}}
setInterval(()=>{if(Date.now()-lastSignalMs>SSE_STALE_MS)markDead()},2000);

function sse(){es=new EventSource('/api/v1/events');
es.onopen=()=>{markSignal();
  // The stream pushes state on change only, so after a reconnect an idle
  // machine could stay silent for minutes. Pull the current state once so the
  // page is correct the moment the banner clears, not merely un-dimmed.
  fetch('/api/v1/state').then(r=>r.json()).then(upd).catch(()=>{})};
es.onmessage=e=>{markSignal();try{upd(JSON.parse(e.data))}catch(x){}};
es.addEventListener('heartbeat',markSignal);
es.addEventListener('log',e=>{markSignal();try{const d=JSON.parse(e.data);addLogLines(d.log)}catch(x){}});
es.onerror=()=>{es.close();markDead();setTimeout(sse,3000)}}
sse();

// Lines arrive pre-formatted by webLogLevel() (main/globals.cpp) as
//   "H:MM:SS.mmm L [Category] message"
// with L one of D/I/W/E - millisecond resolution and a category field, which
// lineLevel()'s regex below has to match exactly. Kept client-side (capped to
// match the firmware's own ring size) so the level filter can re-render without
// losing history the DOM already dropped.
const LOG_MAX_LINES=500;
let logLines=[],logFilter='';
function escHtml(s){return s.replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;')}
function lineLevel(l){const m=/^\d+:\d\d:\d\d\.\d\d\d ([DIWE]) /.exec(l);return m?m[1]:'I'}
function addLogLines(lines){
  for(const l of lines){if(l!==logLines[logLines.length-1])logLines.push(l)}
  if(logLines.length>LOG_MAX_LINES)logLines=logLines.slice(logLines.length-LOG_MAX_LINES);
  renderLog();
}
let logsLoaded=false;
function loadLogs(){
  // GET /api/v1/system/logs returns the full retained backlog - the SSE "log"
  // event only pushes what's new since the last broadcast (a single global
  // watermark), so a page opened after boot's first client would otherwise
  // never see the early lines. Only replaces on first open; afterwards SSE
  // keeps logLines current without needing another full re-fetch.
  if(logsLoaded)return;
  fetch('/api/v1/system/logs').then(r=>r.json()).then(d=>{logsLoaded=true;logLines=d.logs||[];renderLog()}).catch(()=>{});
}
function renderLog(){
  const lb=document.getElementById('logBox');
  const shown=logFilter?logLines.filter(l=>lineLevel(l)===logFilter):logLines;
  lb.innerHTML=shown.map(l=>'<div class="ll ll-'+lineLevel(l)+'">'+escHtml(l)+'</div>').join('');
  lb.scrollTop=lb.scrollHeight;
}
function setLogFilter(lvl){
  logFilter=lvl;
  document.querySelectorAll('#logFilter .btn').forEach(b=>b.className='btn btn-sm '+(b.dataset.lvl===lvl?'btn-blue':'btn-dark'));
  renderLog();
}
function clearLog(){
  logLines=[];
  renderLog();
  ctl('/api/v1/system/logs',{method:'DELETE'});
}

function loadWifiScan(){
  const s=document.getElementById('nsList');
  s.innerHTML='';
  const scanning=document.createElement('option');scanning.value='';scanning.textContent='Scanning…';
  s.appendChild(scanning);
  fetch('/api/v1/wifi/scan').then(r=>r.json()).then(list=>{
    s.innerHTML='';
    const def=document.createElement('option');def.value='';
    def.textContent=list.length?'-- available networks --':'No networks found';
    s.appendChild(def);
    // DOM building, not string concatenation: an SSID is attacker-chosen text
    // off the air and must never be interpreted as markup.
    list.forEach(n=>{
      const o=document.createElement('option');
      o.value=n.ssid;
      o.textContent=n.ssid+' ('+n.rssi+' dBm'+(n.secure?'':', OPEN')+')';
      s.appendChild(o);});
  }).catch(()=>{s.innerHTML='';const e=document.createElement('option');e.value='';e.textContent='Scan failed - retry ↻';s.appendChild(e)});
}
function loadServerLogLevel(){
  fetch('/api/v1/system/log_level').then(r=>r.json()).then(d=>{
    document.querySelectorAll('#logLevelCtl .btn').forEach(b=>b.className='btn btn-sm '+(b.dataset.lvl===d.level?'btn-blue':'btn-dark'));
  }).catch(()=>{});
}
function setServerLogLevel(level){
  ctl('/api/v1/system/log_level?level='+level,{method:'PUT'}).then(loadServerLogLevel);
}

function showPage(id){
  document.querySelectorAll('.page').forEach(p=>p.classList.remove('active'));
  document.getElementById(id).classList.add('active');
  document.querySelectorAll('.nav-footer button').forEach(b=>b.classList.toggle('active',b.dataset.page===id));
  window.scrollTo(0,0);
  if(id==='pageDiag')loadDiag();
  if(id==='pageWifi')loadWifiScan();
  if(id==='pageLog'){loadLogs();loadServerLogLevel()}
}

// ---- nav footer -----------------------------------------------------------
// Built once from NAV_ROWS into every page's empty .nav-footer div, so the
// page list lives in exactly one place.
const NAV_ROWS=[[['pageMain','Main'],['pageConfig','Configuration'],['pageWifi','WiFi']],
                [['pageLog','Log'],['pageDiag','Diag'],['pageFW','Firmware']]];
function buildNavFooters(){
  document.querySelectorAll('.nav-footer').forEach(f=>{
    NAV_ROWS.forEach(row=>{
      const div=document.createElement('div');
      div.className='nav-row';
      row.forEach(([id,label])=>{
        const b=document.createElement('button');
        b.dataset.page=id;
        b.textContent=label;
        b.onclick=()=>showPage(id);
        b.classList.toggle('active',id==='pageMain');
        div.appendChild(b);
      });
      f.appendChild(div);
    });
  });
}
buildNavFooters();

function fmtMs(ms){
  if(ms<1000)return ms+'ms';
  const s=Math.floor(ms/1000);
  if(s<60)return s+'s';
  const m=Math.floor(s/60);
  if(m<60)return m+'m '+(s%60)+'s';
  const h=Math.floor(m/60);
  return h+'h '+(m%60)+'m';
}

function loadDiag(){
  fetch('/api/v1/diagnostics/info').then(r=>r.json()).then(d=>{
    document.getElementById('dgVer').textContent=d.version;
    document.getElementById('dgIdf').textContent=d.idfVersion;
    document.getElementById('dgBuilt').textContent=d.compileDate+' '+d.compileTime;
    document.getElementById('dgSha').textContent=d.elfSha256;
    document.getElementById('dgPart').textContent=d.runningPartition;
    document.getElementById('dgUptime').textContent=fmtMs(d.uptimeMs);
    document.getElementById('dgReset').textContent=d.resetReason;
    document.getElementById('dgCoredump').textContent=d.coredumpPresent?'Yes':'No';
    document.getElementById('dgHeap').textContent=(d.freeHeap/1024).toFixed(1)+' KB';
    document.getElementById('dgMinHeap').textContent=(d.minFreeHeap/1024).toFixed(1)+' KB';
    document.getElementById('dgLargestBlock').textContent=(d.largestFreeBlock/1024).toFixed(1)+' KB';
    document.getElementById('dgFlash').textContent=(d.flashSizeBytes/1048576).toFixed(0)+' MB';
    document.getElementById('dgCpu').textContent=d.cpuFreqMhz+' MHz';
    document.getElementById('dgRssi').textContent=d.wifiRssi===null?'-':d.wifiRssi+' dBm';
    document.getElementById('dgStack').textContent=d.pumpStackHighWaterMark+' B';
    document.getElementById('dgSettingsVer').textContent=d.settingsVersion;
    const h=d.health||{};
    document.getElementById('dgCycles').textContent=h.cycleCount;
    document.getElementById('dgStalls').textContent=h.stallCount;
    document.getElementById('dgAvgCycle').textContent=fmtMs(h.avgCycleMs);
    document.getElementById('dgLongestCycle').textContent=fmtMs(h.longestCycleMs);
    document.getElementById('dgCalCount').textContent=h.calibrationCount;
    document.getElementById('dgOtaCount').textContent=h.otaCount;
    document.getElementById('dgResetCount').textContent=h.resetCount;
  }).catch(()=>{});
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
// Mirrors RUN_SG_TRIP_MIN/MAX in main/config.h (and the "0 - 500" hint the rows
// render). sgLast[i] is the last value the firmware reported for profile i - the
// value an invalid entry is reverted to, and the one used to tell a real edit
// apart from a bare blur. See setSg().
const SG_MIN=0,SG_MAX=500;
let sgLast=[];
function buildSgControls(profiles,activeIdx){
  const c=document.getElementById('sgProfiles');
  if(!c)return;
  // Skip full rebuild if already built and an input is focused
  if(sgBuilt){
    // Just update the display values and highlights
    profiles.forEach((p,i)=>{
      sgLast[i]=p.sg;
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
    sgLast[i]=p.sg;
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
  // sgTrip 0 disables runtime jam detection entirely and persists across
  // reboots - surface it rather than leaving it as a "0" nobody reads.
  document.getElementById('sgOffAlert').style.display=d.sgTrip===0?'block':'none';
    document.getElementById('cp').textContent=d.position;
    document.getElementById('wzv').textContent=d.workZone;
  if(d.wifiStatus){document.getElementById('wfStatus').textContent=d.wifiStatus;document.getElementById('wfSSID').textContent=d.wifiSSID;document.getElementById('wfIP').textContent=d.wifiIP}
  // Don't fight a drag in progress: while the slider holds focus its thumb and
  // label are being driven by showCurrent(), and echoing the last *committed*
  // value back would snap both to where the drag started. Same guard the SG
  // inputs use below; it matters more now that a commit only happens on release.
  if(d.currentMa){const mcs=document.getElementById('mcSlider');
    if(document.activeElement!==mcs){mcs.value=d.currentMa;showCurrent(d.currentMa)}}
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
  // Same for the jam-recovery home: it is the same blocking search, so it gets
  // the same escape. Aborting leaves the press IDLE with the axis unreferenced,
  // so a run is still refused and Return Home is still offered - cancelling is
  // never a way past the recovery.
  else if(d.state==='HOMING'){ja.style.display='block';bh.disabled=false;bh.textContent='Cancel'}
  else if(d.positionStale&&d.calibrated){jt.textContent='\u26A0 POSITION UNCONFIRMED';jm.textContent='Calibration restored after a reboot - return home to re-reference the axis before running.';ja.style.display='block';bh.disabled=false;bh.textContent='Return Home'}
  else{ja.style.display='none'}
  const br=document.getElementById('br');
  if(d.state==='RUNNING'){br.textContent='STOP';br.classList.add('active')}
  else{br.textContent='RUN';br.classList.remove('active')}
  // A calibration blocks the motion task for tens of seconds and used to be
  // uninterruptible, so the button simply went dead and said "Calibrating...".
  // It is now the way out: same button, Cancel while the search runs.
  const bc=document.getElementById('bc');
  const calibrating=d.state==='CALIBRATING';
  bc.disabled=false;
  bc.textContent=calibrating?'Cancel Calibration':'Calibrate';
  bc.className='btn '+(calibrating?'btn-red':'btn-dark');
  bc.onclick=calibrating?()=>doAct('/api/v1/motion/abort')
                        :()=>doAct('/api/v1/motion/calibrate');
  if(d.calibrated){
    document.getElementById('ru').textContent=d.rawUp;
    document.getElementById('rd').textContent=d.rawDown;
    document.getElementById('rt').textContent=d.rawDown-d.rawUp;
    document.getElementById('eu').textContent=d.endpointUp+' (off '+(d.upOffset>=0?'+':'')+d.upOffset+')';
    document.getElementById('ed').textContent=d.endpointDown+' (off '+(d.downOffset>=0?'+':'')+d.downOffset+')';
  }else{['ru','rd','rt','eu','ed'].forEach(i=>document.getElementById(i).textContent='-')}
}

// ---- control POSTs --------------------------------------------------------
// Every control action goes through ctl(). The endpoints answer 400 with
// {error,message} for a malformed request and 409 for one the press cannot
// carry out from its current state (not calibrated, position unreferenced,
// wrong run state, no batch target); 403 is the factory-password gate. None of
// that used to reach the operator: these were bare fire-and-forget fetches with
// no .then and no .catch, and every route answered 200 regardless, so a refused
// command and an honoured one looked exactly the same - nothing moved, nothing
// said why.
//
// Resolves to true/false and never rejects, so callers can branch without
// needing their own error handling.
let toastT;
function toast(msg){
  const t=document.getElementById('toast');
  t.textContent=msg;
  t.classList.add('on');
  clearTimeout(toastT);
  toastT=setTimeout(()=>t.classList.remove('on'),5000);
}
function ctl(url,opts){
  return fetch(url,Object.assign({method:'POST'},opts||{})).then(r=>{
    if(r.ok)return true;
    return r.text().then(t=>{
      let m='';
      try{const j=JSON.parse(t);if(j&&j.message)m=j.message}catch(x){}
      toast(m||t||('HTTP '+r.status));
      return false;
    });
  }).catch(()=>{toast('AutoLee did not answer.');return false});
}
function toggleRun(){ctl('/api/v1/motion/toggle_run')}
function setSg(p){
  const inp=document.getElementById('sgIn'+p);
  const raw=inp.value.trim();
  // Strict, deliberately not parseInt: parseInt('') and parseInt('abc') are NaN,
  // and the old `||0` turned that into a perfectly valid request to set SG=0 -
  // which switches runtime jam detection off for that profile and persists.
  // Clearing the box and tabbing away was enough to do it, as was the blur that
  // fires when the tab is backgrounded, and nothing said it had happened.
  // A typed 0 is still honoured: it is a legitimate setting (RUN_SG_TRIP_MIN),
  // which is exactly why it must not double as the fallback for garbage.
  const v=/^[0-9]+$/.test(raw)?parseInt(raw,10):NaN;
  if(!Number.isInteger(v)||v<SG_MIN||v>SG_MAX){
    inp.value=sgLast[p]!==undefined?sgLast[p]:'';
    inp.style.borderColor='var(--red)';
    setTimeout(()=>{inp.style.borderColor='#444'},1500);
    return;
  }
  // blur fires whether or not anything was edited. Don't spend a request - and a
  // log line - re-sending the value the firmware already holds.
  if(v===sgLast[p])return;
  // On refusal put the box back to what the device still holds, so the field
  // never shows a value that was not applied.
  ctl('/api/v1/motion/sg_trip?profile='+p+'&value='+v).then(ok=>{
    if(!ok&&sgLast[p]!==undefined)inp.value=sgLast[p]})}
function setWz(d){ctl('/api/v1/motion/work_zone?delta='+d)}
function setBatch(d){ctl('/api/v1/motion/batch?delta='+d)}
function doBatch(a){ctl('/api/v1/motion/batch?action='+a)}
function setProfile(i){ctl('/api/v1/motion/profile?idx='+i)}
// Split so a drag costs one request instead of ~35. The slider is bound to
// showCurrent() on oninput (fires continuously while dragging) and setCurrent()
// on onchange (once, on release). Each committed value is a POST that ends in an
// rms_current() write to the TMC5160 over the SPI bus pump_task shares with the
// display, so firing them per pixel was both pointless traffic and pointless
// contention.
function showCurrent(v){document.getElementById('mcv').textContent=v;document.getElementById('mcWarn').style.display=v>4000?'block':'none'}
function setCurrent(v){showCurrent(v);ctl('/api/v1/motion/current?ma='+v)}
function adj(w,d){ctl('/api/v1/motion/endpoint?which='+w+'&delta='+d)}
function doAct(p){ctl(p)}
// One button, two meanings, decided by the state the label already reflects -
// rather than a second button that is disabled most of the time. doAct() is
// ctl()-based, so the abort inherits the refusal reporting for free.
function jamAction(){
  doAct(document.getElementById('bh').textContent==='Cancel'
        ?'/api/v1/motion/abort':'/api/v1/motion/return_home')}
function resetSettings(){
  if(!confirm('Discard the stored calibration and all tuning, and restore factory defaults?\n\nThe press will be UNCALIBRATED and must be recalibrated before it can run. WiFi and the web password are not affected.'))return;
  ctl('/api/v1/settings',{method:'DELETE'})}
function saveWifi(){
  const s=document.getElementById('ns').value,p=document.getElementById('np').value;
  if(!s){alert('SSID required');return}
  // Credentials go in the BODY, not the query string: a query string lands in
  // browser history and in any proxy/server access log along the way. The
  // firmware reads query and form-encoded body params from the same map, so
  // the handler is unchanged.
  // Parity with the captive portal, which renders an explicit error page for a
  // refusal. This used to alert "Connecting..." on the .then() of ANY response,
  // so a 400 (bad SSID) or the force-change 403 read as success and the
  // operator went off waiting for a join that was never attempted.
  fetch('/api/v1/wifi/save',{method:'POST',
    headers:{'Content-Type':'application/x-www-form-urlencoded'},
    body:'ssid='+encodeURIComponent(s)+'&pass='+encodeURIComponent(p)})
  .then(r=>{if(r.ok){alert('Connecting to the new network - no reboot. If it succeeds this page goes unreachable at the old address; the new IP is on the device screen (WiFi page). If it fails, AutoLee falls back to its setup network.')}
    else{r.text().then(t=>alert('Not saved: '+(t||('HTTP '+r.status))))}})
  .catch(()=>alert('Request failed - AutoLee did not answer.'))}
function resetWifi(){
  if(!confirm('Clear saved WiFi credentials and switch to setup mode? This page will go unreachable.'))return;
  fetch('/api/v1/wifi/reset',{method:'POST'})
  .then(r=>{if(r.ok){alert('WiFi cleared - the AutoLee-Setup network is coming up now (key on the device screen).')}
    else{r.text().then(t=>alert('Not cleared: '+(t||('HTTP '+r.status))))}})
  .catch(()=>alert('Request failed - AutoLee did not answer.'))}
// Shared by both password boxes. The dashboard never had a reveal control at
// all (the captive portal always did), which is a poor place to leave it when
// the field it hides can lock you out of the machine.
function togPw(id,b){
  const e=document.getElementById(id),hidden=e.type==='password';
  e.type=hidden?'text':'password';
  b.innerHTML=hidden?'&#128584;':'&#128065;';
  b.setAttribute('aria-label',hidden?'Hide password':'Show password')}
function saveWebPassword(){
  const p=document.getElementById('wpNew').value,p2=document.getElementById('wpNew2').value,
        m=document.getElementById('wpMsg');
  if(!p){m.textContent='Enter a new password.';m.style.color='#FF4444';return}
  // Caught here as well as on the device: a typo in a masked box would
  // otherwise be discovered only by being refused a login later.
  if(p!==p2){m.textContent='The two passwords do not match.';m.style.color='#FF4444';return}
  if(p.length<8){m.textContent='Password must be at least 8 characters.';m.style.color='#FF4444';return}
  fetch('/api/v1/system/web_password',{method:'POST',
    headers:{'Content-Type':'application/x-www-form-urlencoded'},
    body:'pass='+encodeURIComponent(p)})
  .then(r=>{if(r.ok){document.getElementById('wpNew').value='';
    document.getElementById('wpNew2').value='';m.style.color='#00FF00';
    m.textContent='Password changed. The browser will ask for it on your next action.'}
    else{m.style.color='#FF4444';r.text().then(t=>m.textContent='Failed: '+t)}})
  .catch(()=>{m.style.color='#FF4444';m.textContent='Request failed.'})}

let otaBusy=false;
function upFW(f){
  if(!f||otaBusy)return;
  // Clear the picker so cancelling and re-picking the same file fires change again.
  document.getElementById('fw').value='';
  // docs/UX.md class 4: confirm, and say a running press is stopped.
  if(!confirm('Install "'+f.name+'"?\n\nA running press is stopped first, and AutoLee reboots into the new firmware when the upload completes.'))return;
  const ua=document.getElementById('ua'),pb=document.getElementById('pb'),pf=document.getElementById('pf'),st=document.getElementById('otaS');
  ua.classList.add('on');ua.textContent=f.name;pb.style.display='block';pf.style.width='0%';
  st.textContent='Uploading...';st.style.color='#888';
  const x=new XMLHttpRequest();x.open('POST','/api/v1/system/ota');
  x.upload.onprogress=e=>{if(e.lengthComputable){const p=Math.round(e.loaded/e.total*100);pf.style.width=p+'%';st.textContent='Uploading... '+p+'%'}};
  x.onload=()=>{if(x.status===200){pf.style.width='100%';st.textContent='Success! Rebooting...';st.style.color='#00FF00';setTimeout(()=>location.reload(),5000)}
  else{otaBusy=false;st.textContent='Error: '+x.responseText;st.style.color='#FF4444'}};
  x.onerror=()=>{otaBusy=false;st.textContent='Upload failed';st.style.color='#FF4444'};
  otaBusy=true;
  const fd=new FormData();fd.append('firmware',f);x.send(fd)}

const uae=document.getElementById('ua');
uae.addEventListener('dragover',e=>{e.preventDefault();uae.classList.add('on')});
uae.addEventListener('dragleave',()=>uae.classList.remove('on'));
uae.addEventListener('drop',e=>{e.preventDefault();upFW(e.dataTransfer.files[0])});
fetch('/api/v1/state').then(r=>r.json()).then(upd).catch(()=>{});
</script></body></html>
)rawliteral";
