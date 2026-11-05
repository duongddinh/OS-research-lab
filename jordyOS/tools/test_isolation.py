"""Exercise real ring-3 faults and inspect per-process paging in QEMU."""
import os
import pathlib
import re
import selectors
import socket
import subprocess
import tempfile
import time

temporary = tempfile.TemporaryDirectory(prefix='jordyos-isolation-')
monitor_path = pathlib.Path(temporary.name) / 'monitor.sock'
p = subprocess.Popen([os.environ.get('QEMU','qemu-system-x86_64'),'-cpu','qemu64',
    '-drive','if=floppy,format=raw,file=os.img','-boot','a','-m','32','-snapshot',
    '-display','none','-serial','stdio','-nic','none','-no-reboot',
    '-monitor',f'unix:{monitor_path},server=on,wait=off'],
    stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
selector=selectors.DefaultSelector(); selector.register(p.stdout,selectors.EVENT_READ)
transcript=bytearray()


def until(marker,timeout=20):
    data=bytearray(); deadline=time.monotonic()+timeout
    while not data.endswith(marker) and time.monotonic()<deadline:
        if selector.select(.1):
            chunk=os.read(p.stdout.fileno(),65536)
            if not chunk: break
            data.extend(chunk); transcript.extend(chunk)
    assert data.endswith(marker),(marker,data)
    return data.decode(errors='replace').replace('\r','')


def command(text,expected,marker=b'sh> '):
    p.stdin.write(text.encode()+b'\n'); p.stdin.flush()
    output=until(marker)
    assert expected in output,output
    assert 'KERNEL EXCEPTION' not in output,output
    return output


try:
    until(b'desktop> '); p.stdin.write(b'4'); p.stdin.flush(); until(b'sh> ')
    command('isolation','Application isolation: ring 3; private CR3')
    command('arch','ABI: PASS')  # exercise non-default floating-point status flags first
    output=command('isolation test','ISOLATION TESTS PASS')
    assert 'FAIL ' not in output,output
    for name in ['kernel read blocked','kernel write blocked','other process memory read blocked',
                 'other process memory write blocked','privileged HLT blocked','direct device I/O blocked',
                 'page-table register access blocked','code page write blocked','data execution blocked by NX',
                 'stack guard blocked','read-only startup page write blocked',
                 'kernel validates syscall pointers and sizes','healthy app runs after all faults']:
        assert 'PASS '+name in output,output
    print(output)
    command('arch','ABI: PASS')
    with socket.socket(socket.AF_UNIX,socket.SOCK_STREAM) as monitor:
        monitor.settimeout(3); monitor.connect(str(monitor_path))

        def prompt():
            data=bytearray()
            while not data.endswith(b'(qemu) '):
                chunk=monitor.recv(65536); assert chunk
                data.extend(chunk)
            return re.sub(r'\x1b\[[0-9;]*[A-Za-z]','',data.decode(errors='replace'))

        prompt()
        def inspect(text):
            monitor.sendall(text.encode()+b'\n'); return prompt()

        def cr3():
            registers=inspect('info registers')
            return int(re.search(r'CR3=([0-9a-fA-F]+)',registers)[1],16)

        def word(address):
            result=inspect(f'xp /1gx {address:#x}')
            return int(re.findall(r'0x([0-9a-fA-F]{16})',result)[0],16)

        physical_mask=0x000ffffffffff000
        assert cr3()==0x80000
        roots=[]; code_frames=[]
        for _ in range(2):
            command('ape','ape> ',b'ape> ')
            root=cr3(); roots.append(root); assert root!=0x80000
            pdpt=word(root); assert pdpt & 7 == 7
            pdpt &= physical_mask
            kernel=word(pdpt); assert kernel & 4 == 0 and kernel & physical_mask == 0x82000
            user_directory=word(pdpt+8); assert user_directory & 7 == 7
            user_directory &= physical_mask
            code_table=word(user_directory) & physical_mask
            code=word(code_table); code_frames.append(code & physical_mask)
            assert code & 5 == 5 and code & 2 == 0 and code >> 63 == 0
            stack_table=word(user_directory+4*8) & physical_mask
            startup=word(stack_table); assert startup & 5 == 5 and startup & 2 == 0 and startup >> 63 == 1
            assert word(stack_table+8)==0,'Stack guard must have no PTE'
            stack=word(stack_table+16); assert stack & 7 == 7 and stack >> 63 == 1
            command('tree 6 eek 7','42\n',b'ape> ')
            command('exit','Apelang finished')
            assert cr3()==0x80000,'Kernel address space must be restored after exit'
        assert roots[0]!=roots[1] and code_frames[0]!=code_frames[1],(roots,code_frames)
    command('calc','calc> ',b'calc> \r\n')
    command('add 5 2','7\n',b'calc> \r\n')
    command('exit','Exiting calculator')
    command('ape demo.ape','DEMO PASS')
    print('PASS private CR3/physical frames, supervisor kernel map, user RX/RW+NX, guard, and kernel recovery')
finally:
    p.terminate()
    try: p.wait(timeout=3)
    except subprocess.TimeoutExpired: p.kill(); p.wait()
    selector.close(); temporary.cleanup()
    log=pathlib.Path(os.environ.get('ISOLATION_LOG','/tmp/jordyos-isolation.log'))
    log.write_bytes(transcript); print(f'Transcript: {log}')
