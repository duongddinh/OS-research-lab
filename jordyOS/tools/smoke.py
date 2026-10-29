"""Boot the actual image and exercise its shell over COM1; no host runtime involved."""
import os
import pathlib
import selectors
import subprocess
import time

qemu=os.environ.get("QEMU", "qemu-system-i386")
p=subprocess.Popen([qemu,"-drive","if=floppy,format=raw,file=os.img","-snapshot",
    "-boot","a","-m","32","-display","none","-monitor","none","-serial","stdio",
    "-nic","none","-no-reboot"],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
selector=selectors.DefaultSelector(); selector.register(p.stdout,selectors.EVENT_READ)
transcript=bytearray()
def until(marker,timeout=15):
    data=bytearray(); end=time.monotonic()+timeout
    while not data.endswith(marker) and time.monotonic()<end:
        if selector.select(.1):
            chunk=os.read(p.stdout.fileno(),65536)
            if not chunk: break
            data.extend(chunk); transcript.extend(chunk)
    if not data.endswith(marker): raise AssertionError(f"Missing {marker!r}; output: {data!r}")
    return data.decode(errors="replace").replace("\r","")
def command(line,expected,marker=b"sh> "):
    p.stdin.write(line.encode()+b"\n"); p.stdin.flush()
    text=until(marker)
    assert expected in text, text
    assert "Apelang stopped:" not in text, text
    print(f"PASS {line[:90]}")
try:
    until(b"desktop> ")
    p.stdin.write(b"4"); p.stdin.flush(); until(b"sh> ")
    command("ape demo.ape","DEMO PASS")
    command("compile helper.ape","Compiled: helper.apb")
    command("ape module.ape","42\nMODULE PASS")
    command("compile demo.ape","Compiled: demo.apb")
    command("apb demo.apb","DEMO PASS")
    command("ape","Apelang on jordyOS",b"ape> ")
    command("tribe twice(n) { give n eek 2 }","ape> ",b"ape> ")
    command("tree twice(21)","42\n",b"ape> ")
    command('tumble { tree tally(12) } catch (e) { tree "CAUGHT" }','CAUGHT\n',b"ape> ")
    command("tree tally(12)","Runtime Error",b"ape> ")
    command("tree twice(3)","6\n",b"ape> ")
    command('tree tally(shed(""))',"0\n",b"ape> ")
    command('ape m = {} '+ ' '.join(f'm["k{i}"] = {i}' for i in range(40))+' tree m["k39"]',"39\n",b"ape> ")
    command('swing 100000 { ape tmp = graft("banana", "split") } tree twice(21)',"42\n",b"ape> ")
    command('banana (true) {}',"Instruction budget exceeded",b"ape> ")
    command('tree "RECOVERED"',"RECOVERED\n",b"ape> ")
    command('exit',"Apelang finished")
    command('calc',"calc> ",b"calc> \r\n")
    command('add 5 2',"7\n",b"calc> \r\n")
    command('exit',"Exiting calculator")
    for _ in range(3): command('ape demo.ape',"DEMO PASS")
    command('edit',"edit# ",b"edit# \r\n")
    command('new note.ape',"New file in buffer",b"edit# \r\n")
    command('edit',"A single . finishes",b"buffer.\r\n")
    p.stdin.write(b'tree 123\n.\n'); p.stdin.flush(); until(b"edit# \r\n")
    command('save',"File saved",b"edit# \r\n")
    command('open note.ape',"File opened",b"edit# \r\n")
    command('show',"tree 123",b"edit# \r\n")
    command('list',"note.ape",b"edit# \r\n")
    command('quit',"Exiting editor")
    command('ape note.ape',"123\n")
    command('edit',"edit# ",b"edit# \r\n")
    command('delete note.ape',"File deleted",b"edit# \r\n")
    command('quit',"Exiting editor")
    command('desktop',"Desktop:",b"desktop> ")
    p.stdin.write(b"1"); p.stdin.flush(); until(b"calc> \r\n")
    command('mul 6 7',"42\n",b"calc> \r\n")
    command('exit',"Exiting calculator",b"desktop> ")
    p.stdin.write(b"2"); p.stdin.flush(); until(b"edit# \r\n")
    command('quit',"Exiting editor",b"desktop> ")
    p.stdin.write(b"3"); p.stdin.flush(); until(b"ape> ")
    command('tree 42',"42\n",b"ape> ")
    command('exit',"Apelang finished",b"desktop> ")
    p.stdin.write(b"4"); p.stdin.flush(); until(b"sh> ")
    command('snake',"Snake ready.",b"exits.\r\n")
    p.stdin.write(b"\n"); p.stdin.flush()
    time.sleep(.6)
    p.stdin.write(b"q"); p.stdin.flush()
    assert "Snake closed." in until(b"sh> ")
    print("PASS Snake graphics, nonblocking input, timing, and exit")
    command('desktop',"Desktop:",b"desktop> ")
    p.stdin.write(b"5"); p.stdin.flush(); until(b"exits.\r\n")
    p.stdin.write(b"q"); p.stdin.flush(); until(b"desktop> ")
    print("PASS Snake desktop launcher")
    p.stdin.write(b"6"); p.stdin.flush(); until(b"closes.\r\n")
    p.stdin.write(b"q"); p.stdin.flush()
    assert "Project history closed." in until(b"desktop> ")
    print("PASS history.ape desktop app")
    print("All QEMU integration checks passed.")
finally:
    p.terminate()
    try: p.wait(timeout=3)
    except subprocess.TimeoutExpired: p.kill(); p.wait()
    selector.close()
    log=pathlib.Path(os.environ.get('SMOKE_LOG','/tmp/jordyos-smoke.log'))
    log.write_bytes(transcript)
    print(f"Transcript: {log}")
