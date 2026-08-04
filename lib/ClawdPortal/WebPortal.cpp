#include "WebPortal.h"
#include "ConfigStore.h"
#include "Console.h"
#include <WiFi.h>
#include "mbedtls/base64.h"

#ifdef CLAWD_ENABLE_OTA
  #include <Update.h>
  #include <SD.h>
  #include <HTTPClient.h>
  #include <WiFiClientSecure.h>
  #include <vector>
using fs::File;
static const bool  OTA_ON = true;
static const char *REPO_RAW = "https://raw.githubusercontent.com/BirdRa1n/clawd-on-esp/main/data/";
static const char *ASSET_FILES[] = {"idle", "thinking", "working", "juggling", "carrying",
                                    "sweeping", "attention", "notification", "error", "sleeping"};

// Streams a URL to a file on the given filesystem. HTTPS with cert check off
// (public repo assets on the LAN). Returns bytes written.
static int downloadTo(fs::FS &fsdst, const String &url, const String &path) {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  if (!http.begin(client, url)) return 0;
  int code = http.GET();
  if (code != 200) { http.end(); return 0; }
  File f = fsdst.open(path, FILE_WRITE);
  if (!f) { http.end(); return 0; }
  WiFiClient *stream = http.getStreamPtr();
  int remaining = http.getSize();
  uint8_t buf[1024];
  int total = 0;
  while (http.connected() && (remaining > 0 || remaining == -1)) {
    size_t avail = stream->available();
    if (avail) {
      int r = stream->readBytes(buf, avail > sizeof(buf) ? sizeof(buf) : avail);
      f.write(buf, r);
      total += r;
      if (remaining > 0) remaining -= r;
    } else {
      delay(1);
    }
    yield();
  }
  f.close();
  http.end();
  return total;
}
#else
static const bool OTA_ON = false;
#endif

// ── Setup captive portal theme (kept minimal) ───────────────────────────────
static const char *STYLE = R"CSS(
<style>
*{box-sizing:border-box}
body{background:#111318;color:#e7e7ea;font-family:system-ui,-apple-system,sans-serif;margin:0;padding:16px}
.card{max-width:460px;margin:14px auto;background:#1b1e26;border:1px solid #2a2e3a;border-radius:14px;padding:20px}
h1{color:#d97757;font-size:20px;margin:0 0 2px}
h2{font-size:15px;margin:18px 0 6px;border-top:1px solid #2a2e3a;padding-top:14px}
.sub{color:#8b8f9a;font-size:13px;margin:0 0 8px}
label{display:block;font-size:12px;color:#b9bdc8;margin:10px 0 4px}
input,select{width:100%;background:#0e1016;color:#fff;border:1px solid #2a2e3a;border-radius:8px;padding:10px;font-size:15px}
button{width:100%;margin-top:16px;background:#d97757;color:#161821;border:0;border-radius:8px;padding:12px;font-size:16px;font-weight:600;cursor:pointer}
.mini{font-size:12px;color:#8b8f9a;margin-top:8px}a{color:#d97757}
</style>)CSS";

static String head(const char *title) {
  String s = "<!doctype html><html><head><meta charset=utf-8>"
             "<meta name=viewport content='width=device-width,initial-scale=1'><title>Clawd</title>";
  s += STYLE;
  s += "</head><body><div class=card><h1>\xF0\x9F\xA6\x80 ";
  s += title;
  s += "</h1>";
  return s;
}
static String foot() { return "</div></body></html>"; }
static String esc(const String &v) {
  String o;
  for (char c : v) { if (c == '<') o += "&lt;"; else if (c == '>') o += "&gt;"; else if (c == '"') o += "&quot;"; else o += c; }
  return o;
}

// ── The dashboard SPA (served from PROGMEM). Auto light/dark; terminal stays dark. ──
static const char DASH_HTML[] PROGMEM = R"HTML(<!doctype html><html><head>
<meta charset=utf-8><meta name=viewport content="width=device-width,initial-scale=1">
<title>Clawd Display</title>
<style>
:root{--bg:#111318;--surface:#1b1e26;--surface2:#20242e;--inset:#0e1016;--line:#2a2e3a;--line2:#363b48;
--text:#e7e7ea;--muted:#8b8f9a;--faint:#5b606d;--accent:#d97757;--accent-ink:#161821;
--good:#22c55e;--info:#3b82f6;--warn:#f0a641;--crit:#ef4444;--r:12px;--r2:8px}
@media(prefers-color-scheme:light){:root{--bg:#f4f5f7;--surface:#fff;--surface2:#f1f2f5;--inset:#eceef2;
--line:#e0e3e9;--line2:#d1d5dd;--text:#1a1d24;--muted:#5b616e;--faint:#9aa0ac;--accent:#c6613f;--accent-ink:#fff;
--warn:#b45309;--good:#16a34a;--crit:#dc2626}}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--text);font-family:system-ui,-apple-system,"Segoe UI",Roboto,sans-serif;font-size:15px}
.mono{font-family:"JetBrains Mono",ui-monospace,"SF Mono",Menlo,Consolas,monospace}
.tnum{font-variant-numeric:tabular-nums}
h1,h2{margin:0}a{color:var(--accent)}
.app{display:grid;grid-template-columns:224px 1fr;min-height:100vh}
.sidebar{border-right:1px solid var(--line);padding:18px 14px;position:sticky;top:0;height:100vh;display:flex;flex-direction:column;gap:4px;background:var(--surface)}
.brand{display:flex;align-items:center;gap:10px;padding:4px 8px 16px}
.brand .g{font-size:24px}.brand .n{font-family:'JetBrains Mono',monospace;font-weight:600;font-size:15px}
.brand .s{font-family:'JetBrains Mono',monospace;font-size:10px;color:var(--faint);text-transform:uppercase;letter-spacing:.08em}
.nav a{display:flex;align-items:center;gap:10px;padding:9px 11px;border-radius:var(--r2);color:var(--muted);font-size:14px;font-weight:500;cursor:pointer;border:1px solid transparent}
.nav a:hover{color:var(--text);background:var(--surface2)}
.nav a.on{color:var(--text);background:var(--surface2);border-color:var(--line2)}
.nav a.on .i{color:var(--accent)}.nav .i{width:18px;text-align:center;color:var(--faint)}
.sfoot{margin-top:auto;padding-top:10px;border-top:1px solid var(--line);display:flex;align-items:center;gap:8px}
.sfoot .d{width:8px;height:8px;border-radius:50%;background:var(--good)}.sfoot .t{font-family:'JetBrains Mono',monospace;font-size:11px;color:var(--muted)}
.main{padding:24px 28px 320px;min-width:0}
.top{display:flex;align-items:flex-end;justify-content:space-between;margin-bottom:20px}
.top .c{font-family:'JetBrains Mono',monospace;font-size:11px;color:var(--faint);text-transform:uppercase;letter-spacing:.1em;margin-bottom:4px}
.top h1{font-size:22px;font-weight:650}
.wrap{max-width:720px}.sec{display:none}.sec.on{display:block}
.card{background:var(--surface);border:1px solid var(--line);border-radius:var(--r);padding:18px}
.card+.card{margin-top:16px}.card h2{font-size:15px;margin-bottom:2px}.desc{color:var(--muted);font-size:13px;margin:2px 0 12px}
.eyebrow{font-family:'JetBrains Mono',monospace;font-size:11px;letter-spacing:.14em;text-transform:uppercase;color:var(--muted)}
.hero{display:flex;gap:18px;align-items:center}
.hero .m{width:88px;height:88px;border-radius:16px;background:var(--inset);border:1px solid var(--line);display:grid;place-items:center;font-size:48px}
.kv{display:flex;gap:22px;flex-wrap:wrap;margin-top:8px}
.kv .k{font-family:'JetBrains Mono',monospace;font-size:10px;text-transform:uppercase;letter-spacing:.09em;color:var(--faint)}
.kv .v{font-family:'JetBrains Mono',monospace;font-size:14px}
.pill{display:inline-flex;align-items:center;gap:7px;padding:4px 12px;border-radius:999px;font-family:'JetBrains Mono',monospace;font-size:12px;font-weight:600;color:#fff}
.pill .d{width:7px;height:7px;border-radius:50%;background:#fff}
.row{display:flex;align-items:center;gap:12px;padding:11px 13px;background:var(--inset);border:1px solid var(--line);border-radius:var(--r2)}
.row+.row{margin-top:8px}.row .t1{font-family:'JetBrains Mono',monospace;font-size:14px}.row .t2{font-size:11px;color:var(--faint)}
.row .sp{flex:1}.badge{font-family:'JetBrains Mono',monospace;font-size:11px;color:var(--muted);border:1px solid var(--line2);border-radius:6px;padding:2px 7px}
.badge.pri{color:var(--accent);border-color:var(--accent)}
.x{background:none;border:none;color:var(--faint);cursor:pointer;font-size:15px;padding:4px 6px;border-radius:6px}.x:hover{color:var(--crit)}
label.f{display:block;font-size:12px;color:var(--muted);margin:12px 0 5px}
input,select{width:100%;background:var(--inset);color:var(--text);border:1px solid var(--line);border-radius:var(--r2);padding:10px 12px;font-size:14px;font-family:'JetBrains Mono',monospace;outline:none}
input:focus{border-color:var(--accent)}
.fr{display:flex;gap:10px}.fr>*{flex:1}.mt{margin-top:14px}
.btn{border:none;border-radius:var(--r2);padding:11px 16px;font-size:14px;font-weight:600;cursor:pointer;font-family:inherit}
.btn.p{background:var(--accent);color:var(--accent-ink)}.btn.g{background:var(--surface2);color:var(--text);border:1px solid var(--line2)}
.btn.d{background:transparent;color:var(--crit);border:1px solid var(--crit)}.btn.w{width:100%}
.rl{display:flex;justify-content:space-between;margin:14px 0 6px}.rl .v{font-family:'JetBrains Mono',monospace;color:var(--accent)}
input[type=range]{-webkit-appearance:none;height:5px;padding:0;background:var(--line2);border:none;border-radius:3px}
input[type=range]::-webkit-slider-thumb{-webkit-appearance:none;width:16px;height:16px;border-radius:50%;background:var(--accent);cursor:pointer}
.drop{border:1.5px dashed var(--line2);border-radius:var(--r);padding:26px;text-align:center;background:var(--inset);cursor:pointer}
.drop:hover{border-color:var(--accent)}.drop .u{font-size:24px;color:var(--accent)}.drop .b{font-weight:600;margin-top:6px}.drop .s{color:var(--muted);font-size:12px;margin-top:3px}
.prog{height:8px;background:var(--inset);border:1px solid var(--line);border-radius:99px;overflow:hidden;margin-top:12px}.prog>i{display:block;height:100%;width:0;background:var(--accent)}
.note{display:flex;gap:8px;font-size:12px;color:var(--warn);background:rgba(240,166,65,.1);border:1px solid rgba(240,166,65,.25);border-radius:var(--r2);padding:10px;margin-top:12px}
/* console dock (always dark) */
.con{position:fixed;right:18px;bottom:18px;width:410px;max-width:calc(100vw - 36px);background:#1b1e26;border:1px solid #363b48;border-radius:14px;box-shadow:0 18px 50px -12px rgba(0,0,0,.7);z-index:40;overflow:hidden;color:#e7e7ea}
.ch{display:flex;align-items:center;gap:8px;padding:10px 12px;border-bottom:1px solid #2a2e3a;background:#191c24}
.ch .lv{width:8px;height:8px;border-radius:50%;background:#22c55e}.ch .ti{font-family:'JetBrains Mono',monospace;font-size:12px;font-weight:600}.ch .sp{flex:1}
.ch button{background:none;border:1px solid #363b48;color:#8b8f9a;font-family:'JetBrains Mono',monospace;font-size:11px;padding:3px 8px;border-radius:6px;cursor:pointer}
.tel{display:grid;grid-template-columns:repeat(4,1fr);gap:1px;background:#2a2e3a}
.st{background:#1b1e26;padding:8px 10px}.st .l{font-family:'JetBrains Mono',monospace;font-size:9px;text-transform:uppercase;letter-spacing:.08em;color:#5b606d}
.st .n{font-family:'JetBrains Mono',monospace;font-size:15px;font-weight:600}.st .n small{font-size:10px;color:#8b8f9a;font-weight:400}
.spk{grid-column:1/-1;background:#1b1e26;padding:4px 10px 8px}.spk .l{font-family:'JetBrains Mono',monospace;font-size:9px;text-transform:uppercase;color:#5b606d}
#spk{width:100%;height:30px;display:block}
.term{background:#0a0b0f;height:180px;overflow:auto;padding:8px 11px;font-family:'JetBrains Mono',monospace;font-size:12px;line-height:1.5;border-top:1px solid #2a2e3a}
.tl{white-space:pre-wrap;word-break:break-word;color:#c8ccd6}.tt{color:#5b606d;margin-right:6px}
.tg{font-weight:600;margin-right:6px}.tg.net{color:#7db0ff}.tg.anim{color:#d97757}.tg.wifi{color:#5fe08c}.tg.hb{color:#5b606d}.tg.cfg{color:#f0a641}.tg.app{color:#c1c5cf}
.con.min .tel,.con.min .term,.con.min .spk{display:none}
@media(max-width:940px){.app{grid-template-columns:1fr}
.sidebar{flex-direction:row;height:auto;position:sticky;top:0;align-items:center;gap:8px;padding:10px;border-right:none;border-bottom:1px solid var(--line);z-index:30}
.brand{padding:0 4px}.brand .s{display:none}.nav{display:flex;flex:1;overflow-x:auto;gap:4px}.nav a .lb{display:none}.nav a.on .lb{display:inline}.sfoot{display:none}
.main{padding:18px 14px 40px}.con{position:static;width:auto;max-width:none;margin:20px 0 0}}
@media(prefers-reduced-motion:reduce){*{animation:none!important}}
</style></head><body>
<div class=app>
<aside class=sidebar>
<div class=brand><span class=g>&#129408;</span><div><div class=n>Clawd</div><div class=s>clawd-on-esp</div></div></div>
<nav class=nav id=nav>
<a data-s=overview class=on><span class=i>&#9635;</span><span class=lb>Visão geral</span></a>
<a data-s=wifi><span class=i>&#8767;</span><span class=lb>Redes Wi-Fi</span></a>
<a data-s=hosts><span class=i>&#10697;</span><span class=lb>Hosts</span></a>
<a data-s=token><span class=i>&#9919;</span><span class=lb>Token</span></a>
<a data-s=appear><span class=i>&#9681;</span><span class=lb>Aparência</span></a>
<a data-s=update><span class=i>&#11015;</span><span class=lb>Atualização</span></a>
<a data-s=storage><span class=i>&#128190;</span><span class=lb>Armazenamento</span></a>
<a data-s=system><span class=i>&#9881;</span><span class=lb>Sistema</span></a>
</nav>
<div class=sfoot><span class=d></span><span class=t id=footip>—</span></div>
</aside>
<main class=main>
<div class=top><div><div class=c id=crumb>Dispositivo</div><h1 id=ptitle>Visão geral</h1></div><span class=pill id=pill><span class=d></span>—</span></div>
<div class=wrap>
<section class="sec on" data-s=overview>
<div class="card hero"><div class=m>&#129408;</div><div><div style=font-size:18px;font-weight:650>Clawd Display</div>
<div class=kv><div><div class=k>Estado</div><div class=v id=ov-st>—</div></div><div><div class=k>Link</div><div class=v id=ov-lk>—</div></div>
<div><div class=k>IP</div><div class=v id=ov-ip>—</div></div><div><div class=k>Firmware</div><div class=v id=ov-fw>—</div></div></div></div></div>
<div class=card><span class=eyebrow>Sessão atual</span><h2 id=ov-title style=margin-top:8px>—</h2><div class=desc id=ov-sess>—</div></div>
</section>
<section class=sec data-s=wifi><div class=card><h2>Redes conhecidas</h2><p class=desc>Conecta na rede de maior prioridade por perto — sem reconfigurar entre casa e trabalho.</p>
<div id=netlist></div><div class="fr mt"><input id=ns placeholder=SSID><input id=np type=number value=10 style=max-width:88px></div>
<input id=npw type=password placeholder="senha da rede" style=margin-top:8px><button class="btn g w mt" onclick=addNet()>Adicionar rede</button></div></section>
<section class=sec data-s=hosts><div class=card><h2>Hosts do Clawd on Desk</h2><p class=desc>Se o IP do desktop mudar, o dispositivo tenta o próximo.</p>
<div id=hostlist></div><div class="fr mt"><input id=hh placeholder="IP ou host"><input id=hp type=number value=23334 style=max-width:120px></div>
<button class="btn g w mt" onclick=addHost()>Adicionar host</button></div></section>
<section class=sec data-s=token><div class=card><h2>Token de pareamento</h2><p class=desc>Do app desktop em Settings → Mobile. Rotaciona sozinho a cada 24 h.</p>
<label class=f>Token atual</label><input id=tok><button class="btn p w mt" onclick=saveTok()>Salvar token</button></div></section>
<section class=sec data-s=appear><div class=card><h2>Aparência</h2><p class=desc>Aplicado na tela do dispositivo em tempo real.</p>
<div class=rl><label>Tamanho do mascote</label><span class=v id=scv>100%</span></div><input type=range id=sc min=30 max=100 step=5 value=100>
<div class=rl><label>Brilho</label><span class=v id=brv>100%</span></div><input type=range id=br min=10 max=100 step=5 value=100>
<button class="btn p w mt" onclick=saveDisp()>Aplicar</button></div></section>
<section class=sec data-s=update><div class=card><h2>Atualização de firmware</h2><p class=desc>Envie um <span class=mono>.bin</span> compilado (OTA). A configuração é preservada.</p>
<div style="margin:2px 0 14px"><span class=eyebrow>Versão atual</span><div class=mono id=up-fw style="font-size:16px;margin-top:4px">—</div></div>
<div class=drop id=drop><div class=u>&#11015;</div><div class=b id=dropt>Escolher firmware.bin</div><div class=s id=drops>clique para selecionar</div></div>
<input type=file id=fw accept=.bin style=display:none>
<div id=pw style=display:none><div class=prog><i id=pb></i></div><div class=mono id=pt style="font-size:12px;color:var(--muted);margin-top:7px"></div></div>
<button class="btn p w mt" id=flash disabled style=opacity:.5 onclick=doFlash()>Enviar e atualizar</button>
<div class=note><span>&#9888;</span><span>Não desligue durante a atualização. O dispositivo reinicia sozinho ao concluir.</span></div></div></section>
<section class=sec data-s=storage><div class=card><h2>Cartão SD</h2><p class=desc>Neste build (OTA) os assets do mascote ficam no cartão. Sincronize do GitHub e o dispositivo reinicia.</p>
<div id=sdstat class=kv style=margin-bottom:10px></div><div id=sdlist></div>
<div class="fr mt"><button class="btn p" onclick=sdsync()>Sincronizar assets</button><button class="btn d" onclick=sdformat()>Formatar</button></div>
<div class=note><span>&#9432;</span><span>A sincronização baixa ~2 MB e pode levar alguns minutos. Acompanhe no terminal; o dispositivo reinicia ao terminar.</span></div></div></section>
<section class=sec data-s=system><div class=card><h2>Sistema</h2><p class=desc>Informações e manutenção.</p>
<div id=sysinfo></div><div class="fr mt"><button class="btn g" onclick=reb()>Reiniciar</button><button class="btn d" onclick=rst()>Restaurar padrão</button></div></div></section>
</div></main></div>
<div class=con id=con>
<div class=ch><span class=lv></span><span class=ti>SISTEMA · SERIAL</span><span class=sp></span>
<button onclick=clr()>limpar</button><button id=minb onclick=mini()>&#9662;</button></div>
<div class=tel>
<div class=st><div class=l>Heap</div><div class=n tnum><span id=t-heap>—</span> <small>KB</small></div></div>
<div class=st><div class=l>Temp</div><div class=n tnum><span id=t-temp>—</span><small>°C</small></div></div>
<div class=st><div class=l>Uptime</div><div class=n tnum id=t-up style=font-size:13px>—</div></div>
<div class=st><div class=l>Loop</div><div class=n tnum><span id=t-loop>—</span><small>k/s</small></div></div>
<div class=spk><div class=l>Heap · últimos 60 s</div><canvas id=spk></canvas></div></div>
<div class=term id=term></div></div>
<script>
var q=function(s){return document.getElementById(s)},state=null,logSeq=0,hist=[];
function api(u,o){return fetch(u,o).then(function(r){return r.json()})}
function post(u,b){return fetch(u,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:b}).then(function(){load()})}
var COL={ERROR:'#ef4444',NOTIFICATION:'#d97757',SWEEPING:'#71717a',ATTENTION:'#b45309',CARRYING:'#71717a',JUGGLING:'#22c55e',WORKING:'#22c55e',THINKING:'#3b82f6',IDLE:'#71717a',SLEEPING:'#a1a1aa'};
q('nav').addEventListener('click',function(e){var a=e.target.closest('a');if(!a)return;var s=a.dataset.s;
[].forEach.call(document.querySelectorAll('.nav a'),function(n){n.classList.toggle('on',n===a)});
[].forEach.call(document.querySelectorAll('.sec'),function(x){x.classList.toggle('on',x.dataset.s===s)});
q('crumb').textContent=({overview:'Dispositivo',wifi:'Rede',hosts:'Rede',token:'Segurança',appear:'Tela',update:'Manutenção',storage:'Manutenção',system:'Manutenção'})[s];
q('ptitle').textContent=a.querySelector('.lb').textContent;window.scrollTo(0,0);if(s=='storage'&&state&&state.info.ota)loadSD()});
function fmtUp(s){var h=(s/3600)|0,m=((s%3600)/60)|0,x=s%60;return(h?h+':':'')+('0'+m).slice(-2)+':'+('0'+x).slice(-2)}
function renderCfg(c){
q('footip').textContent='online';
var nl='';(c.networks||[]).forEach(function(n,i){nl+='<div class=row><div><div class=t1>'+esc(n.ssid)+'</div><div class=t2>salva</div></div><span class=sp></span><span class="badge'+(n.prio>=10?' pri':'')+'">prioridade '+n.prio+'</span><button class=x onclick="delNet('+i+')">&#10005;</button></div>'});
q('netlist').innerHTML=nl||'<div class=desc>Nenhuma rede salva.</div>';
var hl='';(c.hosts||[]).forEach(function(h,i){hl+='<div class=row><div><div class=t1>'+esc(h.host)+':'+h.port+'</div></div><span class=sp></span><button class=x onclick="delHost('+i+')">&#10005;</button></div>'});
q('hostlist').innerHTML=hl||'<div class=desc>Nenhum host.</div>';
q('tok').value=c.token||'';q('sc').value=c.scale;q('br').value=c.bri;q('scv').textContent=c.scale+'%';q('brv').textContent=c.bri+'%';}
function renderStatus(s,info){
var st=s.state||'IDLE';var col=COL[st]||'#71717a';
var p=q('pill');p.style.background=col;p.innerHTML='<span class=d></span>'+st;
q('ov-st').textContent=st.toLowerCase();q('ov-lk').textContent=s.link=='up'?'conectado':s.link=='auth'?'auth falhou':'conectando';
q('ov-lk').style.color=s.link=='up'?'var(--good)':s.link=='auth'?'var(--crit)':'var(--warn)';
q('ov-ip').textContent=s.ip;q('ov-fw').textContent='v'+s.fw;q('up-fw').textContent='v'+s.fw;
q('ov-title').textContent=s.title||'—';q('ov-sess').textContent=(s.sessions||0)+' sessão(ões) · RSSI '+s.rssi+' dBm';q('footip').textContent=s.ip;
q('t-heap').textContent=s.heap;q('t-temp').textContent=s.temp;q('t-up').textContent=fmtUp(s.up);q('t-loop').textContent=(s.loop/1000).toFixed(0);
hist.push(s.heap);if(hist.length>60)hist.shift();spark();
q('sysinfo').innerHTML=kv('Chip',info.chip)+kv('MAC',info.mac)+kv('Flash','app '+s.flashUsed+'/'+s.flashTotal+' KB')+kv('Heap',s.heap+'/'+s.heapTotal+' KB')+kv('IP',s.ip)+kv('Uptime',fmtUp(s.up));}
function kv(k,v){return '<div class=row><div><div class=t2>'+k+'</div><div class=t1>'+esc(''+v)+'</div></div></div>'}
function esc(s){return (''+s).replace(/[<>&"]/g,function(c){return{'<':'&lt;','>':'&gt;','&':'&amp;','"':'&quot;'}[c]})}
function load(){api('/api/state').then(function(d){state=d;renderCfg(d.config);renderStatus(d.status,d.info);toggleOta(d.info.ota);if(d.info.ota)loadSD();})}
function poll(){api('/api/state').then(function(d){renderStatus(d.status,d.info)})}
function toggleOta(on){['update','storage'].forEach(function(s){var a=document.querySelector('.nav a[data-s='+s+']');if(a)a.style.display=on?'':'none'})}
function loadSD(){api('/api/sd').then(function(d){
q('sdstat').innerHTML='<div><div class=k>Usado</div><div class=v>'+d.used+' / '+d.total+' KB</div></div><div><div class=k>Arquivos</div><div class=v>'+d.files.length+'</div></div>';
var h='';d.files.forEach(function(f){h+='<div class=row><div class=t1>'+esc(f.name)+'</div><span class=sp></span><span class=badge>'+(f.size/1024).toFixed(0)+' KB</span></div>'});
q('sdlist').innerHTML=h||'<div class=desc>Cartão vazio — sincronize os assets.</div>'}).catch(function(){q('sdstat').innerHTML='<div class=desc style="color:var(--crit)">Cartão SD não detectado.</div>';q('sdlist').innerHTML=''})}
function sdsync(){if(!confirm('Baixar os assets do GitHub para o cartão? Pode levar minutos; o dispositivo reinicia ao terminar.'))return;
q('sdlist').innerHTML='<div class=desc>Sincronizando… acompanhe no terminal. Não feche a página.</div>';
fetch('/api/sd/sync',{method:'POST'}).then(function(r){return r.json()}).then(function(d){q('sdlist').innerHTML='<div class=desc>Sincronizado ('+d.got+'/10). Reiniciando…</div>'}).catch(function(){})}
function sdformat(){if(!confirm('Apagar TODOS os arquivos do cartão?'))return;post('/api/sd/format','')}
function addNet(){post('/api/net','ssid='+encodeURIComponent(q('ns').value)+'&pass='+encodeURIComponent(q('npw').value)+'&prio='+q('np').value);q('ns').value=q('npw').value=''}
function delNet(i){post('/api/net/del','i='+i)}
function addHost(){post('/api/host','host='+encodeURIComponent(q('hh').value)+'&port='+q('hp').value);q('hh').value=''}
function delHost(i){post('/api/host/del','i='+i)}
function saveTok(){post('/api/token','token='+encodeURIComponent(q('tok').value))}
function saveDisp(){post('/api/display','scale='+q('sc').value+'&bri='+q('br').value)}
function reb(){if(confirm('Reiniciar o dispositivo?'))post('/api/reboot','')}
function rst(){if(confirm('Apagar toda a configuração e reprovisionar?'))post('/api/reset','')}
q('sc').oninput=function(){q('scv').textContent=this.value+'%'};q('br').oninput=function(){q('brv').textContent=this.value+'%'};
/* firmware */
var file=null;q('drop').onclick=function(){q('fw').click()};
q('fw').onchange=function(){file=this.files[0];if(file){q('dropt').textContent=file.name;q('drops').textContent=(file.size/1024).toFixed(0)+' KB · pronto';q('flash').disabled=false;q('flash').style.opacity=1}};
function doFlash(){if(!file)return;q('flash').disabled=true;q('flash').style.opacity=.6;q('pw').style.display='block';
var fd=new FormData();fd.append('firmware',file);var x=new XMLHttpRequest();x.open('POST','/update');
x.upload.onprogress=function(e){var p=e.total?(e.loaded/e.total*100):0;q('pb').style.width=p+'%';q('pt').textContent='Enviando… '+p.toFixed(0)+'%'};
x.onload=function(){q('pt').textContent='Gravado — reiniciando…';q('pb').style.width='100%';q('flash').textContent='Concluído';};
x.onerror=function(){q('pt').textContent='Falha no envio'};x.send(fd)}
/* terminal */
var term=q('term'),lines=0;
function two(n){return('0'+n).slice(-2)}
function pushLine(txt){var tag='app',m=txt.match(/^\[(\w+)\]/);if(m)tag=m[1];
var d=new Date(),ts=two(d.getHours())+':'+two(d.getMinutes())+':'+two(d.getSeconds());
var div=document.createElement('div');div.className='tl';
div.innerHTML='<span class=tt>'+ts+'</span>'+esc(txt).replace(/^\[(\w+)\]/,'<span class="tg '+tag+'">[$1]</span>');
term.appendChild(div);lines++;while(lines>80){term.removeChild(term.firstChild);lines--}term.scrollTop=term.scrollHeight}
function polllog(){api('/api/log?since='+logSeq).then(function(d){logSeq=d.seq;(d.lines||[]).forEach(pushLine)})}
function clr(){term.innerHTML='';lines=0}
function mini(){q('con').classList.toggle('min');q('minb').innerHTML=q('con').classList.contains('min')?'&#9652;':'&#9662;'}
/* sparkline */
var cv=q('spk'),cx=cv.getContext('2d');
function rs(){cv.width=cv.clientWidth*devicePixelRatio;cv.height=cv.clientHeight*devicePixelRatio}
function spark(){if(hist.length<2)return;var w=cv.width,h=cv.height,mn=Math.min.apply(0,hist)-2,mx=Math.max.apply(0,hist)+2,n=hist.length;
cx.clearRect(0,0,w,h);cx.beginPath();hist.forEach(function(v,i){var x=i/(n-1)*w,y=h-(v-mn)/(mx-mn)*h;i?cx.lineTo(x,y):cx.moveTo(x,y)});
var g=cx.createLinearGradient(0,0,0,h);g.addColorStop(0,'rgba(217,119,87,.35)');g.addColorStop(1,'rgba(217,119,87,0)');
cx.lineTo(w,h);cx.lineTo(0,h);cx.closePath();cx.fillStyle=g;cx.fill();
cx.beginPath();hist.forEach(function(v,i){var x=i/(n-1)*w,y=h-(v-mn)/(mx-mn)*h;i?cx.lineTo(x,y):cx.moveTo(x,y)});
cx.strokeStyle='#d97757';cx.lineWidth=1.5*devicePixelRatio;cx.stroke()}
addEventListener('resize',function(){rs();spark()});rs();
load();polllog();setInterval(poll,1500);setInterval(polllog,1000);
</script></body></html>)HTML";

// ── Setup (AP captive) ──────────────────────────────────────────────────────
void WebPortal::beginSetup() {
  _isSetup = true;
  _dns.start(53, "*", WiFi.softAPIP());
  routesSetup();
  _server.begin();
}

void WebPortal::routesSetup() {
  _server.on("/", [this]() {
    String p = head("Configurar dispositivo");
    p += "<p class=sub>Conecte seu Clawd à sua rede Wi-Fi.</p><form method=POST action=/save>";
    p += "<label>Rede Wi-Fi (SSID)</label><input name=ssid list=nets autocomplete=off required><datalist id=nets></datalist>";
    p += "<label>Senha do Wi-Fi</label><input name=pass type=password>";
    p += "<h2>Clawd on Desk</h2><label>Host / IP</label><input name=host placeholder=192.168.0.10 required>";
    p += "<label>Porta</label><input name=port value=23334><label>Token</label><input name=token>";
    p += "<h2>Acesso</h2><label>Senha de admin</label><input name=admin type=password required>";
    p += "<button>Salvar e conectar</button></form>";
    p += "<script>fetch('/scan').then(r=>r.json()).then(a=>{let d=document.getElementById('nets');a.forEach(s=>{let o=document.createElement('option');o.value=s;d.appendChild(o)})}).catch(()=>{})</script>";
    p += foot();
    _server.send(200, "text/html", p);
  });
  _server.on("/scan", [this]() {
    int n = WiFi.scanNetworks();
    String j = "[";
    for (int i = 0; i < n; i++) { if (i) j += ","; j += "\"" + esc(WiFi.SSID(i)) + "\""; }
    j += "]";
    _server.send(200, "application/json", j);
  });
  _server.on("/save", HTTP_POST, [this]() {
    ClawdConfigData c;
    WiFiNet net; net.ssid = _server.arg("ssid"); net.pass = _server.arg("pass"); net.priority = 10;
    if (net.ssid.length()) c.networks.push_back(net);
    ClawdHost h; h.host = _server.arg("host"); h.port = (uint16_t)_server.arg("port").toInt(); if (!h.port) h.port = 23334;
    if (h.host.length()) c.hosts.push_back(h);
    c.token = _server.arg("token");
    if (_server.arg("admin").length()) ConfigStore::setAdminPassword(c, _server.arg("admin"));
    c.provisioned = true;
    ConfigStore::save(c);
    _server.send(200, "text/html", head("Salvo") + "<p class=sub>Configuração salva. Reiniciando…</p>" + foot());
    _reboot = true;
  });
  _server.onNotFound([this]() {
    _server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString(), true);
    _server.send(302, "text/plain", "");
  });
}

// ── Auth ────────────────────────────────────────────────────────────────────
bool WebPortal::authed() {
  ClawdConfigData cfg = ConfigStore::load();
  if (!cfg.hasAdmin()) return true;   // not set yet
  String h = _server.hasHeader("Authorization") ? _server.header("Authorization") : "";
  if (h.startsWith("Basic ")) {
    String b64 = h.substring(6);
    uint8_t out[160]; size_t olen = 0;
    if (mbedtls_base64_decode(out, sizeof(out), &olen, (const uint8_t *)b64.c_str(), b64.length()) == 0) {
      String creds((char *)out, olen);
      int colon = creds.indexOf(':');
      if (colon >= 0 && ConfigStore::checkAdminPassword(cfg, creds.substring(colon + 1))) return true;
    }
  }
  return false;
}
bool WebPortal::requireAdmin() {
  if (authed()) return true;
  _server.sendHeader("WWW-Authenticate", "Basic realm=\"Clawd\"");
  _server.send(401, "text/plain", "Auth required");
  return false;
}

// ── Dashboard (STA) ─────────────────────────────────────────────────────────
static String cfgJson(const ClawdConfigData &c) {
  String s = "{\"networks\":[";
  for (size_t i = 0; i < c.networks.size(); i++) {
    if (i) s += ",";
    s += "{\"ssid\":\"" + esc(c.networks[i].ssid) + "\",\"prio\":" + String(c.networks[i].priority) + "}";
  }
  s += "],\"hosts\":[";
  for (size_t i = 0; i < c.hosts.size(); i++) {
    if (i) s += ",";
    s += "{\"host\":\"" + esc(c.hosts[i].host) + "\",\"port\":" + String(c.hosts[i].port) + "}";
  }
  s += "],\"token\":\"" + esc(c.token) + "\",\"scale\":" + String(c.mascotScale) +
       ",\"bri\":" + String(c.brightness) + ",\"hasAdmin\":" + (c.hasAdmin() ? "true" : "false") + "}";
  return s;
}

void WebPortal::beginDashboard(const ClawdConfigData &cfg) {
  _isSetup = false;
  _cfg = cfg;
  const char *hdrs[] = {"Authorization"};
  _server.collectHeaders(hdrs, 1);
  routesDashboard();
  _server.begin();
}

void WebPortal::routesDashboard() {
  auto ok = [this]() {
    if (onConfigChanged) onConfigChanged();
    _server.send(200, "application/json", "{\"ok\":true}");
  };

  _server.on("/", [this]() { if (!requireAdmin()) return; _server.send_P(200, "text/html", DASH_HTML); });

  _server.on("/api/state", [this]() {
    if (!requireAdmin()) return;
    ClawdConfigData c = ConfigStore::load();
    String s = "{\"config\":" + cfgJson(c) + ",\"status\":" + (statusProvider ? statusProvider() : String("{}"));
    s += ",\"info\":{\"mac\":\"" + WiFi.macAddress() + "\",\"chip\":\"" + String(ESP.getChipModel()) +
         "\",\"flashMB\":" + String(ESP.getFlashChipSize() / (1024 * 1024)) +
         ",\"ota\":" + (OTA_ON ? "true" : "false") +
         ",\"assetSrc\":\"" + (OTA_ON ? "SD" : "LittleFS") + "\"}}";
    _server.send(200, "application/json", s);
  });

  _server.on("/api/log", [this]() {
    if (!requireAdmin()) return;
    uint32_t since = _server.hasArg("since") ? (uint32_t)strtoul(_server.arg("since").c_str(), nullptr, 10) : 0;
    _server.send(200, "application/json", console.jsonSince(since));
  });

  _server.on("/api/net", HTTP_POST, [this, ok]() {
    if (!requireAdmin()) return;
    ClawdConfigData c = ConfigStore::load();
    WiFiNet n; n.ssid = _server.arg("ssid"); n.pass = _server.arg("pass"); n.priority = _server.arg("prio").toInt();
    if (n.ssid.length()) { c.networks.push_back(n); ConfigStore::save(c); }
    ok();
  });
  _server.on("/api/net/del", HTTP_POST, [this, ok]() {
    if (!requireAdmin()) return;
    ClawdConfigData c = ConfigStore::load(); int i = _server.arg("i").toInt();
    if (i >= 0 && i < (int)c.networks.size()) { c.networks.erase(c.networks.begin() + i); ConfigStore::save(c); }
    ok();
  });
  _server.on("/api/host", HTTP_POST, [this, ok]() {
    if (!requireAdmin()) return;
    ClawdConfigData c = ConfigStore::load();
    ClawdHost h; h.host = _server.arg("host"); h.port = (uint16_t)_server.arg("port").toInt(); if (!h.port) h.port = 23334;
    if (h.host.length()) { c.hosts.push_back(h); ConfigStore::save(c); }
    ok();
  });
  _server.on("/api/host/del", HTTP_POST, [this, ok]() {
    if (!requireAdmin()) return;
    ClawdConfigData c = ConfigStore::load(); int i = _server.arg("i").toInt();
    if (i >= 0 && i < (int)c.hosts.size()) { c.hosts.erase(c.hosts.begin() + i); ConfigStore::save(c); }
    ok();
  });
  _server.on("/api/token", HTTP_POST, [this, ok]() {
    if (!requireAdmin()) return;
    ClawdConfigData c = ConfigStore::load(); c.token = _server.arg("token"); ConfigStore::save(c); ok();
  });
  _server.on("/api/display", HTTP_POST, [this, ok]() {
    if (!requireAdmin()) return;
    ClawdConfigData c = ConfigStore::load();
    int sc = _server.arg("scale").toInt(); if (sc >= 30 && sc <= 100) c.mascotScale = (uint8_t)sc;
    int br = _server.arg("bri").toInt();   if (br >= 10 && br <= 100) c.brightness = (uint8_t)br;
    ConfigStore::save(c); ok();
  });
  _server.on("/api/admin", HTTP_POST, [this, ok]() {
    if (!requireAdmin()) return;
    ClawdConfigData c = ConfigStore::load();
    if (_server.arg("admin").length()) { ConfigStore::setAdminPassword(c, _server.arg("admin")); ConfigStore::save(c); }
    ok();
  });
  _server.on("/api/reboot", HTTP_POST, [this]() {
    if (!requireAdmin()) return;
    _server.send(200, "application/json", "{\"ok\":true}"); _reboot = true;
  });
  _server.on("/api/reset", HTTP_POST, [this]() {
    if (!requireAdmin()) return;
    ConfigStore::clear();
    _server.send(200, "application/json", "{\"ok\":true}"); _reboot = true;
  });

#ifdef CLAWD_ENABLE_OTA
  // OTA firmware upload
  _server.on("/update", HTTP_POST,
    [this]() {   // completion
      bool good = _otaAuth && !Update.hasError();
      _server.send(good ? 200 : 500, "application/json", good ? "{\"ok\":true}" : "{\"ok\":false}");
      if (good) _reboot = true;
    },
    [this]() {   // upload chunks
      HTTPUpload &up = _server.upload();
      if (up.status == UPLOAD_FILE_START) {
        _otaAuth = authed();
        if (_otaAuth) { console.logf("[cfg] OTA start: %s", up.filename.c_str()); Update.begin(UPDATE_SIZE_UNKNOWN); }
      } else if (up.status == UPLOAD_FILE_WRITE) {
        if (_otaAuth) Update.write(up.buf, up.currentSize);
      } else if (up.status == UPLOAD_FILE_END) {
        if (_otaAuth && Update.end(true)) console.logf("[cfg] OTA ok: %u bytes", up.totalSize);
      }
    });

  // SD card: status + file listing
  _server.on("/api/sd", [this]() {
    if (!requireAdmin()) return;
    String s = "{\"total\":" + String((uint32_t)(SD.totalBytes() / 1024)) +
               ",\"used\":" + String((uint32_t)(SD.usedBytes() / 1024)) + ",\"files\":[";
    File root = SD.open("/");
    bool first = true;
    if (root) {
      File f = root.openNextFile();
      while (f) {
        if (!f.isDirectory()) {
          if (!first) s += ",";
          s += "{\"name\":\"" + String(f.name()) + "\",\"size\":" + String((uint32_t)f.size()) + "}";
          first = false;
        }
        f = root.openNextFile();
      }
    }
    s += "]}";
    _server.send(200, "application/json", s);
  });

  // "Format": delete every file on the card
  _server.on("/api/sd/format", HTTP_POST, [this]() {
    if (!requireAdmin()) return;
    std::vector<String> names;
    File root = SD.open("/");
    if (root) { File f = root.openNextFile(); while (f) { if (!f.isDirectory()) names.push_back(f.name()); f = root.openNextFile(); } }
    for (auto &n : names) SD.remove(n.startsWith("/") ? n : "/" + n);
    console.logf("[sd] cleared %d files", (int)names.size());
    _server.send(200, "application/json", "{\"ok\":true}");
    _reboot = true;
  });

  // Sync animation assets from the GitHub repo to the SD card
  _server.on("/api/sd/sync", HTTP_POST, [this]() {
    if (!requireAdmin()) return;
    int okc = 0;
    for (const char *name : ASSET_FILES) {
      String url = String(REPO_RAW) + name + ".crli";
      console.logf("[sd] downloading %s.crli", name);
      if (downloadTo(SD, url, String("/") + name + ".crli") > 0) okc++;
      else console.logf("[sd] FAILED %s.crli", name);
    }
    console.logf("[sd] sync done: %d/10", okc);
    _server.send(200, "application/json", String("{\"ok\":") + (okc == 10 ? "true" : "false") + ",\"got\":" + okc + "}");
    if (okc > 0) _reboot = true;   // reboot for a clean mount/read of the new assets
  });
#else
  _server.on("/update", HTTP_POST, [this]() { _server.send(501, "application/json", "{\"ok\":false,\"err\":\"no-ota\"}"); });
#endif
}

void WebPortal::loop() {
  if (_isSetup) _dns.processNextRequest();
  _server.handleClient();
}
