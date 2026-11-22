"""Find the YOLO UNO serial port, because Windows does not always give it the same COM number.

`--port auto` (the default) picks the one port whose USB vendor is Espressif (0x303A: the
ESP32-S3 native USB Serial/JTAG of the YOLO UNO). It never guesses: zero or several
candidates stop with the list so the user passes `--port COMx` explicitly.

  py -3 camera/serial_port.py          # print the ports and which one auto would use
"""
import sys

ESPRESSIF_VID = 0x303A
AUTO = "auto"


class PortError(RuntimeError):
    """No unique YOLO UNO port; the message lists what Windows sees."""


def _ports():
    from serial.tools import list_ports
    return list(list_ports.comports())


def describe(ports):
    if not ports:
        return "  (khong co cong nao: cam cap USB, doi Windows nhan bo)"
    return "\n".join(f"  {p.device}: {p.description}"
                     + (f" [VID {p.vid:04X}:{p.pid:04X}]" if p.vid is not None and p.pid is not None else "")
                     for p in ports)


def candidates(ports):
    return [p for p in ports if p.vid == ESPRESSIF_VID]


def resolve_port(name=AUTO, ports=None):
    """Return the COM name to open. A name other than 'auto' is returned unchanged."""
    if name and name.strip().lower() != AUTO:
        return name.strip()
    ports = _ports() if ports is None else ports
    found = candidates(ports)
    if len(found) == 1:
        return found[0].device
    if not found:
        raise PortError("Khong tim thay YOLO UNO (USB Espressif 303A). Cac cong dang co:\n"
                        + describe(ports) + "\nCam lai cap, hoac chay voi --port COMx.")
    raise PortError("Co nhieu bo Espressif, chon mot bang --port COMx:\n" + describe(found))


def add_port_argument(parser):
    parser.add_argument("--port", default=AUTO,
                        help="Cong COM cua YOLO UNO, vi du COM7; mac dinh 'auto' = tu tim (USB Espressif)")


def port_or_exit(name):
    try:
        port = resolve_port(name)
    except PortError as error:
        print(error, file=sys.stderr, flush=True)
        raise SystemExit(2) from None
    if not name or name.strip().lower() == AUTO:
        print(f"Tu tim thay YOLO UNO o {port}.", flush=True)
    return port


def main():
    ports = _ports()
    print(describe(ports))
    try:
        print(f"auto -> {resolve_port(AUTO, ports)}")
        return 0
    except PortError as error:
        print(error)
        return 1


if __name__ == "__main__":
    sys.exit(main())
