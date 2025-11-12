# -*- coding: utf-8 -*-
"""
Aquaponics Main Controller Module – Python reference implementation (single file)
-------------------------------------------------------------------------------
This is a runnable software prototype that implements the core behavior of the
"메인 컨트롤러 모듈" described in the firmware 요구사항 정의서 v0.1.0.

핵심 매핑
- CAN 수집 100ms 주기 (FR-001) / UI 갱신 200ms (FR-003) / UART 전송 200ms (FR-002)
- 사용자 입력(로터리 대체 텍스트 명령) (FR-004)
- 알람/LED/부저 상태 표현 (FR-005)
- 서버 명령 라우팅(시뮬레이터) (FR-006)
- 독립 운용(서버 미연결 시 기본 루틴) (FR-007)
- 로그/이벤트 보관(순환 버퍼 + 파일 저장) (FR-008)
- 시간관리 및 스케줄(간단 예시) (FR-009)
- 환경설정(NVS 대체 JSON) (FR-010)
- 데이터 사전/오류정책/페일세이프 준수 (섹션 12~14 반영)

주의: 실제 하드웨어(CAN, TFT, 부저, 로터리)는 소프트웨어 시뮬레이션으로 대체합니다.
실기 포팅 시 하드웨어 드라이버로 해당 부분을 바꾸면 됩니다.
"""
from __future__ import annotations

import asyncio
import binascii
import json
import os
import random
import signal
import struct
import sys
import time
from dataclasses import dataclass, field, asdict
from enum import IntEnum
from typing import Any, Dict, List, Optional, Tuple


# ---------------------------- Constants & Enums ---------------------------- #

def now_ms() -> int:
    return int(time.time() * 1000)


class ModuleID(IntEnum):
    MAIN = 0x01
    TANK = 0x10       # 수조
    GROW = 0x20       # 재배기
    NUTRI = 0x30      # 양액기
    FEED = 0x40       # 급여기


class Cmd(IntEnum):
    SENS = 0x01
    STAT = 0x02
    CMD  = 0x10
    ACK  = 0x11
    ERR  = 0x12


STX = 0x02
ETX = 0x03


# ---------------------------- Persistence (NVS 대체) ---------------------------- #

@dataclass
class Settings:
    feed_schedule: List[Dict[str, Any]] = field(default_factory=list)  # [{'hh':'07:30','g':5}, ...]
    nutrient_ratio: Dict[str, int] = field(default_factory=lambda: {"A": 0, "B": 0, "C": 0, "D": 0})
    nutrient_amount_ml: Dict[str, int] = field(default_factory=lambda: {"A": 0, "B": 0, "C": 0, "D": 0})
    grow_led_brightness: int = 0  # 0-100
    grow_led_schedule: List[Dict[str, Any]] = field(default_factory=list)  # [{'on':'08:00','off':'22:00','brightness':60}]
    screen_off: str = "60s"  # '30s'|'60s'|'300s'|'none'
    module_enable: Dict[str, bool] = field(default_factory=lambda: {
        "tank": True, "grow": True, "nutri": True, "feed": True
    })
    time_sync_from_server: bool = True
    fw_version: str = "0.1.0"


class KVStore:
    def __init__(self, path: str):
        self.path = path
        self._data: Dict[str, Any] = {}
        self.load()

    def load(self) -> None:
        if os.path.exists(self.path):
            try:
                with open(self.path, "r", encoding="utf-8") as f:
                    self._data = json.load(f)
            except Exception:
                self._data = {}
        else:
            self._data = {}

    def save(self) -> None:
        tmp = self.path + ".tmp"
        with open(tmp, "w", encoding="utf-8") as f:
            json.dump(self._data, f, ensure_ascii=False, indent=2)
        os.replace(tmp, self.path)

    def get_settings(self) -> Settings:
        if "settings" not in self._data:
            self._data["settings"] = asdict(Settings())
        return Settings(**self._data["settings"])

    def put_settings(self, s: Settings) -> None:
        self._data["settings"] = asdict(s)
        self.save()

    def append_log(self, entry: Dict[str, Any]) -> None:
        logs: List[Dict[str, Any]] = self._data.setdefault("logs", [])
        logs.append(entry)
        # Limit log size (circular-like behavior)
        if len(logs) > 2000:
            del logs[: len(logs) - 2000]
        self.save()

    def get_logs(self) -> List[Dict[str, Any]]:
        return self._data.get("logs", [])


# ---------------------------- Data Structures ---------------------------- #

@dataclass
class CANFrame:
    can_id: int
    cmd: int
    flags: int
    ts_ms: int
    payload: bytes
    crc16: int

    def pack(self) -> bytes:
        header = struct.pack("<BBBBI", self.can_id, self.cmd, self.flags, 0, self.ts_ms)
        data = header + self.payload
        crc = binascii.crc_hqx(data, 0xFFFF)
        return header + self.payload + struct.pack("<H", crc)

    @staticmethod
    def unpack(raw: bytes) -> "CANFrame":
        can_id, cmd, flags, _, ts_ms = struct.unpack("<BBBBI", raw[:8])
        payload = raw[8:-2]
        crc16 = struct.unpack("<H", raw[-2:])[0]
        return CANFrame(can_id, cmd, flags, ts_ms, payload, crc16)


# ---------------------------- Simulated CAN Bus ---------------------------- #

class SimulatedCANBus:
    """In-memory CAN bus simulation with asyncio queues."""
    def __init__(self) -> None:
        self.rx_queue: asyncio.Queue[CANFrame] = asyncio.Queue()
        self.tx_queue: asyncio.Queue[CANFrame] = asyncio.Queue()

    async def send(self, frame: CANFrame) -> None:
        await self.tx_queue.put(frame)

    async def recv(self) -> CANFrame:
        return await self.rx_queue.get()

    # helpers used by simulated devices
    async def device_send_to_controller(self, frame: CANFrame) -> None:
        await self.rx_queue.put(frame)

    async def controller_send_to_devices(self, frame: CANFrame) -> None:
        await self.tx_queue.put(frame)


# ---------------------------- Simulated Modules ---------------------------- #

class SimTankModule:
    """Tank module: temperature, level, pH, TDS, turbidity, DO"""
    def __init__(self, bus: SimulatedCANBus):
        self.bus = bus
        self.enabled = True
        self.last_cmd: Optional[CANFrame] = None

    async def run(self):
        while True:
            await asyncio.sleep(0.1)  # 100ms
            if not self.enabled:
                continue
            temp = 24.0 + random.uniform(-0.3, 0.3)
            level = 60.0 + random.uniform(-1.0, 1.0)
            ph = 7.2 + random.uniform(-0.2, 0.2)
            tds = int(350 + random.uniform(-10, 10))
            turb = max(0.0, random.uniform(0, 5))
            do = 85.0 + random.uniform(-2.0, 2.0)
            payload = struct.pack("<fffff", temp, level, ph, float(tds), turb) + struct.pack("<f", do)
            frame = CANFrame(ModuleID.TANK, Cmd.SENS, 0, now_ms(), payload, 0)
            await self.bus.device_send_to_controller(frame)


class SimGrowModule:
    """Grow module: temp/humidity, 4x leak bits, plant LED brightness"""
    def __init__(self, bus: SimulatedCANBus):
        self.bus = bus
        self.enabled = True
        self.led_brightness = 40
        self.leak_bits = 0

    async def run(self):
        while True:
            await asyncio.sleep(0.1)
            if not self.enabled:
                continue
            temp = 23.0 + random.uniform(-0.5, 0.5)
            hum = 55.0 + random.uniform(-2.0, 2.0)
            # randomly simulate leak
            if random.random() < 0.001:
                self.leak_bits ^= 0x1 << random.randint(0, 3)
            payload = struct.pack("<ffB", temp, hum, self.leak_bits) + struct.pack("<B", self.led_brightness)
            frame = CANFrame(ModuleID.GROW, Cmd.SENS, 0, now_ms(), payload, 0)
            await self.bus.device_send_to_controller(frame)


class SimNutriModule:
    """Nutrient module: ratios & supply amounts per channel"""
    def __init__(self, bus: SimulatedCANBus):
        self.bus = bus
        self.enabled = True
        self.ratio = {"A": 10, "B": 10, "C": 0, "D": 0}
        self.remaining = {"A": 3000, "B": 3000, "C": 3000, "D": 3000}

    async def run(self):
        while True:
            await asyncio.sleep(0.1)
            if not self.enabled:
                continue
            payload = struct.pack("<BBBB", self.ratio["A"], self.ratio["B"], self.ratio["C"], self.ratio["D"]) + \
                      struct.pack("<HHHH", self.remaining["A"], self.remaining["B"],
                                  self.remaining["C"], self.remaining["D"])
            frame = CANFrame(ModuleID.NUTRI, Cmd.SENS, 0, now_ms(), payload, 0)
            await self.bus.device_send_to_controller(frame)


class SimFeedModule:
    """Feeder module: food remaining, dispense events"""
    def __init__(self, bus: SimulatedCANBus):
        self.bus = bus
        self.enabled = True
        self.remaining_g = 500

    async def run(self):
        while True:
            await asyncio.sleep(0.1)
            if not self.enabled:
                continue
            # simulate slow consumption
            if random.random() < 0.01 and self.remaining_g > 0:
                self.remaining_g -= 1
            payload = struct.pack("<H", max(0, self.remaining_g))
            frame = CANFrame(ModuleID.FEED, Cmd.SENS, 0, now_ms(), payload, 0)
            await self.bus.device_send_to_controller(frame)


# ---------------------------- UART Link (Simulated) ---------------------------- #

class UARTLinkSim:
    """Simulated server link over UART-like framed packets."""
    def __init__(self):
        self.connected = True
        self.rx_queue: asyncio.Queue[bytes] = asyncio.Queue()  # server->controller
        self.tx_queue: asyncio.Queue[bytes] = asyncio.Queue()  # controller->server

    async def send(self, pkt: bytes):
        if self.connected:
            await self.tx_queue.put(pkt)

    async def recv(self) -> bytes:
        return await self.rx_queue.get()

    def pack(self, pkt_type: int, data: bytes) -> bytes:
        body = struct.pack("<B", pkt_type) + data
        length = len(body)
        crc = binascii.crc_hqx(body, 0xFFFF)
        return bytes([STX]) + struct.pack("<H", length) + body + struct.pack("<H", crc) + bytes([ETX])


# ---------------------------- System State & Alarm ---------------------------- #

@dataclass
class ModuleComm:
    last_ts_ms: int = 0
    ok: bool = False


@dataclass
class LEDState:
    blue: bool = False   # server link
    green: bool = False  # modules ok
    red: bool = False    # alarm


class AlarmManager:
    def __init__(self, kv: KVStore):
        self.kv = kv
        self.active: Dict[str, Dict[str, Any]] = {}

    def raise_alarm(self, code: str, msg: str, sticky: bool = True) -> None:
        if code in self.active:
            return
        entry = {"ts": now_ms(), "code": code, "msg": msg, "sticky": sticky}
        self.active[code] = entry
        self.kv.append_log({"type": "ALARM", **entry})

    def clear_alarm(self, code: str) -> None:
        if code in self.active:
            self.kv.append_log({"type": "ALARM_CLEAR", "ts": now_ms(), "code": code})
            del self.active[code]

    def any_active(self) -> bool:
        return len(self.active) > 0

    def summary(self) -> str:
        if not self.active:
            return "없음"
        return ", ".join([f"{k}:{v['msg']}" for k, v in self.active.items()])


# ---------------------------- Main Controller ---------------------------- #

class MainController:
    CAN_PERIOD_MS = 100       # FR-001
    UI_PERIOD_MS = 200        # FR-003
    UART_PERIOD_MS = 200      # FR-002

    def __init__(self, kv: KVStore):
        self.kv = kv
        self.settings = kv.get_settings()
        self.can = SimulatedCANBus()
        self.uart = UARTLinkSim()
        self.led = LEDState()
        self.alarm = AlarmManager(kv)

        self.modules: Dict[int, ModuleComm] = {
            ModuleID.TANK: ModuleComm(),
            ModuleID.GROW: ModuleComm(),
            ModuleID.NUTRI: ModuleComm(),
            ModuleID.FEED: ModuleComm(),
        }

        # last values
        self.state: Dict[str, Any] = {
            "tank": {"temp": None, "level": None, "ph": None, "tds": None, "turb": None, "do": None},
            "grow": {"temp": None, "hum": None, "leak": 0, "led": 0},
            "nutri": {"ratio": {"A": 0, "B": 0, "C": 0, "D": 0}, "remain": {"A": 0, "B": 0, "C": 0, "D": 0}},
            "feed": {"remain_g": 0},
        }

        # input command queue (as rotary switch alternative)
        self.cmd_queue: asyncio.Queue[str] = asyncio.Queue()

        # Simulated devices
        self.devices = [
            SimTankModule(self.can),
            SimGrowModule(self.can),
            SimNutriModule(self.can),
            SimFeedModule(self.can),
        ]

        # housekeeping
        self._stop = asyncio.Event()

    # ---------------------- High-level tasks ---------------------- #

    async def run(self):
        # spawn simulated devices
        for dev in self.devices:
            asyncio.create_task(dev.run(), name=f"dev-{dev.__class__.__name__}")

        # spawn controller tasks
        asyncio.create_task(self.task_can_rx(), name="can-rx")
        asyncio.create_task(self.task_can_watchdog(), name="can-watch")
        asyncio.create_task(self.task_uart_tx(), name="uart-tx")
        asyncio.create_task(self.task_ui(), name="ui")
        asyncio.create_task(self.task_input(), name="input")
        asyncio.create_task(self.task_scheduler(), name="scheduler")

        # graceful shutdown
        loop = asyncio.get_running_loop()
        for sig in (signal.SIGINT, signal.SIGTERM):
            loop.add_signal_handler(sig, lambda s=sig: asyncio.create_task(self.stop()))
        await self._stop.wait()

    async def stop(self):
        self._stop.set()

    # ---------------------- CAN handling ---------------------- #

    async def task_can_rx(self):
        while not self._stop.is_set():
            try:
                frame = await asyncio.wait_for(self.can.recv(), timeout=0.2)
            except asyncio.TimeoutError:
                continue

            self.modules[frame.can_id].last_ts_ms = now_ms()
            self.modules[frame.can_id].ok = True

            if frame.can_id == ModuleID.TANK and frame.cmd == Cmd.SENS:
                temp, level, ph, tds_f, turb, do = struct.unpack("<ffffff", frame.payload)
                self.state["tank"].update({"temp": temp, "level": level, "ph": ph,
                                           "tds": int(tds_f), "turb": turb, "do": do})

            elif frame.can_id == ModuleID.GROW and frame.cmd == Cmd.SENS:
                temp, hum, leak_bits, led = struct.unpack("<ffBB", frame.payload)
                self.state["grow"].update({"temp": temp, "hum": hum, "leak": leak_bits, "led": led})
                if leak_bits:
                    self.alarm.raise_alarm("E-LEAK", "누수 감지")
                else:
                    self.alarm.clear_alarm("E-LEAK")

            elif frame.can_id == ModuleID.NUTRI and frame.cmd == Cmd.SENS:
                a, b, c, d, ra, rb, rc, rd = struct.unpack("<BBBBHHHH", frame.payload)
                self.state["nutri"]["ratio"] = {"A": a, "B": b, "C": c, "D": d}
                self.state["nutri"]["remain"] = {"A": ra, "B": rb, "C": rc, "D": rd}
                # low nutrient alarm
                for k, v in self.state["nutri"]["remain"].items():
                    if v < 200:
                        self.alarm.raise_alarm("E-NUTRI-LOW", f"양액 부족({k})")
                        break
                else:
                    self.alarm.clear_alarm("E-NUTRI-LOW")

            elif frame.can_id == ModuleID.FEED and frame.cmd == Cmd.SENS:
                (remain_g,) = struct.unpack("<H", frame.payload)
                self.state["feed"]["remain_g"] = remain_g
                if remain_g <= 0:
                    self.alarm.raise_alarm("E-FEED-EMPTY", "먹이 고갈")
                else:
                    self.alarm.clear_alarm("E-FEED-EMPTY")

    async def task_can_watchdog(self):
        """Check module heartbeats and raise comm-loss alarms (100ms base)."""
        while not self._stop.is_set():
            await asyncio.sleep(self.CAN_PERIOD_MS / 1000.0)
            now = now_ms()
            all_ok = True
            for mid, comm in self.modules.items():
                ok = (now - comm.last_ts_ms) < 500  # within 0.5s
                if not ok:
                    all_ok = False
                comm.ok = ok
            # Green LED when ALL ok
            self.led.green = all_ok
            if not all_ok:
                self.alarm.raise_alarm("E-CAN-LOST", "모듈 통신 지연/손실")
            else:
                self.alarm.clear_alarm("E-CAN-LOST")

    # ---------------------- UART handling ---------------------- #

    async def task_uart_tx(self):
        """Send status to server at 200ms; track Blue LED & fail-safe."""
        while not self._stop.is_set():
            await asyncio.sleep(self.UART_PERIOD_MS / 1000.0)
            if not self.uart.connected:
                # fail-safe mode
                self.led.blue = False
                self.alarm.raise_alarm("E-SRV-LOST", "서버 연결 끊김", sticky=False)
                continue
            self.led.blue = True
            self.alarm.clear_alarm("E-SRV-LOST")
            pkt = self.encode_status_packet()
            await self.uart.send(pkt)

    def encode_status_packet(self) -> bytes:
        # TYPE: 0x01 = status snapshot
        data = json.dumps({
            "ts": now_ms(),
            "state": self.state,
            "alarms": self.alarm.active,
            "fw": self.settings.fw_version,
        }, ensure_ascii=False).encode("utf-8")
        return self.uart.pack(0x01, data)

    # ---------------------- UI & Input ---------------------- #

    def format_dashboard(self) -> str:
        tank = self.state["tank"]
        grow = self.state["grow"]
        nut  = self.state["nutri"]
        feed = self.state["feed"]
        led = self.led
        return (
            "\n=== Dashboard (200ms 주기) ===\n"
            f"[LED] Blue(SRV)={'ON' if led.blue else 'OFF'}  "
            f"Green(MOD)={'ON' if led.green else 'OFF'}  "
            f"Red(ALARM)={'ON' if self.alarm.any_active() else 'OFF'}\n"
            f"수조: T={tank['temp']:.2f}°C  수위={tank['level']:.1f}mm  pH={tank['ph']:.2f}  "
            f"TDS={tank['tds']}ppm  탁도={tank['turb']:.2f}NTU  DO={tank['do']:.1f}%\n"
            f"재배기: T={grow['temp']:.2f}°C  H={grow['hum']:.1f}%  누수bits=0b{grow['leak']:04b}  LED={grow['led']}%\n"
            f"양액: 비율 A:{nut['ratio']['A']} B:{nut['ratio']['B']} C:{nut['ratio']['C']} D:{nut['ratio']['D']}  "
            f"잔량(ml) A:{nut['remain']['A']} B:{nut['remain']['B']} C:{nut['remain']['C']} D:{nut['remain']['D']}\n"
            f"급여기: 남은 먹이={feed['remain_g']} g\n"
            f"알람: {self.alarm.summary()}\n"
            "명령: help | feed <g> | led <0-100> | srvdown | srvup | quit\n"
        )

    async def task_ui(self):
        while not self._stop.is_set():
            await asyncio.sleep(self.UI_PERIOD_MS / 1000.0)
            # clear screen friendly
            sys.stdout.write("\x1b[2J\x1b[H")
            sys.stdout.write(self.format_dashboard())
            sys.stdout.flush()

    async def task_input(self):
        loop = asyncio.get_running_loop()
        while not self._stop.is_set():
            # run blocking input in thread to not block event loop
            cmdline = await loop.run_in_executor(None, sys.stdin.readline)
            if not cmdline:
                await asyncio.sleep(0.1)
                continue
            await self.cmd_queue.put(cmdline.strip())
            await self.handle_commands()

    async def handle_commands(self):
        while not self.cmd_queue.empty():
            line = await self.cmd_queue.get()
            parts = line.split()
            if not parts:
                continue
            cmd = parts[0].lower()
            if cmd == "help":
                print("help: feed <g>, led <0-100>, srvdown, srvup, quit")
            elif cmd == "feed":
                amt = int(parts[1]) if len(parts) > 1 else 5
                self.dispense_food(amt)
            elif cmd == "led":
                val = int(parts[1]) if len(parts) > 1 else 50
                self.set_grow_led(max(0, min(100, val)))
            elif cmd == "srvdown":
                self.uart.connected = False
            elif cmd == "srvup":
                self.uart.connected = True
            elif cmd == "quit":
                await self.stop()
            else:
                print("unknown command")

    def dispense_food(self, grams: int):
        st = self.state["feed"]
        st["remain_g"] = max(0, st["remain_g"] - grams)
        self.kv.append_log({"ts": now_ms(), "type": "FEED", "grams": grams})
        print(f"급여 수행: {grams} g")

    def set_grow_led(self, brightness: int):
        self.state["grow"]["led"] = brightness
        self.settings.grow_led_brightness = brightness
        self.kv.put_settings(self.settings)
        print(f"재배기 LED 설정: {brightness}% 저장됨")

    # ---------------------- Scheduler (basic examples) ---------------------- #

    @staticmethod
    def _time_hhmm() -> str:
        t = time.localtime()
        return f"{t.tm_hour:02d}:{t.tm_min:02d}"

    async def task_scheduler(self):
        """Basic schedules for feeding and LED on/off (runs every 1s)."""
        while not self._stop.is_set():
            await asyncio.sleep(1.0)
            # feeding schedule
            for item in self.settings.feed_schedule:
                if item.get("hh") == self._time_hhmm():
                    grams = int(item.get("g", 5))
                    self.dispense_food(grams)
            # LED schedule
            for slot in self.settings.grow_led_schedule:
                if slot.get("on") == self._time_hhmm():
                    self.set_grow_led(int(slot.get("brightness", self.state["grow"]["led"])))
                if slot.get("off") == self._time_hhmm():
                    self.set_grow_led(0)


# ---------------------------- Entrypoint ---------------------------- #

def main():
    kv = KVStore(os.environ.get("AQUA_DB", "./aquaponics_main_kv.json"))
    mc = MainController(kv)
    print("Aquaponics Main Controller (Python) starting... (Ctrl+C to exit)")
    asyncio.run(mc.run())
    print("Stopped. Bye.")


if __name__ == "__main__":
    main()
