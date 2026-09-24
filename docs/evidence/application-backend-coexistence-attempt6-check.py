from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import json,subprocess,time,threading,traceback
from tools.session_smoke import Probe
from tools.soak import diagnostics
r={'attempt':6,'purpose':'diagnose whether concurrent Internet stalls owner TCP flow or entire Nano interface','status':'failed','started_utc':time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime())}
try:
 with Probe('192.168.77.1','a1b2c3d4e5f60718','nano_rp2040_connect',timeout=20) as owner:
  r['hello']=owner.hello;owner.command('serial.open',channel_id=1);event=threading.Event()
  def transfer():
   v={'begin_monotonic':time.monotonic()};event.set()
   try:v['result']=owner.echo(65536,slow_read=True);v['status']='passed'
   except Exception as e:v['error']=repr(e);v['status']='failed'
   v['end_monotonic']=time.monotonic();return v
  with ThreadPoolExecutor(max_workers=1) as pool:
   work=pool.submit(transfer);event.wait(1)
   q=subprocess.run(['curl','-4','--fail','--silent','--show-error','--max-time','10','--output','/dev/null','--write-out','%{http_code} %{remote_ip}',f'https://example.com/?firmingo_v2_diag={time.time_ns()}'],capture_output=True,text=True,timeout=12)
   r['https']={'exit_code':q.returncode,'output':q.stdout,'error':q.stderr}
   route=subprocess.run(['route','-n','get','192.168.77.1'],capture_output=True,text=True,timeout=3)
   r['board_route']={'exit_code':route.returncode,'output':route.stdout,'error':route.stderr}
   try:
    with Probe('192.168.77.1','a1b2c3d4e5f60718','nano_rp2040_connect',timeout=3) as observer:
     r['concurrent_observer']={'status':'passed','hello':observer.hello,'diagnostics':diagnostics(observer)};observer.finish()
   except Exception as e:r['concurrent_observer']={'status':'failed','error':repr(e)}
   r['transfer']=work.result()
  if r['transfer']['status']!='passed':raise RuntimeError(r['transfer']['error'])
  owner.command('serial.close',channel_id=1);owner.finish();r['status']='passed'
except Exception as e:r['error']=repr(e);traceback.print_exc()
r['finished_utc']=time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime());Path('build/application-backend-coexistence-attempt6.json').write_text(json.dumps(r,indent=2)+'\n');print(json.dumps(r,indent=2));raise SystemExit(0 if r['status']=='passed' else 1)
