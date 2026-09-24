from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import json,subprocess,time,threading,traceback
from tools.session_smoke import Probe
from tools.soak import diagnostics
report={'firmware_sha256':'6cf40edf7438d9b8c173556f14ac0fbee2799f71cc4399c0b31b8f48e0f97d57',
        'board':'nano_rp2040_connect','device_id':'a1b2c3d4e5f60718','address':'192.168.77.1',
        'started_utc':time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime()),'status':'failed','attempt':3,'purpose':'numbered reproduction of concurrent condition after one retained failure and one isolated pass'}
try:
 with Probe(report['address'],report['device_id'],report['board'],timeout=20) as peer:
  report['hello']=peer.hello
  if peer.hello['boot_id']!='737993054f7a1daa':raise RuntimeError('unexpected boot ID after soak')
  report['initial_diagnostics']=diagnostics(peer)
  peer.command('serial.open',channel_id=1)
  event=threading.Event()
  def transfer():
   result={'begin_monotonic':time.monotonic()};event.set()
   try:result['result']=peer.echo(65536,slow_read=True);result['status']='passed'
   except Exception as exc:result['status']='failed';result['error']=repr(exc)
   result['end_monotonic']=time.monotonic();return result
  with ThreadPoolExecutor(max_workers=1) as pool:
   work=pool.submit(transfer)
   if not event.wait(1):raise RuntimeError('transfer did not start')
   begin=time.monotonic()
   internet=subprocess.run(['curl','-4','--fail','--silent','--show-error','--max-time','10',
       '--output','/dev/null','--write-out','%{http_code} %{remote_ip}',
       f'https://example.com/?firmingo_application_v2={time.time_ns()}'],capture_output=True,text=True,timeout=12)
   report['https']={'begin_monotonic':begin,'end_monotonic':time.monotonic(),
                    'exit_code':internet.returncode,'output':internet.stdout,'error':internet.stderr}
   report['transfer']=work.result()
  t,h=report['transfer'],report['https']
  report['internet_contained_in_transfer']=t['begin_monotonic']<=h['begin_monotonic']<=h['end_monotonic']<=t['end_monotonic']
  if t['status']!='passed':raise RuntimeError(t['error'])
  report['backend_status']=peer.command('serial.status',channel_id=1)
  if report['backend_status']!={'channel_id':1,'backend_rx_bytes':65536,'backend_tx_bytes':65536,'pending_to_backend':0,'pending_to_peer':0}:
   raise RuntimeError(f'backend status mismatch: {report["backend_status"]!r}')
  report['final_diagnostics']=diagnostics(peer,report['initial_diagnostics'])
  for field in ('application_rx_pending','application_tx_pending','application_rx_discarded','application_tx_discarded'):
   if report['final_diagnostics'][field]!=0:raise RuntimeError(f'unexpected {field}: {report["final_diagnostics"][field]}')
  if internet.returncode or not report['internet_contained_in_transfer']:raise RuntimeError('concurrent Internet check failed')
  peer.command('serial.close',channel_id=1);peer.finish();report['status']='passed'
except Exception as exc:
 report['error']=repr(exc);traceback.print_exc()
finally:
 report['finished_utc']=time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime())
 Path('build/application-backend-coexistence-attempt3.json').write_text(json.dumps(report,indent=2)+'\n')
 print(json.dumps(report,indent=2))
if report['status']!='passed':raise SystemExit(1)
