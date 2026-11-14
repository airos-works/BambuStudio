#!/usr/bin/env python3
"""A mock Bambu Lab P1S for testing printing without a printer.

    tools/haiku/test/mock-printer.py [--state DIR] [--serial SN] [--code CODE]

It answers what Bambu Studio and its network plug-in (Open Bamboo Networking)
use to print over the LAN to a printer in LAN-only + Developer Mode:

- SSDP: NOTIFY announcements every 5 s, sent by unicast to --ssdp-target
  (default 127.0.0.1:12021, which tools/haiku/test/run-mock-vm.sh forwards
  to the guest's UDP port 2021: QEMU's NAT carries no broadcasts);
- TCP 3000: the "detect" request of "connect via IP" (A5 A5 | u16le length |
  JSON | A7 A7 frames);
- MQTT over TLS (port 8883): login `bblp` / access code, the topics
  device/<SN>/request and device/<SN>/report; answers pushall with a full
  P1S push_status, get_version with a module list, project_file with an ack,
  and then plays a short print (PREPARE, RUNNING with progress, FINISH);
  every other command is acknowledged;
- implicit FTPS (port 990): USER/PASS, TYPE, PBSZ, PROT, PASV/EPSV, STOR,
  LIST, SIZE, DELE...; data connections are wrapped in TLS after the 150
  reply, as the printers do; uploads land in DIR/uploads;
- TLS port 6000: the P1 camera (80-byte 0x3000 login, then 16-byte header +
  JPEG frames, drawn with Pillow when it is installed: a clock and the print
  progress) and enough of the file browser's CTRL protocol to answer
  (login, setup, empty file lists).

The TLS certificate has CN = serial and is signed by a CA of its own
(DIR/mock-ca.pem): add that to the client's printer CA bundle (Bambu Studio's
resources/cert/printer.cer) so that the plug-in verifies the mock as it
verifies a printer. Ports listen on 127.0.0.1 at --port-offset + the
printer's port (18883, 10990, 13000; passive data 15000 for the printer's
50000), the mapping run-mock-vm.sh's guestfwd rules use. Every event is
printed and appended to DIR/events.jsonl as one JSON object per line
("mqtt_connect", "mqtt_request", "ftp_stor"...), which tests read.

Needs only Python 3.8+ and the openssl command (for the certificates).
"""
import argparse
import hashlib
import json
import os
import pathlib
import random
import socket
import socketserver
import ssl
import struct
import subprocess
import sys
import threading
import time

ARGS = None
STATE = None
EVENTS_LOCK = threading.Lock()


def event(kind, **fields):
    record = {'time': round(time.time(), 3), 'event': kind, **fields}
    line = json.dumps(record, ensure_ascii=False)
    with EVENTS_LOCK:
        print(line, flush=True)
        with open(STATE / 'events.jsonl', 'a', encoding='utf-8') as f:
            f.write(line + '\n')


# --- certificates ------------------------------------------------------------

def make_certificates(serial):
    ca_key, ca_pem = STATE / 'mock-ca.key', STATE / 'mock-ca.pem'
    key, crt = STATE / 'printer.key', STATE / f'printer-{serial}.pem'
    run = lambda *a: subprocess.run(['openssl', *a], check=True, capture_output=True)
    if not ca_pem.exists():
        run('req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-keyout', str(ca_key),
            '-out', str(ca_pem), '-days', '3650', '-subj', '/O=Mock Printer Test CA/CN=Mock BBL CA',
            '-addext', 'basicConstraints=critical,CA:TRUE',
            '-addext', 'keyUsage=critical,keyCertSign,cRLSign')
    if not crt.exists():
        csr = STATE / 'printer.csr'
        ext = STATE / 'printer.ext'
        run('req', '-newkey', 'rsa:2048', '-nodes', '-keyout', str(key), '-out', str(csr),
            '-subj', f'/CN={serial}')
        ext.write_text(f'subjectAltName=DNS:{serial}\nbasicConstraints=CA:FALSE\n'
                       'keyUsage=critical,digitalSignature,keyEncipherment\n'
                       'extendedKeyUsage=serverAuth\n')
        run('x509', '-req', '-in', str(csr), '-CA', str(ca_pem), '-CAkey', str(ca_key),
            '-CAcreateserial', '-out', str(crt), '-days', '3650', '-extfile', str(ext))
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    ctx.load_cert_chain(str(crt), str(key))
    return ctx


# --- the printer's state -----------------------------------------------------

class Printer:
    """push_status of an idle P1S with an AMS, and a simulated print."""

    def __init__(self, serial):
        self.serial = serial
        self.lock = threading.Lock()
        self.status = {
            'command': 'push_status', 'msg': 0, 'sequence_id': '0',
            'gcode_state': 'IDLE', 'gcode_file': '', 'subtask_name': '',
            'subtask_id': '0', 'task_id': '0', 'project_id': '0', 'profile_id': '0',
            'print_type': 'idle', 'stg_cur': -1, 'stg': [], 'mc_print_stage': '1',
            'mc_percent': 0, 'mc_remaining_time': 0, 'mc_print_sub_stage': 0,
            'layer_num': 0, 'total_layer_num': 0,
            'bed_temper': 24.1, 'bed_target_temper': 0.0,
            'nozzle_temper': 25.3, 'nozzle_target_temper': 0.0,
            'chamber_temper': 24.0, 'cooling_fan_speed': '0', 'big_fan1_speed': '0',
            'big_fan2_speed': '0', 'heatbreak_fan_speed': '0', 'fan_gear': 0,
            'spd_lvl': 2, 'spd_mag': 100, 'nozzle_diameter': '0.4',
            'nozzle_type': 'stainless_steel', 'wifi_signal': '-52dBm',
            'sdcard': True, 'home_flag': 6292888, 'hw_switch_state': 0,
            'print_error': 0, 'mc_print_error_code': '0', 'hms': [],
            'lifecycle': 'product', 'print_gcode_action': 0, 'queue_number': 0,
            'gcode_file_prepare_percent': '0', 'force_upgrade': False,
            'lights_report': [{'node': 'chamber_light', 'mode': 'on'}],
            'ipcam': {'ipcam_dev': '1', 'ipcam_record': 'enable', 'timelapse': 'disable',
                      'resolution': '1080p', 'tutk_server': 'disable', 'mode_bits': 3,
                      'file': {'local': 'local', 'remote': 'none', 'model_download': 'enable'}},
            'upload': {'status': 'idle', 'progress': 0, 'message': ''},
            # the printer's own address, as a little-endian integer (Studio
            # takes the camera's and the file browser's address from it)
            'net': {'conf': 16, 'info': [{'ip': sum(int(b) << (8 * i) for i, b in
                                                    enumerate(ARGS.printer_ip.split('.'))),
                                          'mask': 16777215}]},
            'online': {'ahb': False, 'rfid': False, 'version': 7},
            'xcam': {'allow_skip_parts': False, 'buildplate_marker_detector': True,
                     'first_layer_inspector': False, 'halt_print_sensitivity': 'medium',
                     'print_halt': True, 'printing_monitor': True, 'spaghetti_detector': True},
            'upgrade_state': {'sequence_id': 0, 'progress': '', 'status': '', 'consistency_request': False,
                              'dis_state': 0, 'err_code': 0, 'force_upgrade': False, 'message': '',
                              'module': '', 'new_version_state': 2, 'new_ver_list': []},
            'ams': {
                'ams_exist_bits': '1', 'tray_exist_bits': 'f', 'tray_is_bbl_bits': 'f',
                'tray_read_done_bits': 'f', 'tray_reading_bits': '0', 'tray_now': '255',
                'tray_pre': '255', 'tray_tar': '255', 'version': 402, 'insert_flag': True,
                'power_on_flag': False,
                'ams': [{'id': '0', 'humidity': '4', 'temp': '24.5', 'tray': [
                    {'id': '0', 'tray_type': 'PLA', 'tray_sub_brands': 'PLA Basic',
                     'tray_color': 'FFFFFFFF', 'tray_info_idx': 'GFA00', 'remain': 71,
                     'nozzle_temp_min': '190', 'nozzle_temp_max': '230', 'tray_diameter': '1.75'},
                    {'id': '1', 'tray_type': 'PLA', 'tray_sub_brands': 'PLA Basic',
                     'tray_color': '00AE42FF', 'tray_info_idx': 'GFA00', 'remain': 92,
                     'nozzle_temp_min': '190', 'nozzle_temp_max': '230', 'tray_diameter': '1.75'},
                    {'id': '2', 'tray_type': 'PETG', 'tray_sub_brands': 'PETG Basic',
                     'tray_color': '0A2989FF', 'tray_info_idx': 'GFG00', 'remain': 34,
                     'nozzle_temp_min': '220', 'nozzle_temp_max': '260', 'tray_diameter': '1.75'},
                    {'id': '3', 'tray_type': 'PLA', 'tray_sub_brands': 'PLA Matte',
                     'tray_color': 'F5A623FF', 'tray_info_idx': 'GFA01', 'remain': 55,
                     'nozzle_temp_min': '190', 'nozzle_temp_max': '230', 'tray_diameter': '1.75'},
                ]}],
            },
            'vt_tray': {'id': '254', 'tray_type': '', 'tray_color': '00000000',
                        'tray_info_idx': '', 'remain': 0},
        }
        self.job = None

    def full(self, seq):
        with self.lock:
            s = json.loads(json.dumps(self.status))
        s['msg'] = 0
        s['sequence_id'] = str(seq)
        return {'print': s}

    def update(self, **changes):
        with self.lock:
            self.status.update(changes)

    def start_job(self, cmd, broker):
        name = cmd.get('subtask_name') or 'print'
        self.update(gcode_state='PREPARE', subtask_name=name, gcode_file=cmd.get('url', ''),
                    print_type='local', mc_percent=0, mc_remaining_time=12, layer_num=0,
                    total_layer_num=240, stg_cur=2, task_id=str(cmd.get('task_id', '0')),
                    subtask_id=str(cmd.get('subtask_id', '0')),
                    project_id=str(cmd.get('project_id', '0')),
                    bed_target_temper=55.0, nozzle_target_temper=220.0)
        job = object()
        self.job = job

        def play():
            time.sleep(4)
            for step in range(0, 101, 10):
                if self.job is not job:
                    return
                if self.status['gcode_state'] == 'PAUSE':
                    time.sleep(1)
                    continue
                self.update(gcode_state='RUNNING', stg_cur=0, mc_percent=step,
                            mc_remaining_time=max(0, 12 - step // 10), layer_num=step * 240 // 100,
                            bed_temper=55.0, nozzle_temper=219.6)
                broker.report_delta(gcode_state='RUNNING', stg_cur=0, mc_percent=step,
                                    mc_remaining_time=max(0, 12 - step // 10),
                                    layer_num=step * 240 // 100, bed_temper=55.0, nozzle_temper=219.6)
                time.sleep(ARGS.print_step)
            if self.job is job:
                self.update(gcode_state='FINISH', stg_cur=-1, mc_percent=100, mc_remaining_time=0,
                            bed_target_temper=0.0, nozzle_target_temper=0.0)
                broker.report_delta(gcode_state='FINISH', stg_cur=-1, mc_percent=100,
                                    mc_remaining_time=0, bed_target_temper=0.0,
                                    nozzle_target_temper=0.0)
                event('print_finished', subtask_name=name)

        threading.Thread(target=play, daemon=True).start()


# --- MQTT broker (3.1.1, enough of 5) ---------------------------------------

def read_exact(sock, n):
    data = b''
    while len(data) < n:
        chunk = sock.recv(n - len(data))
        if not chunk:
            raise ConnectionError('closed')
        data += chunk
    return data


def read_packet(sock):
    header = read_exact(sock, 1)[0]
    length, shift = 0, 0
    while True:
        byte = read_exact(sock, 1)[0]
        length |= (byte & 0x7f) << shift
        shift += 7
        if not byte & 0x80:
            break
    return header, read_exact(sock, length) if length else b''


def encode_length(n):
    out = bytearray()
    while True:
        byte = n % 128
        n //= 128
        out.append(byte | (0x80 if n else 0))
        if not n:
            return bytes(out)


def mqtt_string(s):
    b = s.encode()
    return struct.pack('>H', len(b)) + b


class Broker:
    def __init__(self, printer, ctx):
        self.printer = printer
        self.ctx = ctx
        self.clients = []  # (session, topics)
        self.lock = threading.Lock()
        self.seq = 1000

    def next_seq(self):
        with self.lock:
            self.seq += 1
            return self.seq

    def publish(self, payload):
        topic = f'device/{self.printer.serial}/report'
        data = json.dumps(payload).encode()
        with self.lock:
            sessions = [s for s in self.clients if s.subscribed(topic)]
        for s in sessions:
            s.send_publish(topic, data)

    def report_delta(self, **fields):
        self.publish({'print': {'command': 'push_status', 'msg': 1,
                                'sequence_id': str(self.next_seq()), **fields}})

    def serve(self, port):
        broker = self

        class Handler(socketserver.BaseRequestHandler):
            def handle(self):
                try:
                    tls = broker.ctx.wrap_socket(self.request, server_side=True)
                except (ssl.SSLError, OSError) as e:
                    event('mqtt_tls_failed', error=str(e))
                    return
                MqttSession(broker, tls, self.client_address).run()

        server = socketserver.ThreadingTCPServer(('127.0.0.1', port), Handler)
        server.daemon_threads = True
        threading.Thread(target=server.serve_forever, daemon=True).start()
        return server


class MqttSession:
    def __init__(self, broker, sock, address):
        self.broker = broker
        self.sock = sock
        self.address = address
        self.topics = []
        self.version = 4
        self.send_lock = threading.Lock()
        self.client_id = ''

    def subscribed(self, topic):
        return any(t == topic or t == '#' or (t.endswith('/#') and topic.startswith(t[:-1]))
                   for t in self.topics)

    def send(self, data):
        with self.send_lock:
            self.sock.sendall(data)

    def send_publish(self, topic, payload):
        body = mqtt_string(topic)
        if self.version == 5:
            body += b'\x00'  # no properties
        body += payload
        try:
            self.send(bytes([0x30]) + encode_length(len(body)) + body)
        except OSError:
            pass

    def run(self):
        try:
            self.loop()
        except (ConnectionError, OSError, ssl.SSLError):
            pass
        finally:
            with self.broker.lock:
                if self in self.broker.clients:
                    self.broker.clients.remove(self)
            event('mqtt_disconnect', client_id=self.client_id)
            try:
                self.sock.close()
            except OSError:
                pass

    def loop(self):
        header, body = read_packet(self.sock)
        if header >> 4 != 1:
            return
        pos = 2 + struct.unpack('>H', body[:2])[0]
        self.version = body[pos]
        flags = body[pos + 1]
        pos += 4  # level, flags, keepalive
        if self.version == 5:
            plen, shift, n = 0, 0, 0
            while True:
                b = body[pos + n]
                plen |= (b & 0x7f) << shift
                n += 1
                shift += 7
                if not b & 0x80:
                    break
            pos += n + plen

        def field():
            nonlocal pos
            n = struct.unpack('>H', body[pos:pos + 2])[0]
            value = body[pos + 2:pos + 2 + n]
            pos += 2 + n
            return value

        self.client_id = field().decode(errors='replace')
        if flags & 0x04:  # will
            if self.version == 5:
                pos += 1 + body[pos]  # will properties (short)
            field()
            field()
        user = field().decode(errors='replace') if flags & 0x80 else ''
        password = field().decode(errors='replace') if flags & 0x40 else ''
        ok = user == 'bblp' and password == ARGS.code
        event('mqtt_connect', client_id=self.client_id, user=user, accepted=ok,
              protocol=self.version)
        if self.version == 5:
            self.send(bytes([0x20, 3, 0, 0 if ok else 0x86, 0]))
        else:
            self.send(bytes([0x20, 2, 0, 0 if ok else 5]))
        if not ok:
            return
        with self.broker.lock:
            self.broker.clients.append(self)
        while True:
            header, body = read_packet(self.sock)
            kind = header >> 4
            if kind == 3:  # PUBLISH
                qos = (header >> 1) & 3
                n = struct.unpack('>H', body[:2])[0]
                topic = body[2:2 + n].decode()
                pos = 2 + n
                packet_id = None
                if qos:
                    packet_id = body[pos:pos + 2]
                    pos += 2
                if self.version == 5:
                    pos += 1 + body[pos]
                payload = body[pos:]
                if qos == 1:
                    self.send(b'\x40\x02' + packet_id)
                elif qos == 2:
                    self.send(b'\x50\x02' + packet_id)
                self.on_request(topic, payload)
            elif kind == 6:  # PUBREL
                self.send(b'\x70\x02' + body[:2])
            elif kind == 8:  # SUBSCRIBE
                packet_id = body[:2]
                pos = 2
                if self.version == 5:
                    pos += 1 + body[pos]
                codes = b''
                while pos < len(body):
                    n = struct.unpack('>H', body[pos:pos + 2])[0]
                    topic = body[pos + 2:pos + 2 + n].decode()
                    qos = body[pos + 2 + n] & 3
                    pos += 3 + n
                    self.topics.append(topic)
                    codes += bytes([min(qos, 1)])
                    event('mqtt_subscribe', topic=topic)
                rest = packet_id + (b'\x00' if self.version == 5 else b'') + codes
                self.send(bytes([0x90]) + encode_length(len(rest)) + rest)
            elif kind == 10:  # UNSUBSCRIBE
                rest = body[:2] + (b'\x00' if self.version == 5 else b'')
                self.send(bytes([0xb0]) + encode_length(len(rest)) + rest)
            elif kind == 12:  # PINGREQ
                self.send(b'\xd0\x00')
            elif kind == 14:  # DISCONNECT
                return

    def on_request(self, topic, payload):
        printer, broker = self.broker.printer, self.broker
        if topic != f'device/{printer.serial}/request':
            event('mqtt_publish_ignored', topic=topic)
            return
        try:
            message = json.loads(payload)
        except ValueError:
            event('mqtt_bad_json', payload=payload[:200].decode(errors='replace'))
            return
        for section, cmd in message.items():
            if not isinstance(cmd, dict):
                continue
            name = cmd.get('command', '')
            seq = cmd.get('sequence_id', '0')
            event('mqtt_request', section=section, command=name, sequence_id=seq,
                  body=cmd if name != 'push_status' else None)
            if section == 'pushing' and name == 'pushall':
                broker.publish(printer.full(seq))
            elif section == 'info' and name == 'get_version':
                broker.publish({'info': {'command': 'get_version', 'sequence_id': seq, 'module': [
                    {'name': 'ota', 'project_name': 'C12', 'product_name': 'Bambu Lab P1S',
                     'sw_ver': '01.08.02.00', 'hw_ver': 'OTA', 'sn': printer.serial, 'flag': 0},
                    {'name': 'mc', 'project_name': 'C12', 'sw_ver': '00.00.31.48', 'hw_ver': 'MC02',
                     'sn': printer.serial, 'flag': 0},
                    {'name': 'th', 'project_name': 'C12', 'sw_ver': '00.00.06.39', 'hw_ver': 'TH03',
                     'sn': printer.serial, 'flag': 0},
                    {'name': 'ams/0', 'project_name': '', 'product_name': 'AMS', 'sw_ver': '00.00.06.49',
                     'hw_ver': 'AMS08', 'sn': '006A0000000000', 'flag': 0}]}})
            elif section == 'system' and name == 'get_access_code':
                broker.publish({'system': {'command': name, 'sequence_id': seq,
                                           'result': 'success', 'access_code': ARGS.code}})
            elif section == 'print' and name == 'project_file':
                broker.publish({'print': {'command': 'project_file', 'sequence_id': seq,
                                          'param': cmd.get('param', ''), 'subtask_name':
                                          cmd.get('subtask_name', ''), 'result': 'success',
                                          'reason': 'success'}})
                event('print_started', subtask_name=cmd.get('subtask_name', ''),
                      url=cmd.get('url', ''), param=cmd.get('param', ''),
                      use_ams=cmd.get('use_ams'), ams_mapping=cmd.get('ams_mapping'))
                printer.start_job(cmd, broker)
                broker.report_delta(gcode_state='PREPARE', subtask_name=cmd.get('subtask_name', ''),
                                    stg_cur=2, print_type='local', mc_percent=0)
            elif section == 'print' and name in ('stop', 'pause', 'resume'):
                state = {'stop': 'IDLE', 'pause': 'PAUSE', 'resume': 'RUNNING'}[name]
                if name == 'stop':
                    printer.job = None
                printer.update(gcode_state=state)
                broker.publish({'print': {'command': name, 'sequence_id': seq,
                                          'result': 'success', 'reason': 'success'}})
                broker.report_delta(gcode_state=state)
            else:
                reply = {'command': name, 'sequence_id': seq, 'result': 'success',
                         'reason': 'success', 'err_code': 0}
                broker.publish({section: reply})


# --- implicit FTPS -----------------------------------------------------------

class PassiveListener:
    """The one data port (one transfer at a time, as on the printer)."""

    def __init__(self, port):
        self.sock = socket.socket()
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.sock.bind(('127.0.0.1', port))
        self.sock.listen(4)
        self.lock = threading.Lock()

    def accept(self, timeout=15):
        self.sock.settimeout(timeout)
        conn, _ = self.sock.accept()
        conn.settimeout(60)
        return conn


def serve_ftps(port, ctx, passive):
    uploads = STATE / 'uploads'
    uploads.mkdir(exist_ok=True)

    class Handler(socketserver.StreamRequestHandler):
        def setup(self):
            try:
                self.request = ctx.wrap_socket(self.request, server_side=True)
            except (ssl.SSLError, OSError) as e:
                event('ftp_tls_failed', error=str(e))
                raise
            super().setup()

        def reply(self, text):
            self.wfile.write((text + '\r\n').encode())
            self.wfile.flush()

        def data_connection(self):
            conn = passive.accept()
            return conn

        def handle(self):
            self.reply('220 Mock Bambu P1S FTP server ready')
            user, logged_in, cwd = '', False, '/'
            while True:
                line = self.rfile.readline()
                if not line:
                    return
                line = line.decode(errors='replace').rstrip('\r\n')
                verb, _, arg = line.partition(' ')
                verb = verb.upper()
                event('ftp_command', command=verb, argument=arg if verb != 'PASS' else '***')
                if verb == 'USER':
                    user = arg
                    self.reply('331 Password required')
                elif verb == 'PASS':
                    logged_in = user == 'bblp' and arg == ARGS.code
                    self.reply('230 User logged in' if logged_in else '530 Login incorrect')
                elif not logged_in and verb not in ('QUIT', 'FEAT', 'SYST'):
                    self.reply('530 Please login with USER and PASS')
                elif verb in ('TYPE', 'PBSZ', 'PROT', 'MODE', 'STRU', 'NOOP', 'OPTS'):
                    self.reply('200 OK')
                elif verb == 'SYST':
                    self.reply('215 UNIX Type: L8')
                elif verb == 'FEAT':
                    self.reply('211-Features:\r\n PASV\r\n SIZE\r\n UTF8\r\n211 End')
                elif verb == 'PWD':
                    self.reply(f'257 "{cwd}" is the current directory')
                elif verb == 'CWD':
                    cwd = arg if arg.startswith('/') else cwd.rstrip('/') + '/' + arg
                    self.reply('250 OK')
                elif verb == 'MKD':
                    self.reply(f'257 "{arg}" created')
                elif verb == 'PASV':
                    p = ARGS.passive_port
                    ip = ARGS.printer_ip.replace('.', ',')
                    self.reply(f'227 Entering Passive Mode ({ip},{p >> 8},{p & 255})')
                elif verb == 'EPSV':
                    self.reply(f'229 Entering Extended Passive Mode (|||{ARGS.passive_port}|)')
                elif verb == 'SIZE':
                    f = uploads / pathlib.PurePosixPath(arg).name
                    self.reply(f'213 {f.stat().st_size}' if f.exists() else '550 No such file')
                elif verb == 'DELE':
                    f = uploads / pathlib.PurePosixPath(arg).name
                    f.unlink(missing_ok=True)
                    self.reply('250 Deleted')
                elif verb in ('STOR', 'LIST', 'NLST', 'RETR'):
                    with passive.lock:
                        self.transfer(verb, arg, uploads)
                elif verb == 'QUIT':
                    self.reply('221 Bye')
                    return
                else:
                    self.reply('502 Command not implemented')

        def transfer(self, verb, arg, uploads):
            try:
                raw = self.data_connection()
            except OSError as e:
                event('ftp_data_failed', error=str(e))
                self.reply('425 No data connection')
                return
            self.reply('150 Opening BINARY mode data connection')
            try:
                data = ctx.wrap_socket(raw, server_side=True)
            except (ssl.SSLError, OSError) as e:
                event('ftp_data_tls_failed', error=str(e))
                raw.close()
                self.reply('425 TLS on the data connection failed')
                return
            try:
                if verb == 'STOR':
                    name = pathlib.PurePosixPath(arg).name or 'upload.bin'
                    digest, size = hashlib.md5(), 0
                    with open(uploads / name, 'wb') as f:
                        while True:
                            chunk = data.recv(65536)
                            if not chunk:
                                break
                            f.write(chunk)
                            digest.update(chunk)
                            size += len(chunk)
                    event('ftp_stor', path=arg, file=str(uploads / name), size=size,
                          md5=digest.hexdigest())
                elif verb in ('LIST', 'NLST'):
                    lines = []
                    for f in sorted(uploads.iterdir()):
                        if verb == 'NLST':
                            lines.append(f.name)
                        else:
                            lines.append(f'-rw-rw-rw- 1 root root {f.stat().st_size} '
                                         f'{time.strftime("%b %d %H:%M", time.localtime(f.stat().st_mtime))} {f.name}')
                    data.sendall(('\r\n'.join(lines) + ('\r\n' if lines else '')).encode())
                elif verb == 'RETR':
                    f = uploads / pathlib.PurePosixPath(arg).name
                    if f.exists():
                        data.sendall(f.read_bytes())
                try:
                    data.unwrap()
                except (ssl.SSLError, OSError):
                    pass
            finally:
                data.close()
            self.reply('226 Transfer complete')

    server = socketserver.ThreadingTCPServer(('127.0.0.1', port), Handler)
    server.daemon_threads = True
    threading.Thread(target=server.serve_forever, daemon=True).start()
    return server


# --- TLS 6000: camera (MJPEG) and file browser (CTRL) ---------------------------

def camera_frame(printer, n):
    try:
        from PIL import Image, ImageDraw
    except ImportError:
        return None
    import io
    img = Image.new('RGB', (1280, 720), (24, 28, 34))
    d = ImageDraw.Draw(img)
    for x in range(0, 1280, 80):
        d.line([(x, 0), (x, 720)], fill=(40, 46, 56))
    for y in range(0, 720, 80):
        d.line([(0, y), (1280, y)], fill=(40, 46, 56))
    st = printer.status
    x = 40 + (n * 23) % 1100
    d.rectangle([x, 520, x + 140, 600], fill=(0, 174, 66))
    lines = [f'Mock P1S camera  frame {n}', time.strftime('%H:%M:%S'),
             f"{st['gcode_state']}  {st['mc_percent']} %  {st['subtask_name']}"]
    for i, text in enumerate(lines):
        d.text((40, 40 + 40 * i), text, fill=(235, 235, 235))
    out = io.BytesIO()
    img.save(out, 'JPEG', quality=70)
    return out.getvalue()


def serve_port6000(port, ctx, printer):
    class Handler(socketserver.BaseRequestHandler):
        def handle(self):
            try:
                tls = ctx.wrap_socket(self.request, server_side=True)
                head = read_exact(tls, 16)
            except (ssl.SSLError, OSError, ConnectionError) as e:
                event('port6000_failed', error=str(e))
                return
            size, kind = struct.unpack('<II', head[:8])
            try:
                if kind == 0x3000:
                    self.camera(tls, read_exact(tls, size))
                elif kind == 0x0101013f:
                    self.ctrl(tls, read_exact(tls, size))
                else:
                    event('port6000_unknown', kind=hex(kind))
            except (ssl.SSLError, OSError, ConnectionError):
                pass
            finally:
                event('port6000_closed')

        def camera(self, tls, auth):
            user = auth[0:32].rstrip(b'\0').decode(errors='replace')
            code = auth[32:64].rstrip(b'\0').decode(errors='replace')
            ok = user == 'bblp' and code == ARGS.code
            event('camera_login', user=user, accepted=ok)
            if not ok:
                return
            n = 0
            while True:
                jpeg = camera_frame(printer, n)
                if jpeg is None:
                    event('camera_no_pillow')
                    return
                tls.sendall(struct.pack('<IIII', len(jpeg), 0, 1, 0) + jpeg)
                if n % 20 == 0:
                    event('camera_frame', frame=n, bytes=len(jpeg))
                n += 1
                time.sleep(0.5)

        def ctrl(self, tls, login):
            user = login[0:8].rstrip(b'\0').decode(errors='replace')
            code = login[8:16].rstrip(b'\0').decode(errors='replace')
            ok = user == 'bblp' and code == ARGS.code
            event('ctrl_login', user=user, accepted=ok)
            if not ok:
                return
            seq = 0

            def send(obj, magic=0x0002013f):
                nonlocal seq
                body = json.dumps(obj).encode()
                tls.sendall(struct.pack('<IIII', len(body), magic, seq, 0) + body)
                seq += 1
            tls.sendall(struct.pack('<IIII', 4, 0x0001013f, 0, 0) + bytes(4))
            seq += 1
            while True:
                size, magic = struct.unpack('<II', read_exact(tls, 16)[:8])
                body = read_exact(tls, size) if size else b''
                try:
                    request = json.loads(body.split(b'\n\n', 1)[0])
                except ValueError:
                    continue
                event('ctrl_request', mtype=request.get('mtype'), cmdtype=request.get('cmdtype'))
                if request.get('mtype') == 12291:
                    send({'mtype': 12291, 'sequence': request.get('sequence', 0), 'result': 0, 'reply': {}})
                elif request.get('cmdtype') == 1:
                    send({'cmdtype': 1, 'mtype': 12289, 'reply': {'file_lists': []}, 'result': 0,
                          'sequence': request.get('sequence', 0)})
                else:
                    send({'cmdtype': request.get('cmdtype'), 'mtype': 12289, 'reply': {}, 'result': 0,
                          'sequence': request.get('sequence', 0)})

    server = socketserver.ThreadingTCPServer(('127.0.0.1', port), Handler)
    server.daemon_threads = True
    threading.Thread(target=server.serve_forever, daemon=True).start()
    return server


# --- TCP 3000: detect ----------------------------------------------------------

def frame(payload):
    body = json.dumps(payload).encode()
    return b'\xa5\xa5' + struct.pack('<H', len(body) + 6) + body + b'\xa7\xa7'


def serve_detect(port, printer):
    class Handler(socketserver.BaseRequestHandler):
        def handle(self):
            self.request.settimeout(10)
            try:
                head = read_exact(self.request, 4)
                if head[:2] != b'\xa5\xa5':
                    return
                total = struct.unpack('<H', head[2:4])[0]
                body = read_exact(self.request, total - 4)
                request = json.loads(body[:-2])
            except (OSError, ConnectionError, ValueError) as e:
                event('detect_bad_request', error=str(e))
                return
            login = request.get('login', {})
            event('detect_request', command=login.get('command'))
            if login.get('command') == 'detect':
                self.request.sendall(frame({'login': {
                    'bind': 'free', 'command': 'detect', 'connect': 'lan', 'dev_cap': 1,
                    'id': printer.serial, 'model': 'C12', 'name': ARGS.name,
                    'sequence_id': random.randint(1000, 9999), 'version': '01.08.02.00'}}))

    server = socketserver.ThreadingTCPServer(('127.0.0.1', port), Handler)
    server.daemon_threads = True
    threading.Thread(target=server.serve_forever, daemon=True).start()
    return server


# --- SSDP ----------------------------------------------------------------------

def ssdp_loop(printer):
    host, _, port = ARGS.ssdp_target.rpartition(':')
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    message = ('NOTIFY * HTTP/1.1\r\n'
               'Host: 239.255.255.250:1990\r\n'
               'Server: UPnP/1.0\r\n'
               f'Location: {ARGS.printer_ip}\r\n'
               'NT: urn:bambulab-com:device:3dprinter:1\r\n'
               'NTS: ssdp:alive\r\n'
               f'USN: {printer.serial}\r\n'
               'Cache-Control: max-age=1800\r\n'
               'DevModel.bambu.com: C12\r\n'
               f'DevName.bambu.com: {ARGS.name}\r\n'
               'DevSignal.bambu.com: -52\r\n'
               'DevConnect.bambu.com: lan\r\n'
               'DevBind.bambu.com: free\r\n'
               'Devseclink.bambu.com: secure\r\n'
               'DevVersion.bambu.com: 01.08.02.00\r\n'
               'DevCap.bambu.com: 1\r\n\r\n').encode()
    while True:
        try:
            sock.sendto(message, (host, int(port)))
        except OSError as e:
            event('ssdp_send_failed', error=str(e))
        time.sleep(5)


def main():
    global ARGS, STATE
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('--state', default='/data1/scratch/bambustudio/mock-printer',
                        help='directory for certificates, uploads and events.jsonl')
    parser.add_argument('--serial', default='01P00A391800001')
    parser.add_argument('--code', default='12345678', help='the access code')
    parser.add_argument('--name', default='Mock P1S')
    parser.add_argument('--printer-ip', default='10.0.2.100',
                        help='the address the client reaches the mock at (SSDP Location, PASV)')
    parser.add_argument('--port-offset', type=int, default=10000,
                        help='host port = offset + printer port (8883 -> 18883, 990 -> 10990)')
    parser.add_argument('--passive-port', type=int, default=50000,
                        help='the data port announced by PASV (host: 15000)')
    parser.add_argument('--ssdp-target', default='127.0.0.1:12021')
    parser.add_argument('--print-step', type=float, default=3.0,
                        help='seconds per 10 %% of the simulated print')
    ARGS = parser.parse_args()
    STATE = pathlib.Path(ARGS.state)
    STATE.mkdir(parents=True, exist_ok=True)
    ctx = make_certificates(ARGS.serial)
    printer = Printer(ARGS.serial)
    broker = Broker(printer, ctx)
    offset = ARGS.port_offset
    socketserver.ThreadingTCPServer.allow_reuse_address = True
    broker.serve(offset + 8883)
    serve_ftps(offset + 990, ctx, PassiveListener(offset + ARGS.passive_port % 1000 + 5000))
    serve_detect(offset + 3000, printer)
    serve_port6000(offset + 6000, ctx, printer)
    threading.Thread(target=ssdp_loop, args=(printer,), daemon=True).start()

    def heartbeat():
        # A P1 reports about once a second while connected; the plug-in falls
        # back from a LAN session that stays silent for 6 s.
        while True:
            time.sleep(2)
            t = printer.status
            broker.report_delta(bed_temper=t['bed_temper'], nozzle_temper=t['nozzle_temper'],
                                wifi_signal='-52dBm')
    threading.Thread(target=heartbeat, daemon=True).start()
    event('ready', serial=ARGS.serial, code=ARGS.code, ca=str(STATE / 'mock-ca.pem'),
          mqtt=offset + 8883, ftps=offset + 990, detect=offset + 3000, port6000=offset + 6000,
          data=offset + ARGS.passive_port % 1000 + 5000)
    try:
        while True:
            time.sleep(3600)
    except KeyboardInterrupt:
        pass


if __name__ == '__main__':
    main()
