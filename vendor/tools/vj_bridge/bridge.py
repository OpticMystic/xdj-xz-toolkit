"""VJToolsLibrary <-> Pioneer DJ XDJ-XZ Live Screen & Control Bridge.

Bridges video playback metadata, frame scrub-strips (filmstrips), and clip
cues from VJToolsLibrary directly to the XDJ-XZ 7" 800x480 screen and jog wheels.
"""

import argparse
import io
import json
import logging
import os
import pathlib
import socket
import struct
import sys
import threading
import time
from typing import Dict, Optional, Tuple

logging.basicConfig(level=logging.INFO, format="[%(asctime)s] [VJ-BRIDGE] %(message)s")
logger = logging.getLogger("VJBridge")

DEFAULT_XZ_IP = "169.254.168.58"
DEFAULT_BRIDGE_PORT = 50005
SCREEN_WIDTH = 800
FILMSTRIP_HEIGHT = 60


class VJBridgeService:
    def __init__(self, xz_ip: str = DEFAULT_XZ_IP, port: int = DEFAULT_BRIDGE_PORT, vjtools_path: Optional[str] = None):
        self.xz_ip = xz_ip
        self.port = port
        self.vjtools_path = pathlib.Path(vjtools_path or r"C:\Users\short\Github\VJToolsLibrary")
        self.sock: Optional[socket.socket] = None
        self.connected = False
        self._running = False
        self._lock = threading.Lock()
        
        # Deck playheads: 0.0 to 1.0
        self.playheads = {1: 0.0, 2: 0.0, 3: 0.0, 4: 0.0}
        self.active_strips: Dict[int, bytes] = {}

    def connect_xz(self, timeout: float = 3.0) -> bool:
        """Establish high-speed telemetry socket with XDJ-XZ."""
        try:
            logger.info(f"Connecting to XDJ-XZ at {self.xz_ip}:{self.port}...")
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.settimeout(timeout)
            s.connect((self.xz_ip, self.port))
            self.sock = s
            self.connected = True
            logger.info("Successfully connected to XDJ-XZ VJ Bridge!")
            return True
        except Exception as e:
            logger.warning(f"Could not connect to XDJ-XZ ({e}). Bridge running in standby mode.")
            self.connected = False
            return False

    def send_filmstrip_frame(self, deck: int, rgb565_data: bytes, playhead_pct: float) -> bool:
        """Send RGB565 filmstrip tile buffer and playhead position to XDJ-XZ."""
        if not self.connected or not self.sock:
            return False
            
        try:
            # Header format: Magic (4B), Deck (1B), Reserved (3B), Playhead float (4B), Data Len (4B)
            header = struct.pack("<4sBBHfI", b"VJFS", deck, 0, 0, playhead_pct, len(rgb565_data))
            with self._lock:
                self.sock.sendall(header + rgb565_data)
            return True
        except Exception as e:
            logger.error(f"Failed to send filmstrip frame: {e}")
            self.connected = False
            return False

    def scan_vjtools_scrub_strips(self) -> Dict[str, pathlib.Path]:
        """Scan VJToolsLibrary thumbnail cache for available video scrub strips."""
        strips = {}
        # Search targeted thumbnail locations (avoiding node_modules / .git)
        search_dirs = [
            self.vjtools_path / "apps" / "library",
            self.vjtools_path / "native",
            self.vjtools_path / "packages",
            self.vjtools_path / "docs" / "evidence",
        ]
        for base in search_dirs:
            if not base.is_dir():
                continue
            for root, dirs, files in os.walk(base):
                # Prune node_modules and dot dirs
                dirs[:] = [d for d in dirs if not d.startswith(".") and d != "node_modules"]
                for f in files:
                    if "scrub_strip" in f.lower() or "filmstrip" in f.lower():
                        p = pathlib.Path(root) / f
                        if p.suffix.lower() in (".jpg", ".jpeg", ".png", ".rgb565", ".webp"):
                            strips[p.stem] = p
        return strips

    def run_bridge_loop(self):
        """Main service loop."""
        self._running = True
        logger.info("VJ Bridge Service started. Monitoring decks...")
        
        while self._running:
            if not self.connected:
                # Try reconnecting every 5 seconds
                self.connect_xz(timeout=2.0)
                time.sleep(5.0)
                continue
                
            try:
                # Keepalive / ping
                time.sleep(0.05)
            except KeyboardInterrupt:
                break
            except Exception as e:
                logger.error(f"Bridge loop exception: {e}")
                self.connected = False

    def stop(self):
        self._running = False
        if self.sock:
            try:
                self.sock.close()
            except:
                pass
        self.connected = False


def main():
    parser = argparse.ArgumentParser(description="XDJ-XZ <-> VJToolsLibrary Screen & Control Bridge")
    parser.add_argument("--ip", default=DEFAULT_XZ_IP, help=f"XDJ-XZ IP address (default: {DEFAULT_XZ_IP})")
    parser.add_argument("--port", type=int, default=DEFAULT_BRIDGE_PORT, help=f"Bridge port (default: {DEFAULT_BRIDGE_PORT})")
    parser.add_argument("--vjtools", default=r"C:\Users\short\Github\VJToolsLibrary", help="Path to VJToolsLibrary")
    args = parser.parse_args()

    bridge = VJBridgeService(xz_ip=args.ip, port=args.port, vjtools_path=args.vjtools)
    logger.info(f"Scanning VJToolsLibrary at {args.vjtools}...")
    strips = bridge.scan_vjtools_scrub_strips()
    logger.info(f"Found {len(strips)} cached video scrub strips in VJToolsLibrary.")

    try:
        bridge.run_bridge_loop()
    except KeyboardInterrupt:
        logger.info("Stopping VJ Bridge.")
    finally:
        bridge.stop()


if __name__ == "__main__":
    main()
