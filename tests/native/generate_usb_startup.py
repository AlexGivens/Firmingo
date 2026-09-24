"""Compile actual patched startup function bodies against narrow fake hardware.

This verifies call ordering, not a host/device USB exchange. Headers and the full
SDK translation units are independently verified by the pinned firmware build.
"""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys

root, output = map(Path, sys.argv[1:])
package = root / 'patches/arduino-pico-6.0.0-usb-startup'
manifest = json.loads((package / 'manifest.json').read_text())
patch = package / manifest['patch']
assert hashlib.sha256(patch.read_bytes()).hexdigest() == manifest['patch_sha256']
stage = output.parent / 'usb-startup-sources'
stage.mkdir(exist_ok=True)
for name, hashes in manifest['files'].items():
    source = root / 'third_party/arduino-pico-usb-startup/upstream' / name
    assert hashlib.sha256(source.read_bytes()).hexdigest() == hashes['upstream_sha256'], name
    destination = stage / name
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, destination)
subprocess.run(['patch', '--batch', '--fuzz=0', '--no-backup-if-mismatch', '-p1', '-i', str(patch)], cwd=stage, check=True)
for name, hashes in manifest['files'].items():
    assert hashlib.sha256((stage / name).read_bytes()).hexdigest() == hashes['patched_sha256'], name


def function(source, declaration):
    start = source.index(declaration)
    body = source.index('{', start)
    depth = 1
    end = body + 1
    # These selected bodies contain no braces inside literals/comments.
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end] + '\n'

main = (stage / 'cores/rp2040/main.cpp').read_text()
start = main.index('\n#ifndef NO_USB\n')
end = main.index('\n#if defined DEBUG_RP2040_PORT', start)
usb = (stage / 'cores/rp2040/USB.cpp').read_text()
ncm = (stage / 'libraries/lwIP_USB_NCM/src/utility/NCMEthernet.cpp').read_text()
ncm_lwip = (stage / 'libraries/lwIP_USB_NCM/src/NCMEthernetlwIP.cpp').read_text()
ncm_irq_prelude = '''#ifdef __FREERTOS
void NCMEthernetlwIP::_call_irq(void *cbData) {
#else
void NCMEthernetlwIP::_call_irq(async_context_t *context, async_when_pending_worker_t *worker) {
    (void) context;
#endif
#ifdef __FREERTOS
    // In freertos we can afford to block, as long as no other code uses tinyUSB and lwIP at the same time.
    CoreMutex m(&USB.mutex);
#else
'''
ncm_irq_native_prelude = '''void NCMEthernetlwIP::_call_irq(async_context_t *context, async_when_pending_worker_t *worker) {
    (void) context;
'''
assert ncm_irq_prelude in ncm_lwip
ncm_lwip_native = ncm_lwip.replace(ncm_irq_prelude, ncm_irq_native_prelude, 1)
ncm_irq_mutex_end = '''        return;
    }
#endif
    // We have both mutexes now'''
ncm_irq_native_mutex_end = '''        return;
    }
    // We have both mutexes now'''
assert ncm_irq_mutex_end in ncm_lwip_native
ncm_lwip_native = ncm_lwip_native.replace(ncm_irq_mutex_end, ncm_irq_native_mutex_end, 1)
sketch = (root / 'examples/firmingo_local_only/firmingo_local_only.ino').read_text()
reference = (root / 'examples/firmingo_application/firmingo_application.ino').read_text()
output.write_text('\n'.join([
    'void core_usb_start() {' + main[start:end] + '\n}',
    function(usb, 'void USBClass::prepare()'),
    function(usb, 'void USBClass::begin()'),
    function(ncm, 'bool NCMEthernet::begin('),
    function(ncm_lwip_native, 'void NCMEthernetlwIP::_call_irq('),
    '#ifdef FIRMINGO_USB_DEFERRED_START\n' + function(sketch, 'void setup()') +
    function(reference, 'void prepareIdentity()') +
    function(reference, 'void setup()').replace('void setup()', 'void reference_setup()', 1) + '#endif\n',
]))
