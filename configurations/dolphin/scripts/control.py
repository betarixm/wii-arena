# type: ignore
import os
import socket
import struct

from dolphin import controller, event

CONTROLLER_STRUCT = "<H6f"
CONTROLLER_SIZE = struct.calcsize(CONTROLLER_STRUCT)

# Make sure to initialize dolphin before socket listening
await event.frameadvance()  # noqa: F704

server_sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
server_sock_path = os.getenv("CONTROL_SOCKET", "/tmp/dolphin_control.sock")

server_sock.bind(server_sock_path)
os.chmod(server_sock_path, 0o666)
server_sock.listen(1)
conn, _ = server_sock.accept()

while True:
    data = conn.recv(1024)

    if data.startswith(b"E"):
        payload = data[1:]
        num_controllers_in_packet = payload[0]
        controller_data = payload[1:]
        for controller_index in range(num_controllers_in_packet):
            offset = controller_index * CONTROLLER_SIZE

            button_mask, sx, sy, csx, csy, tl, tr = struct.unpack_from(
                CONTROLLER_STRUCT, controller_data, offset
            )

            inputs = {
                btn: bool(button_mask & (1 << j))
                for j, btn in enumerate(
                    [
                        "A",
                        "B",
                        "X",
                        "Y",
                        "Z",
                        "Start",
                        "Up",
                        "Down",
                        "Left",
                        "Right",
                        "L",
                        "R",
                    ]
                )
            }
            inputs.update(
                {
                    "StickX": sx,
                    "StickY": sy,
                    "CStickX": csx,
                    "CStickY": csy,
                    "TriggerLeft": tl,
                    "TriggerRight": tr,
                }
            )

            controller.set_gc_buttons(controller_index, inputs)

        await event.frameadvance()  # noqa: F704
        conn.sendall(b"D")

    elif data.startswith(b"Q"):
        conn.close()
        break
