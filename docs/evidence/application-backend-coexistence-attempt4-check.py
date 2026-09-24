from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import json,time,threading,traceback,urllib.request
from tools.session_smoke import Probe
from tools.soak import diagnostics
r={'attempt':4,'purpose':'isolate subprocess/curl from concurrent Wi-Fi traffic using in-process urllib','firmware_sha256':'6cf40edf7438d9b8c173556f14ac0fbee2799f71cc4399c0b31b8f48e0f97d57','status':'failed','started_utc':time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime())}
try:
 with Probe('192.168.77.1','a1b2c3d4e5f60718','nano_rp2040_connect',timeout=20) as p:
  r['hello']=p.hello;r['initial_diagnostics']=diagnostics(p);p.command('serial.open',channel_id=1)
  event=threading.Event()
  def transfer():
   v={'begin_monotonic':time.monotonic()};event.set()
   try:v['result']=p.echo(65536,slow_read=True);v['status']='passed'
   except Exception as e:v['error']=repr(e);v['status']='failed'
   v['end_monotonic']=time.monotonic();return v
  with ThreadPoolExecutor(max_workers=1) as pool:
   w=pool.submit(transfer);event.wait(1);begin=time.monotonic()
   with urllib.request.urlopen(f'https://example.com/?firmingo_v2_urllib={time.time_ns()}',timeout=10) as response:
    body=response.read(4096);code=response.status
   r['https']={'begin_monotonic':begin,'end_monotonic':time.monotonic(),'status_code':code,'body_bytes':len(body)};r['transfer']=w.result()
  r['internet_contained_in_transfer']=r['transfer']['begin_monotonic']<=r['https']['begin_monotonic']<=r['https']['end_monotonic']<=r['transfer']['end_monotonic']
  if r['transfer']['status']!='passed':raise RuntimeError(r['transfer']['error'])
  r['backend_status']=p.command('serial.status',channel_id=1);r['final_diagnostics']=diagnostics(p,r['initial_diagnostics'])
  if code!=200 or not r['internet_contained_in_transfer']:raise RuntimeError('Internet check not contained')
  p.command('serial.close',channel_id=1);p.finish();r['status']='passed'
except Exception as e:r['error']=repr(e);traceback.print_exc()
r['finished_utc']=time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime());Path('build/application-backend-coexistence-attempt4.json').write_text(json.dumps(r,indent=2)+'\n');print(json.dumps(r,indent=2))
raise SystemExit(0 if r['status']=='passed' else 1)
