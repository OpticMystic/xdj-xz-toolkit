import socket, sys, pathlib, subprocess
sys.path.insert(0, '.')
from tools.xz_nand_dump import open_telnet
W, H = 240, 960
total = W * H * 2
server = socket.socket(); server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
server.bind(('169.254.168.58', 4247)); server.listen(1); server.settimeout(15)
telnet = open_telnet('169.254.168.59')
telnet.sendall(f"dd if=/dev/fb1 bs={total} count=1 2>/dev/null | nc 169.254.168.58 4247; exit\r\n".encode())
conn, _ = server.accept(); conn.settimeout(10)
data = bytearray()
while len(data) < total:
    chunk = conn.recv(min(262144, total - len(data)))
    if not chunk: break
    data.extend(chunk)
conn.close(); telnet.close(); server.close()
raw = pathlib.Path('captures/jog-fb1.rgb565'); raw.parent.mkdir(exist_ok=True); raw.write_bytes(data)
print('bytes', len(data), 'nonzero', sum(1 for b in data if b))
subprocess.run(['ffmpeg','-y','-loglevel','error','-f','rawvideo','-pixel_format','rgb565le','-video_size',f'{W}x{H}','-i',str(raw),'-frames:v','1','captures/jog-fb1.png'], check=True)
print('wrote captures/jog-fb1.png')
